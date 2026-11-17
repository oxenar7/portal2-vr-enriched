#pragma once

#include "sdk/vector.h"
#include <cmath>
#include <optional>

// Tracking positions are Source-oriented meters until converted at this boundary.
namespace TrackingSpace {

inline Vector OpenVrToSourceMeters(const Vector &openVrMeters)
{
    return {-openVrMeters.z, -openVrMeters.x, openVrMeters.y};
}

inline Vector SourceToOpenVrMeters(const Vector &sourceMeters)
{
    return {-sourceMeters.y, sourceMeters.z, -sourceMeters.x};
}

inline Vector RotateYawDegrees(const Vector &sourceVector, float degrees)
{
    const float radians = DEG2RAD(degrees);
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);
    return {sourceVector.x * cosine - sourceVector.y * sine,
            sourceVector.x * sine + sourceVector.y * cosine,
            sourceVector.z};
}

enum class TrackingMode { Seated, Standing };
enum class MovementDirection { Hmd, LeftController, RightController };

struct DeviceDirection {
    bool valid = false;
    Vector forward{0.0f, 0.0f, 0.0f};
};

inline std::optional<Vector> HorizontalDirection(const DeviceDirection &device)
{
    if (!device.valid || !std::isfinite(device.forward.x) ||
        !std::isfinite(device.forward.y) || !std::isfinite(device.forward.z))
        return std::nullopt;
    const float length = std::sqrt(device.forward.x * device.forward.x +
                                   device.forward.y * device.forward.y);
    if (!std::isfinite(length) || length < 0.05f)
        return std::nullopt;
    return Vector{device.forward.x / length, device.forward.y / length, 0.0f};
}

inline Vector SelectMovementForward(MovementDirection mode, const DeviceDirection &hmd,
                                    const DeviceDirection &left, const DeviceDirection &right)
{
    const DeviceDirection *selected = &hmd;
    if (mode == MovementDirection::LeftController)
        selected = &left;
    else if (mode == MovementDirection::RightController)
        selected = &right;
    if (const auto direction = HorizontalDirection(*selected))
        return *direction;
    if (const auto fallback = HorizontalDirection(hmd))
        return *fallback;
    return {0.0f, 0.0f, 0.0f};
}

struct MoveAxes {
    float forward = 0.0f;
    float side = 0.0f;
};

inline MoveAxes RebaseAnalogToView(float stickX, float stickY,
                                   const Vector &selectedForward, const Vector &hmdForward)
{
    const auto selected = HorizontalDirection({true, selectedForward});
    const auto hmd = HorizontalDirection({true, hmdForward});
    if (!selected || !hmd)
        return {stickY, stickX};
    const Vector selectedRight{selected->y, -selected->x, 0.0f};
    const Vector hmdRight{hmd->y, -hmd->x, 0.0f};
    const Vector worldMove = *selected * stickY + selectedRight * stickX;
    return {worldMove.x * hmd->x + worldMove.y * hmd->y,
            worldMove.x * hmdRight.x + worldMove.y * hmdRight.y};
}

// The movement keys a command's axes stand for. Source's ComputeForwardMove and
// ComputeSideMove add forwardmove for +forward and sidemove for +moveright, so
// the key bits of a command must carry the same signs as its axes.
struct MoveButtons {
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
};

inline MoveButtons ButtonsForAxes(float forwardMove, float sideMove)
{
    return {forwardMove > 0.0f, forwardMove < 0.0f, sideMove < 0.0f, sideMove > 0.0f};
}

inline std::optional<float> EyeHeightUnits(float eyeZ, float originZ)
{
    if (!std::isfinite(eyeZ) || !std::isfinite(originZ))
        return std::nullopt;
    const float height = eyeZ - originZ;
    if (!std::isfinite(height))
        return std::nullopt;
    return height;
}

// Temporary Standing anchor: track vertical motion relative to the HMD height at
// player entry/recenter. This does not claim to measure the Source avatar's eye
// offset; the client-player vtable slots for that measurement are unverified.
inline std::optional<float> StandingEyeAnchorUnits(float hmdHeightMeters, float scale)
{
    if (!std::isfinite(hmdHeightMeters) || hmdHeightMeters < 0.2f ||
        hmdHeightMeters > 3.0f || !std::isfinite(scale) || scale <= 0.0f)
        return std::nullopt;
    const float height = hmdHeightMeters * scale;
    return std::isfinite(height) ? std::optional<float>(height) : std::nullopt;
}

inline Vector ControllerWorldOrigin(const Vector &setupOrigin, const Vector &controllerRelative,
                                    const Vector &hmdOffset, bool sixDof)
{
    return setupOrigin + controllerRelative + (sixDof ? hmdOffset : Vector{0.0f, 0.0f, 0.0f});
}

struct PlayspaceState {
    Vector centerMeters{0.0f, 0.0f, 0.0f};
    Vector translationUnits{0.0f, 0.0f, 0.0f};
    float yawDegrees = 0.0f;
    float scale = 43.2f;
    float heightOffsetMeters = 0.0f;
    TrackingMode mode = TrackingMode::Seated;

    void Recenter(const Vector &hmdMeters)
    {
        centerMeters.x = hmdMeters.x;
        centerMeters.y = hmdMeters.y;
        if (mode == TrackingMode::Seated)
            centerMeters.z = hmdMeters.z;
        translationUnits = {0.0f, 0.0f, 0.0f};
    }

    Vector HmdOffsetUnits(const Vector &hmdMeters, float eyeHeightUnits) const
    {
        const Vector rotated = RotateYawDegrees(hmdMeters - centerMeters, yawDegrees);
        Vector offset = rotated * scale + translationUnits;
        if (mode == TrackingMode::Standing)
            offset.z = (hmdMeters.z + heightOffsetMeters) * scale - eyeHeightUnits + translationUnits.z;
        return offset;
    }

    Vector ControllerOffsetUnits(const Vector &controllerMeters, const Vector &hmdMeters,
                                 float eyeHeightUnits) const
    {
        return HmdOffsetUnits(hmdMeters, eyeHeightUnits) +
               RotateYawDegrees(controllerMeters - hmdMeters, yawDegrees) * scale;
    }

    std::optional<Vector> ControllerRelativeOffsetUnits(bool valid, const Vector &controllerMeters,
                                                        const Vector &hmdMeters, float eyeHeightUnits) const
    {
        if (!valid)
            return std::nullopt;
        return ControllerOffsetUnits(controllerMeters, hmdMeters, eyeHeightUnits) -
               HmdOffsetUnits(hmdMeters, eyeHeightUnits);
    }

    void TurnAboutHmd(float deltaDegrees, const Vector &hmdMeters, float eyeHeightUnits)
    {
        const Vector before = HmdOffsetUnits(hmdMeters, eyeHeightUnits);
        yawDegrees += deltaDegrees;
        const Vector after = HmdOffsetUnits(hmdMeters, eyeHeightUnits);
        translationUnits.x += before.x - after.x;
        translationUnits.y += before.y - after.y;
    }

    void PreserveOffsetOnRecovery(const Vector &freshHmdMeters,
                                  const Vector &lastRenderedOffsetUnits, float eyeHeightUnits)
    {
        const Vector correction = lastRenderedOffsetUnits - HmdOffsetUnits(freshHmdMeters, eyeHeightUnits);
        translationUnits += correction;
    }
};

} // namespace TrackingSpace
