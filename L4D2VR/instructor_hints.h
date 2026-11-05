#pragma once

// Game instructor hints ("press [E] to use") in the caption overlay.
//
// CLocatorPanel lays hints out on ScreenWidth() x ScreenHeight(). The HUD
// capture paints into the stereo-sized target while the overlay shows only
// the Source window crop, so hooks.cpp reports the window size to the
// locator and projects world-anchored hints onto the overlay plane with
// ProjectToOverlay. Hints for commands the VR actions issue show the flat
// VR icon of the bound control (A, RT, the right stick pressed...) instead
// of the keyboard key. The icons come from hint_icons.h, whose header notes
// where they come from.

#include "hint_icons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

namespace InstructorHints {

inline char LowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](char x, char y) { return LowerAscii(x) == LowerAscii(y); });
}

// Commands VR::ProcessInput issues for the OpenVR actions.
enum class Action : std::uint8_t { None, Jump, Use, Crouch, PrimaryAttack, SecondaryAttack, Reload, Zoom, Count };

// Hint bindings may chain commands with ';' (SetBinding splits them the same
// way); the first command names the action to show. Like Key_LookupBinding,
// the leading '+' is optional: Portal 2's own lessons ask for "use".
inline Action ActionForBinding(const char *binding)
{
    if (!binding)
        return Action::None;
    std::string_view text(binding);
    text.remove_prefix((std::min)(text.find_first_not_of(' '), text.size()));
    text = text.substr(0, text.find_first_of("; "));
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    struct Entry { std::string_view command; Action action; };
    static constexpr Entry kEntries[] = {{"jump", Action::Jump}, {"use", Action::Use},
        {"duck", Action::Crouch}, {"attack", Action::PrimaryAttack},
        {"attack2", Action::SecondaryAttack}, {"reload", Action::Reload}, {"zoom", Action::Zoom}};
    for (const auto &entry : kEntries)
        if (EqualsIgnoreCase(text, entry.command))
            return entry.action;
    return Action::None;
}

// The icon a hint shows in place of the keyboard key.
using Glyph = HintIcons::Id;

// One OpenVR binding (IVRInput::GetActionBindingInfo): device
// "/user/hand/right", input "/input/joystick", slot "click" (a stick in
// dpad mode binds "north", "south", "east" or "west"). Other devices and
// inputs, such as trackpads, keep the native key icon.
inline std::optional<Glyph> GlyphForInput(const char *devicePath, const char *inputPath,
    const char *slotName = nullptr)
{
    using HintIcons::Id;
    if (!devicePath || !inputPath)
        return std::nullopt;
    const std::string_view device(devicePath), input(inputPath), slot(slotName ? slotName : "");
    bool left;
    if (EqualsIgnoreCase(device, "/user/hand/left"))
        left = true;
    else if (EqualsIgnoreCase(device, "/user/hand/right"))
        left = false;
    else
        return std::nullopt;
    constexpr std::string_view kPrefix = "/input/";
    if (input.size() <= kPrefix.size() || !EqualsIgnoreCase(input.substr(0, kPrefix.size()), kPrefix))
        return std::nullopt;
    const std::string_view name = input.substr(kPrefix.size());
    if (name.size() == 1) {
        switch (LowerAscii(name[0])) {
        case 'a': return Id::A;
        case 'b': return Id::B;
        case 'x': return Id::X;
        case 'y': return Id::Y;
        default: return std::nullopt;
        }
    }
    if (EqualsIgnoreCase(name, "joystick") || EqualsIgnoreCase(name, "thumbstick")) {
        if (EqualsIgnoreCase(slot, "click") || EqualsIgnoreCase(slot, "center"))
            return left ? Id::LeftStickPress : Id::RightStickPress;
        if (EqualsIgnoreCase(slot, "north"))
            return left ? Id::LeftStickUp : Id::RightStickUp;
        if (EqualsIgnoreCase(slot, "south"))
            return left ? Id::LeftStickDown : Id::RightStickDown;
        if (EqualsIgnoreCase(slot, "east"))
            return left ? Id::LeftStickRight : Id::RightStickRight;
        if (EqualsIgnoreCase(slot, "west"))
            return left ? Id::LeftStickLeft : Id::RightStickLeft;
        return left ? Id::LeftStick : Id::RightStick;
    }
    if (EqualsIgnoreCase(name, "trigger"))
        return left ? Id::LeftTrigger : Id::RightTrigger;
    if (EqualsIgnoreCase(name, "grip"))
        return left ? Id::LeftGrip : Id::RightGrip;
    // On Touch controllers the left "system" input is the menu button, the right one the Oculus button.
    if (EqualsIgnoreCase(name, "system") || EqualsIgnoreCase(name, "application_menu"))
        return left ? Id::Menu : Id::System;
    return std::nullopt;
}

