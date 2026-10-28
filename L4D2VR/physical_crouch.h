#pragma once

#include <algorithm>
#include <cmath>

// Physical crouch: hold IN_DUCK while the tracked head is well below its calibrated
// height, and keep the camera on the real head while Source lowers its own eye offset.
// Without the compensation the view would drop twice: once from the head and once
// from Source's ducked eye height.
namespace PhysicalCrouch {

// Hysteresis so a head hovering at the threshold does not toggle duck every frame.
inline constexpr float kReleaseMarginMeters = 0.05f;
// After the head comes back up, Source may stay ducked under a low ceiling. Keep the
// real-head camera this long, then fall back to Source's ducked eye.
inline constexpr float kBlockedHoldSeconds = 1.5f;
// Maximum camera correction speed in units/s. Source's own duck transition is slower,
// so this only smooths landings and the blocked-stand fallback.
inline constexpr float kMaxCompensationSpeed = 200.0f;

class State
{
public:
    // headDropMeters: how far the head is below its calibrated height (positive = lower).
    bool UpdateIntent(bool eligible, float headDropMeters, float thresholdMeters)
    {
        if (!eligible || !std::isfinite(headDropMeters) || !std::isfinite(thresholdMeters) ||
            thresholdMeters <= kReleaseMarginMeters) {
            m_Ducking = false;
            return false;
        }
        m_Ducking = m_Ducking ? headDropMeters > thresholdMeters - kReleaseMarginMeters :
                                headDropMeters >= thresholdMeters;
        return m_Ducking;
    }

    bool Ducking() const { return m_Ducking; }
    float Compensation() const { return m_Compensation; }

    // Returns units to add to the camera height. viewOffsetZ is Source's current eye
    // height above the player origin. A crouch-button duck is never compensated: the
    // compensation only grows while the physical crouch is holding IN_DUCK.
    float UpdateCompensation(float viewOffsetZ, bool onGround, float deltaSeconds)
    {
        if (!std::isfinite(viewOffsetZ) || viewOffsetZ <= 0.0f ||
            !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
            Reset();
            return 0.0f;
        }
        // Source's standing eye is the highest view offset it ever reports.
        m_StandingViewZ = (std::max)(m_StandingViewZ, viewOffsetZ);
        const float lowered = m_StandingViewZ - viewOffsetZ;

        float target = m_Compensation;
        if (!onGround) {
            // An airborne duck raises the origin instead of lowering the eye; hold until landing.
        } else if (m_Ducking) {
            target = lowered;
            m_ReleasedSeconds = 0.0f;
        } else {
            m_ReleasedSeconds += deltaSeconds;
            target = m_ReleasedSeconds < kBlockedHoldSeconds ? (std::min)(m_Compensation, lowered) : 0.0f;
        }

        const float step = kMaxCompensationSpeed * deltaSeconds;
        m_Compensation += std::clamp(target - m_Compensation, -step, step);
        m_Compensation = (std::max)(m_Compensation, 0.0f);
        return m_Compensation;
    }

    void Reset()
    {
        m_Ducking = false;
        m_Compensation = 0.0f;
        m_StandingViewZ = 0.0f;
        m_ReleasedSeconds = 0.0f;
    }

private:
    bool m_Ducking = false;
    float m_Compensation = 0.0f;
    float m_StandingViewZ = 0.0f;
    float m_ReleasedSeconds = 0.0f;
};

} // namespace PhysicalCrouch
