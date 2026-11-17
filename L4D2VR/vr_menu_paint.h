#pragma once

// In-world VR menu panels, drawn on the CPU into straight-alpha BGRA canvases and shown as
// SteamVR overlays. The look follows Half-Life: Alyx's holographic panels (a list beside a
// list beside a detail page) in Aperture blue. Text goes through GDI, so the same code
// runs inside the game and in the offline preview (tests/vr_menu_preview.cpp).

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace VrMenuPaint {

struct Rgba {
    std::uint8_t r, g, b, a;
};

struct Theme {
    Rgba panel{5, 20, 34, 172};
    Rgba frame{58, 160, 220, 150};
    Rgba rule{58, 160, 220, 60};
    Rgba accent{86, 200, 255, 255}; // portal blue
    Rgba bright{218, 245, 255, 255};
    Rgba text{170, 226, 255, 255};
    Rgba textDim{92, 156, 194, 255};
    Rgba textOnAccent{3, 26, 42, 255};
    Rgba warm{255, 154, 30, 255};   // portal orange, for the pointer only
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

class Canvas {
public:
    Canvas(int width, int height) : m_Width(width), m_Height(height), m_Pixels(static_cast<std::size_t>(width) * height) {}

    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    std::uint32_t *Data() { return m_Pixels.data(); }
    const std::uint32_t *Data() const { return m_Pixels.data(); }
    void Clear() { std::fill(m_Pixels.begin(), m_Pixels.end(), 0u); }

    // Source-over on straight alpha; coverage is 0-255.
    void Blend(int x, int y, Rgba c, unsigned coverage = 255)
    {
        if (x < 0 || y < 0 || x >= m_Width || y >= m_Height) return;
        const unsigned sa = c.a * coverage / 255;
        if (!sa) return;
        std::uint32_t &dst = m_Pixels[static_cast<std::size_t>(y) * m_Width + x];
        const unsigned da = dst >> 24, dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
        const unsigned oa = sa + da * (255 - sa) / 255;
        const auto mix = [&](unsigned s, unsigned d) {
            return (s * sa * 255 + d * da * (255 - sa)) / (oa * 255);
        };
        dst = (oa << 24) | (mix(c.r, dr) << 16) | (mix(c.g, dg) << 8) | mix(c.b, db);
    }

    // Overwrites, for the first coat on a cleared canvas: no blending to pay for.
    void Put(const Rect &r, Rgba c)
    {
        const std::uint32_t value = (static_cast<std::uint32_t>(c.a) << 24) | (c.r << 16) | (c.g << 8) | c.b;
        for (int y = (std::max)(r.y, 0); y < (std::min)(r.y + r.h, m_Height); ++y)
            std::fill_n(m_Pixels.begin() + static_cast<std::size_t>(y) * m_Width + (std::max)(r.x, 0),
                        (std::max)((std::min)(r.x + r.w, m_Width) - (std::max)(r.x, 0), 0), value);
    }

    void Fill(const Rect &r, Rgba c)
    {
        for (int y = (std::max)(r.y, 0); y < (std::min)(r.y + r.h, m_Height); ++y)
            for (int x = (std::max)(r.x, 0); x < (std::min)(r.x + r.w, m_Width); ++x)
                Blend(x, y, c);
    }

    void GradientH(const Rect &r, Rgba left, Rgba right)
    {
        for (int x = (std::max)(r.x, 0); x < (std::min)(r.x + r.w, m_Width); ++x) {
            const float t = r.w > 1 ? static_cast<float>(x - r.x) / (r.w - 1) : 0.0f;
            const auto lerp = [&](std::uint8_t a, std::uint8_t b) {
                return static_cast<std::uint8_t>(a + (b - a) * t + 0.5f);
            };
            const Rgba c{lerp(left.r, right.r), lerp(left.g, right.g), lerp(left.b, right.b), lerp(left.a, right.a)};
            for (int y = (std::max)(r.y, 0); y < (std::min)(r.y + r.h, m_Height); ++y)
                Blend(x, y, c);
        }
    }

    void Outline(const Rect &r, int thickness, Rgba c)
    {
        Fill({r.x, r.y, r.w, thickness}, c);
        Fill({r.x, r.y + r.h - thickness, r.w, thickness}, c);
        Fill({r.x, r.y + thickness, thickness, r.h - 2 * thickness}, c);
        Fill({r.x + r.w - thickness, r.y + thickness, thickness, r.h - 2 * thickness}, c);
    }

    // Antialiased thick segment and disc: coverage from the distance to the shape.
    void Line(float x0, float y0, float x1, float y1, float thickness, Rgba c)
    {
        const float half = thickness * 0.5f, dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
        const int minX = static_cast<int>(std::floor((std::min)(x0, x1) - half - 1));
        const int maxX = static_cast<int>(std::ceil((std::max)(x0, x1) + half + 1));
        const int minY = static_cast<int>(std::floor((std::min)(y0, y1) - half - 1));
        const int maxY = static_cast<int>(std::ceil((std::max)(y0, y1) + half + 1));
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x) {
                const float px = x + 0.5f - x0, py = y + 0.5f - y0;
                const float t = len2 > 0.0f ? std::clamp((px * dx + py * dy) / len2, 0.0f, 1.0f) : 0.0f;
                const float ex = px - dx * t, ey = py - dy * t;
                const float cover = std::clamp(half + 0.5f - std::sqrt(ex * ex + ey * ey), 0.0f, 1.0f);
                if (cover > 0.0f) Blend(x, y, c, static_cast<unsigned>(cover * 255.0f));
            }
    }

    void Disc(float cx, float cy, float radius, Rgba c, float ring = 0.0f)
    {
        for (int y = static_cast<int>(cy - radius - 1); y <= static_cast<int>(cy + radius + 1); ++y)
            for (int x = static_cast<int>(cx - radius - 1); x <= static_cast<int>(cx + radius + 1); ++x) {
                const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                float cover = std::clamp(radius + 0.5f - d, 0.0f, 1.0f);
                if (ring > 0.0f) cover *= std::clamp(d - (radius - ring) + 0.5f, 0.0f, 1.0f);
                if (cover > 0.0f) Blend(x, y, c, static_cast<unsigned>(cover * 255.0f));
            }
    }

private:
    int m_Width, m_Height;
    std::vector<std::uint32_t> m_Pixels;
};

// GDI text as a coverage mask, coloured and blended by the canvas.
class Text {
public:
    Text(int maxWidth, int maxHeight) : m_Width(maxWidth), m_Height(maxHeight)
    {
        m_Dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = maxWidth;
        info.bmiHeader.biHeight = -maxHeight;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        m_Bitmap = CreateDIBSection(m_Dc, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&m_Mask), nullptr, 0);
        m_Previous = SelectObject(m_Dc, m_Bitmap);
        SetBkMode(m_Dc, TRANSPARENT);
        SetTextColor(m_Dc, RGB(255, 255, 255));
        const std::pair<const wchar_t *, const wchar_t *> families[] = {
            {L"UniversLTStd-Cn", L"UniversLTStd-BoldCn"}, // Portal 2's menu faces, loaded by the game
            {L"Bahnschrift Condensed", L"Bahnschrift SemiBold Condensed"},
            {L"Arial Narrow", L"Arial Narrow"},
            {L"Segoe UI", L"Segoe UI Semibold"}};
        for (const auto &family : families)
            if (Installed(family.first) && Installed(family.second)) {
                m_Regular = family.first;
                m_Bold = family.second;
                break;
            }
    }

    ~Text()
    {
        for (auto &font : m_Fonts) DeleteObject(font.second);
        if (m_Dc) {
            SelectObject(m_Dc, m_Previous);
            DeleteObject(m_Bitmap);
            DeleteDC(m_Dc);
        }
    }
    Text(const Text &) = delete;
    Text &operator=(const Text &) = delete;

    const std::wstring &Family() const { return m_Bold; }

    int Measure(const std::wstring &text, int pixels, bool bold, int tracking)
    {
        Select(pixels, bold, tracking);
        SIZE size{};
        GetTextExtentPoint32W(m_Dc, text.c_str(), static_cast<int>(text.size()), &size);
        return size.cx - (text.empty() ? 0 : tracking);
    }

    // Draws with its top-left at (x, y); returns the advance.
    int Draw(Canvas &canvas, int x, int y, const std::wstring &text, int pixels, bool bold, int tracking, Rgba color)
    {
        if (text.empty() || !m_Mask) return 0;
        Select(pixels, bold, tracking);
        SIZE size{};
        GetTextExtentPoint32W(m_Dc, text.c_str(), static_cast<int>(text.size()), &size);
        const int w = (std::min)(static_cast<int>(size.cx) + 2, m_Width), h = (std::min)(static_cast<int>(size.cy) + 2, m_Height);
        PatBlt(m_Dc, 0, 0, w, h, BLACKNESS);
        TextOutW(m_Dc, 0, 0, text.c_str(), static_cast<int>(text.size()));
        GdiFlush();
        for (int row = 0; row < h; ++row)
            for (int column = 0; column < w; ++column) {
                const unsigned cover = (m_Mask[static_cast<std::size_t>(row) * m_Width + column] >> 8) & 255;
                if (cover) canvas.Blend(x + column, y + row, color, cover);
            }
        return size.cx - tracking;
    }

private:
    static int CALLBACK Found(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM found)
    {
        *reinterpret_cast<bool *>(found) = true;
        return 0;
    }

    bool Installed(const wchar_t *family) const
    {
        LOGFONTW probe{};
        probe.lfCharSet = DEFAULT_CHARSET;
        wcsncpy_s(probe.lfFaceName, family, _TRUNCATE);
        bool found = false;
        EnumFontFamiliesExW(m_Dc, &probe, &Found, reinterpret_cast<LPARAM>(&found), 0);
        return found;
    }

    void Select(int pixels, bool bold, int tracking)
    {
        const int key = pixels * 2 + (bold ? 1 : 0);
        auto font = m_Fonts.find(key);
        if (font == m_Fonts.end())
            font = m_Fonts.emplace(key, CreateFontW(-pixels, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                (bold ? m_Bold : m_Regular).c_str())).first;
        SelectObject(m_Dc, font->second);
        SetTextCharacterExtra(m_Dc, tracking);
    }

    int m_Width, m_Height;
    HDC m_Dc = nullptr;
    HBITMAP m_Bitmap = nullptr;
    HGDIOBJ m_Previous = nullptr;
    std::uint32_t *m_Mask = nullptr;
    std::wstring m_Regular = L"Segoe UI", m_Bold = L"Segoe UI";
    std::map<int, HFONT> m_Fonts;
};