inline const char *GlyphName(Glyph glyph)
{
    const auto index = static_cast<std::size_t>(glyph);
    return index < std::size(HintIcons::kIcons) ? HintIcons::kIcons[index].name : "?";
}

// The locator keeps a handful of hint targets alive. Remember each one's
// binding (SetBinding) and the glyph picked for it (UseBindingImage).
class TargetTable
{
public:
    static constexpr std::size_t kCapacity = 32;
    static constexpr std::size_t kBindingLength = 64;

    void SetBinding(const void *target, const char *binding)
    {
        if (!target)
            return;
        Entry &entry = Claim(target);
        std::string_view text(binding ? binding : "");
        text = text.substr(0, kBindingLength - 1);
        if (text == entry.binding.data())
            return;
        std::memcpy(entry.binding.data(), text.data(), text.size());
        entry.binding[text.size()] = '\0';
        entry.glyph.reset(); // chosen again when Source revalidates the icon
        entry.draws = 0;
    }

    // How many times the target has been drawn with its current binding, this one included.
    std::uint32_t CountDraw(const void *target)
    {
        if (!target)
            return 0;
        Entry &entry = Claim(target);
        if (entry.draws < UINT32_MAX)
            ++entry.draws;
        return entry.draws;
    }

    const char *Binding(const void *target) const
    {
        const Entry *entry = Find(target);
        return entry ? entry->binding.data() : nullptr;
    }

    void SetGlyph(const void *target, std::optional<Glyph> glyph)
    {
        if (!target)
            return;
        Claim(target).glyph = glyph;
    }

    std::optional<Glyph> GlyphFor(const void *target) const
    {
        const Entry *entry = Find(target);
        return entry ? entry->glyph : std::nullopt;
    }

private:
    struct Entry {
        const void *target = nullptr;
        std::array<char, kBindingLength> binding{};
        std::optional<Glyph> glyph;
        std::uint32_t draws = 0;
        std::uint32_t used = 0;
    };

    const Entry *Find(const void *target) const
    {
        for (const auto &entry : m_Entries)
            if (target && entry.target == target)
                return &entry;
        return nullptr;
    }

    Entry &Claim(const void *target)
    {
        ++m_Clock;
        Entry *oldest = &m_Entries[0];
        for (auto &entry : m_Entries) {
            if (entry.target == target) {
                entry.used = m_Clock;
                return entry;
            }
            if (entry.used < oldest->used)
                oldest = &entry;
        }
        *oldest = Entry{};
        oldest->target = target;
        oldest->used = m_Clock;
        return *oldest;
    }

    std::array<Entry, kCapacity> m_Entries{};
    std::uint32_t m_Clock = 0;
};

struct Vec3 { float x, y, z; };

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// The Source head this frame: world origin and axes, in game units.
struct HeadFrame {
    Vec3 origin, forward, right, up;
    float unitsPerMeter;
};

struct ScreenPoint { int x, y; };

// Where `world` appears on the caption overlay, in Source window pixels
// (origin top left), looking from the head through the overlay plane.
// `hmd` and `overlay` are OpenVR 3x4 poses in the same tracking space; the
// overlay shows the whole window crop across `overlayWidthMeters`. Returns
// nullopt when the line of sight never reaches the plane, which Source
// treats as a target behind the player.
inline std::optional<ScreenPoint> ProjectToOverlay(const Vec3 &world, const HeadFrame &head,
    const float (&hmd)[3][4], const float (&overlay)[3][4], float overlayWidthMeters,
    int windowWidth, int windowHeight)
{
    if (!(head.unitsPerMeter > 0.0f) || !(overlayWidthMeters > 0.0f) || windowWidth <= 0 || windowHeight <= 0)
        return std::nullopt;
    const Vec3 offset = world - head.origin;
    // OpenVR head space: +x right, +y up, -z forward.
    const Vec3 local{Dot(offset, head.right) / head.unitsPerMeter, Dot(offset, head.up) / head.unitsPerMeter,
        -Dot(offset, head.forward) / head.unitsPerMeter};
    const Vec3 eye{hmd[0][3], hmd[1][3], hmd[2][3]};
    const Vec3 point = eye + Vec3{
        hmd[0][0] * local.x + hmd[0][1] * local.y + hmd[0][2] * local.z,
        hmd[1][0] * local.x + hmd[1][1] * local.y + hmd[1][2] * local.z,
        hmd[2][0] * local.x + hmd[2][1] * local.y + hmd[2][2] * local.z};
    // Overlay space: +x right, +y up, +z toward the viewer.
    const Vec3 center{overlay[0][3], overlay[1][3], overlay[2][3]};
    const auto toOverlay = [&](Vec3 p) {
        const Vec3 q = p - center;
        return Vec3{Dot(q, {overlay[0][0], overlay[1][0], overlay[2][0]}),
            Dot(q, {overlay[0][1], overlay[1][1], overlay[2][1]}),
            Dot(q, {overlay[0][2], overlay[1][2], overlay[2][2]})};
    };
    const Vec3 e = toOverlay(eye), p = toOverlay(point);
    const float approach = e.z - p.z;
    if (!(e.z > 0.0f) || !(approach > 1e-6f))
        return std::nullopt;
    const float t = e.z / approach;
    const float hitX = e.x + (p.x - e.x) * t, hitY = e.y + (p.y - e.y) * t;
    const float heightMeters = overlayWidthMeters * windowHeight / windowWidth;
    const float x = (hitX / overlayWidthMeters + 0.5f) * windowWidth;
    const float y = (0.5f - hitY / heightMeters) * windowHeight;
    if (!std::isfinite(x) || !std::isfinite(y))
        return std::nullopt;
    // Far off the panel only needs a direction for the edge arrow.
    constexpr float kLimit = 1.0e6f;
    return ScreenPoint{static_cast<int>(std::lround((std::clamp)(x, -kLimit, kLimit))),
        static_cast<int>(std::lround((std::clamp)(y, -kLimit, kLimit)))};
}

