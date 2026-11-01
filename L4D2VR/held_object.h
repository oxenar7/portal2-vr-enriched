#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// The game's CGrabController, the gun anchor built on it, the ConVars the mod tunes and the
// summaries written to portal2vr_held.log. The offsets belong to the verified server.dll: use
// them only when VerifiedPortalSymbols::ResolveLoadedHeldObjectLayout() returned a non-empty layout.
namespace HeldObject {

inline constexpr const char *kLogFile = "portal2vr_held.log";

// CGrabController
inline constexpr std::size_t kTargetPosition = 0x4;  // m_shadow.targetPosition
inline constexpr std::size_t kTargetAngles = 0x10;   // m_shadow.targetRotation
inline constexpr std::size_t kError = 0x203C;
inline constexpr std::size_t kAngleAlignment = 0x2044;
inline constexpr std::size_t kIgnoreRelativePitch = 0x2049;
inline constexpr std::size_t kLoadWeight = 0x204C;
inline constexpr std::size_t kAttachedEntity = 0x2054;
inline constexpr std::size_t kPreferredAngles = 0x2058;
inline constexpr std::size_t kHasPreferredAngles = 0x2064;
inline constexpr std::size_t kDistanceOffset = 0x2068;
inline constexpr std::size_t kAttachedAngles = 0x206C;
inline constexpr std::size_t kAllowOverhead = 0x2088;

// CBaseEntity
inline constexpr std::size_t kClassName = 0x60;
inline constexpr std::size_t kModelName = 0xAC;
inline constexpr std::size_t kCollisionRadius = 0x10C;
inline constexpr std::size_t kAbsOrigin = 0x1CC;
inline constexpr std::size_t kAbsAngles = 0x1D8;

// ConVar
inline constexpr std::size_t kConVarName = 0x0C;
inline constexpr std::size_t kConVarParent = 0x1C;
inline constexpr std::size_t kConVarFloat = 0x2C;
inline constexpr std::size_t kConVarInt = 0x30;

// CGlobalEntityList slots: {IHandleEntity *entity; int serial; ...}
inline constexpr std::uint32_t kEntitySlots = 0x2000;
inline constexpr std::size_t kEntitySlotSize = 24;
inline constexpr std::size_t kEntitySlotBase = 4;

// UpdateObject's reach is radius + hull reach + player_held_object_distance, never less than
// radius + hull reach. Cancelling the cvar puts the object on that floor, so a blocked ray
// cannot pull it off the gun.
inline constexpr float kNativeHoldDistance = 15.0f;

// Gun anchor tuning, in degrees.
inline constexpr float kLevelSoftness = 10.0f;
inline constexpr float kAnchorEyePitch = 85.0f;  // a vertical eye direction has no yaw
inline constexpr float kAnchorErrorScale = 2.0f; // a hand outruns the walking player

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

template <typename T>
T Field(const void *object, std::size_t offset)
{
    T value{};
    std::memcpy(&value, static_cast<const char *>(object) + offset, sizeof(T));
    return value;
}

template <typename T>
void Store(void *object, std::size_t offset, const T &value)
{
    std::memcpy(static_cast<char *>(object) + offset, &value, sizeof(T));
}

struct GrabState {
    Vec3 targetPosition, targetAngles, preferredAngles, attachedAngles;
    float error = 0, angleAlignment = 0, loadWeight = 0, distanceOffset = 0;
    std::uint32_t attachedHandle = 0;
    bool ignoreRelativePitch = false, hasPreferredAngles = false, allowOverhead = false;
};

inline GrabState ReadGrabState(const void *controller)
{
    GrabState state;
    state.targetPosition = Field<Vec3>(controller, kTargetPosition);
    state.targetAngles = Field<Vec3>(controller, kTargetAngles);
    state.preferredAngles = Field<Vec3>(controller, kPreferredAngles);
    state.attachedAngles = Field<Vec3>(controller, kAttachedAngles);
    state.error = Field<float>(controller, kError);
    state.angleAlignment = Field<float>(controller, kAngleAlignment);
    state.loadWeight = Field<float>(controller, kLoadWeight);
    state.distanceOffset = Field<float>(controller, kDistanceOffset);
    state.attachedHandle = Field<std::uint32_t>(controller, kAttachedEntity);
    state.ignoreRelativePitch = Field<std::uint8_t>(controller, kIgnoreRelativePitch) != 0;
    state.hasPreferredAngles = Field<std::uint8_t>(controller, kHasPreferredAngles) != 0;
    state.allowOverhead = Field<std::uint8_t>(controller, kAllowOverhead) != 0;
    return state;
}

// The same walk server.dll does for an EHANDLE; null for a stale or invalid handle.
inline const void *EntityFromHandle(std::uintptr_t slots, std::uint32_t handle)
{
    const std::uint32_t index = handle & 0xFFFF;
    if (!slots || index >= kEntitySlots) return nullptr;
    const auto *slot = reinterpret_cast<const char *>(slots) + kEntitySlotBase + index * kEntitySlotSize;
    if (Field<std::uint32_t>(slot, 4) != handle >> 16) return nullptr;
    return Field<const void *>(slot, 0);
}

// Both cvars are development-only, so neither the console nor launch options reach them, and
// a cheat cvar reverts on level load. Writes the parent's int and float; true on a change.
inline bool SetConVarInt(std::uintptr_t conVar, const char *name, int value)
{
    const auto *object = reinterpret_cast<const char *>(conVar);
    if (!object || !name) return false;
    const char *actualName = Field<const char *>(object, kConVarName);
    if (!actualName || std::strcmp(actualName, name) != 0) return false;
    auto *parent = Field<char *>(object, kConVarParent);
    if (!parent) return false;
    const float number = static_cast<float>(value);
    if (Field<int>(parent, kConVarInt) == value && Field<float>(parent, kConVarFloat) == number)
        return false;
    Store(parent, kConVarInt, value);
    Store(parent, kConVarFloat, number);
    return true;
}

// Source angles are pitch, yaw, roll in degrees; the columns are forward, left, up.
struct Mat3 {
    float m[3][3] = {};
};

inline Mat3 AnglesToMatrix(const Vec3 &angles)
{
    constexpr float kRadians = 3.14159265358979f / 180.0f;
    const float sp = std::sin(angles.x * kRadians), cp = std::cos(angles.x * kRadians);
    const float sy = std::sin(angles.y * kRadians), cy = std::cos(angles.y * kRadians);
    const float sr = std::sin(angles.z * kRadians), cr = std::cos(angles.z * kRadians);
    Mat3 out;
    out.m[0][0] = cp * cy;
    out.m[1][0] = cp * sy;
    out.m[2][0] = -sp;
    out.m[0][1] = sp * sr * cy - cr * sy;
    out.m[1][1] = sp * sr * sy + cr * cy;
    out.m[2][1] = sr * cp;
    out.m[0][2] = sp * cr * cy + sr * sy;
    out.m[1][2] = sp * cr * sy - sr * cy;
    out.m[2][2] = cr * cp;
    return out;
}

inline Vec3 MatrixToAngles(const Mat3 &matrix)
{
    constexpr float kDegrees = 180.0f / 3.14159265358979f;
    const auto &m = matrix.m;
    const float flat = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0]);
    Vec3 angles;
    angles.x = std::atan2(-m[2][0], flat) * kDegrees;
    if (flat > 0.001f) {
        angles.y = std::atan2(m[1][0], m[0][0]) * kDegrees;
        angles.z = std::atan2(m[2][1], m[2][2]) * kDegrees;
    } else {
        angles.y = std::atan2(-m[0][1], m[1][1]) * kDegrees;
    }
    return angles;
}

