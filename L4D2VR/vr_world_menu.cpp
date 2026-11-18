#include "vr_world_menu.h"
#include "game.h"
#include "logger.h"
#include "sdk.h"
#include "vr.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <d3d9_vr.h>

namespace {

// The arc the panels hang on: metres from the head, panel widths, and where each panel's
// centre sits on it, in degrees, relative to the others. The whole arc turns so that the
// panels on show are centred on the heading the player had when pausing.
constexpr float kRadius = 1.6f;
constexpr float kTopAboveEyes = 0.22f;
constexpr float kWidthMeters[3] = {0.52f, 0.52f, 0.66f};
constexpr float kAngleDegrees[3] = {-20.0f, 0.0f, 22.5f};
constexpr int kWidthPixels[3] = {560, 560, 700};
// Room for about fifteen rows; a taller panel is cut at the bottom.
constexpr int kTextureHeight = 1600;
constexpr float kWorldDim = 0.45f;
// How quickly the arc turns to recentre, in seconds (time constant of the easing).
constexpr float kSlideSeconds = 0.08f;
constexpr float kPi = 3.14159265f;

const char *kKeys[3] = {"Portal2VR.Menu.Root", "Portal2VR.Menu.List", "Portal2VR.Menu.Page"};

float HalfSpanDegrees(int column)
{
    return std::atan(kWidthMeters[column] * 0.5f / kRadius) * 180.0f / kPi;
}

// The turn that centres panels 0..columns-1 on the heading.
float CentringShift(int columns)
{
    const int last = std::clamp(columns, 1, 3) - 1;
    const float left = kAngleDegrees[0] - HalfSpanDegrees(0);
    const float right = kAngleDegrees[last] + HalfSpanDegrees(last);
    return -(left + right) * 0.5f;
}

} // namespace

bool WorldMenu::Create(vr::IVROverlay *overlay)
{
    m_Overlay = overlay;
    if (!overlay) return false;
    for (int column = 0; column < kColumns; ++column) {
        const auto error = overlay->CreateOverlay(kKeys[column], kKeys[column], &m_Handles[column]);
        if (error != vr::VROverlayError_None ||
            overlay->SetOverlayInputMethod(m_Handles[column], vr::VROverlayInputMethod_Mouse) != vr::VROverlayError_None) {
            Logger::Write(std::string("In-world menu unavailable: overlay ") + kKeys[column] + " failed (" +
                std::to_string(error) + "); Portal 2's own pause screen is used");
            Destroy();
            return false;
        }
        overlay->SetOverlaySortOrder(m_Handles[column], 2);
    }
    m_Text = std::make_unique<VrMenuPaint::Text>(1024, 256);
    m_Ready = true;
    return true;
}

// The textures stay until VR::InvalidateD3DResources, after its queue drain.
void WorldMenu::Destroy()
{
    Close();
    for (auto &handle : m_Handles) {
        if (m_Overlay && handle != vr::k_ulOverlayHandleInvalid) m_Overlay->DestroyOverlay(handle);
        handle = vr::k_ulOverlayHandleInvalid;
    }
    m_Text.reset();
    m_Ready = false;
}

void WorldMenu::Update(VR &vr, bool wanted, bool cursorVisible, bool inputReady)
{
    if (!cursorVisible) m_Classic = false;
    const bool show = m_Ready && wanted && !m_Classic;
    if (show && !m_Showing) Open(vr);
    else if (!show && m_Showing) Close();
    if (!m_Showing) return;

    bool submitted[kColumns]{};
    Submit(vr, submitted);
    if (inputReady) Pointer(vr);
    if (!m_Showing) return;
    const auto now = std::chrono::steady_clock::now();
    if (m_RescanAt != std::chrono::steady_clock::time_point{} && now >= m_RescanAt) {
        m_RescanAt = {};
        m_Model.SetSaves(VrMenuData::ListSaves(""));
    }
    Slide();
    Repaint(vr, submitted);
}