// Info rows only say something: no pointer response, no control.
enum class RowKind { Action, Submenu, Toggle, Choice, Slider, Info };

struct Row {
    std::wstring label;
    std::wstring value;
    RowKind kind = RowKind::Action;
    bool on = false;        // Toggle
    float fraction = 0.0f;  // Slider, 0-1
    bool enabled = true;

    bool operator==(const Row &other) const
    {
        return label == other.label && value == other.value && kind == other.kind && on == other.on &&
            fraction == other.fraction && enabled == other.enabled;
    }
};

struct Panel {
    std::wstring title;               // caption in the header band
    std::vector<Row> rows;
    std::vector<std::wstring> buttons; // footer, right to left
    int selected = -1;                // the row whose page is open beside this panel
    int hovered = -1;                 // the row under the pointer
    int hoveredButton = -1;

    bool operator==(const Panel &other) const
    {
        return title == other.title && rows == other.rows && buttons == other.buttons && selected == other.selected &&
            hovered == other.hovered && hoveredButton == other.hoveredButton;
    }
};

// Where things are, for painting and for the pointer.
struct Layout {
    float scale = 1.0f;
    Rect body, header;
    std::vector<Rect> rows;
    std::vector<Rect> buttons;
    std::vector<Rect> less, more;     // Choice and Slider steppers, empty rects elsewhere

