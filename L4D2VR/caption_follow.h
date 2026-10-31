#pragma once
#include <cmath>
#include <optional>

// Caption panel placement in raw OpenVR meters. The panel sits on a sphere
// around the head and faces it: yaw and pitch stay put while the gaze is
// inside the deadzone, so reading never drags the text, and ease back to the
// gaze once it leaves. Roll is ignored to keep the text level.
class CaptionFollow
{
public:
    struct Settings {
        float deadzoneDegrees;
        float speed; // ease rate per second
        float distanceMeters;
        float verticalOffsetMeters; // along the panel's own up axis, so it stays relative to the gaze
    };
    struct Pose { float m[3][4]; };

    void Reset() { m_Valid = false; }

    std::optional<Pose> Update(const float (&hmd)[3][4], float dt, const Settings &settings)
    {
        for (const auto &row : hmd)
            for (float value : row)
                if (!std::isfinite(value))
                    return std::nullopt;

        const float horizontal = std::hypot(hmd[0][2], hmd[2][2]);
        const bool headingUsable = horizontal >= kMinHorizontal; // straight up or down has no heading
        const float headYaw = std::atan2(hmd[0][2], hmd[2][2]); // OpenVR forward is -Z
        const float headPitch = std::atan2(-hmd[1][2], horizontal);
        if (!m_Valid) {
            if (!headingUsable)
                return std::nullopt;
            m_Yaw = {headYaw, false};
            m_Pitch = {headPitch, false};
            for (int i = 0; i < 3; ++i)
                m_Position[i] = hmd[i][3];
            m_Valid = true;
        } else {
            const float blend = 1.0f - std::exp(-dt * settings.speed);
            const float deadzone = settings.deadzoneDegrees * kRadians;
            for (int i = 0; i < 3; ++i)
                m_Position[i] += (hmd[i][3] - m_Position[i]) * blend;
            if (headingUsable)
                m_Yaw.Ease(std::remainder(headYaw - m_Yaw.value, 2.0f * kPi), deadzone, blend);
            m_Pitch.Ease(headPitch - m_Pitch.value, deadzone, blend);
        }

        const float cy = std::cos(m_Yaw.value), sy = std::sin(m_Yaw.value);
        const float cp = std::cos(m_Pitch.value), sp = std::sin(m_Pitch.value);
        const float d = settings.distanceMeters, v = settings.verticalOffsetMeters;
        return Pose{{{cy, sy * sp, sy * cp, m_Position[0] + sy * (sp * v - cp * d)},
                     {0.0f, cp, -sp, m_Position[1] + cp * v + sp * d},
                     {-sy, cy * sp, cy * cp, m_Position[2] + cy * (sp * v - cp * d)}}};
    }

private:
    static constexpr float kPi = 3.14159265f;
    static constexpr float kRadians = kPi / 180.0f;
    static constexpr float kMinHorizontal = 0.2f;
    static constexpr float kSettledRadians = 2.0f * kRadians;

    struct Axis {
        float value = 0.0f;
        bool recentering = false;
        void Ease(float delta, float deadzone, float blend) {
            if (std::fabs(delta) > deadzone)
                recentering = true;
            if (!recentering)
                return;
            value += delta * blend;
            if (std::fabs(delta) < kSettledRadians)
                recentering = false;
        }
    };

    bool m_Valid = false;
    Axis m_Yaw, m_Pitch;
    float m_Position[3] = {};
};