void WorldMenu::Open(VR &vr)
{
    if (!m_TextsLoaded) {
        VrMenuData::Strings strings;
        strings.Load("");
        VrMenu::Texts texts;
        texts.Load(strings);
        m_Model.SetTexts(std::move(texts));
        m_TextsLoaded = true;
        Logger::Write("In-world menu: " + std::to_string(strings.Count()) + " game texts in " + strings.Language() +
            "; font " + std::string(m_Text->Family().begin(), m_Text->Family().end()));
    }
    m_Model.SetSaves(VrMenuData::ListSaves(""));
    m_Model.Open();

    // Anchor to where the head is and faces now; the panels then stay put in the room.
    const auto &head = vr.m_Poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
    m_Yaw = std::atan2(-head.m[0][2], head.m[2][2]);
    for (int axis = 0; axis < 3; ++axis) m_Head[axis] = head.m[axis][3];
    for (int column = 0; column < kColumns; ++column) {
        m_Visible[column] = m_Uploaded[column] = m_Pending[column] = false;
    }
    // The first panel opens straight ahead.
    m_Shift = CentringShift(1);
    m_LastSlide = {};

    m_Showing = true;
    m_Repaints = 0;
    m_RepaintMs = 0.0;
    if (vr::VRCompositor()) vr::VRCompositor()->FadeToColor(0.2f, 0.0f, 0.0f, 0.0f, kWorldDim, false);
    Logger::Write("In-world menu opened");
}

void WorldMenu::Close()
{
    if (!m_Showing) return;
    m_Showing = false;
    for (int column = 0; column < kColumns; ++column) {
        Hide(column);
        m_Uploaded[column] = m_Pending[column] = false;
    }
    if (vr::VRCompositor()) vr::VRCompositor()->FadeToColor(0.15f, 0.0f, 0.0f, 0.0f, 0.0f, false);
    Logger::Write("In-world menu closed: " + std::to_string(m_Repaints) + " panel repaints, " +
        std::to_string(m_Repaints ? m_RepaintMs / m_Repaints : 0.0) + " ms each");
}

