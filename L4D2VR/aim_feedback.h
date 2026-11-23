#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>
#include "sdk/vector.h"

namespace AimFeedback {


inline bool ShouldUseWorldAimMarker(int aimMode, bool enabled, bool overlayAvailable,
                                   bool controllerTracked, bool activeWeapon, bool cursorVisible)
{
    return aimMode == 2 && enabled && overlayAvailable && controllerTracked &&
           activeWeapon && !cursorVisible;
}

inline bool ShouldInspectActiveWeaponForAim(int aimMode, bool markerEnabled, bool overlayAvailable)
{
    return aimMode == 2 && markerEnabled && overlayAvailable;
}

struct WorldAimGeometry
{
    Vector beamEnd;
};

inline float WorldAimOverlayLifetime(float frameIntervalSeconds)
{
    // Retain roughly one frame of history so small timing jitter does not
    // cause gaps, without accumulating a long trail of old controller poses.
    if (!std::isfinite(frameIntervalSeconds) || frameIntervalSeconds <= 0.0f)
        frameIntervalSeconds = 1.0f / 90.0f;
    return std::clamp(frameIntervalSeconds * 1.25f, 0.006f, 0.05f);
}

struct ProjectedPoint
{
    float x;
    float y;
};

inline std::optional<ProjectedPoint> ProjectWorldToEye(
    const Vector &target, const Vector &eyeOrigin, const Vector &forward,
    const Vector &right, const Vector &up, float horizontalFovDegrees,
    float aspect, int width, int height)
{
    if (width <= 0 || height <= 0 || !std::isfinite(horizontalFovDegrees) ||
        horizontalFovDegrees <= 1.0f || horizontalFovDegrees >= 179.0f ||
        !std::isfinite(aspect) || aspect <= 0.0f)
        return std::nullopt;
    const Vector toTarget = target - eyeOrigin;
    const double depth = static_cast<double>(DotProduct(toTarget, forward));
    if (!std::isfinite(depth) || depth <= 0.01)
        return std::nullopt;
    constexpr double radiansPerDegree = 3.14159265358979323846 / 180.0;
    const double tanHalfX = std::tan(horizontalFovDegrees * radiansPerDegree * 0.5);
    const double horizontal = static_cast<double>(DotProduct(toTarget, right));
    const double vertical = static_cast<double>(DotProduct(toTarget, up));
    const double x = width * (0.5 + horizontal / (2.0 * depth * tanHalfX));
    const double y = height * (0.5 - vertical * aspect / (2.0 * depth * tanHalfX));
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || x >= width ||
        y < 0.0 || y >= height)
        return std::nullopt;
    return ProjectedPoint{static_cast<float>(x), static_cast<float>(y)};
}

inline bool IsCenteredReticleSprite(int x, int y, int width, int height,
                                    int windowWidth, int windowHeight)
{
    if (width <= 0 || height <= 0 || width > 192 || height > 192 ||
        windowWidth <= 0 || windowHeight <= 0)
        return false;
    const double centerX = static_cast<double>(x) + width * 0.5;
    const double centerY = static_cast<double>(y) + height * 0.5;
    return std::abs(centerX - windowWidth * 0.5) <= 128.0 &&
           std::abs(centerY - windowHeight * 0.5) <= 128.0;
}

inline bool ContainsAsciiInsensitive(std::string_view text, std::string_view needle)
{
    const auto lower = [](char value) {
        return value >= 'A' && value <= 'Z' ?
            static_cast<char>(value + ('a' - 'A')) : value;
    };
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
        [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
}

inline bool IsReticleIconName(std::string_view shortName, std::string_view textureFile)
{
    return ContainsAsciiInsensitive(shortName, "crosshair") ||
           ContainsAsciiInsensitive(textureFile, "crosshair") ||
           ContainsAsciiInsensitive(shortName, "qi_center") ||
           ContainsAsciiInsensitive(textureFile, "qi_center");
}

inline bool IsPortalStatusIconName(std::string_view shortName, std::string_view textureFile)
{
    return ContainsAsciiInsensitive(shortName, "portal_crosshair") ||
           ContainsAsciiInsensitive(textureFile, "portal_crosshair") ||
           ContainsAsciiInsensitive(shortName, "qi_center") ||
           ContainsAsciiInsensitive(textureFile, "qi_center");
}

struct AtlasTexels
{
    float x0, y0, x1, y1;
};

inline std::optional<AtlasTexels> SourceHudAtlasRect(
    const std::array<float, 4> &uv, int left, int right, int top, int bottom,
    int atlasWidth, int atlasHeight, int spriteWidth, int spriteHeight)
{
    if (atlasWidth <= 0 || atlasHeight <= 0 || atlasWidth > 4096 || atlasHeight > 4096 ||
        left < 0 || top < 0 || right <= left || bottom <= top ||
        right > atlasWidth || bottom > atlasHeight ||
        right - left != spriteWidth || bottom - top != spriteHeight)
        return std::nullopt;
    const std::array<float, 4> expected{
        (left + 0.5f) / atlasWidth, (top + 0.5f) / atlasHeight,
        (right - 0.5f) / atlasWidth, (bottom - 0.5f) / atlasHeight};
    for (size_t i = 0; i < uv.size(); ++i)
        if (!std::isfinite(uv[i]) || std::abs(uv[i] - expected[i]) > 0.001f)
            return std::nullopt;
    return AtlasTexels{static_cast<float>(left), static_cast<float>(top),
                       static_cast<float>(right - 1), static_cast<float>(bottom - 1)};
}

inline std::optional<WorldAimGeometry> PrepareWorldAimGeometry(
    const Vector &origin, const Vector &target)
{
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z))
        return std::nullopt;
    const Vector delta = target - origin;
    const double distance = std::sqrt(static_cast<double>(delta.x) * delta.x +
        static_cast<double>(delta.y) * delta.y + static_cast<double>(delta.z) * delta.z);
    if (!std::isfinite(distance) || distance < 0.01)
        return std::nullopt;

    return WorldAimGeometry{target};
}

