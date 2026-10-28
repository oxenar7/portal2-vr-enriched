#include "vr_options_menu.h"

#include "vr.h"
#include "game.h"
#include "sdk.h"
#include "logger.h"

#include <Windows.h>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <utility>
#include <vector>

using Portal2Vgui::HFont;
using Portal2Vgui::VPANEL;
namespace Units = VrSettings::Units;

namespace {

constexpr const char *kTextureFiles[] = {
    "vgui/menu_tiles/UI_tile_128_crnr_topL", "vgui/menu_tiles/UI_tile_128_top",
    "vgui/menu_tiles/UI_tile_128_crnr_topR", "vgui/menu_tiles/UI_tile_128_left",
    "vgui/menu_tiles/UI_tile_128_interior1", "vgui/menu_tiles/UI_tile_128_right",
    "vgui/menu_tiles/UI_tile_128_crnr_bottomL", "vgui/menu_tiles/UI_tile_128_bottom",
    "vgui/menu_tiles/UI_tile_128_crnr_bottomR", "vgui/btn_bg", "vgui/btn_bg_over"};

// basemodui_scheme.res HybridButton/FooterPanel colours. The cursor bar is the
// colour measured on the native Video dialog over the tile interior.
constexpr VrOptionsMenu::Colour kText{0, 0, 0, 255};
constexpr VrOptionsMenu::Colour kFocusText{240, 240, 240, 255};
constexpr VrOptionsMenu::Colour kDisabledText{140, 140, 140, 255};
constexpr VrOptionsMenu::Colour kCursor{57, 57, 57, 255};
constexpr VrOptionsMenu::Colour kArrowActive{255, 255, 255, 255};
constexpr VrOptionsMenu::Colour kArrowInactive{255, 255, 255, 60};

// BaseModUI's own menu sounds.
constexpr const char *kFocusSound = "UI/menu_focus.wav";
constexpr const char *kAcceptSound = "UI/menu_accept.wav";
constexpr const char *kBackSound = "UI/menu_back.wav";
constexpr const char *kInvalidSound = "UI/menu_invalid.wav";

std::wstring Widen(const std::string &text)
{
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(length > 0 ? length : 0, L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::wstring Upper(std::wstring text)
{
    for (auto &c : text) c = static_cast<wchar_t>(std::towupper(c));
    return text;
}

std::string Describe(const VrSettings::Rect &rect)
{
    return std::to_string(rect.x) + "," + std::to_string(rect.y) + " " +
        std::to_string(rect.w) + "x" + std::to_string(rect.h);
}

} // namespace

bool VrOptionsMenu::Initialize(VR *vr, void *surface, void *panel, void *schemeManager)
{
    m_VR = vr;
    std::string failure;
    if (!m_Api.Resolve(surface, panel, schemeManager, failure)) {
        Logger::Write("In-game VR options unavailable: unverified VGUI " + failure +
            "; VR/config.txt remains the only settings source");
        return false;
    }
    Logger::Write("In-game VR options: verified VGUI surface, panel and scheme ABI");
    if (m_Transition.Resolve(failure))
        Logger::Write("In-game VR options: verified BaseModUI tile transition");
    else
        Logger::Write("In-game VR options: tile transition unavailable (unverified " + failure +
            "); the VR page opens without it");
    return true;
}

bool VrOptionsMenu::CursorVisible() const
{
    return m_VR && m_VR->m_Game && m_VR->m_Game->m_VguiSurface &&
        m_VR->m_Game->m_VguiSurface->IsCursorVisible();
}

int VrOptionsMenu::Px(float units) const
{
    return static_cast<int>(std::lround(units * m_Unit));
}

VrSettings::Rect VrOptionsMenu::PanelRect(VPANEL panel) const
{
    VrSettings::Rect rect;
    m_Api.GetAbsPos(panel, rect.x, rect.y);
    m_Api.GetSize(panel, rect.w, rect.h);
    return rect;
}

void VrOptionsMenu::Paint(VPANEL panel, bool paintPopups)
{
    if (!m_Api.Ready() || !paintPopups || !m_VR || !m_VR->m_IsVREnabled ||
        panel != m_Api.GetEmbeddedPanel())
        return;
    const bool cursor = CursorVisible();
    if (cursor && !m_MenuPaintLogged) {
        m_MenuPaintLogged = true;
        const char *name = m_Api.GetName(panel);
        Logger::Write(std::string("In-game VR options: first menu paint, root=") + (name ? name : "?") +
            " children=" + std::to_string(m_Api.GetChildCount(panel)) +
            " popups=" + std::to_string(m_Api.GetPopupCount()));
    }
    m_OptionsVisible = cursor && FindOptionsScreen();
    if (!m_OptionsVisible) {
        // The native Options screen closed underneath (Esc, Back, map change).
        if (m_PageOpen) ClosePage();
        RestoreNativeFooter();
        m_EntryHovered = false;
        m_EntrySelected = false;
        m_ReleasePending = false;
        m_TransitionDepth = 0;
        return;
    }
    if (m_PageOpen) HideNativeFooter();
    else RestoreNativeFooter();
    UpdateTransition();
    if (m_ReleasePending) {
        m_ReleasePending = false;
        // The pointer may already be on a native button that took focus itself.
        if (m_EntryHovered) ReleaseNativeFocus();
        else m_FocusReleased = false;
    }
    TrackNativeFocus();
    const bool started = !m_Api.InDrawing();
    if (started) m_Api.StartDrawing();
    if (LoadResources()) {
        if (m_PageOpen) DrawPage();
        else DrawEntry();
    }
    if (started) m_Api.FinishDrawing();
}

template <typename Match>
VPANEL VrOptionsMenu::Search(Match &&match, bool throughHidden)
{
    int budget = throughHidden ? 16384 : 4096;
    static thread_local std::vector<std::pair<VPANEL, int>> stack;
    const int popups = m_Api.GetPopupCount();
    for (int root = -1; root < popups && budget > 0; ++root) {
        stack.clear();
        stack.emplace_back(root < 0 ? m_Api.GetEmbeddedPanel() : m_Api.GetPopup(root), 0);
        while (!stack.empty() && budget-- > 0) {
            const auto [panel, depth] = stack.back();
            stack.pop_back();
            if (!panel) continue;
            if (match(panel)) return panel;
            if (depth >= 16 || (!throughHidden && !m_Api.IsVisible(panel))) continue;
            const int children = m_Api.GetChildCount(panel);
            for (int i = 0; i < children; ++i)
                stack.emplace_back(m_Api.GetChild(panel, i), depth + 1);
        }
    }
    return 0;
}

bool VrOptionsMenu::FindOptionsScreen()
{
    return Search([this](VPANEL panel) {
        const char *name = m_Api.GetName(panel);
        return name && std::strcmp(name, "Options") == 0 && m_Api.IsVisible(panel) && ReadOptionsFrame(panel);
    }) != 0;
}

bool VrOptionsMenu::IsNativeButton(VPANEL panel) const
{
    const char *name = panel ? m_Api.GetName(panel) : nullptr;
    return name && std::strncmp(name, "Btn", 3) == 0;
}

void VrOptionsMenu::ReleaseNativeFocus()
{
    // A BaseModHybridButton draws its focus bar while it holds VGUI key focus,
    // so the last hovered button stays lit. Hand key focus to the frame's
    // caption grip, which draws nothing; hovering a button takes it back.
    const VPANEL before = m_Api.GetCurrentKeyFocus(m_Frame);
    if (m_Caption) m_Api.RequestFocus(m_Caption);
    const VPANEL after = m_Api.GetCurrentKeyFocus(m_Frame);
    m_FocusReleased = m_Caption && !IsNativeButton(after);
    if (!m_FocusLogged) {
        m_FocusLogged = true;
        const auto name = [this](VPANEL panel) {
            const char *text = panel ? m_Api.GetName(panel) : nullptr;
            return std::string(text ? text : "none");
        };
        Logger::Write(m_FocusReleased ?
            "In-game VR options: native key focus moved from " + name(before) + " to " + name(after) :
            "In-game VR options: native key focus stays on " + name(after) +
                (m_Caption ? "" : " (no frame_caption)") + "; the VR entry highlights only while pointed at");
    }
}

void VrOptionsMenu::TrackNativeFocus()
{
    if (!m_EntrySelected || m_EntryHovered) return;
    // Without a released focus a native button is still lit: do not stay lit too.
    if (!m_FocusReleased || IsNativeButton(m_Api.GetCurrentKeyFocus(m_Frame)))
        m_EntrySelected = false;
}

void VrOptionsMenu::HideNativeFooter()
{
    // The Options screen's own BACK footer sits where the page draws its footer.
    const VPANEL footer = Search([this](VPANEL panel) {
        const char *name = m_Api.GetName(panel);
        return name && std::strcmp(name, "FooterPanel") == 0 && m_Api.IsVisible(panel);
    });
    if (!m_FooterLogged && (footer || !m_HiddenFooter)) {
        m_FooterLogged = true;
        Logger::Write(footer ? "In-game VR options: native footer hidden while the VR page is open" :
            "In-game VR options: native FooterPanel not found; its buttons may show under the VR page");
    }
    if (footer) {
        m_Api.SetVisible(footer, false);
        m_HiddenFooter = footer;
    }
}

void VrOptionsMenu::RestoreNativeFooter()
{
    if (!m_HiddenFooter) return;
    const VPANEL hidden = m_HiddenFooter;
    m_HiddenFooter = 0;
    // Only touch the VPANEL if VGUI still has it; its parents may be hidden by now
    // (the menu closed), so search through hidden panels too.
    if (Search([hidden](VPANEL panel) { return panel == hidden; }, true))
        m_Api.SetVisible(hidden, true);
}

namespace {
// The pages flip like Video and Advanced Video do from Options.
int TransitionWindowType(int depth)
{
    return depth == 0 ? Portal2Transition::kWindowOptions :
        depth == 1 ? Portal2Transition::kWindowVideo : Portal2Transition::kWindowAdvancedVideo;
}
}

void VrOptionsMenu::UpdateTransition()
{
    m_TransitionPanel = m_Transition.Panel();
    const int depth = m_PageOpen ? 1 + m_Page : 0;
    // As CBaseModPanel::OpenWindow (forward) and the Back path (backward) do
    // before the new window first marks its tiles.
    if (m_TransitionPanel && depth != m_TransitionDepth)
        m_Transition.SetExpectedDirection(m_TransitionPanel, depth > m_TransitionDepth, TransitionWindowType(depth));
    m_TransitionDepth = depth;
}

bool VrOptionsMenu::ReadOptionsFrame(VPANEL frame)
{
    // PC Options.res: Audio, Video, Keyboard/Mouse, Controller at a 25-unit pitch.
    VPANEL keyboard = 0, controller = 0, caption = 0;
    const int children = m_Api.GetChildCount(frame);
    for (int i = 0; i < children; ++i) {
        const VPANEL child = m_Api.GetChild(frame, i);
        const char *name = child ? m_Api.GetName(child) : nullptr;
        if (!name) continue;
        if (std::strcmp(name, "BtnKeyboardMouse") == 0) keyboard = child;
        else if (std::strcmp(name, "BtnController") == 0) controller = child;
        else if (std::strcmp(name, "frame_caption") == 0) caption = child; // vgui::Frame's title grip
    }
    if (!keyboard || !controller || !m_Api.IsFullyVisible(frame) || !m_Api.IsVisible(controller))
        return false;
    m_Frame = frame;
    m_Caption = caption;
    const auto frameRect = PanelRect(frame);
    const auto keyboardRect = PanelRect(keyboard);
    const auto controllerRect = PanelRect(controller);
    const int pitch = controllerRect.y - keyboardRect.y;
    if (pitch <= 0 || controllerRect.w <= 0 || controllerRect.h <= 0 || frameRect.w <= 0 || frameRect.h <= 0)
        return false;
    m_LastButton = controllerRect;
    m_RowPitch = pitch;
    m_Unit = pitch / Units::RowPitch;
    m_EntryRect = VrSettings::EntryRect(m_LastButton, m_RowPitch);
    // Options.res: a 5x3-tile body with CONTROLLER as its fourth row (ypos 100).
    // The frame is that body, or the body under a one-tile title row; its exact
    // width then gives the unit (the row pitch is rounded to whole pixels).
    // Otherwise derive the body from the row.
    const auto measures = [this](int pixels, float units) { return std::abs(pixels - Px(units)) <= Px(10.0f); };
    const bool frameIsBody = measures(frameRect.w, 5 * Units::Tile) && measures(frameRect.h, 3 * Units::Tile);
    const bool frameHasTitle = measures(frameRect.w, 5 * Units::Tile) && measures(frameRect.h, 4 * Units::Tile);
    if (frameIsBody || frameHasTitle) {
        m_Unit = frameRect.w / (5 * Units::Tile);
        const int title = frameHasTitle ? frameRect.h / 4 : 0;
        m_OptionsFrame = {frameRect.x, frameRect.y + title, frameRect.w, frameRect.h - title};
    } else {
        m_OptionsFrame = {controllerRect.x - Px(Units::ButtonInset), controllerRect.y - Px(100.0f),
                          Px(5 * Units::Tile), Px(3 * Units::Tile)};
    }
    if (!m_OptionsLogged) {
        m_OptionsLogged = true;
        int screenWide = 0, screenTall = 0;
        m_Api.GetScreenSize(screenWide, screenTall);
        Logger::Write("In-game VR options: native Options screen frame=" + Describe(frameRect) +
            " controller=" + Describe(controllerRect) + " pitch=" + std::to_string(pitch) +
            " unit=" + std::to_string(m_Unit) + " screen=" + std::to_string(screenWide) + "x" +
            std::to_string(screenTall) + "; body " +
            (frameIsBody ? "is frame " : frameHasTitle ? "under frame title " : "from rows ") +
            Describe(m_OptionsFrame) + "; VR entry at " + Describe(m_EntryRect));
    }
    return true;
}

bool VrOptionsMenu::LoadResources()
{
    if (m_ResourcesTried) return m_ResourcesReady;
    m_ResourcesTried = true;
    std::string failure;
    const auto scheme = m_Api.GetScheme("basemodui_scheme");
    void *fonts = scheme ? m_Api.VerifiedScheme(scheme, failure) : nullptr;
    if (!fonts) {
        Logger::Write("In-game VR options unavailable: basemodui_scheme " +
            (failure.empty() ? std::string("missing") : "IScheme::" + failure + " unverified"));
        return false;
    }
    // Dialog fonts are proportional-only; the symbol font is not.
    m_TitleFont = m_Api.GetFont(fonts, "DialogTitle", true);
    m_RowFont = m_Api.GetFont(fonts, "DialogButton", true);
    m_MenuFont = m_Api.GetFont(fonts, "DialogMenuItem", true);
    m_FooterFont = m_Api.GetFont(fonts, "ButtonText_NC", true);
    m_SymbolFont = m_Api.GetFont(fonts, "MarlettLarge", false);
    if (!m_TitleFont || !m_RowFont || !m_MenuFont || !m_FooterFont || !m_SymbolFont) {
        Logger::Write("In-game VR options unavailable: a basemodui_scheme font is missing");
        return false;
    }
    for (int i = 0; i < TextureCount; ++i) {
        int id = m_Api.DrawGetTextureId(kTextureFiles[i]);
        if (id < 0) {
            id = m_Api.CreateNewTextureID();
            m_Api.DrawSetTextureFile(id, kTextureFiles[i]);
        }
        m_Textures[i] = id;
    }
    m_ResourcesReady = true;
    Logger::Write("In-game VR options ready: font tall title=" + std::to_string(m_Api.GetFontTall(m_TitleFont)) +
        " row=" + std::to_string(m_Api.GetFontTall(m_RowFont)) +
        " menu=" + std::to_string(m_Api.GetFontTall(m_MenuFont)) +
        " footer=" + std::to_string(m_Api.GetFontTall(m_FooterFont)) +
        " symbol=" + std::to_string(m_Api.GetFontTall(m_SymbolFont)) +
        "; scheme unit=" + std::to_string(m_Api.ScaledValue(scheme, 1000) / 1000.0f));
    return true;
}

void VrOptionsMenu::Sound(const char *file) const
{
    m_Api.PlayUiSound(file);
}

void VrOptionsMenu::Fill(const VrSettings::Rect &rect, Colour colour) const
{
    m_Api.DrawSetColor(colour.r, colour.g, colour.b, colour.a);
    m_Api.DrawFilledRect(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
}

void VrOptionsMenu::DrawTexture(int texture, int x0, int y0, int x1, int y1) const
{
    m_Api.DrawSetColor(255, 255, 255, 255);
    m_Api.DrawSetTexture(texture);
    m_Api.DrawTexturedRect(x0, y0, x1, y1);
}

void VrOptionsMenu::Text(HFont font, const std::wstring &text, int x, int y, Colour colour) const
{
    if (text.empty()) return;
    m_Api.DrawSetTextFont(font);
    m_Api.DrawSetTextColor(colour.r, colour.g, colour.b, colour.a);
    m_Api.DrawSetTextPos(x, y);
    m_Api.DrawPrintText(text.c_str(), static_cast<int>(text.size()));
}

int VrOptionsMenu::TextWide(HFont font, const std::wstring &text) const
{
    int wide = 0, tall = 0;
    if (!text.empty()) m_Api.GetTextSize(font, text.c_str(), wide, tall);
    return wide;
}

void VrOptionsMenu::DrawEntry()
{
    // Same DefaultButton style as AUDIO..CONTROLLER above it.
    if (m_EntrySelected) Fill(VrSettings::CursorBar(m_EntryRect, m_Unit), kCursor);
    const int tall = m_Api.GetFontTall(m_MenuFont);
    Text(m_MenuFont, L"VR", m_EntryRect.x + Px(Units::TextInset), m_EntryRect.y + (m_EntryRect.h - tall) / 2,
         m_EntrySelected ? kFocusText : kText);
}

void VrOptionsMenu::DrawTiles(const VrSettings::PageLayout &layout)
{
    const auto column = [&](int c) { return layout.body.x + static_cast<int>(std::lround(c * layout.tile)); };
    const auto row = [&](int r) { return layout.body.y + static_cast<int>(std::lround(r * layout.tile)); };
    const int owner = TransitionWindowType(1 + m_Page);
    const auto tile = [&](int texture, int x0, int y0, int x1, int y1) {
        DrawTexture(m_Textures[texture], x0, y0, x1, y1);
        // As DrawDialogBackground: claim the transition grid tile under this one.
        // The centre keeps a pixel of rounding from landing on a neighbour.
        if (m_TransitionPanel) m_Transition.MarkTileAt(m_TransitionPanel, (x0 + x1) / 2, (y0 + y1) / 2, owner);
    };
    for (int c = 0; c < layout.tabTiles; ++c) {
        const int texture = c == 0 ? TopLeft : c == layout.tabTiles - 1 ? TopRight : Top;
        tile(texture, column(c), layout.tab.y, column(c + 1), layout.body.y);
    }
    const int last = layout.wideTiles - 1;
    for (int r = 0; r < layout.tallTiles; ++r)
        for (int c = 0; c <= last; ++c) {
            int texture = Interior;
            if (r == layout.tallTiles - 1)
                texture = c == 0 ? BottomLeft : c == last ? BottomRight : Bottom;
            else if (c == 0)
                texture = Left;
            else if (c == last)
                texture = r == 0 && c >= layout.tabTiles ? TopRight : Right;
            else if (r == 0 && c >= layout.tabTiles)
                texture = Top; // the body's own top edge right of the title tab
            tile(texture, column(c), row(r), column(c + 1), row(r + 1));
        }
}

void VrOptionsMenu::DrawPage()
{
    const auto &page = VrSettings::Pages()[m_Page];
    const auto &config = m_VR->m_Config;
    const std::wstring title = Widen(page.title);
    std::vector<std::wstring> footerText;
    std::vector<int> footerWide;
    for (const auto &button : page.footer) {
        footerText.push_back(Upper(Widen(button.label))); // BACK, USE DEFAULTS as the native footer
        footerWide.push_back(TextWide(m_FooterFont, footerText.back()));
    }
    const int footerTall = m_Api.GetFontTall(m_FooterFont);
    const auto layout = VrSettings::LayoutPage(m_Unit, m_OptionsFrame.x, m_OptionsFrame.y + m_OptionsFrame.h,
        static_cast<int>(page.items.size()), TextWide(m_TitleFont, title), footerWide, footerTall);

    DrawTiles(layout);
    Text(m_TitleFont, title, layout.tab.x + Px(Units::TitleOffsetX), layout.tab.y + Px(Units::TitleOffsetY), kText);

    VrSettings::HitAreas hit;
    hit.layout = layout;
    hit.unit = m_Unit;
    const int rowTall = m_Api.GetFontTall(m_RowFont);
    const int symbolTall = m_Api.GetFontTall(m_SymbolFont);
    const int arrowWide = TextWide(m_SymbolFont, L"4");
    const int gap = Px(4.0f);
    for (int i = 0; i < static_cast<int>(page.items.size()); ++i) {
        const auto &item = page.items[i];
        const auto &rect = layout.rows[i];
        const auto bar = VrSettings::CursorBar(rect, m_Unit);
        const bool focused = !m_Focus.footer && m_Focus.index == i;
        const bool enabled = VrSettings::Enabled(item, config);
        if (focused) Fill(bar, kCursor);
        const Colour colour = !enabled ? kDisabledText : focused ? kFocusText : kText;
        const int textY = rect.y + (rect.h - rowTall) / 2;
        const std::wstring label = item.kind == VrSettings::Kind::Link ? Upper(Widen(item.label)) : Widen(item.label);
        Text(m_RowFont, label, rect.x + Px(Units::TextInset), textY, colour);
        if (item.kind == VrSettings::Kind::Link) continue;

        const std::wstring value = Widen(VrSettings::DisplayValue(item, config));
        int right = rect.x + rect.w - Px(Units::TextInset);
        if (focused && enabled) {
            // DialogListButton: Marlett arrows around the value of the focused row.
            const int arrowY = rect.y + (rect.h - symbolTall) / 2;
            const bool canIncrease = VrSettings::Step(item, config, 1).has_value();
            const bool canDecrease = VrSettings::Step(item, config, -1).has_value();
            Text(m_SymbolFont, L"4", right - arrowWide, arrowY, canIncrease ? kArrowActive : kArrowInactive);
            hit.increase = {right - arrowWide - gap, bar.y, arrowWide + 2 * gap, bar.h};
            right -= arrowWide + gap;
            const int valueWide = TextWide(m_RowFont, value);
            Text(m_RowFont, value, right - valueWide, textY, colour);
            const int leftArrow = right - valueWide - gap - arrowWide;
            Text(m_SymbolFont, L"3", leftArrow, arrowY, canDecrease ? kArrowActive : kArrowInactive);
            hit.decrease = {leftArrow - gap, bar.y, arrowWide + 2 * gap, bar.h};
            hit.arrowRow = i;
        } else {
            Text(m_RowFont, value, right - TextWide(m_RowFont, value), textY, colour);
        }
    }

    for (int i = 0; i < static_cast<int>(page.footer.size()); ++i) {
        const auto &rect = layout.footer[i];
        const bool hot = m_HoverFooter == i || (m_Focus.footer && m_Focus.index == i);
        DrawTexture(m_Textures[hot ? ButtonOver : Button], rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
        Text(m_FooterFont, footerText[i], rect.x + (rect.w - footerWide[i]) / 2,
             rect.y + (rect.h - footerTall) / 2, kText);
    }
    m_Hit = hit;
}

bool VrOptionsMenu::PointerMove(int x, int y)
{
    m_PointerValid = true;
    m_PointerX = x;
    m_PointerY = y;
    if (!m_OptionsVisible) {
        m_EntryHovered = false;
        return false;
    }
    if (!m_PageOpen) {
        const bool hovered = VrSettings::CursorBar(m_EntryRect, m_Unit).Contains(x, y);
        if (hovered && !m_EntrySelected) {
            // As BaseModHybridButton::OnCursorEntered: sound only when gaining focus.
            Sound(kFocusSound);
            m_EntrySelected = true;
            m_ReleasePending = true; // VGUI calls stay on the paint thread
        }
        m_EntryHovered = hovered;
        return hovered;
    }
    const auto target = VrSettings::HitTest(m_Hit, x, y);
    m_HoverFooter = target.type == VrSettings::TargetType::Footer ? target.index : -1;
    VrSettings::Focus focus = m_Focus;
    if (target.type == VrSettings::TargetType::Row || target.type == VrSettings::TargetType::Decrease ||
        target.type == VrSettings::TargetType::Increase)
        focus = {false, target.index};
    else if (target.type == VrSettings::TargetType::Footer)
        focus = {true, target.index};
    if (focus.footer != m_Focus.footer || focus.index != m_Focus.index) {
        m_Focus = focus;
        Sound(kFocusSound);
    }
    return true; // the page is modal
}

bool VrOptionsMenu::PointerPress()
{
    if (!m_OptionsVisible) return false;
    if (!m_PageOpen) {
        if (!m_PointerValid || !m_EntryHovered) return false;
        OpenPage(0);
        return true;
    }
    if (!m_PointerValid) return true;
    const auto target = VrSettings::HitTest(m_Hit, m_PointerX, m_PointerY);
    switch (target.type) {
    case VrSettings::TargetType::Decrease: StepRow(target.index, -1); break;
    case VrSettings::TargetType::Increase: StepRow(target.index, 1); break;
    case VrSettings::TargetType::Row: Activate(target.index); break;
    case VrSettings::TargetType::Footer: RunFooter(target.index); break;
    default: break;
    }
    return true;
}

void VrOptionsMenu::PointerLeave()
{
    m_PointerValid = false;
    m_EntryHovered = false;
    m_HoverFooter = -1;
}

bool VrOptionsMenu::Scroll(int delta)
{
    if (!m_PageOpen || !m_OptionsVisible) return false;
    if (delta != 0 && !m_Focus.footer) StepRow(m_Focus.index, delta > 0 ? 1 : -1);
    return true;
}

bool VrOptionsMenu::Navigate(Nav nav)
{
    if (!m_PageOpen || !m_OptionsVisible) return false;
    const auto &page = VrSettings::Pages()[m_Page];
    const int rows = static_cast<int>(page.items.size());
    const int footers = static_cast<int>(page.footer.size());
    switch (nav) {
    case Nav::Up:
    case Nav::Down:
        m_Focus = VrSettings::MoveFocus(m_Focus, rows, footers, nav == Nav::Down ? 1 : -1);
        Sound(kFocusSound);
        break;
    case Nav::Left:
    case Nav::Right:
        if (m_Focus.footer) {
            m_Focus = VrSettings::MoveFooterFocus(m_Focus, footers, nav == Nav::Right ? 1 : -1);
            Sound(kFocusSound);
        } else {
            StepRow(m_Focus.index, nav == Nav::Right ? 1 : -1);
        }
        break;
    case Nav::Select:
        if (m_Focus.footer) RunFooter(m_Focus.index);
        else Activate(m_Focus.index);
        break;
    case Nav::Back:
        Back();
        break;
    }
    return true;
}

void VrOptionsMenu::OpenPage(int page)
{
    m_PageOpen = true;
    m_Page = page;
    m_Focus = {};
    m_HoverFooter = -1;
    m_Hit = {}; // recomputed by the next paint
    Sound(kAcceptSound);
}

void VrOptionsMenu::ClosePage()
{
    m_PageOpen = false;
    m_Page = 0;
    m_Focus = {};
    m_HoverFooter = -1;
    m_Hit = {};
}

void VrOptionsMenu::Back()
{
    Sound(kBackSound);
    if (m_Page == 0) {
        ClosePage();
        return;
    }
    const int from = m_Page;
    m_Page = 0;
    m_Hit = {};
    m_HoverFooter = -1;
    m_Focus = {};
    const auto &items = VrSettings::Pages()[0].items;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (items[i].kind == VrSettings::Kind::Link && items[i].link == from) m_Focus = {false, i};
}

void VrOptionsMenu::Activate(int row)
{
    const auto &items = VrSettings::Pages()[m_Page].items;
    if (row < 0 || row >= static_cast<int>(items.size())) return;
    if (items[row].kind == VrSettings::Kind::Link) OpenPage(items[row].link);
    else StepRow(row, 1);
}

void VrOptionsMenu::StepRow(int row, int direction)
{
    const auto &items = VrSettings::Pages()[m_Page].items;
    if (row < 0 || row >= static_cast<int>(items.size())) return;
    m_Focus = {false, row};
    const auto &item = items[row];
    if (item.kind == VrSettings::Kind::Link) return;
    const auto next = VrSettings::Step(item, m_VR->m_Config, direction);
    Sound(next && m_VR->ApplyMenuSetting(item.key, *next) ? kAcceptSound : kInvalidSound);
}

void VrOptionsMenu::RunFooter(int index)
{
    const auto &footer = VrSettings::Pages()[m_Page].footer;
    if (index < 0 || index >= static_cast<int>(footer.size())) return;
    m_Focus = {true, index};
    switch (footer[index].action) {
    case VrSettings::FooterAction::Back:
        Back();
        break;
    case VrSettings::FooterAction::UseDefaults:
        m_VR->ResetMenuSettings();
        Sound(kAcceptSound);
        break;
    case VrSettings::FooterAction::Recenter:
        m_VR->ResetPosition();
        Sound(kAcceptSound);
        break;
    }
}