inline Mat3 Multiply(const Mat3 &a, const Mat3 &b)
{
    Mat3 out;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            for (int i = 0; i < 3; ++i)
                out.m[row][column] += a.m[row][i] * b.m[i][column];
    return out;
}

// Keeps the object nearly level around a level gun and never turns it faster than the wrist,
// so the resting grip, which sits around 10 degrees off level, does not wobble it.
inline float SoftenTilt(float degrees)
{
    degrees = std::remainder(degrees, 360.0f);
    return std::copysign(std::sqrt(degrees * degrees + kLevelSoftness * kLevelSoftness) - kLevelSoftness,
                         degrees);
}

// With m_bIgnoreRelativePitch UpdateObject turns the attached angles by the eye yaw alone, so
// the gun's pitch and roll go into the attached angles. A level gun gives the game's own carry
// pose, whatever the gun's pitch was at pickup.
inline Vec3 AnchoredAttachedAngles(const Vec3 &base, float gunPitch, float gunRoll)
{
    Vec3 tilt;
    tilt.x = SoftenTilt(gunPitch);
    tilt.z = SoftenTilt(gunRoll);
    return MatrixToAngles(Multiply(AnglesToMatrix(tilt), AnglesToMatrix(base)));
}

// The layout of matrix3x4_t, rotation only.
struct RotationFrame {
    float m[3][4] = {};
};

