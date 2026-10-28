#pragma once

// Options > VR: an entry under CONTROLLER on Portal 2's own Options screen and
// a Video-style dialog drawn with the game's tiles, fonts and sounds.

#include "portal2_transition.h"
#include "portal2_vgui.h"
#include "vr_settings.h"

#include <string>

class VR;

class VrOptionsMenu
{
public:
    enum class Nav { Up, Down, Left, Right, Select, Back };
    struct Colour { int r, g, b, a; };

    bool Initialize(VR *vr, void *surface, void *panel, void *schemeManager);
    void *PaintTarget() const { return m_Api.Ready() ? m_Api.PaintTraverseExAddress() : nullptr; }
    // Called after CMatSystemSurface::PaintTraverseEx, i.e. on top of every menu panel.
    void Paint(Portal2Vgui::VPANEL panel, bool paintPopups);

    bool PageOpen() const { return m_PageOpen; }
    // Pointer coordinates are VGUI screen pixels. Each returns true when the
    // event belongs to the VR options (the page is modal while open).
    bool PointerMove(int x, int y);
    bool PointerPress();
    void PointerLeave();
    bool Scroll(int delta);
    bool Navigate(Nav nav);

private:
    bool CursorVisible() const;
    // Depth-first over the embedded panel and the popups, descending only into
    // visible panels unless throughHidden; the matched panel itself may be hidden.
    template <typename Match> Portal2Vgui::VPANEL Search(Match &&match, bool throughHidden = false);
    bool FindOptionsScreen();
    bool ReadOptionsFrame(Portal2Vgui::VPANEL frame);
    bool IsNativeButton(Portal2Vgui::VPANEL panel) const;
    void ReleaseNativeFocus();
    void TrackNativeFocus();
    void HideNativeFooter();
    void RestoreNativeFooter();
    void UpdateTransition();
    VrSettings::Rect PanelRect(Portal2Vgui::VPANEL panel) const;
    bool LoadResources();
    void OpenPage(int page);
    void ClosePage();
    void Back();
    void StepRow(int row, int direction);
    void Activate(int row);
    void RunFooter(int index);
    void Sound(const char *file) const;

    void DrawEntry();
    void DrawPage();
    void DrawTiles(const VrSettings::PageLayout &layout);
    void DrawTexture(int texture, int x0, int y0, int x1, int y1) const;
    void Fill(const VrSettings::Rect &rect, Colour colour) const;
    void Text(Portal2Vgui::HFont font, const std::wstring &text, int x, int y, Colour colour) const;
    int TextWide(Portal2Vgui::HFont font, const std::wstring &text) const;
    int Px(float units) const;

    VR *m_VR = nullptr;
    Portal2Vgui::Api m_Api;
    Portal2Transition::Api m_Transition;
    // Rediscovered every paint; null while the tile transition is unavailable.
    void *m_TransitionPanel = nullptr;
    // 0 = native Options, 1 = VR page, 2 = Advanced VR page.
    int m_TransitionDepth = 0;

    bool m_ResourcesTried = false;
    bool m_ResourcesReady = false;
    Portal2Vgui::HFont m_TitleFont = 0;
    Portal2Vgui::HFont m_RowFont = 0;
    Portal2Vgui::HFont m_MenuFont = 0;
    Portal2Vgui::HFont m_FooterFont = 0;
    Portal2Vgui::HFont m_SymbolFont = 0;
    enum Texture { TopLeft, Top, TopRight, Left, Interior, Right, BottomLeft, Bottom, BottomRight,
                   Button, ButtonOver, TextureCount };
    int m_Textures[TextureCount]{};

    // Native Options screen, rediscovered on every paint: VGUI may delete frames.
    bool m_OptionsVisible = false;
    bool m_OptionsLogged = false;
    bool m_MenuPaintLogged = false;
    VrSettings::Rect m_OptionsFrame;
    VrSettings::Rect m_LastButton;
    VrSettings::Rect m_EntryRect;
    int m_RowPitch = 0;
    float m_Unit = 1.0f;
    Portal2Vgui::VPANEL m_Frame = 0;
    Portal2Vgui::VPANEL m_Caption = 0;

    // Like BaseModUI's buttons, the entry stays selected after the pointer leaves
    // it until a native button takes VGUI key focus back.
    bool m_EntrySelected = false;
    bool m_ReleasePending = false;
    bool m_FocusReleased = false;
    bool m_FocusLogged = false;
    Portal2Vgui::VPANEL m_HiddenFooter = 0;
    bool m_FooterLogged = false;

    bool m_PageOpen = false;
    int m_Page = 0;
    VrSettings::Focus m_Focus;
    bool m_EntryHovered = false;
    bool m_PointerValid = false;
    int m_PointerX = 0;
    int m_PointerY = 0;
    int m_HoverFooter = -1;
    VrSettings::HitAreas m_Hit;
};
