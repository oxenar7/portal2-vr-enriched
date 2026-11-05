#pragma once

// Binocular zoom. While the right stick is held in, hooks.cpp renders a
// third, narrow view into its own target and VR shows it in a head-locked
// window cut to a pair of binocular lenses. Portal 2's own +zoom narrows the
// player FOV, which the headset ignores. This header holds the parts free of
// engine and OpenVR types: field of view, lens outline and press policy.

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace BinocularZoom {

// 1024 texels across the default 0.8 m window at 1 m is slightly denser than
// the eye textures at their centre.
inline constexpr int kTextureWidth = 1024;
inline constexpr int kTextureHeight = 640;

// Horizontal field of view, in degrees, that shows the scene `magnification`
// times larger than the eyes see it through a window this wide and this far.
inline float RenderFovDegrees(float windowWidthMeters, float distanceMeters, float magnification)
{
    const float halfTangent = windowWidthMeters * 0.5f / distanceMeters / magnification;
    return 2.0f * std::atan(halfTangent) * 57.2957795f;
}

// Two overlapping lenses as tall as the texture, meeting on its centre line.
struct Outline { float radius, centerY, leftX, rightX, seamX; };

inline Outline OutlineFor(int width, int height)
{
    const float radius = height * 0.5f;
    return {radius, radius, radius, width - radius, width * 0.5f};
}

// Pre-transformed D3D9 vertex (D3DFVF_XYZRHW | D3DFVF_DIFFUSE).
struct Vertex { float x, y, z, rhw; std::uint32_t color; };

inline std::uint32_t Argb(std::uint8_t alpha, std::uint8_t gray)
{
    return (std::uint32_t(alpha) << 24) | (std::uint32_t(gray) << 16) | (std::uint32_t(gray) << 8) | gray;
}

// Triangle list covering the band between two radii, colours blending from
// the inner edge to the outer one. An inner radius of 0 gives a disc.
inline void AppendBand(std::vector<Vertex> &out, float cx, float cy, float innerRadius, float outerRadius,
                       std::uint32_t innerColor, std::uint32_t outerColor, int segments = 96)
{
    const auto point = [&](float radius, int step, std::uint32_t color) {
        const float angle = 6.28318531f * step / segments;
        // D3D9 pixel centres sit on integer coordinates.
        return Vertex{cx + radius * std::cos(angle) - 0.5f, cy + radius * std::sin(angle) - 0.5f, 0.0f, 1.0f, color};
    };
    for (int step = 0; step < segments; ++step) {
        const Vertex in0 = point(innerRadius, step, innerColor), in1 = point(innerRadius, step + 1, innerColor);
        const Vertex out0 = point(outerRadius, step, outerColor), out1 = point(outerRadius, step + 1, outerColor);
        out.insert(out.end(), {in0, out0, out1});
        if (innerRadius > 0.0f)
            out.insert(out.end(), {in0, out1, in1});
    }
}

inline constexpr float kFeather = 0.03f;     // soft edge, fraction of the radius
inline constexpr float kShadeWidth = 0.14f;  // dark inner rim, fraction of the radius
inline constexpr std::uint8_t kShadeAlpha = 150;

// lenses: written to alpha only with a MAX blend, so the overlap stays opaque
// and everything outside stays transparent. Shades darken the colour near
// the rim; each must be clipped to its own half so a lens's rim never
// darkens the middle of the other lens.
struct Mask {
    std::vector<Vertex> lenses, leftShade, rightShade;
};

inline Mask MaskFor(int width, int height)
{
    const Outline outline = OutlineFor(width, height);
    const float solid = outline.radius * (1.0f - kFeather);
    const float shadeStart = outline.radius * (1.0f - kShadeWidth);
    const float shadeMiddle = outline.radius * (1.0f - kShadeWidth / 3.0f);
    Mask mask;
    for (const float cx : {outline.leftX, outline.rightX}) {
        AppendBand(mask.lenses, cx, outline.centerY, 0.0f, solid, Argb(255, 0), Argb(255, 0));
        AppendBand(mask.lenses, cx, outline.centerY, solid, outline.radius, Argb(255, 0), Argb(0, 0));
        auto &shade = cx < outline.seamX ? mask.leftShade : mask.rightShade;
        AppendBand(shade, cx, outline.centerY, shadeStart, shadeMiddle, Argb(0, 0), Argb(40, 0));
        AppendBand(shade, cx, outline.centerY, shadeMiddle, outline.radius, Argb(40, 0), Argb(kShadeAlpha, 0));
    }
    return mask;
}

// Hold to look through the binoculars. A click while the stick is pushed
// over is a turn pressed too hard, not a request to zoom.
class Hold {
public:
    enum class Change { None, Started, Ignored, Ended };
    static constexpr float kMaxStickDeflection = 0.5f;

    Change Update(bool pressed, float stickX, float stickY)
    {
        if (pressed == m_Pressed)
            return Change::None;
        m_Pressed = pressed;
        if (!pressed) {
            const bool wasActive = m_Active;
            m_Active = false;
            return wasActive ? Change::Ended : Change::None;
        }
        m_Active = std::fabs(stickX) < kMaxStickDeflection && std::fabs(stickY) < kMaxStickDeflection;
        return m_Active ? Change::Started : Change::Ignored;
    }
    // Menus and lost input end the zoom. A click still held afterwards counts
    // as a new press.
    bool Cancel()
    {
        const bool wasActive = m_Active;
        m_Active = m_Pressed = false;
        return wasActive;
    }
    bool Active() const { return m_Active; }

private:
    bool m_Pressed = false;
    bool m_Active = false;
};

} // namespace BinocularZoom