// Four lines around the beam that leave the gun together and part by `spread` radians,
// so a one-pixel debug line reads as a band of roughly constant width on screen.
inline std::array<std::pair<Vector, Vector>, 4> BeamStrands(const Vector &origin, const Vector &end, float spread)
{
    Vector direction = end - origin;
    const float length = VectorNormalize(direction);
    const Vector up = std::abs(direction.z) > 0.95f ? Vector{1, 0, 0} : Vector{0, 0, 1};
    Vector side = CrossProduct(direction, up);
    VectorNormalize(side);
    const Vector lift = CrossProduct(side, direction);
    const float offset = length * std::tan(spread);
    return {{{origin, end + side * offset}, {origin, end - side * offset},
             {origin, end + lift * offset}, {origin, end - lift * offset}}};
}

// Without the portal gun there is no beam to show what +use would act on, and a beam from the
// empty hand would make no sense. A ring lies on the surface the controller points at instead,
// like the in-world menu's pointer, while use follows the controller.
inline bool ShouldShowGunlessAimRing(bool controllerUse, bool overlayAvailable, bool activeWeapon,
                                     bool cursorVisible, bool holding, bool traceHit)
{
    return controllerUse && overlayAvailable && !activeWeapon && !cursorVisible && !holding && traceHit;
}

// About the same angle anywhere: the outer radius grows with the distance from the controller,
// within limits that keep it on a button up close and below a cube's face far away (units).
inline constexpr float kGunlessRingAngle = 0.012f;
inline constexpr float kGunlessRingMinRadius = 0.6f;
inline constexpr float kGunlessRingMaxRadius = 6.0f;
inline constexpr float kGunlessRingInner = 0.55f;        // inner radius over outer
inline constexpr float kGunlessRingTargetScale = 1.25f;  // a bit larger on something usable
inline constexpr float kGunlessRingLift = 0.25f;         // off the surface, against depth fighting
inline constexpr int kGunlessRingSegments = 20;

// The ring shows what the empty hand can use, so it stays within the game's +use reach: 80 units
// (PLAYER_USE_RADIUS) from the eye, which the controller stands in for. On a target the game
// offers it reaches a little farther, as the game measures to the target's box. Past that the
// ring hid behind nothing: Mel's tram walls and windows have no collision, so it lay on the
// scenery outside (headset test 2026-11-23).
inline constexpr float kGunlessRingReach = 80.0f;
inline constexpr float kGunlessRingTargetReach = 110.0f;

inline bool GunlessRingInReach(float distance, bool onTarget)
{
    return std::isfinite(distance) && distance >= 0.0f &&
        distance <= (onTarget ? kGunlessRingTargetReach : kGunlessRingReach);
}

// The surface's normal, or the way back to the hand when the trace gave none.
inline Vector GunlessRingNormal(const Vector &normal, const Vector &aimForward)
{
    Vector result = normal;
    if (VectorNormalize(result) >= 0.5f)
        return result;
    result = aimForward * -1.0f;
    if (VectorNormalize(result) >= 0.5f)
        return result;
    return Vector{0.0f, 0.0f, 1.0f};
}

inline float GunlessRingRadius(float distance, bool onTarget)
{
    if (!std::isfinite(distance) || distance < 0.0f)
        distance = 0.0f;
    const float radius = std::clamp(distance * kGunlessRingAngle, kGunlessRingMinRadius, kGunlessRingMaxRadius);
    return onTarget ? radius * kGunlessRingTargetScale : radius;
}

using Triangle = std::array<Vector, 3>;