void WorldMenu::Hide(int column)
{
    if (m_Overlay && m_Handles[column] != vr::k_ulOverlayHandleInvalid) {
        m_Overlay->SetOverlayFlag(m_Handles[column], vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
        m_Overlay->HideOverlay(m_Handles[column]);
    }
    m_Visible[column] = false;
}

unsigned WorldMenu::PendingSurfaces(IDirect3DSurface9 **out, unsigned room) const
{
    unsigned count = 0;
    for (int column = 0; column < kColumns && count < room; ++column) {
        if (m_Showing && m_Pending[column] && m_Sheets[column].surface)
            out[count++] = m_Sheets[column].surface;
    }
    return count;
}

void WorldMenu::DetachTextures()
{
    for (int column = 0; column < kColumns; ++column) {
        Hide(column);
        if (m_Overlay && m_Handles[column] != vr::k_ulOverlayHandleInvalid)
            m_Overlay->ClearOverlayTexture(m_Handles[column]);
        m_Uploaded[column] = m_Pending[column] = false;
    }
}

void WorldMenu::ReleaseTextures()
{
    for (auto &sheet : m_Sheets) {
        if (sheet.surface) sheet.surface->Release();
        if (sheet.texture) sheet.texture->Release();
        sheet = {};
    }
    for (int column = 0; column < kColumns; ++column) m_Uploaded[column] = m_Pending[column] = false;
}

void WorldMenu::Pointer(VR &vr)
{
    int hoverColumn = -1, hoverRow = -1, hoverButton = -1;
    for (int column = 0; column < kColumns; ++column) {
        if (!m_Visible[column]) continue;
        const auto handle = m_Handles[column];
        const bool pointed = vr.CheckOverlayIntersectionForController(handle, vr::TrackedControllerRole_LeftHand) ||
            vr.CheckOverlayIntersectionForController(handle, vr::TrackedControllerRole_RightHand);
        const VrMenuPaint::Layout &layout = m_Layouts[column];
        vr::VREvent_t event{};
        while (m_Overlay->PollNextOverlayEvent(handle, &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_MouseMove) {
                // Overlay mouse coordinates are texture coordinates times the mouse scale,
                // counted from the bottom of the whole texture, not of the shown part.
                m_PointerX[column] = static_cast<int>(event.data.mouse.x);
                m_PointerY[column] = m_Sheets[column].height - static_cast<int>(event.data.mouse.y);
            } else if (event.eventType == vr::VREvent_MouseButtonDown && pointed) {
                const int x = m_PointerX[column], y = m_PointerY[column], row = layout.RowAt(x, y);
                int direction = 0;
                if (row >= 0 && layout.less[row].Contains(x, y)) direction = -1;
                else if (row >= 0 && layout.more[row].Contains(x, y)) direction = 1;
                const VrMenu::Command command = m_Model.Click(column, row, direction, layout.ButtonAt(x, y), vr.m_Config);
                const VrMenu::Cue cue = m_Model.TakeCue();
                Play(vr, Run(vr, command) ? cue : VrMenu::Cue::Invalid);
                if (!m_Showing) return;
            }
        }
        if (pointed) {
            hoverColumn = column;
            hoverRow = layout.RowAt(m_PointerX[column], m_PointerY[column]);
            hoverButton = layout.ButtonAt(m_PointerX[column], m_PointerY[column]);
        }
    }
    if (m_Model.Hover(hoverColumn, hoverRow, hoverButton) && m_Model.HoverLive(vr.m_Config)) {
        vr.QueueHapticEvent(Haptics::Event::UseHover);
        Play(vr, VrMenu::Cue::Focus);
    }

    if (vr.MenuBackPressed()) {
        const VrMenu::Command command = m_Model.Back();
        Play(vr, m_Model.TakeCue());
        Run(vr, command);
    }
}

// Hands the panels painted last frame to their overlays. The bridge has just moved their
// images into the layout OpenVR copies from, and the copy goes on DXVK's queue, so this
// runs under the same submission lock as the eyes.
void WorldMenu::Submit(VR &vr, bool (&submitted)[kColumns])
{
    bool any = false;
    for (int column = 0; column < kColumns; ++column) any = any || m_Pending[column];
    if (!any) return;
    if (vr.m_D3DVR) vr.m_D3DVR->LockSubmissionQueue();
    for (int column = 0; column < kColumns; ++column) {
        if (!m_Pending[column]) continue;
        m_Pending[column] = false;
        submitted[column] = true;
        const Sheet &sheet = m_Sheets[column];
        const auto handle = m_Handles[column];
        const int width = kWidthPixels[column], height = m_NextHeights[column];
        const vr::VRTextureBounds_t bounds{0.0f, 0.0f, 1.0f, static_cast<float>(height) / sheet.height};
        // One mouse unit per texel over the whole texture (see Pointer).
        const vr::HmdVector2_t mouse{static_cast<float>(width), static_cast<float>(sheet.height)};
        const auto boundsError = m_Overlay->SetOverlayTextureBounds(handle, &bounds);
        const auto textureError = m_Overlay->SetOverlayTexture(handle, &sheet.shared);
        m_Overlay->SetOverlayMouseScale(handle, &mouse);
        m_Overlay->SetOverlayWidthInMeters(handle, kWidthMeters[column]);
        m_Heights[column] = height;
        m_Layouts[column] = std::move(m_NextLayouts[column]);
        Place(column);
        const bool textured = boundsError == vr::VROverlayError_None && textureError == vr::VROverlayError_None;
        const auto showError = textured ? m_Overlay->ShowOverlay(handle) : textureError;
        // Laser-mouse mode for as long as the panels are up, so SteamVR keeps the
        // controllers and the laser on screen even while they point elsewhere.
        m_Overlay->SetOverlayFlag(handle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);
        m_Visible[column] = textured && showError == vr::VROverlayError_None;
        if (!m_Visible[column] && !m_SubmitLogged) {
            m_SubmitLogged = true;
            Logger::Write("In-world menu: panel texture refused, bounds=" + std::to_string(boundsError) +
                " texture=" + std::to_string(textureError) + " show=" + std::to_string(showError));
        }
    }
    if (vr.m_D3DVR) vr.m_D3DVR->UnlockSubmissionQueue();
}

// Eases the arc toward centring the panels on show.
void WorldMenu::Slide()
{
    int shown = 0;
    while (shown < kColumns && m_Visible[shown]) ++shown;
    const float target = CentringShift(std::max(shown, 1));
    const auto now = std::chrono::steady_clock::now();
    const float dt = m_LastSlide == std::chrono::steady_clock::time_point{} ? 0.0f :
        std::min(std::chrono::duration<float>(now - m_LastSlide).count(), 0.1f);
    m_LastSlide = now;
    if (std::fabs(target - m_Shift) < 0.01f) return;
    m_Shift += (target - m_Shift) * (1.0f - std::exp(-dt / kSlideSeconds));
    if (std::fabs(target - m_Shift) < 0.05f) m_Shift = target;
    for (int column = 0; column < kColumns; ++column)
        if (m_Visible[column]) Place(column);
}

// On the arc, facing the head, tops level a little above the eyes.
void WorldMenu::Place(int column)
{
    const float angle = m_Yaw + (kAngleDegrees[column] + m_Shift) * kPi / 180.0f;
    const float tall = kWidthMeters[column] * m_Heights[column] / kWidthPixels[column];
    vr::HmdMatrix34_t pose{};
    pose.m[0][0] = std::cos(angle);
    pose.m[0][2] = -std::sin(angle);
    pose.m[1][1] = 1.0f;
    pose.m[2][0] = std::sin(angle);
    pose.m[2][2] = std::cos(angle);
    pose.m[0][3] = m_Head[0] + std::sin(angle) * kRadius;
    pose.m[1][3] = m_Head[1] + kTopAboveEyes - tall * 0.5f;
    pose.m[2][3] = m_Head[2] - std::cos(angle) * kRadius;
    const auto origin = vr::VRCompositor() ? vr::VRCompositor()->GetTrackingSpace() : vr::TrackingUniverseStanding;
    m_Overlay->SetOverlayTransformAbsolute(m_Handles[column], origin, &pose);
}

void WorldMenu::Repaint(VR &vr, const bool (&submitted)[kColumns])
{
    const int columns = m_Model.Columns();
    for (int column = 0; column < kColumns; ++column) {
        if (column >= columns) {
            if (m_Visible[column]) Hide(column);
            m_Uploaded[column] = m_Pending[column] = false;
            continue;
        }
        // Until this Present ends its image is in the layout OpenVR reads; repaint next frame.
        if (submitted[column]) continue;
        VrMenuPaint::Panel panel = m_Model.Panel(column, vr.m_Config);
        if (m_Uploaded[column] && panel == m_Painted[column]) continue;

        const auto start = std::chrono::steady_clock::now();
        const int width = kWidthPixels[column];
        const int height = std::min(VrMenuPaint::ContentHeight(panel, width), kTextureHeight);
        VrMenuPaint::Canvas canvas(width, height);
        VrMenuPaint::Layout layout = VrMenuPaint::Arrange(panel, width, height, *m_Text);
        VrMenuPaint::Paint(canvas, *m_Text, panel, layout);
        VrMenuPaint::Glow(canvas, static_cast<int>(7 * layout.scale), 0.55f);
        if (!Upload(vr, column, canvas.Data(), width, height)) continue;

        m_NextLayouts[column] = std::move(layout);
        m_NextHeights[column] = height;
        m_Painted[column] = std::move(panel);
        m_Uploaded[column] = m_Pending[column] = true;
        ++m_Repaints;
        m_RepaintMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
}

// Into the panel's managed texture (BGRA, like the canvas); PreLoad puts the upload on the
// queue now instead of at a draw that never comes.
bool WorldMenu::Upload(VR &vr, int column, const std::uint32_t *pixels, int width, int height)
{
    Sheet &sheet = m_Sheets[column];
    const auto fail = [&](const char *step, HRESULT result) {
        if (!m_UploadLogged) {
            m_UploadLogged = true;
            char code[16]{};
            snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(result));
            Logger::Write(std::string("In-world menu: panel texture ") + step + " failed, hr=" + code);
        }
        return false;
    };
    if (!sheet.texture) {
        if (!vr.m_D3DDevice || !vr.m_D3DVR) return fail("device", E_POINTER);
        HRESULT result = vr.m_D3DDevice->CreateTexture(width, kTextureHeight, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, &sheet.texture, nullptr);
        if (FAILED(result) || !sheet.texture) return fail("creation", result);
        D3D9_TEXTURE_VR_DESC desc{};
        result = sheet.texture->GetSurfaceLevel(0, &sheet.surface);
        if (SUCCEEDED(result)) result = vr.m_D3DVR->GetVRDesc(sheet.surface, &desc);
        if (FAILED(result)) {
            if (sheet.surface) sheet.surface->Release();
            sheet.texture->Release();
            sheet = {};
            return fail("sharing", result);
        }
        sheet.vulkan.m_nImage = desc.Image;
        sheet.vulkan.m_pDevice = desc.Device;
        sheet.vulkan.m_pPhysicalDevice = desc.PhysicalDevice;
        sheet.vulkan.m_pInstance = desc.Instance;
        sheet.vulkan.m_pQueue = desc.Queue;
        sheet.vulkan.m_nQueueFamilyIndex = desc.QueueFamilyIndex;
        sheet.vulkan.m_nWidth = desc.Width;
        sheet.vulkan.m_nHeight = desc.Height;
        sheet.vulkan.m_nFormat = static_cast<std::uint32_t>(desc.Format);
        sheet.vulkan.m_nSampleCount = desc.SampleCount;
        sheet.shared = {&sheet.vulkan, vr::TextureType_Vulkan, vr::ColorSpace_Auto};
        sheet.height = kTextureHeight;
    }
    RECT area{0, 0, width, height};
    D3DLOCKED_RECT locked{};
    const HRESULT lockResult = sheet.texture->LockRect(0, &locked, &area, 0);
    if (FAILED(lockResult)) return fail("lock", lockResult);
    for (int y = 0; y < height; ++y)
        std::memcpy(static_cast<std::uint8_t *>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch,
            pixels + static_cast<std::size_t>(y) * width, static_cast<std::size_t>(width) * 4);
    sheet.texture->UnlockRect(0);
    sheet.texture->PreLoad();
    return true;
}

// The same sounds Portal 2's own menus make.
void WorldMenu::Play(VR &vr, VrMenu::Cue cue)
{
    const char *file = cue == VrMenu::Cue::Focus ? "UI/menu_focus.wav" : cue == VrMenu::Cue::Accept ? "UI/menu_accept.wav" :
        cue == VrMenu::Cue::Back ? "UI/menu_back.wav" : cue == VrMenu::Cue::Invalid ? "UI/menu_invalid.wav" : nullptr;
    if (file) vr.m_OptionsMenu.PlayMenuSound(file);
}

// False when the command could not be carried out.
bool WorldMenu::Run(VR &vr, const VrMenu::Command &command)
{
    using Kind = VrMenu::Command::Kind;
    switch (command.kind) {
    case Kind::None:
        return true;
    case Kind::Resume:
        vr.m_Game->ClientCmd_Unrestricted("gameui_hide");
        break;
    case Kind::ExitToMainMenu:
        vr.m_Game->ClientCmd_Unrestricted("disconnect");
        break;
    case Kind::ClassicMenu:
        m_Classic = true;
        Close();
        break;
    case Kind::Recenter:
        vr.ResetPosition();
        break;
    case Kind::Defaults:
        vr.ResetMenuSettings();
        break;
    case Kind::Save:
    case Kind::Load:
        if (!VrMenuData::SafeSaveName(command.text)) return false;
        vr.m_Game->ClientCmd_Unrestricted(((command.kind == Kind::Save ? "save " : "load ") + command.text).c_str());
        if (command.kind == Kind::Save)
            m_RescanAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
        break;
    case Kind::Set:
        if (!vr.ApplyMenuSetting(command.text, command.value)) return false;
        break;
    }
    Logger::Write("In-world menu: " + std::string(
        command.kind == Kind::Resume ? "resume" : command.kind == Kind::ExitToMainMenu ? "exit to main menu" :
        command.kind == Kind::ClassicMenu ? "Portal 2's own menu" : command.kind == Kind::Recenter ? "recenter" :
        command.kind == Kind::Defaults ? "defaults" : command.kind == Kind::Save ? "save " + command.text :
        command.kind == Kind::Load ? "load " + command.text : command.text + "=" + command.value));
    return true;
}
