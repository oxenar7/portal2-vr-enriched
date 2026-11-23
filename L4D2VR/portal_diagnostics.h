#pragma once
// Portal crossing diagnostics for issue #4: after a floor-to-wall fling in Alive & Kicking the
// player is reported to walk on walls until the next load, with Classic portal orientation.
// The VR camera is the headset plus a yaw offset there, so the cause has to be in the game's
// own state. These lines record, after each crossing and whenever it stays off later:
// - the engine's camera (CViewSetup as Source built it),
// - the player's origin and view offset,
// - the player's local up and stick-camera state (C_Portal_Player::m_PortalLocal),
// - the VR camera.
// Logging only: nothing here changes the game.
#include "sdk/vector.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace PortalDiagnostics {

// Source's typedescription_t starts {int fieldType; const char *fieldName; int fieldOffset; ...}.
inline constexpr int kFieldVector = 3;
inline constexpr int kFieldInteger = 5;
inline constexpr int kFieldEmbedded = 10;

// A loaded section: its bytes, size and address in the process.
struct Section
{
    const unsigned char *bytes = nullptr;
    std::size_t size = 0;
    std::uintptr_t address = 0;
};

inline std::uint32_t ReadU32(const unsigned char *bytes)
{
    std::uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

// The offset of the one datamap field called `name` of the given type, read from the
// module's data sections. Empty when the name is missing, the field is listed more than
// once, or its offset is at least `maxOffset`. Each of the five supported games names
// m_PortalLocal, m_vLocalUp and m_nStickCameraState once, so no per-build table is needed.
inline std::optional<std::uint32_t> FieldOffset(const std::vector<Section> &sections, const char *name,
                                                int type, std::uint32_t maxOffset)
{
    const std::size_t length = std::strlen(name) + 1; // with its NUL
    std::optional<std::uint32_t> found;
    int matches = 0;
    for (const auto &text : sections) {
        for (std::size_t at = 1; at + length <= text.size; ++at) {
            if (text.bytes[at] != static_cast<unsigned char>(name[0]) || text.bytes[at - 1] != 0 ||
                std::memcmp(text.bytes + at, name, length) != 0)
                continue;
            const auto pointer = static_cast<std::uint32_t>(text.address + at);
            for (const auto &data : sections) {
                // Entries are 4-byte aligned: the pointer sits at +4 with the type before it.
                for (std::size_t word = 4; word + 8 <= data.size; word += 4) {
                    if (ReadU32(data.bytes + word) != pointer ||
                        ReadU32(data.bytes + word - 4) != static_cast<std::uint32_t>(type))
                        continue;
                    const std::uint32_t offset = ReadU32(data.bytes + word + 4);
                    if (offset >= maxOffset)
                        continue;
                    ++matches;
                    found = offset;
                }
            }
        }
    }
    return matches == 1 ? found : std::nullopt;
}

// Where the player's local up and stick-camera state live, from the client's datamap.
struct PlayerLayout
{
    std::uint32_t localUp = 0;     // m_PortalLocal + m_vLocalUp
    std::uint32_t stickCamera = 0; // m_PortalLocal + m_nStickCameraState
    bool Valid() const { return localUp && stickCamera; }
};

inline PlayerLayout FindPlayerLayout(const std::vector<Section> &sections)
{
    const auto local = FieldOffset(sections, "m_PortalLocal", kFieldEmbedded, 0x4000);
    const auto up = FieldOffset(sections, "m_vLocalUp", kFieldVector, 0x400);
    const auto stick = FieldOffset(sections, "m_nStickCameraState", kFieldInteger, 0x400);
    if (!local || !up || !stick)
        return {};
    return {*local + *up, *local + *stick};
}

// One reading, taken in RenderView.
struct Snapshot
{
    Vector engineEye{0.0f, 0.0f, 0.0f};           // CViewSetup origin as Source built it
    Vector engineAngles{0.0f, 0.0f, 0.0f};        // CViewSetup angles as Source built them
    Vector playerOrigin{NAN, NAN, NAN};          // NAN until read
    float viewOffsetZ = NAN;    // NAN when the player field is unverified
    std::optional<Vector> localUp;
    std::optional<int> stickCamera;
    Vector hmdAngles{0.0f, 0.0f, 0.0f};           // the VR camera's angles
    float yawOffset = 0.0f;     // Classic's portal yaw offset
    Vector leftEye{0.0f, 0.0f, 0.0f};             // the left eye's final origin
    bool leftEyeThroughPortal = false; // TraceEye moved the left eye through a portal
};

inline float Length(const Vector &v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

inline bool Finite(const Vector &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// How far Source's camera sits from origin + view offset; 0 when either is unknown.
inline float EyeDisplacement(const Snapshot &s)
{
    if (!std::isfinite(s.viewOffsetZ) || !Finite(s.playerOrigin))
        return 0.0f;
    const Vector expected(s.playerOrigin.x, s.playerOrigin.y, s.playerOrigin.z + s.viewOffsetZ);
    return Length(Vector(s.engineEye.x - expected.x, s.engineEye.y - expected.y, s.engineEye.z - expected.z));
}

inline float WrappedDegrees(float angle)
{
    angle = std::fmod(angle, 360.0f);
    if (angle > 180.0f) angle -= 360.0f;
    if (angle < -180.0f) angle += 360.0f;
    return angle;
}

// Something a level player standing on a floor never shows.
inline bool Anomalous(const Snapshot &s)
{
    if (s.localUp && (Length(*s.localUp) < 0.5f || s.localUp->z < 0.95f))
        return true;
    if (EyeDisplacement(s) > 24.0f)
        return true;
    return std::fabs(WrappedDegrees(s.engineAngles.z)) > 10.0f;
}

inline std::string Format(const Vector &v, int decimals = 1)
{
    char text[96];
    std::snprintf(text, sizeof(text), "(%.*f %.*f %.*f)", decimals, v.x, decimals, v.y, decimals, v.z);
    return text;
}

inline std::string Describe(const Snapshot &s)
{
    std::string line = "engine eye " + Format(s.engineEye) + " angles " + Format(s.engineAngles) +
        "; player origin " + (Finite(s.playerOrigin) ? Format(s.playerOrigin) : std::string("unknown"));
    if (std::isfinite(s.viewOffsetZ) && Finite(s.playerOrigin)) {
        char text[64];
        std::snprintf(text, sizeof(text), " view offset %.1f, eye %.1f off", s.viewOffsetZ, EyeDisplacement(s));
        line += text;
    }
    line += "; local up " + (s.localUp ? Format(*s.localUp, 3) : std::string("unknown"));
    line += " stick camera " + (s.stickCamera ? std::to_string(*s.stickCamera) : std::string("unknown"));
    char yaw[32];
    std::snprintf(yaw, sizeof(yaw), "%.1f", s.yawOffset);
    line += "; VR angles " + Format(s.hmdAngles) + " yaw offset " + yaw + ", left eye " + Format(s.leftEye) +
        (s.leftEyeThroughPortal ? " through a portal" : "");
    return line;
}

// Decides which readings become log lines: every quarter second for three seconds after each
// of the first 20 crossings of a level, then only a state that stays off for half a second,
// every two seconds up to 30 lines per level, and once when it clears.
class Watch
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kWindow = std::chrono::seconds(3);
    static constexpr auto kWindowStep = std::chrono::milliseconds(250);
    static constexpr auto kHold = std::chrono::milliseconds(500);
    static constexpr auto kAnomalyStep = std::chrono::seconds(2);
    static constexpr int kWindowCrossings = 20;
    static constexpr int kAnomalyLines = 30;

    enum class Line { None, Window, Anomaly, Cleared };

    void Reset() { *this = Watch{}; }

    // Returns the crossing's number in this level.
    int OnCrossing(Clock::time_point now)
    {
        ++m_Crossings;
        if (m_Crossings <= kWindowCrossings) {
            m_WindowStart = now;
            m_WindowEnd = now + kWindow;
            m_NextWindowLine = now;
        }
        return m_Crossings;
    }

    Line Next(Clock::time_point now, bool anomalous)
    {
        if (now < m_WindowEnd) {
            // The window shows the state itself; a later anomaly is timed from its end.
            m_AnomalyActive = false;
            if (now < m_NextWindowLine)
                return Line::None;
            m_NextWindowLine = now + kWindowStep;
            return Line::Window;
        }
        if (anomalous) {
            if (!m_AnomalyActive) {
                m_AnomalyActive = true;
                m_AnomalySince = now;
                m_AnomalyReported = false;
                m_NextAnomalyLine = now + kHold;
            }
            if (now < m_NextAnomalyLine || m_AnomalyLinesWritten >= kAnomalyLines)
                return Line::None;
            m_NextAnomalyLine = now + kAnomalyStep;
            ++m_AnomalyLinesWritten;
            m_AnomalyReported = true;
            return Line::Anomaly;
        }
        if (m_AnomalyActive) {
            m_AnomalyActive = false;
            if (m_AnomalyReported)
                return Line::Cleared;
        }
        return Line::None;
    }

    // Seconds since the last crossing's window opened, for window lines.
    float WindowSeconds(Clock::time_point now) const
    {
        return std::chrono::duration<float>(now - m_WindowStart).count();
    }
    float AnomalySeconds(Clock::time_point now) const
    {
        return std::chrono::duration<float>(now - m_AnomalySince).count();
    }
    int Crossings() const { return m_Crossings; }

private:
    int m_Crossings = 0;
    Clock::time_point m_WindowStart{}, m_WindowEnd{}, m_NextWindowLine{};
    bool m_AnomalyActive = false, m_AnomalyReported = false;
    Clock::time_point m_AnomalySince{}, m_NextAnomalyLine{};
    int m_AnomalyLinesWritten = 0;
};

} // namespace PortalDiagnostics
