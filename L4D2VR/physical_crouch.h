#pragma once

#include <algorithm>
#include <cmath>

// Physical crouch: hold IN_DUCK while the tracked head is well below the standing height.
// Source's duck stays visible: when the head reaches the threshold the camera settles on
// Source's ducked eye, then follows the head 1:1 below it. A real crouch is much shorter
// than Source's 36-unit eye drop (0.83 m at the default VRScale), so a camera kept on the
// real head would sit above the 36-unit ducked hull, inside the top of any gap it fits.
namespace PhysicalCrouch {

// g_DefaultViewVectors in client.dll and server.dll.
inline constexpr float kStandingEyeUnits = 64.0f;
inline constexpr float kDuckedEyeUnits = 28.0f;
// Hysteresis so a head hovering at the threshold does not toggle duck every frame.
inline constexpr float kReleaseMarginMeters = 0.05f;
// A quick dip still completes Source's duck instead of aborting it halfway.
inline constexpr float kMinDuckSeconds = 0.5f;
// A deep squat stops here so the camera stays above the floor.
inline constexpr float kMinEyeUnits = 14.0f;
// Standing up from a low recenter raises the standing height within a few seconds, while a
// headset lifted for a moment cannot raise it enough to leave the player ducked.
inline constexpr float kStandingRiseMetersPerSecond = 0.1f;
// Source unducks in about 0.2 s, twice as fast as it ducks, and the head is still low when
// it starts, so the camera would jump up by more than the head moved. The camera eases
// through the same travel on its own, staying inside the standing hull meanwhile: it keeps
// pace with the head, which ends the rise as early as the first value allows, and a head
// that rises slowly or stops midway finishes within the second one.
inline constexpr float kCameraRiseFastestSeconds = 0.25f;
inline constexpr float kCameraRiseSlowestSeconds = 0.45f;
// Maximum camera correction speed in units/s, used to smooth landings.
inline constexpr float kMaxCompensationSpeed = 200.0f;

class State
{
public:
    // Returns how far the head is below the standing height (positive = lower).
    float UpdateHeadDrop(float headMeters, float deltaSeconds)
    {
        if (!std::isfinite(headMeters) || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
            return NAN;
        if (!m_HasStandingHead) {
            m_StandingHead = headMeters;
            m_HasStandingHead = true;
        } else if (headMeters > m_StandingHead) {
            m_StandingHead = (std::min)(headMeters, m_StandingHead + kStandingRiseMetersPerSecond * deltaSeconds);
        }
        return m_StandingHead - headMeters;
    }

    bool UpdateIntent(bool eligible, float headDropMeters, float thresholdMeters, float deltaSeconds)
    {
        if (!eligible || !std::isfinite(headDropMeters) || !std::isfinite(thresholdMeters) ||
            thresholdMeters <= kReleaseMarginMeters || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
            m_Ducking = false;
            return false;
        }
        const bool wanted = m_Ducking ? headDropMeters > thresholdMeters - kReleaseMarginMeters :
                                        headDropMeters >= thresholdMeters;
        m_DuckedSeconds = m_Ducking ? m_DuckedSeconds + deltaSeconds : 0.0f;
        m_Ducking = wanted || (m_Ducking && m_DuckedSeconds < kMinDuckSeconds);
        return m_Ducking;
    }

    bool Ducking() const { return m_Ducking; }
    float Compensation() const { return m_Compensation; }
    float StandingHead() const { return m_StandingHead; }
    // True from releasing the physical crouch until the camera is back on Source's eye.
    bool CameraSettling() const { return m_PhysicalDuck && !m_Ducking; }

    // Returns units to add to the camera height. viewOffsetZ is Source's eye height above
    // the player origin in the frame being rendered: during the rise the compensation cancels
    // it, so an older value would show up as a vertical wobble. headOffsetUnits is the head
    // relative to its calibrated height. A crouch-button duck is never compensated: only a
    // duck the physical crouch started is. Source staying ducked under a low ceiling after
    // the head came back up needs no special case: the offset fades as the head rises.
    float UpdateCompensation(float viewOffsetZ, float headOffsetUnits, float thresholdUnits,
                             bool onGround, float deltaSeconds)
    {
        if (!std::isfinite(viewOffsetZ) || viewOffsetZ <= 0.0f || !std::isfinite(headOffsetUnits) ||
            !std::isfinite(thresholdUnits) || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
            Reset();
            return 0.0f;
        }
        const float lowered = std::clamp((kStandingEyeUnits - viewOffsetZ) /
            (kStandingEyeUnits - kDuckedEyeUnits), 0.0f, 1.0f);

        if (m_Ducking)
            m_PhysicalDuck = true;
        if (!onGround) {
            // An airborne duck raises the origin instead of lowering the eye; hold until landing.
            m_CameraLowered = m_RiseFrom = lowered;
            m_RiseSeconds = m_RiseProgress = 0.0f;
            m_RiseFromDropUnits = -headOffsetUnits;
            m_Landing = true;
            return m_Compensation;
        }

        float target = 0.0f;
        if (m_PhysicalDuck) {
            UpdateCameraLowered(lowered, -headOffsetUnits, deltaSeconds);
            const float cameraEye = kStandingEyeUnits - (kStandingEyeUnits - kDuckedEyeUnits) * m_CameraLowered;
            target = cameraEye + m_CameraLowered * (std::min)(-headOffsetUnits, thresholdUnits) - viewOffsetZ;
            if (m_Ducking)
                target = (std::max)(target, kMinEyeUnits - viewOffsetZ - headOffsetUnits);
            else if (lowered <= 0.0f && m_CameraLowered <= 0.0f)
                m_PhysicalDuck = false;
        }

        if (m_Landing) {
            const float step = kMaxCompensationSpeed * deltaSeconds;
            m_Compensation += std::clamp(target - m_Compensation, -step, step);
            m_Landing = m_Compensation != target;
        } else {
            m_Compensation = target;
        }
        return m_Compensation;
    }

    // The calibrated height changed: the next head sample becomes the standing height.
    void Recalibrate() { m_HasStandingHead = false; }

    void Reset()
    {
        m_Ducking = false;
        m_Compensation = 0.0f;
        m_DuckedSeconds = 0.0f;
        m_HasStandingHead = false;
        m_StandingHead = 0.0f;
        m_PhysicalDuck = false;
        m_Landing = false;
        m_CameraLowered = 0.0f;
        m_RiseFrom = 0.0f;
        m_RiseSeconds = 0.0f;
        m_RiseProgress = 0.0f;
        m_RiseFromDropUnits = 0.0f;
    }

private:
    // lowered is Source's progress from standing (0) to ducked (1). The camera follows it
    // down, and on the way up runs its own ease from wherever the rise started.
    void UpdateCameraLowered(float lowered, float headDropUnits, float deltaSeconds)
    {
        if (m_Ducking || lowered >= m_CameraLowered) {
            m_CameraLowered = (std::max)(m_CameraLowered, lowered);
            m_RiseFrom = m_CameraLowered;
            m_RiseSeconds = m_RiseProgress = 0.0f;
            m_RiseFromDropUnits = headDropUnits;
            return;
        }
        m_RiseSeconds += deltaSeconds;
        const float byHead = m_RiseFromDropUnits > 0.0f ?
            1.0f - std::clamp(headDropUnits / m_RiseFromDropUnits, 0.0f, 1.0f) : 1.0f;
        const float wanted = (std::max)(m_RiseSeconds / kCameraRiseSlowestSeconds, byHead);
        const float fastest = m_RiseProgress + deltaSeconds / kCameraRiseFastestSeconds;
        m_RiseProgress = std::clamp(wanted, m_RiseProgress, (std::min)(fastest, 1.0f));
        const float t = m_RiseProgress;
        m_CameraLowered = (std::max)(lowered, m_RiseFrom * (1.0f - t * t * (3.0f - 2.0f * t)));
    }

    bool m_Ducking = false;
    bool m_PhysicalDuck = false;
    bool m_Landing = false;
    bool m_HasStandingHead = false;
    float m_Compensation = 0.0f;
    float m_DuckedSeconds = 0.0f;
    float m_StandingHead = 0.0f;
    float m_CameraLowered = 0.0f;
    float m_RiseFrom = 0.0f;
    float m_RiseSeconds = 0.0f;
    float m_RiseProgress = 0.0f;
    float m_RiseFromDropUnits = 0.0f;
};

} // namespace PhysicalCrouch
