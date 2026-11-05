#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

enum class RenderDiagnosticEvent : std::size_t
{
    TrackingBypass,
    RenderTargetBypass,
    CursorBypass,
    StereoRendered,
    MenuSubmission,
    StereoSubmission,
    OverlayPlacementSucceeded,
    OverlayPlacementFailed,
    HudPaintEntered,
    HudPaintInGame,
    HudPaintEligible,
    HudPaintNoRedirect,
    HudPushOutsidePaint,
    HudPushInPaint,
    HudRedirected,
    HudExplicitCapture,
    HudExplicitUnexpectedPush,
    HudExplicitContextUnavailable,
    HudOverlayShown,
    HudFollowFailed,
    HudAlphaBoostFailed,
    CrosshairShouldDrawTrue,
    CrosshairShouldDrawFalse,
    CrosshairTransformTrue,
    CrosshairTransformFalse,
    CrosshairHudDraw,
    CrosshairWorldLeftEye,
    CrosshairWorldRightEye,
    CrosshairWorldProjectionSkipped,
    CrosshairWorldUnknownIcon,
    CrosshairWorldOutsideStereo,
    CrosshairWorldPortalStatus,
    CrosshairWorldPortalStatusOutsideStereo,
    CrosshairCanvasInside,
    CrosshairCanvasOutsideX,
    CrosshairCanvasOutsideY,
    CrosshairCanvasOutsideBoth,
    CrosshairCanvasInvalid,
    CrosshairDirectAtlasUnavailable,
    CrosshairDirectMaterialUnavailable,
    CrosshairDirectEyeTargetUnavailable,
    CrosshairDirectViewportUnavailable,
    CrosshairDirectColorUnavailable,
    CrosshairDirectAbiUnsupported,
    LaserControlPoints,
    NativeReticleDraw,
    NativeReticleUnavailable,
    NativeReticleCaptureSuppressed,
    NativeBeamPlayerOwned,
    NativeBeamManualOrigin,
    ZoomStateBlockUnavailable,
    ZoomMaskApplied,
    ZoomMaskFailed,
    ZoomClickWhileTurning,
    Count
};

class RenderDiagnosticGate
{
public:
    bool First(RenderDiagnosticEvent event)
    {
        const auto index = static_cast<std::size_t>(event);
        if (m_Seen[index])
            return false;
        m_Seen[index] = true;
        return true;
    }

private:
    std::array<bool, static_cast<std::size_t>(RenderDiagnosticEvent::Count)> m_Seen{};
};

// Menu/workshop refreshes can recreate the same geometry repeatedly. Keep
// normal logs useful without changing resource allocation or hiding failures.
class RenderTargetDiagnosticGate
{
public:
    bool Allocation(std::uint32_t width, std::uint32_t height, bool verbose)
    {
        return Observe(m_Allocation, std::array<double, 2>{double(width), double(height)}, verbose);
    }

    bool Projection(int windowWidth, int windowHeight, std::uint32_t width,
                    std::uint32_t height, float aspect, float fov, bool verbose)
    {
        return Observe(m_Projection, std::array<double, 6>{double(windowWidth), double(windowHeight),
            double(width), double(height), aspect, fov}, verbose);
    }

    bool HudBounds(int windowWidth, int windowHeight, std::uint32_t width,
                   std::uint32_t height, float uMax, float vMax, int error, bool verbose)
    {
        const bool changed = Observe(m_HudBounds, std::array<double, 7>{double(windowWidth),
            double(windowHeight), double(width), double(height), uMax, vMax, double(error)}, verbose);
        return error != 0 || changed;
    }

private:
    template<std::size_t N>
    static bool Observe(std::optional<std::array<double, N>> &previous,
                        const std::array<double, N> &current, bool verbose)
    {
        const bool changed = !previous || *previous != current;
        previous = current;
        return verbose || changed;
    }

    std::optional<std::array<double, 2>> m_Allocation;
    std::optional<std::array<double, 6>> m_Projection;
    std::optional<std::array<double, 7>> m_HudBounds;
};