inline RotationFrame MakeRotationFrame(const Vec3 &angles)
{
    const Mat3 rotation = AnglesToMatrix(angles);
    RotationFrame frame;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            frame.m[row][column] = rotation.m[row][column];
    return frame;
}

// Sets what UpdateObject reads, so its traces, portal transforms and physics keep working.
// AttachEntity writes fresh attached angles and distance offset on every pickup, which is how
// a new hold is recognised. No game code writes m_bAllowObjectOverhead, so the value saved
// from a controller stays valid across its holds.
class Anchor {
public:
    bool Applied() const { return m_Applied; }
    const Vec3 &BaseAngles() const { return m_Base; }

    void Apply(void *controller, float gunPitch, float gunRoll)
    {
        if (!SameHold(controller)) {
            if (controller != m_Controller)
                m_AllowOverhead = Field<std::uint8_t>(controller, kAllowOverhead);
            m_Applied = true;
            m_Controller = controller;
            m_Handle = Field<std::uint32_t>(controller, kAttachedEntity);
            m_Base = Field<Vec3>(controller, kAttachedAngles);
            m_DistanceOffset = Field<float>(controller, kDistanceOffset);
        }
        m_Written = AnchoredAttachedAngles(m_Base, gunPitch, gunRoll);
        Store(controller, kAttachedAngles, m_Written);
        Store(controller, kDistanceOffset, m_DistanceOffset - kNativeHoldDistance);
        // Also turns off the clamp that keeps the object a full reach away from the player's
        // box. Held objects do not collide with their holder.
        Store(controller, kAllowOverhead, std::uint8_t{1});
    }

    // Hands an ongoing hold back to the game, for an option change or lost tracking.
    void Release(void *controller)
    {
        if (SameHold(controller)) {
            Store(controller, kAttachedAngles, m_Base);
            Store(controller, kDistanceOffset, m_DistanceOffset);
        }
        if (m_Applied && controller == m_Controller)
            Store(controller, kAllowOverhead, m_AllowOverhead);
        m_Applied = false;
    }

private:
    bool SameHold(const void *controller) const
    {
        const auto attached = Field<Vec3>(controller, kAttachedAngles);
        return m_Applied && controller == m_Controller &&
            Field<std::uint32_t>(controller, kAttachedEntity) == m_Handle &&
            std::memcmp(&attached, &m_Written, sizeof(attached)) == 0;
    }

    bool m_Applied = false;
    const void *m_Controller = nullptr;
    std::uint32_t m_Handle = 0;
    Vec3 m_Base, m_Written;
    std::uint8_t m_AllowOverhead = 0;
    float m_DistanceOffset = 0;
};

enum class DropReason { Released, Forced };

// One hold from the first UpdateObject call to the release. UpdateObject dispatches to
// UpdateObjectVM, so one game tick can report both paths.
class Trace {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kSampleInterval = std::chrono::milliseconds(250);
    static constexpr auto kReleaseAfter = std::chrono::milliseconds(250);

    struct Summary {
        float seconds = 0, meanError = 0, maxError = 0, lastError = 0;
        int ticks = 0, vmTicks = 0;
    };

    bool Active() const { return m_Active; }

    void Begin(Clock::time_point now)
    {
        *this = Trace{};
        m_Active = true;
        m_Start = m_LastTick = now;
        m_NextSample = now + kSampleInterval;
    }

    // True when a hold sample is due.
    bool Tick(Clock::time_point now, float error, bool viewModel)
    {
        m_LastTick = now;
        ++(viewModel ? m_Summary.vmTicks : m_Summary.ticks);
        m_ErrorSum += error;
        m_Summary.maxError = (std::max)(m_Summary.maxError, error);
        m_Summary.lastError = error;
        if (now < m_NextSample) return false;
        m_NextSample = now + kSampleInterval;
        return true;
    }