// The ring as triangles in the plane of the surface, lifted off it along the normal. Each
// quad is listed in both windings, so it shows from either side whatever the overlay culls.
inline std::vector<Triangle> RingTriangles(const Vector &point, Vector normal, float outer, float inner,
                                           int segments = kGunlessRingSegments)
{
    std::vector<Triangle> triangles;
    if (VectorNormalize(normal) < 0.5f || !(outer > inner) || inner < 0.0f || segments < 3)
        return triangles;
    const Vector reference = std::abs(normal.z) > 0.95f ? Vector{1, 0, 0} : Vector{0, 0, 1};
    Vector u = CrossProduct(normal, reference);
    VectorNormalize(u);
    const Vector v = CrossProduct(normal, u);
    const Vector center = point + normal * kGunlessRingLift;
    const auto at = [&](int i, float radius) {
        const float angle = 6.28318530718f * static_cast<float>(i % segments) / static_cast<float>(segments);
        return center + u * (std::cos(angle) * radius) + v * (std::sin(angle) * radius);
    };
    triangles.reserve(static_cast<std::size_t>(segments) * 4);
    for (int i = 0; i < segments; ++i) {
        const Vector o0 = at(i, outer), o1 = at(i + 1, outer), i0 = at(i, inner), i1 = at(i + 1, inner);
        for (const Triangle &t : {Triangle{o0, o1, i0}, Triangle{i0, o1, i1}}) {
            triangles.push_back(t);
            triangles.push_back(Triangle{t[0], t[2], t[1]});
        }
    }
    return triangles;
}

struct ScreenPoint
{
    int x;
    int y;
};

enum class ReticleCanvasPosition
{
    Invalid,
    Inside,
    OutsideX,
    OutsideY,
    OutsideBoth
};

inline ReticleCanvasPosition ClassifyReticleCanvas(int x, int y, int spriteWidth,
                                                   int spriteHeight, int canvasWidth,
                                                   int canvasHeight)
{
    if (spriteWidth <= 0 || spriteHeight <= 0 || canvasWidth <= 0 || canvasHeight <= 0)
        return ReticleCanvasPosition::Invalid;
    const bool outsideX = x < 0 || static_cast<std::int64_t>(x) + spriteWidth > canvasWidth;
    const bool outsideY = y < 0 || static_cast<std::int64_t>(y) + spriteHeight > canvasHeight;
    if (outsideX && outsideY)
        return ReticleCanvasPosition::OutsideBoth;
    if (outsideX)
        return ReticleCanvasPosition::OutsideX;
    if (outsideY)
        return ReticleCanvasPosition::OutsideY;
    return ReticleCanvasPosition::Inside;
}

inline std::optional<ScreenPoint> ProjectedCrosshairPosition(
    bool clipped, float projectedX, float projectedY, int sourceX, int sourceY,
    int windowWidth, int windowHeight, int renderWidth, int renderHeight,
    int outputWidth, int outputHeight)
{
    if (clipped || !std::isfinite(projectedX) || !std::isfinite(projectedY) ||
        windowWidth <= 0 || windowHeight <= 0 || renderWidth <= 0 || renderHeight <= 0 ||
        outputWidth <= 0 || outputHeight <= 0 ||
        projectedX < 0.0f || projectedX >= renderWidth ||
        projectedY < 0.0f || projectedY >= renderHeight)
        return std::nullopt;

    const float x = projectedX * outputWidth / renderWidth + sourceX - windowWidth * 0.5f;
    const float y = projectedY * outputHeight / renderHeight + sourceY - windowHeight * 0.5f;
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < 0.0f || x >= outputWidth || y < 0.0f || y >= outputHeight)
        return std::nullopt;
    return ScreenPoint{static_cast<int>(x), static_cast<int>(y)};
}

inline std::optional<ScreenPoint> ProjectedCrosshairPosition(
    bool clipped, float projectedX, float projectedY, int sourceX, int sourceY,
    int windowWidth, int windowHeight, int renderWidth, int renderHeight)
{
    return ProjectedCrosshairPosition(clipped, projectedX, projectedY, sourceX, sourceY,
        windowWidth, windowHeight, renderWidth, renderHeight, renderWidth, renderHeight);
}

inline std::optional<ScreenPoint> ProjectReticleSpriteToEye(
    const Vector &target, const Vector &eyeOrigin, const Vector &forward,
    const Vector &right, const Vector &up, float horizontalFovDegrees,
    float aspect, int renderWidth, int renderHeight,
    int spriteX, int spriteY, int spriteWidth, int spriteHeight,
    int windowWidth, int windowHeight)
{
    // Portal's cached panel can still use desktop dimensions, or already
    // have been laid out for the active eye. Do not mix the two centers.
    int sourceWidth = windowWidth, sourceHeight = windowHeight;
    if (!IsCenteredReticleSprite(spriteX, spriteY, spriteWidth, spriteHeight,
                                sourceWidth, sourceHeight)) {
        sourceWidth = renderWidth;
        sourceHeight = renderHeight;
        if (!IsCenteredReticleSprite(spriteX, spriteY, spriteWidth, spriteHeight,
                                    sourceWidth, sourceHeight))
            return std::nullopt;
    }
    const auto point = ProjectWorldToEye(target, eyeOrigin, forward, right, up,
                                         horizontalFovDegrees, aspect, renderWidth, renderHeight);
    if (!point)
        return std::nullopt;
    // Portal 2 supplies sprite placement in Source-window coordinates, but
    // this draw occurs on an eye-sized render target. Keep the original
    // offset from the window center while moving the sprite to the projected
    // hit in the eye target (not the upper-left window-sized region).
    return ProjectedCrosshairPosition(false, point->x, point->y, spriteX, spriteY,
        sourceWidth, sourceHeight, renderWidth, renderHeight);
}

} // namespace AimFeedback