    int RowAt(int x, int y) const
    {
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].Contains(x, y)) return static_cast<int>(i);
        return -1;
    }
    int ButtonAt(int x, int y) const
    {
        for (std::size_t i = 0; i < buttons.size(); ++i)
            if (buttons[i].Contains(x, y)) return static_cast<int>(i);
        return -1;
    }
};

// A panel is as tall as its rows and footer need, so a short list is a short sheet.
inline int ContentHeight(const Panel &panel, int width)
{
    const float s = width / 640.0f;
    const auto px = [s](float v) { return static_cast<int>(v * s + 0.5f); };
    return px(14) + px(40) + px(16) + static_cast<int>(panel.rows.size()) * (px(76) + px(6)) +
        (panel.buttons.empty() ? px(14) : px(96)) + px(14);
}

// Designed on a 640-wide panel; everything scales with the canvas width.
inline Layout Arrange(const Panel &panel, int width, int height, Text &text)
{
    Layout layout;
    const float s = layout.scale = width / 640.0f;
    const auto px = [s](float v) { return static_cast<int>(v * s + 0.5f); };
    layout.body = {px(14), px(14), width - px(28), height - px(28)};
    layout.header = {layout.body.x, layout.body.y, layout.body.w, px(40)};
    int y = layout.header.y + layout.header.h + px(16);
    for (const Row &row : panel.rows) {
        const Rect rect{layout.body.x + px(10), y, layout.body.w - px(20), px(76)};
        layout.rows.push_back(rect);
        const bool stepper = row.kind == RowKind::Choice || row.kind == RowKind::Slider;
        const int box = px(46);
        layout.less.push_back(stepper ? Rect{rect.x + px(12), rect.y + (rect.h - box) / 2, box, box} : Rect{});
        layout.more.push_back(stepper ? Rect{rect.x + rect.w - px(12) - box, rect.y + (rect.h - box) / 2, box, box} : Rect{});
        y += rect.h + px(6);
    }
    int right = layout.body.x + layout.body.w - px(18);
    for (const std::wstring &label : panel.buttons) {
        const int w = text.Measure(label, px(26), true, px(3)) + px(56);
        layout.buttons.push_back({right - w, layout.body.y + layout.body.h - px(74), w, px(54)});
        right -= w + px(16);
    }
    return layout;
}

