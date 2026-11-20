#pragma once

// Colours of the Options > VR entry and page, taken from the game's basemodui_scheme so they
// match the native controls drawn on the same tiles. Portal 2's scheme draws black text on
// light tiles; Alive & Kicking's draws white text on dark tiles with a blue cursor.

namespace VrMenuPalette {

struct Colour { int r, g, b, a; };

constexpr bool operator==(const Colour &a, const Colour &b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
constexpr bool operator!=(const Colour &a, const Colour &b) { return !(a == b); }

struct Palette {
    Colour text, focusText, disabledText, cursor, arrowActive, arrowInactive;
    Colour title, footerText, footerHotText;
};

inline constexpr Colour kBlack{0, 0, 0, 255};

// Portal 2 as the page was matched to it: the HybridButton and Dialog.TitleColor colours, the
// cursor bar measured on the native Video dialog over the tile interior (the scheme says
// 0 0 0 240), and black footer text on both button textures.
inline constexpr Palette kPortal2{kBlack, {240, 240, 240, 255}, {140, 140, 140, 255},
                                  {57, 57, 57, 255}, {255, 255, 255, 255}, {255, 255, 255, 60},
                                  kBlack, kBlack, kBlack};
// Portal 2's scheme values for the two colours the page does not draw as the scheme gives them.
inline constexpr Colour kPortal2SchemeCursor{0, 0, 0, 240};
inline constexpr Colour kPortal2SchemeFooter{100, 100, 100, 255};

constexpr bool IsLight(const Colour &c) { return c.r * 299 + c.g * 587 + c.b * 114 > 128 * 1000; }

// lookup(name, fallback) returns the scheme colour, or the fallback when it cannot be read.
// A scheme identical to Portal 2's gives exactly kPortal2.
template <typename Lookup>
Palette FromScheme(Lookup lookup)
{
    Palette palette = kPortal2;
    palette.text = lookup("HybridButton.TextColor", palette.text);
    palette.focusText = lookup("HybridButton.FocusColor", palette.focusText);
    palette.disabledText = lookup("HybridButton.DisabledColor", palette.disabledText);
    palette.arrowActive = lookup("HybridButton.ListButtonActiveColor", palette.arrowActive);
    palette.arrowInactive = lookup("HybridButton.ListButtonInactiveColor", palette.arrowInactive);
    palette.title = lookup("Dialog.TitleColor", palette.title);
    // Another game's cursor is drawn as its scheme gives it, blended over the tiles.
    const Colour cursor = lookup("HybridButton.CursorColor", kPortal2SchemeCursor);
    if (cursor != kPortal2SchemeCursor) palette.cursor = cursor;
    // Another game's footer text as its scheme gives it. Its hover texture may be light where
    // the idle one is dark (Alive & Kicking: about 214 against 41), so light text turns black
    // on the hovered button.
    const Colour footer = lookup("FooterPanel.TextColor", kPortal2SchemeFooter);
    if (footer != kPortal2SchemeFooter) {
        palette.footerText = footer;
        palette.footerHotText = IsLight(footer) ? kBlack : footer;
    }
    return palette;
}

} // namespace VrMenuPalette
