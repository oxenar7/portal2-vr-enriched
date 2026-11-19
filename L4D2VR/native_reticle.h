#pragma once

#include "aim_feedback.h"
#include <cstdint>
#include <limits>
#include <optional>

// Installed Portal 2 x86 vguimatsurface.dll evidence; NOT the SDK 2013 ABI.
// AA10 translates the rectangle then clips it against the current HUD panel.
// 7A10 sets its orthographic canvas from the active render-context viewport.
namespace NativeReticle
{
inline float DistanceScale(bool enabled, float distanceUnits, float unitsPerMeter)
{
    if (!enabled || !std::isfinite(distanceUnits) || distanceUnits < 0.0f ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0f)
        return 1.0f;
    // Keep the HUD readable: smooth, bounded reduction over 1-10 real meters.
    const double meters = static_cast<double>(distanceUnits) / unitsPerMeter;
    const double t = std::clamp((meters - 1.0) / 9.0, 0.0, 1.0);
    return static_cast<float>(1.0 - 0.2 * t * t * (3.0 - 2.0 * t));
}

struct SpriteLayout
{
    AimFeedback::ScreenPoint position;
    int width, height;
};

inline std::optional<SpriteLayout> ScaleLayout(const AimFeedback::ScreenPoint &position,
    const AimFeedback::ProjectedPoint &hit, int width, int height, float scale)
{
    if (width <= 0 || height <= 0 || width > 192 || height > 192 ||
        !std::isfinite(scale) || scale < 0.8f || scale > 1.0f ||
        !std::isfinite(hit.x) || !std::isfinite(hit.y))
        return std::nullopt;
    // Scale all status layers about the shared world hit, not individual
    // sprite centers. Leave Source's atlas UV, artwork, color and alpha alone.
    const double x = std::round(hit.x + (static_cast<double>(position.x) - hit.x) * scale);
    const double y = std::round(hit.y + (static_cast<double>(position.y) - hit.y) * scale);
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
        return std::nullopt;
    return SpriteLayout{{static_cast<int>(x), static_cast<int>(y)},
        (std::max)(1, static_cast<int>(std::lround(width * scale))),
        (std::max)(1, static_cast<int>(std::lround(height * scale)))};
}

constexpr std::uintptr_t kSurfaceTranslationOffset = 0xC;
constexpr std::size_t kDrawTexturedSubRectSlot = 103;
constexpr std::size_t kGetScreenSizeSlot = 42;
// The clip setter starts by comparing its first argument with the clip rect global:
// cmp ds:[clipRect], eax at this offset, so the global is checked against the code.
constexpr std::size_t kClipSetterRectCompare = 16;

// One verified vguimatsurface.dll build: header identity, CMatSystemSurface table,
// the two slots the reticle route calls, the cdecl clip setter and the clip rect global.
struct SurfaceBuild
{
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    std::uint32_t vtable;
    std::uint32_t drawTexturedSubRect;
    std::uint32_t getScreenSize;
    std::uint32_t clipSetter;
    std::uint32_t clipRect;
};

inline constexpr SurfaceBuild kSurfaceBuilds[] = {
    {0x6A4466CE, 0x198000, 0xC4ED4, 0xAA10, 0xB8C0, 0x1010, 0x13B680}, // Portal 2
    {0x65D4091C, 0x197000, 0xC4ED4, 0xA950, 0xB7B0, 0x1010, 0x13B3E0}, // Portal: Alive & Kicking
};

inline const SurfaceBuild *FindSurfaceBuild(std::uint32_t timestamp, std::uint32_t imageSize)
{
    for (const auto &build : kSurfaceBuilds)
        if (build.timestamp == timestamp && build.imageSize == imageSize) return &build;
    return nullptr;
}

struct SurfaceProbe
{
    std::uintptr_t module;
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    std::uintptr_t vtable;
    std::uintptr_t drawTexturedSubRect;
    std::uintptr_t getScreenSize;
};

inline const SurfaceBuild *Match(const SurfaceProbe &probe)
{
    const SurfaceBuild *build = FindSurfaceBuild(probe.timestamp, probe.imageSize);
    if (!probe.module || !build || probe.module > UINTPTR_MAX - probe.imageSize ||
        probe.vtable != probe.module + build->vtable ||
        probe.drawTexturedSubRect != probe.module + build->drawTexturedSubRect ||
        probe.getScreenSize != probe.module + build->getScreenSize)
        return nullptr;
    return build;
}

inline bool Supported(const SurfaceProbe &probe) { return Match(probe) != nullptr; }

struct ClipRect
{
    int left, top, right, bottom;

    bool Contains(int x, int y, int width, int height) const
    {
        return width > 0 && height > 0 && x >= left && y >= top &&
            static_cast<std::int64_t>(x) + width <= right &&
            static_cast<std::int64_t>(y) + height <= bottom;
    }
};

inline std::optional<AimFeedback::ScreenPoint> SurfacePosition(
    const AimFeedback::ScreenPoint &eyePosition, int translationX, int translationY)
{
    const auto x = static_cast<std::int64_t>(eyePosition.x) - translationX;
    const auto y = static_cast<std::int64_t>(eyePosition.y) - translationY;
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
        return std::nullopt;
    return AimFeedback::ScreenPoint{static_cast<int>(x), static_cast<int>(y)};
}

// SurfaceAccess supplies the verified native cdecl clip setter. No direct
// writes to game object/global fields, and no change to clipping enable state.
template<class SurfaceAccess>
class ClipScope
{
public:
    ClipScope(SurfaceAccess &surface, const ClipRect &previous, const ClipRect &eye)
        : m_Surface(surface), m_Previous(previous) { m_Surface.SetClipRect(eye); }
    ~ClipScope() { m_Surface.SetClipRect(m_Previous); }
    ClipScope(const ClipScope &) = delete;
    ClipScope &operator=(const ClipScope &) = delete;
private:
    SurfaceAccess &m_Surface;
    ClipRect m_Previous;
};
} // namespace NativeReticle
