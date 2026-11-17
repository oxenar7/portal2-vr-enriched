#pragma once

// The pause menu as panels hanging in the paused world: up to three SteamVR overlays on an
// arc in front of the player, painted by VrMenuPaint and driven by VrMenu::Model. Their
// clicks are overlay mouse events, so nothing depends on which Windows window has focus.

#include "vr_menu_model.h"
#include "openvr.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class VR;

class WorldMenu {
public:
    // Three overlays; on failure the menu stays off and Portal 2's own pause screen is used.
    bool Create(vr::IVROverlay *overlay);
    void Destroy();

    bool Ready() const { return m_Ready; }
    bool Showing() const { return m_Showing; }
    // The player asked for Portal 2's own menu; holds until the pause menu closes.
    bool Classic() const { return m_Classic; }

    // Once per VR::Update. wanted: a game is paused with its menu up and the panels should
    // carry it. inputReady: the controller actions of this frame are valid.
    void Update(VR &vr, bool wanted, bool cursorVisible, bool inputReady);

private:
    static constexpr int kColumns = 3;

    void Open(VR &vr);
    void Close();
    void Pointer(VR &vr);
    void Repaint(VR &vr);
    bool Run(VR &vr, const VrMenu::Command &command);
    void Play(VR &vr, VrMenu::Cue cue);

    vr::IVROverlay *m_Overlay = nullptr;
    vr::VROverlayHandle_t m_Handles[kColumns]{vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid,
                                             vr::k_ulOverlayHandleInvalid};
    bool m_Ready = false, m_Showing = false, m_Classic = false, m_TextsLoaded = false;
    VrMenu::Model m_Model;
    std::unique_ptr<VrMenuPaint::Text> m_Text;
    VrMenuPaint::Panel m_Painted[kColumns];
    VrMenuPaint::Layout m_Layouts[kColumns];
    bool m_Visible[kColumns]{};
    int m_Heights[kColumns]{};
    int m_PointerX[kColumns]{}, m_PointerY[kColumns]{};
    std::vector<std::uint8_t> m_Upload;
    float m_Yaw = 0.0f, m_Head[3]{};
    std::chrono::steady_clock::time_point m_RescanAt{};
    unsigned m_Repaints = 0;
    double m_RepaintMs = 0.0;
};
