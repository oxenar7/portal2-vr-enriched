#include "vr_world_menu.h"
#include "game.h"
#include "logger.h"
#include "sdk.h"
#include "vr.h"

#include <cmath>
#include <string>

namespace {

// The arc the panels hang on: metres from the head, panel widths, and the angle of each
// panel's centre from the direction the player faced when pausing. The middle panel sits
// straight ahead, since it and the page beside it are where the reading happens.
constexpr float kRadius = 1.6f;
constexpr float kTopAboveEyes = 0.22f;
constexpr float kWidthMeters[3] = {0.52f, 0.52f, 0.66f};
constexpr float kAngleDegrees[3] = {-20.0f, 0.0f, 22.5f};
constexpr int kWidthPixels[3] = {560, 560, 700};
constexpr float kWorldDim = 0.45f;

const char *kKeys[3] = {"Portal2VR.Menu.Root", "Portal2VR.Menu.List", "Portal2VR.Menu.Page"};

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

    if (inputReady) Pointer(vr);
    const auto now = std::chrono::steady_clock::now();
    if (m_RescanAt != std::chrono::steady_clock::time_point{} && now >= m_RescanAt) {
        m_RescanAt = {};
        m_Model.SetSaves(VrMenuData::ListSaves(""));
    }
    if (m_Showing) Repaint(vr);
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
    for (int column = 0; column < kColumns; ++column) m_Visible[column] = false;

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
        if (m_Overlay && m_Handles[column] != vr::k_ulOverlayHandleInvalid) {
            m_Overlay->SetOverlayFlag(m_Handles[column], vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
            m_Overlay->HideOverlay(m_Handles[column]);
        }
        m_Visible[column] = false;
    }
    if (vr::VRCompositor()) vr::VRCompositor()->FadeToColor(0.15f, 0.0f, 0.0f, 0.0f, 0.0f, false);
    Logger::Write("In-world menu closed: " + std::to_string(m_Repaints) + " panel repaints, " +
        std::to_string(m_Repaints ? m_RepaintMs / m_Repaints : 0.0) + " ms each");
}

void WorldMenu::Pointer(VR &vr)
{
    int hoverColumn = -1, hoverRow = -1, hoverButton = -1;
    for (int column = 0; column < kColumns; ++column) {
        if (!m_Visible[column]) continue;
        const auto handle = m_Handles[column];
        const bool pointed = vr.CheckOverlayIntersectionForController(handle, vr::TrackedControllerRole_LeftHand) ||
            vr.CheckOverlayIntersectionForController(handle, vr::TrackedControllerRole_RightHand);
        m_Overlay->SetOverlayFlag(handle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, pointed);
        const VrMenuPaint::Layout &layout = m_Layouts[column];
        vr::VREvent_t event{};
        while (m_Overlay->PollNextOverlayEvent(handle, &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_MouseMove) {
                // Overlay mouse coordinates start at the bottom left.
                m_PointerX[column] = static_cast<int>(event.data.mouse.x);
                m_PointerY[column] = m_Heights[column] - static_cast<int>(event.data.mouse.y);
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

    const bool back = vr.PressedDigitalAction(vr.m_MenuBack, true);
    const bool pause = vr.PressedDigitalAction(vr.m_Pause, true);
    if (back || pause) {
        const VrMenu::Command command = m_Model.Back();
        Play(vr, m_Model.TakeCue());
        Run(vr, command);
    }
}

void WorldMenu::Repaint(VR &vr)
{
    const int columns = m_Model.Columns();
    for (int column = 0; column < kColumns; ++column) {
        const auto handle = m_Handles[column];
        if (column >= columns) {
            if (m_Visible[column]) {
                m_Overlay->SetOverlayFlag(handle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
                m_Overlay->HideOverlay(handle);
                m_Visible[column] = false;
            }
            continue;
        }
        VrMenuPaint::Panel panel = m_Model.Panel(column, vr.m_Config);
        if (m_Visible[column] && panel == m_Painted[column]) continue;

        const auto start = std::chrono::steady_clock::now();
        const int width = kWidthPixels[column], height = VrMenuPaint::ContentHeight(panel, width);
        VrMenuPaint::Canvas canvas(width, height);
        m_Layouts[column] = VrMenuPaint::Arrange(panel, width, height, *m_Text);
        VrMenuPaint::Paint(canvas, *m_Text, panel, m_Layouts[column]);
        VrMenuPaint::Glow(canvas, static_cast<int>(7 * m_Layouts[column].scale), 0.55f);

        // The canvas is BGRA in memory; OpenVR takes RGBA.
        m_Upload.resize(static_cast<std::size_t>(width) * height * 4);
        const std::uint32_t *pixels = canvas.Data();
        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
            const std::uint32_t p = pixels[i];
            m_Upload[i * 4] = static_cast<std::uint8_t>(p >> 16);
            m_Upload[i * 4 + 1] = static_cast<std::uint8_t>(p >> 8);
            m_Upload[i * 4 + 2] = static_cast<std::uint8_t>(p);
            m_Upload[i * 4 + 3] = static_cast<std::uint8_t>(p >> 24);
        }

        // On the arc, facing the head, tops level a little above the eyes.
        const float angle = m_Yaw + kAngleDegrees[column] * 3.14159265f / 180.0f;
        const float tall = kWidthMeters[column] * height / width;
        vr::HmdMatrix34_t pose{};
        pose.m[0][0] = std::cos(angle);
        pose.m[0][2] = -std::sin(angle);
        pose.m[1][1] = 1.0f;
        pose.m[2][0] = std::sin(angle);
        pose.m[2][2] = std::cos(angle);
        pose.m[0][3] = m_Head[0] + std::sin(angle) * kRadius;
        pose.m[1][3] = m_Head[1] + kTopAboveEyes - tall * 0.5f;
        pose.m[2][3] = m_Head[2] - std::cos(angle) * kRadius;

        const vr::HmdVector2_t mouse{static_cast<float>(width), static_cast<float>(height)};
        const auto origin = vr::VRCompositor() ? vr::VRCompositor()->GetTrackingSpace() : vr::TrackingUniverseStanding;
        const auto rawError = m_Overlay->SetOverlayRaw(handle, m_Upload.data(), width, height, 4);
        m_Overlay->SetOverlayMouseScale(handle, &mouse);
        m_Overlay->SetOverlayWidthInMeters(handle, kWidthMeters[column]);
        m_Overlay->SetOverlayTransformAbsolute(handle, origin, &pose);
        const auto showError = m_Overlay->ShowOverlay(handle);
        if ((rawError != vr::VROverlayError_None || showError != vr::VROverlayError_None) && !m_Repaints)
            Logger::Write("In-world menu: panel upload failed, raw=" + std::to_string(rawError) + " show=" +
                std::to_string(showError));

        m_Heights[column] = height;
        m_Painted[column] = std::move(panel);
        m_Visible[column] = true;
        ++m_Repaints;
        m_RepaintMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
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
