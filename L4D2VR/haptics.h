#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <mutex>

namespace Haptics {

enum class Hand { Left, Right };
enum class Target { GunHand, OffHand, BothHands };

struct Pulse {
    Target target = Target::GunHand;
    float delaySeconds = 0.0f;
    float durationSeconds = 0.0f;
    float frequency = 150.0f;
    float amplitude = 0.0f;
};

struct Pattern {
    std::array<Pulse, 2> pulses{};
    int count = 0;
    bool mapped = true;
};

// Source SDK 2013 rumble_shared.h flags; client.dll CRumbleEffects uses the same bits.
constexpr int kRumbleStop = 0x01;
constexpr int kRumbleUpdateScale = 0x08;
constexpr int kRumbleInitialScale = 0x40;

// Effect indices Portal 2 server.dll actually sends (offline xref of CBasePlayer::RumbleEffect).
enum NativeRumble {
    kRumbleStopAll = 0,
    kRumbleImpactLight = 1,       // RUMBLE_PISTOL slot, VPhysicsCollision
    kRumbleImpactHeavy = 2,       // RUMBLE_357 slot, VPhysicsCollision
    kRumblePortalgunFizzle = 8,   // RUMBLE_RPG_MISSILE slot, "Portal.FizzlerShimmy"
    kRumbleFallLong = 18,
    kRumbleFallShort = 19,
    kRumblePortalgunLeft = 25,
    kRumblePortalgunRight = 26,
    kRumblePlacementFailure = 27,
};

enum class Event { Pickup, Drop, Button, ButtonLocked, FloorPress, FloorRelease };

inline Pattern Single(Target target, float duration, float frequency, float amplitude)
{
    Pattern pattern;
    pattern.pulses[0] = {target, 0.0f, duration, frequency, amplitude};
    pattern.count = 1;
    return pattern;
}

inline Pattern Double(Target target, float gap, float duration, float frequency, float amplitude)
{
    Pattern pattern = Single(target, duration, frequency, amplitude);
    pattern.pulses[1] = {target, gap, duration, frequency, amplitude};
    pattern.count = 2;
    return pattern;
}

// Returns no pulses for stop and scale-update messages: OpenVR pulses are short
// and end on their own, so there is no running effect to cancel or rescale.
inline Pattern NativeRumblePattern(int index, int data, int flags)
{
    if (index == kRumbleStopAll || (flags & kRumbleStop) || (flags & kRumbleUpdateScale))
        return {};
    Pattern pattern;
    switch (index) {
    case kRumbleImpactLight: pattern = Single(Target::BothHands, 0.04f, 120.0f, 0.35f); break;
    case kRumbleImpactHeavy: pattern = Single(Target::BothHands, 0.06f, 100.0f, 0.6f); break;
    case kRumblePortalgunFizzle: pattern = Single(Target::GunHand, 0.15f, 60.0f, 0.5f); break;
    case kRumbleFallLong: pattern = Single(Target::BothHands, 0.12f, 80.0f, 0.8f); break;
    case kRumbleFallShort: pattern = Single(Target::BothHands, 0.06f, 100.0f, 0.4f); break;
    case kRumblePortalgunLeft:
    case kRumblePortalgunRight: pattern = Single(Target::GunHand, 0.04f, 150.0f, 0.45f); break;
    case kRumblePlacementFailure: pattern = Double(Target::GunHand, 0.08f, 0.03f, 120.0f, 0.3f); break;
    default:
        pattern = Single(Target::GunHand, 0.04f, 150.0f, 0.25f);
        pattern.mapped = false;
        break;
    }
    if (flags & kRumbleInitialScale) {
        const float scale = std::clamp(static_cast<float>(data) / 100.0f, 0.0f, 1.0f);
        for (auto &pulse : pattern.pulses) pulse.amplitude *= scale;
    }
    return pattern;
}

inline Pattern EventPattern(Event event)
{
    switch (event) {
    case Event::Pickup: return Single(Target::GunHand, 0.06f, 120.0f, 0.5f);
    case Event::Drop: return Single(Target::GunHand, 0.04f, 120.0f, 0.3f);
    case Event::Button: return Single(Target::GunHand, 0.08f, 100.0f, 0.7f);
    case Event::ButtonLocked: return Double(Target::GunHand, 0.07f, 0.03f, 120.0f, 0.25f);
    case Event::FloorPress: return Single(Target::BothHands, 0.06f, 80.0f, 0.45f);
    case Event::FloorRelease: return Single(Target::BothHands, 0.04f, 80.0f, 0.25f);
    }
    return {};
}

constexpr float kHoldAmplitude = 0.065f;
constexpr float kHoldPulseSeconds = 0.06f;
constexpr auto kHoldPulseInterval = std::chrono::milliseconds(50);

// Keeps every OpenVR request inside the bounds used by the portal-shot path.
inline Pulse ScaledPulse(Pulse pulse, float intensity)
{
    const float scale = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f;
    const float amplitude = std::isfinite(pulse.amplitude) ? pulse.amplitude : 0.0f;
    pulse.amplitude = std::clamp(amplitude * scale, 0.0f, 1.0f);
    pulse.durationSeconds = std::clamp(pulse.durationSeconds, 0.005f, 0.25f);
    pulse.delaySeconds = std::clamp(pulse.delaySeconds, 0.0f, 0.25f);
    pulse.frequency = std::clamp(pulse.frequency, 1.0f, 320.0f);
    return pulse;
}

// Game hooks run outside the VR update; they only queue. Repeats of the same
// key inside the window and overflow beyond the capacity are dropped.
class EventQueue {
public:
    static constexpr std::size_t kCapacity = 8;

