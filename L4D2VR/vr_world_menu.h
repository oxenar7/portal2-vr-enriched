#pragma once

// The pause menu as panels hanging in the paused world: up to three SteamVR overlays on an
// arc in front of the player, painted by VrMenuPaint and driven by VrMenu::Model. Their
// clicks are overlay mouse events, so nothing depends on which Windows window has focus.
// Each panel is a D3D9 texture that reaches its overlay together with the eyes, so a
// repaint swaps the picture without the blank frames SetOverlayRaw leaves.

#include "vr_menu_model.h"
#include "openvr.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class VR;
struct IDirect3DTexture9;
struct IDirect3DSurface9;

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

    // Panels repainted since the last Present. The bridge moves them into the layout OpenVR
    // copies from together with the eyes; Update then hands them to their overlays.
    unsigned PendingSurfaces(IDirect3DSurface9 **out, unsigned room) const;
    // Device lifecycle: the overlays let go of the textures before the queue drains...
    void DetachTextures();
    // ...and the textures go after it. They come back on the next repaint.
    void ReleaseTextures();

private:
    static constexpr int kColumns = 3;

    struct Sheet {
        IDirect3DTexture9 *texture = nullptr;
        IDirect3DSurface9 *surface = nullptr;
        vr::VRVulkanTextureData_t vulkan{};
        vr::Texture_t shared{};
        int height = 0;
    };

    void Open(VR &vr);
    void Close();
    void Pointer(VR &vr);
    void Submit(VR &vr, bool (&submitted)[kColumns]);
    void Slide();
    void Place(int column);
    void Hide(int column);
    void Repaint(VR &vr, const bool (&submitted)[kColumns]);
    bool Upload(VR &vr, int column, const std::uint32_t *pixels, int width, int height);
    bool Run(VR &vr, const VrMenu::Command &command);
    void Play(VR &vr, VrMenu::Cue cue);

    vr::IVROverlay *m_Overlay = nullptr;
    vr::VROverlayHandle_t m_Handles[kColumns]{vr::k_ulOverlayHandleInvalid, vr::k_ulOverlayHandleInvalid,
                                             vr::k_ulOverlayHandleInvalid};
    bool m_Ready = false, m_Showing = false, m_Classic = false, m_TextsLoaded = false;
    VrMenu::Model m_Model;
    std::unique_ptr<VrMenuPaint::Text> m_Text;
    Sheet m_Sheets[kColumns];

    // Painted and uploaded, waiting for the next Present to reach the overlay.
    VrMenuPaint::Panel m_Painted[kColumns];
    bool m_Uploaded[kColumns]{}, m_Pending[kColumns]{};
    VrMenuPaint::Layout m_NextLayouts[kColumns];
    int m_NextHeights[kColumns]{};

    // What the overlays show now; the pointer works on these.
    bool m_Visible[kColumns]{};
    VrMenuPaint::Layout m_Layouts[kColumns];
    int m_Heights[kColumns]{};
    int m_PointerX[kColumns]{}, m_PointerY[kColumns]{};

    // The arc is anchored to the heading at pause and turned so the shown panels sit
    // centred on it; m_Shift eases toward that when a panel opens or closes.
    float m_Yaw = 0.0f, m_Head[3]{};
    float m_Shift = 0.0f;
    std::chrono::steady_clock::time_point m_LastSlide{};

    std::chrono::steady_clock::time_point m_RescanAt{};
    unsigned m_Repaints = 0;
    double m_RepaintMs = 0.0;
    bool m_UploadLogged = false, m_SubmitLogged = false;
};