inline void Paint(Canvas &canvas, Text &text, const Panel &panel, const Layout &layout, const Theme &theme = {})
{
    const float s = layout.scale;
    const auto px = [s](float v) { return static_cast<int>(v * s + 0.5f); };
    const Rect &body = layout.body;
    canvas.Clear();

    // Glass: a dark translucent sheet with scanlines, lighter toward the top.
    for (int y = body.y; y < body.y + body.h; ++y) {
        const float t = static_cast<float>(y - body.y) / body.h;
        Rgba c = theme.panel;
        c.g = static_cast<std::uint8_t>(c.g + 10 * (1.0f - t));
        c.b = static_cast<std::uint8_t>(c.b + 18 * (1.0f - t));
        c.a = static_cast<std::uint8_t>(c.a - ((y / (std::max)(px(3), 1)) % 2 ? 22 : 0));
        canvas.Put({body.x, y, body.w, 1}, c);
    }
    canvas.Outline(body, (std::max)(px(2), 1), theme.frame);

    // Header band with the caption, and bright corner brackets.
    Rgba bandLeft = theme.accent, bandRight = theme.bright;
    bandLeft.a = 150;
    canvas.GradientH(layout.header, bandLeft, bandRight);
    if (!panel.title.empty()) {
        const int size = px(22), tracking = px(3);
        const int w = text.Measure(panel.title, size, true, tracking);
        text.Draw(canvas, layout.header.x + layout.header.w - w - px(18), layout.header.y + (layout.header.h - size) / 2 - px(2),
                  panel.title, size, true, tracking, theme.textOnAccent);
    }
    const int arm = px(30), thick = (std::max)(px(5), 2);
    canvas.Fill({body.x - thick, body.y - thick, arm, thick}, theme.bright);
    canvas.Fill({body.x - thick, body.y - thick, thick, arm}, theme.bright);
    canvas.Fill({body.x + body.w + thick - arm, body.y + body.h, arm, thick}, theme.bright);
    canvas.Fill({body.x + body.w, body.y + body.h + thick - arm, thick, arm}, theme.bright);

    for (std::size_t i = 0; i < panel.rows.size(); ++i) {
        const Row &row = panel.rows[i];
        const Rect &rect = layout.rows[i];
        const bool selected = static_cast<int>(i) == panel.selected, hovered = static_cast<int>(i) == panel.hovered;
        if (selected) {
            Rgba left = theme.accent, right = theme.bright;
            left.a = 225;
            right.a = 245;
            canvas.GradientH(rect, left, right);
        } else if (hovered && row.enabled) {
            Rgba wash = theme.accent;
            wash.a = 64;
            canvas.Fill(rect, wash);
            canvas.Fill({rect.x, rect.y, (std::max)(px(5), 2), rect.h}, theme.bright);
        }
        canvas.Fill({rect.x, rect.y + rect.h + px(2), rect.w, 1}, theme.rule);

        const Rgba ink = selected ? theme.textOnAccent : !row.enabled ? theme.textDim : hovered ? theme.bright : theme.text;
        const bool stepper = row.kind == RowKind::Choice || row.kind == RowKind::Slider;
        int right = rect.x + rect.w - px(stepper ? 78 : row.kind == RowKind::Toggle ? 132 : row.kind == RowKind::Info ? 24 : 48);

        // "LABEL : VALUE", set flush right as in Alyx. Long text (another language, a long
        // chapter name) is set smaller instead of running off the sheet.
        int size = px(34);
        const int tracking = px(3), room = right - rect.x - px(stepper ? 70 : 14);
        const auto natural = [&](int pixels) {
            int w = text.Measure(row.label, pixels, row.value.empty(), tracking);
            if (!row.value.empty())
                w += text.Measure(L" : ", pixels, false, tracking) + text.Measure(row.value, pixels, true, tracking);
            return w;
        };
        if (const int wide = natural(size); wide > room && wide > 0)
            size = (std::max)(size * room / wide, px(18));
        const int baseline = rect.y + (rect.h - size) / 2 - px(3);
        if (!row.value.empty()) {
            const int w = text.Measure(row.value, size, true, tracking);
            text.Draw(canvas, right - w, baseline, row.value, size, true, tracking, selected ? theme.textOnAccent : theme.bright);
            right -= w;
            const std::wstring colon = L" : ";
            right -= text.Measure(colon, size, false, tracking);
            text.Draw(canvas, right, baseline, colon, size, false, tracking, ink);
        }
        const int labelWidth = text.Measure(row.label, size, row.value.empty(), tracking);
        text.Draw(canvas, right - labelWidth, baseline, row.label, size, row.value.empty(), tracking, ink);

        const float midY = rect.y + rect.h * 0.5f;
        const float edge = static_cast<float>(rect.x + rect.w);
        if (row.kind == RowKind::Submenu) {
            const float m = px(11) * 1.0f, cx = edge - px(26);
            canvas.Line(cx - m * 0.5f, midY - m, cx + m * 0.5f, midY, px(3) * 1.0f, ink);
            canvas.Line(cx + m * 0.5f, midY, cx - m * 0.5f, midY + m, px(3) * 1.0f, ink);
        } else if (row.kind == RowKind::Toggle) {
            const float x0 = edge - px(92), x1 = edge - px(30), radius = px(13) * 1.0f;
            canvas.Line(x0, midY, x1, midY, px(3) * 1.0f, row.on ? ink : theme.textDim);
            if (row.on) canvas.Disc(x1, midY, radius, selected ? theme.textOnAccent : theme.bright);
            else canvas.Disc(x0, midY, radius, theme.textDim, px(3) * 1.0f);
        } else if (stepper) {
            for (int side = 0; side < 2; ++side) {
                const Rect &box = side ? layout.more[i] : layout.less[i];
                Rgba line = ink;
                line.a = 200;
                canvas.Outline(box, (std::max)(px(2), 1), line);
                const float cx = box.x + box.w * 0.5f, m = px(8) * 1.0f, dir = side ? 1.0f : -1.0f;
                canvas.Line(cx - dir * m * 0.5f, midY - m, cx + dir * m * 0.5f, midY, px(3) * 1.0f, ink);
                canvas.Line(cx + dir * m * 0.5f, midY, cx - dir * m * 0.5f, midY + m, px(3) * 1.0f, ink);
            }
            if (row.kind == RowKind::Slider) {
                // Alyx's dotted scale under the text.
                const int dots = 10, lit = static_cast<int>(std::clamp(row.fraction, 0.0f, 1.0f) * dots + 0.5f);
                const float spacing = px(14) * 1.0f, start = rect.x + rect.w * 0.5f - spacing * (dots - 1) * 0.5f;
                for (int dot = 0; dot < dots; ++dot)
                    canvas.Disc(start + dot * spacing, rect.y + rect.h - px(12) * 1.0f, px(3) * 1.0f,
                                dot < lit ? (selected ? theme.textOnAccent : theme.bright) : theme.textDim);
            }
        }
    }

    for (std::size_t i = 0; i < panel.buttons.size(); ++i) {
        const Rect &rect = layout.buttons[i];
        const bool hovered = static_cast<int>(i) == panel.hoveredButton;
        if (hovered) {
            Rgba left = theme.accent, right = theme.bright;
            left.a = 225;
            canvas.GradientH(rect, left, right);
        }
        canvas.Outline(rect, (std::max)(px(2), 1), hovered ? theme.bright : theme.accent);
        const int size = px(26), tracking = px(3), w = text.Measure(panel.buttons[i], size, true, tracking);
        text.Draw(canvas, rect.x + (rect.w - w) / 2, rect.y + (rect.h - size) / 2 - px(2), panel.buttons[i], size, true,
                  tracking, hovered ? theme.textOnAccent : theme.text);
    }
}