    bool Push(int key, const Pattern &pattern, std::chrono::steady_clock::time_point now)
    {
        if (!pattern.count) return false;
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (auto &recent : m_Recent) {
            if (recent.used && recent.key == key &&
                now - recent.at < std::chrono::milliseconds(60))
                return false;
        }
        if (m_Count == kCapacity) return false;
        m_Pending[m_Count++] = pattern;
        const auto age = [](const Recent &recent) {
            return recent.used ? recent.at : (std::chrono::steady_clock::time_point::min)();
        };
        auto &slot = *std::min_element(m_Recent.begin(), m_Recent.end(),
            [&](const Recent &a, const Recent &b) { return age(a) < age(b); });
        slot = {true, key, now};
        return true;
    }

    std::size_t Drain(std::array<Pattern, kCapacity> &out)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const std::size_t count = m_Count;
        std::copy_n(m_Pending.begin(), count, out.begin());
        m_Count = 0;
        return count;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Count = 0;
    }

private:
    struct Recent {
        bool used = false;
        int key = 0;
        std::chrono::steady_clock::time_point at{};
    };
    std::mutex m_Mutex;
    std::array<Pattern, kCapacity> m_Pending{};
    std::size_t m_Count = 0;
    std::array<Recent, kCapacity> m_Recent{};
};

// The server grab controller updates every tick while the local player holds
// an object. A missing update for kTimeout means the object was released.
class HoldTracker {
public:
    enum class Change { None, Pickup, Drop };
    static constexpr auto kTimeout = std::chrono::milliseconds(250);

    void Update(std::chrono::steady_clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_LastUpdate = now;
        m_Seen = true;
    }

    Change Poll(std::chrono::steady_clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const bool fresh = m_Seen && now - m_LastUpdate < kTimeout;
        if (fresh == m_Holding) return Change::None;
        m_Holding = fresh;
        return fresh ? Change::Pickup : Change::Drop;
    }

    bool Holding()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Holding;
    }

    // Menus and tracking loss pause the game ticks; do not report a drop for that.
    void Suspend(std::chrono::steady_clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Holding) m_LastUpdate = now;
    }

private:
    std::mutex m_Mutex;
    std::chrono::steady_clock::time_point m_LastUpdate{};
    bool m_Seen = false;
    bool m_Holding = false;
};

inline Hand OutputHand(bool leftHanded)
{
    return leftHanded ? Hand::Left : Hand::Right;
}

inline bool CanDeliverShot(bool enabled, bool actionsReady, bool gameplay,
                           bool trackingValid, bool controllerValid, bool outputAvailable)
{
    return enabled && actionsReady && gameplay && trackingValid &&
        controllerValid && outputAvailable;
}

inline bool IsLocalShot(int localIndex, int ownerIndex)
{
    return localIndex > 0 && ownerIndex == localIndex;
}

// The server-side fire hook can run independently of the render/update thread.
// Keep only one notification, and never call the VR runtime from that hook.
class ShotGate {
public:
    bool Queue(std::chrono::steady_clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Pending || (m_HasLast && now - m_LastQueued < std::chrono::milliseconds(100)))
            return false;
        m_LastQueued = now;
        m_HasLast = true;
        m_Pending = true;
        return true;
    }

    bool Consume(std::chrono::steady_clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const bool pending = m_Pending;
        m_Pending = false;
        if (!pending || (m_HasAttempted &&
            now - m_LastAttempted < std::chrono::milliseconds(100)))
            return false;
        m_LastAttempted = now;
        m_HasAttempted = true;
        return true;
    }

    bool Consume() { return Consume(std::chrono::steady_clock::now()); }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pending = false;
        m_HasLast = false;
    }

private:
    std::mutex m_Mutex;
    bool m_Pending = false;
    bool m_HasLast = false;
    std::chrono::steady_clock::time_point m_LastQueued{};
    bool m_HasAttempted = false;
    std::chrono::steady_clock::time_point m_LastAttempted{};
};

} // namespace Haptics