    float Elapsed(Clock::time_point now) const
    {
        return std::chrono::duration<float>(now - m_Start).count();
    }

    // Menus and loading screens stop the game ticks; do not report a release for that.
    bool Released(Clock::time_point now, bool ticking)
    {
        if (!m_Active) return false;
        if (!ticking) {
            m_LastTick = now;
            return false;
        }
        return now - m_LastTick >= kReleaseAfter;
    }

    Summary End()
    {
        Summary summary = m_Summary;
        summary.seconds = Elapsed(m_LastTick);
        const int calls = summary.ticks + summary.vmTicks;
        summary.meanError = calls ? m_ErrorSum / calls : 0;
        *this = Trace{};
        return summary;
    }

private:
    bool m_Active = false;
    Clock::time_point m_Start{}, m_LastTick{}, m_NextSample{};
    Summary m_Summary;
    float m_ErrorSum = 0;
};

template <typename... Args>
std::string FormatLine(const char *format, Args... args)
{
    char buffer[640];
    const int length = std::snprintf(buffer, sizeof(buffer), format, args...);
    if (length <= 0) return {};
    return std::string(buffer, (std::min)(static_cast<std::size_t>(length), sizeof(buffer) - 1));
}

inline std::string FormatPickup(const std::string &className, const std::string &modelName,
                                float maxError, const GrabState &state, float radius,
                                bool column, bool aimAssist, bool anchored)
{
    return FormatLine("pickup class=%s model=%s maxError=%.1f ignorePitch=%d preferred=%d "
        "preferredAngles=%.1f,%.1f,%.1f alignment=%.3f weight=%.1f distanceOffset=%.1f overhead=%d "
        "attached=%.1f,%.1f,%.1f radius=%.1f column=%d aimAssist=%d anchor=%d",
        className.c_str(), modelName.c_str(), maxError, state.ignoreRelativePitch ? 1 : 0,
        state.hasPreferredAngles ? 1 : 0, state.preferredAngles.x, state.preferredAngles.y,
        state.preferredAngles.z, state.angleAlignment, state.loadWeight, state.distanceOffset,
        state.allowOverhead ? 1 : 0, state.attachedAngles.x, state.attachedAngles.y,
        state.attachedAngles.z, radius, column ? 1 : 0, aimAssist ? 1 : 0, anchored ? 1 : 0);
}

inline std::string FormatSample(float seconds, const Vec3 &hand, const Vec3 &handAngles,
                                const Vec3 &headAngles, const Vec3 &object, const Vec3 &objectAngles,
                                const GrabState &state)
{
    return FormatLine("hold t=%.2f hand=%.1f,%.1f,%.1f handAng=%.1f,%.1f,%.1f headAng=%.1f,%.1f,%.1f "
        "target=%.1f,%.1f,%.1f targetAng=%.1f,%.1f,%.1f object=%.1f,%.1f,%.1f objectAng=%.1f,%.1f,%.1f "
        "attached=%.1f,%.1f,%.1f distanceOffset=%.1f error=%.2f",
        seconds, hand.x, hand.y, hand.z, handAngles.x, handAngles.y, handAngles.z,
        headAngles.x, headAngles.y, headAngles.z, state.targetPosition.x, state.targetPosition.y,
        state.targetPosition.z, state.targetAngles.x, state.targetAngles.y, state.targetAngles.z,
        object.x, object.y, object.z, objectAngles.x, objectAngles.y, objectAngles.z,
        state.attachedAngles.x, state.attachedAngles.y, state.attachedAngles.z,
        state.distanceOffset, state.error);
}

inline std::string FormatDrop(DropReason reason, const Trace::Summary &summary)
{
    return FormatLine("drop reason=%s seconds=%.2f ticks=%d vmTicks=%d meanError=%.2f maxError=%.2f lastError=%.2f",
        reason == DropReason::Forced ? "forced" : "released", summary.seconds, summary.ticks,
        summary.vmTicks, summary.meanError, summary.maxError, summary.lastError);
}

} // namespace HeldObject