// Soft light around the bright parts, the holographic look. The lit pixels are gathered at a
// quarter of the size, blurred there and laid back over the sheet, which keeps a repaint cheap.
inline void Glow(Canvas &canvas, int radius, float strength)
{
    constexpr int k = 4;
    const int w = canvas.Width(), h = canvas.Height(), sw = (w + k - 1) / k, sh = (h + k - 1) / k;
    const int reach = (std::max)(radius / k, 1);
    if (radius < 1 || strength <= 0.0f || sw < 2 || sh < 2) return;
    std::vector<float> light(static_cast<std::size_t>(sw) * sh * 3, 0.0f), scratch(light.size());
    std::uint32_t *pixels = canvas.Data();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::uint32_t p = pixels[static_cast<std::size_t>(y) * w + x];
            const unsigned top = (std::max)({(p >> 16) & 255, (p >> 8) & 255, p & 255});
            if (top < 141 || !(p >> 24)) continue;
            const float lit = (top - 140) / 115.0f * ((p >> 24) / 255.0f) / (255.0f * k * k);
            float *cell = &light[(static_cast<std::size_t>(y / k) * sw + x / k) * 3];
            cell[0] += ((p >> 16) & 255) * lit;
            cell[1] += ((p >> 8) & 255) * lit;
            cell[2] += (p & 255) * lit;
        }
    const auto blur = [&](const std::vector<float> &from, std::vector<float> &to, bool horizontal) {
        const int outer = horizontal ? sh : sw, inner = horizontal ? sw : sh;
        for (int o = 0; o < outer; ++o)
            for (int channel = 0; channel < 3; ++channel) {
                const auto at = [&](int i) {
                    i = std::clamp(i, 0, inner - 1);
                    return from[(static_cast<std::size_t>(horizontal ? o : i) * sw + (horizontal ? i : o)) * 3 + channel];
                };
                float sum = 0.0f;
                for (int i = -reach; i <= reach; ++i) sum += at(i);
                for (int i = 0; i < inner; ++i) {
                    to[(static_cast<std::size_t>(horizontal ? o : i) * sw + (horizontal ? i : o)) * 3 + channel] = sum / (2 * reach + 1);
                    sum += at(i + reach + 1) - at(i - reach);
                }
            }
    };
    blur(light, scratch, true);
    blur(scratch, light, false);
    blur(light, scratch, true);
    blur(scratch, light, false);
    for (int y = 0; y < h; ++y) {
        const float fy = (y + 0.5f) / k - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, sh - 1), y1 = (std::min)(y0 + 1, sh - 1);
        const float ty = std::clamp(fy - y0, 0.0f, 1.0f);
        for (int x = 0; x < w; ++x) {
            const float fx = (x + 0.5f) / k - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, sw - 1), x1 = (std::min)(x0 + 1, sw - 1);
            const float tx = std::clamp(fx - x0, 0.0f, 1.0f);
            // Most of a sheet is dark glass far from any light: skip it before interpolating.
            const auto dark = [&](int cx, int cy) {
                const float *cell = &light[(static_cast<std::size_t>(cy) * sw + cx) * 3];
                return (cell[0] + cell[1] + cell[2]) * strength < 0.004f;
            };
            if (dark(x0, y0) && dark(x1, y0) && dark(x0, y1) && dark(x1, y1)) continue;
            float add[3];
            for (int channel = 0; channel < 3; ++channel) {
                const auto cell = [&](int cx, int cy) { return light[(static_cast<std::size_t>(cy) * sw + cx) * 3 + channel]; };
                add[channel] = ((cell(x0, y0) * (1 - tx) + cell(x1, y0) * tx) * (1 - ty) +
                                (cell(x0, y1) * (1 - tx) + cell(x1, y1) * tx) * ty) * strength;
            }
            const float glow = (std::min)((std::max)({add[0], add[1], add[2]}), 1.0f);
            if (glow < 0.004f) continue;
            std::uint32_t &p = pixels[static_cast<std::size_t>(y) * w + x];
            const float a = (p >> 24) / 255.0f;
            // Add light where there is glass, and let it spill a little past the edges.
            const float outA = (std::min)(1.0f, a + glow * (1.0f - a));
            const auto channel = [&](unsigned c, float light) {
                return static_cast<unsigned>((std::min)(1.0f, (c / 255.0f * a + light) / outA) * 255.0f + 0.5f);
            };
            p = (static_cast<unsigned>(outA * 255.0f + 0.5f) << 24) | (channel((p >> 16) & 255, add[0]) << 16) |
                (channel((p >> 8) & 255, add[1]) << 8) | channel(p & 255, add[2]);
        }
    }
}

// The pointer on a panel: a small orange ring, the one warm thing on the sheet.
inline void Pointer(Canvas &canvas, int x, int y, float scale, const Theme &theme = {})
{
    canvas.Disc(x + 0.0f, y + 0.0f, 11.0f * scale, theme.warm, 3.5f * scale);
    canvas.Disc(x + 0.0f, y + 0.0f, 3.0f * scale, theme.bright);
}

} // namespace VrMenuPaint
