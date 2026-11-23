#pragma once

#include <chrono>
#include <cstdint>

// What +use would act on while the gun aims it (ControllerAimedUse). The server's use
// search reports it once a tick; the aim line and the gun hand show it before the press.
namespace UseTarget {

struct Color {
    int r, g, b;
};

// Blue to Aperture orange: a hue swap that a one-pixel line still shows. A light green
// beside the blue was too close in brightness to notice in the headset.
inline Color LineColor(bool hasTarget)
{
    return hasTarget ? Color{255, 140, 0} : Color{64, 200, 255};
}

// On a target the line also widens: extra strands fan out from the gun by this angle
// (radians), about 0.15 degrees, a few pixels wide anywhere along the beam.
inline constexpr float kStrandSpread = 0.0025f;

// A pick off the gun's line stands when it lies near the line, but not when it hides behind the
// surface the line stops on: Mel's tram floats on a usable, passable func_tanktrain box under its
// floor, which counted as aimed at whenever the hand pointed down (headset test 2026-11-23).
// Distances from the gun to the line's hit and to the pick's nearest point; a few units of
// slack keep a button flush with the wall the line grazes.
inline constexpr float kBehindSurfaceSlack = 6.0f;

inline bool HiddenBehindHit(float hitDistance, float pickDistance)
{
    return pickDistance > hitDistance + kBehindSurfaceSlack;
}

// The server entity's ObjectCaps (CBaseEntity vtable slot 39) and its use bits in this engine
// branch: 1 impulse, 2 continuous, 4 on/off, 8 directional. Directional alone is a train's
// driving control, and a child inherits its parent's use bits: Mel's tram body, floor and doors
// ride a func_tanktrain, so the use search took them while a press does nothing (headset test
// 2026-11-23, the line stayed orange in the whole tram). Checked by RTTI in the server.dll of
// Portal 2, Alive & Kicking, Mel, Time Machine and Reloaded.
inline constexpr int kObjectCapsSlot = 39;
inline constexpr int kPressUseCaps = 0x7;

inline bool PressUsable(int objectCaps)
{
    return (objectCaps & kPressUseCaps) != 0;
}

class Hover {
public:
    using Clock = std::chrono::steady_clock;

    // Returns true when the gun has just settled on a usable entity: a new one, or the
    // same one after a real absence. Grazing its edge back and forth stays silent.
    bool Observe(std::uintptr_t entity, Clock::time_point now)
    {
        const bool returned = m_Entity == 0 && m_HasLeft && now - m_LeftAt >= kReturnGap;
        const bool tick = entity != 0 && entity != m_Entity &&
            (entity != m_Ticked || returned) && (!m_HasTick || now - m_TickAt >= kTickGap);
        if (entity == 0 && m_Entity != 0) {
            m_LeftAt = now;
            m_HasLeft = true;
        }
        m_Entity = entity;
        m_SeenAt = now;
        m_HasSeen = true;
        if (tick) {
            m_Ticked = entity;
            m_TickAt = now;
            m_HasTick = true;
        }
        return tick;
    }

    // False once the reports stop (pause, menu, option off), so the line never stays lit.
    bool Active(Clock::time_point now) const
    {
        return m_Entity != 0 && m_HasSeen && now >= m_SeenAt && now - m_SeenAt <= kStale;
    }

private:
    static constexpr auto kTickGap = std::chrono::milliseconds(100);
    static constexpr auto kReturnGap = std::chrono::milliseconds(400);
    static constexpr auto kStale = std::chrono::milliseconds(250);

    std::uintptr_t m_Entity = 0;
    std::uintptr_t m_Ticked = 0;
    bool m_HasSeen = false, m_HasLeft = false, m_HasTick = false;
    Clock::time_point m_SeenAt{}, m_LeftAt{}, m_TickAt{};
};

} // namespace UseTarget