// RGBA8888 (straight alpha), `side` x `side`: the icon's canvas scaled on the
// CPU to the hint's exact size, so the GPU draws it 1:1 and the letters stay
// sharp. Shrinking averages the canvas pixels each output pixel covers;
// growing interpolates between them. Gray is weighted by alpha, so the dark
// sheet the icons were cut from never bleeds into their rims.
inline std::vector<std::uint8_t> RenderGlyph(Glyph glyph, int side)
{
    const std::size_t edge = static_cast<std::size_t>((std::max)(side, 0));
    std::vector<std::uint8_t> rgba(edge * edge * 4u, 0);
    const auto index = static_cast<std::size_t>(glyph);
    if (side <= 0 || index >= std::size(HintIcons::kIcons))
        return rgba;
    const HintIcons::Icon &icon = HintIcons::kIcons[index];
    // Canvas pixel (x, y): alpha and alpha-weighted gray, both 0..1.
    const auto texel = [&icon](int x, int y, float &alpha, float &gray) {
        x -= icon.left;
        y -= icon.top;
        if (x < 0 || y < 0 || x >= icon.width || y >= icon.height) {
            alpha = gray = 0.0f;
            return;
        }
        const std::uint8_t *pixel =
            &HintIcons::kPixels[icon.offset + (static_cast<std::size_t>(y) * icon.width + x) * 2u];
        alpha = pixel[1] / 255.0f;
        gray = pixel[0] / 255.0f * alpha;
    };
    const float scale = static_cast<float>(HintIcons::kCanvas) / side; // canvas pixels per output pixel
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            float alpha = 0.0f, gray = 0.0f;
            if (scale > 1.0f) {
                const float x0 = column * scale, x1 = x0 + scale, y0 = row * scale, y1 = y0 + scale;
                float total = 0.0f;
                for (int y = static_cast<int>(y0); y < y1 && y < HintIcons::kCanvas; ++y) {
                    const float wy = (std::min)(y1, y + 1.0f) - (std::max)(y0, static_cast<float>(y));
                    for (int x = static_cast<int>(x0); x < x1 && x < HintIcons::kCanvas; ++x) {
                        const float weight = wy * ((std::min)(x1, x + 1.0f) - (std::max)(x0, static_cast<float>(x)));
                        float a, g;
                        texel(x, y, a, g);
                        alpha += a * weight;
                        gray += g * weight;
                        total += weight;
                    }
                }
                if (total > 0.0f) {
                    alpha /= total;
                    gray /= total;
                }
            } else {
                const float x = (column + 0.5f) * scale - 0.5f, y = (row + 0.5f) * scale - 0.5f;
                const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
                const float fx = x - ix, fy = y - iy;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const float weight = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
                        float a, g;
                        texel(ix + dx, iy + dy, a, g);
                        alpha += a * weight;
                        gray += g * weight;
                    }
                }
            }
            std::uint8_t *out = &rgba[(static_cast<std::size_t>(row) * side + column) * 4u];
            const float straight = alpha > 0.0f ? (std::min)(gray / alpha, 1.0f) : 0.0f;
            out[0] = out[1] = out[2] = static_cast<std::uint8_t>(std::lround(straight * 255.0f));
            out[3] = static_cast<std::uint8_t>(std::lround((std::clamp)(alpha, 0.0f, 1.0f) * 255.0f));
        }
    }
    return rgba;
}

// One texture per distinct glyph.
inline int GlyphKey(Glyph glyph) { return static_cast<int>(glyph); }

} // namespace InstructorHints
