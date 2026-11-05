#include "vr.h"
#include <Windows.h>
#include "sdk.h"
#include "game.h"
#include "hooks.h"
#include "offsets.h"
#include "logger.h"
#include "trace.h"
#include "aim_feedback.h"
#include "native_beam.h"
#include "hud_capture.h"
#include "sdk/ivdebugoverlay.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <d3d9_vr.h>

// Original Portal2VR viewmodel calibration, retained for the legacy viewmodel.
static const Vector kLegacyViewmodelPositionOffset{4.5f, -1.0f, 1.5f};
// v_portalgun.mdl's origin is the flat-screen camera eye; the gun hull spans
// x -0.7..37.3, y -12.8..3.5, z -16.4..5.2. Pivoting the controller-locked model
// there puts the gun on a 40-85 cm lever arm that amplifies wrist tremor. This
// moves the white body onto the controller (headset-calibrated).
static const Vector kAlignedViewmodelGripOffset{15.0f, 6.0f, -8.0f};

static const Vector &ViewmodelBasePositionOffset(bool aligned)
{
    return aligned ? kAlignedViewmodelGripOffset : kLegacyViewmodelPositionOffset;
}

namespace {
class VRSubmissionScope
{
public:
    explicit VRSubmissionScope(IDirect3DVR9* bridge) : m_Bridge(bridge)
    { if (m_Bridge) m_Bridge->LockSubmissionQueue(); }
    ~VRSubmissionScope() { if (m_Bridge) m_Bridge->UnlockSubmissionQueue(); }
    VRSubmissionScope(const VRSubmissionScope&) = delete;
    VRSubmissionScope& operator=(const VRSubmissionScope&) = delete;
private:
    IDirect3DVR9* m_Bridge;
};

bool TextureSubmissionReady(const SharedTextureHolder& holder)
{
    const auto& data = holder.m_VulkanData;
    return VRTextureSubmissionReadiness{holder.m_VRTexture.handle != nullptr, data.m_nImage,
        data.m_nWidth, data.m_nHeight, data.m_pDevice != nullptr, data.m_pPhysicalDevice != nullptr,
        data.m_pInstance != nullptr, data.m_pQueue != nullptr, data.m_nSampleCount}.Ready();
}

std::string HRESULTText(HRESULT value)
{
    char text[16]{};
    snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(value));
    return text;
}
}

VR::VR(Game *game) 
{
    m_Game = game;

    char errorString[MAX_STR_LEN];

    vr::HmdError error = vr::VRInitError_None;
    m_System = m_OpenVRSession.Acquire(error);

    if (error != vr::VRInitError_None || !m_System)
    {
        snprintf(errorString, MAX_STR_LEN, "VR_Init failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(error));
        Game::errorMsg(errorString);
        return;
    }
    m_OpenVRStarted = true;
    Logger::Write("VR_Init OK");

    if (!vr::VRCompositor())
    {
        Game::errorMsg("Compositor initialization failed.");
        return;
    }
    Logger::Write("OpenVR compositor OK");

    m_Input = vr::VRInput();
    if (!m_Input) {
        Game::errorMsg("OpenVR input initialization failed.");
        return;
    }
    Logger::Write("OpenVR input OK");
    m_RenderModels = vr::VRRenderModels();
    Logger::Write(m_RenderModels ? "OpenVR render models OK" :
                  "OpenVR render models unavailable; controller tip offset disabled");

    m_System->GetRecommendedRenderTargetSize(&m_RenderWidth, &m_RenderHeight);
    if (!m_RenderWidth || !m_RenderHeight) {
        Game::errorMsg("OpenVR returned an invalid render target size.");
        return;
    }
    Logger::Write("Recommended eye render target: " + std::to_string(m_RenderWidth) +
                  "x" + std::to_string(m_RenderHeight));
    char hmdName[vr::k_unMaxPropertyStringSize]{};
    m_System->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_ModelNumber_String, hmdName, sizeof(hmdName));
    Logger::Write(std::string("HMD: ") + (hmdName[0] ? hmdName : "unknown"));
    m_AntiAliasing = 0;

    float l_left = 0.0f, l_right = 0.0f, l_top = 0.0f, l_bottom = 0.0f;
    m_System->GetProjectionRaw(vr::EVREye::Eye_Left, &l_left, &l_right, &l_top, &l_bottom);

    float r_left = 0.0f, r_right = 0.0f, r_top = 0.0f, r_bottom = 0.0f;
    m_System->GetProjectionRaw(vr::EVREye::Eye_Right, &r_left, &r_right, &r_top, &r_bottom);

    float tanHalfFov[2];

    tanHalfFov[0] = std::max({ -l_left, l_right, -r_left, r_right });
    tanHalfFov[1] = std::max({ -l_top, l_bottom, -r_top, r_bottom });

    m_TextureBounds[0].uMin = 0.5f + 0.5f * l_left / tanHalfFov[0];
    m_TextureBounds[0].uMax = 0.5f + 0.5f * l_right / tanHalfFov[0];
    m_TextureBounds[0].vMin = 0.5f - 0.5f * l_bottom / tanHalfFov[1];
    m_TextureBounds[0].vMax = 0.5f - 0.5f * l_top / tanHalfFov[1];

    m_TextureBounds[1].uMin = 0.5f + 0.5f * r_left / tanHalfFov[0];
    m_TextureBounds[1].uMax = 0.5f + 0.5f * r_right / tanHalfFov[0];
    m_TextureBounds[1].vMin = 0.5f - 0.5f * r_bottom / tanHalfFov[1];
    m_TextureBounds[1].vMax = 0.5f - 0.5f * r_top / tanHalfFov[1];

    m_Aspect = tanHalfFov[0] / tanHalfFov[1];
    m_Fov = 2.0f * atan(tanHalfFov[0]) * 360 / (3.14159265358979323846 * 2);

    if (!InstallApplicationManifest("manifest.vrmanifest") ||
        !SetActionManifest("action_manifest.json"))
        return;

    ParseConfigFile();
    const auto trackingOrigin = m_Playspace.mode == TrackingSpace::TrackingMode::Standing ?
        vr::TrackingUniverseStanding : vr::TrackingUniverseSeated;
    vr::VRCompositor()->SetTrackingSpace(trackingOrigin);
    if (vr::VRCompositor()->GetTrackingSpace() != trackingOrigin) {
        Game::errorMsg("OpenVR compositor did not accept the configured tracking space.");
        return;
    }
    Logger::Write(std::string("OpenVR tracking space: ") +
                  (trackingOrigin == vr::TrackingUniverseStanding ? "Standing" : "Seated"));
    m_ExplicitTiming = m_Config.explicitTiming;
    if (m_ExplicitTiming)
        vr::VRCompositor()->SetExplicitTimingMode(vr::VRCompositorTimingMode_Explicit_RuntimePerformsPostPresentHandoff);
    Logger::Write(std::string("OpenVR timing mode: ") + (m_ExplicitTiming ? "explicit" : "implicit"));
    std::error_code configTimeError;
    m_ConfigLastModified = std::filesystem::last_write_time("VR\\config.txt", configTimeError);
    m_SettingsLastModified = std::filesystem::last_write_time(VrSettings::kOverridesPath, configTimeError);
    if (configTimeError) m_SettingsLastModified = {};

    const auto d3dStart = GetTickCount64();
    while (FAILED(AcquireSoleVRBridge(&m_D3DDevice, &m_D3DVR))) {
        if (GetTickCount64() - d3dStart > 30000) {
            Game::errorMsg("Timed out waiting for the DXVK VR bridge.");
            return;
        }
        Sleep(10);
    }

    if (FAILED(m_D3DVR->GetBackBufferData(&m_VKBackBuffer))) {
        Game::errorMsg("DXVK VR back buffer initialization failed.");
        return;
    }
    m_Overlay = vr::VROverlay();
    if (!m_Overlay) {
        Game::errorMsg("OpenVR overlay initialization failed.");
        return;
    }
    const auto overlayError = m_Overlay->CreateOverlay("MenuOverlayKey", "MenuOverlay", &m_MainMenuHandle);
    if (overlayError != vr::VROverlayError_None) {
        Game::errorMsg((std::string("OpenVR menu overlay creation failed: ") +
            m_Overlay->GetOverlayErrorNameFromEnum(overlayError)).c_str());
        return;
    }
    if (m_Overlay->SetOverlayInputMethod(m_MainMenuHandle, vr::VROverlayInputMethod_Mouse) != vr::VROverlayError_None ||
        m_Overlay->SetOverlayFlag(m_MainMenuHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true) != vr::VROverlayError_None) {
        Game::errorMsg("OpenVR menu overlay configuration failed.");
        return;
    }

    const vr::HmdVector2_t mouseScaleMenu = {
        static_cast<float>(m_VKBackBuffer.m_VulkanData.m_nWidth),
        static_cast<float>(m_VKBackBuffer.m_VulkanData.m_nHeight)};
    if (m_Overlay->SetOverlayCurvature(m_MainMenuHandle, 0.15f) != vr::VROverlayError_None ||
        m_Overlay->SetOverlayMouseScale(m_MainMenuHandle, &mouseScaleMenu) != vr::VROverlayError_None) {
        Game::errorMsg("OpenVR menu overlay geometry setup failed.");
        return;
    }
    if (m_Config.experimentalHudOverlay)
        CreateExperimentalHUDOverlay();
    CreateZoomOverlay();

    if (!UpdatePosesAndActions()) {
        Game::errorMsg("Initial OpenVR pose/action update failed.");
        return;
    }

    m_IsInitialized = true;
    m_IsVREnabled = true;
    m_PrevFrameTime = std::chrono::steady_clock::now();
    Logger::Write("OpenVR initialization complete.");
}

VR::~VR()
{
    ReleaseMenuMouse();
    DetachBackBufferOverlay();
    if (m_OpenVRStarted && vr::VRCompositor()) {
        const VRSubmissionScope queue(m_D3DVR);
        vr::VRCompositor()->ClearLastSubmittedFrame();
    }
    if (m_Overlay && m_HUDHandle != vr::k_ulOverlayHandleInvalid) {
        const auto result = m_Overlay->DestroyOverlay(m_HUDHandle);
        if (result != vr::VROverlayError_None)
            Logger::Write("HUD overlay cleanup failed: " + std::to_string(result));
    }
    if (m_Overlay && m_ZoomHandle != vr::k_ulOverlayHandleInvalid) {
        const auto result = m_Overlay->DestroyOverlay(m_ZoomHandle);
        if (result != vr::VROverlayError_None)
            Logger::Write("Binocular zoom overlay cleanup failed: " + std::to_string(result));
    }
    if (m_Overlay && m_MainMenuHandle != vr::k_ulOverlayHandleInvalid) {
        const auto result = m_Overlay->DestroyOverlay(m_MainMenuHandle);
        if (result != vr::VROverlayError_None)
            Logger::Write("OpenVR overlay cleanup failed: " + std::to_string(result));
    }
    if (m_OpenVRStarted) {
        // The adopted bridge's bootstrap lease must not defer this owner's
        // shutdown until after its submitted image owners have been released.
        if (m_D3DVR) {
            m_D3DVR->WaitDeviceIdle();
            m_D3DVR->ReleaseRuntimeLease();
        }
        m_OpenVRSession.Reset();
        m_OpenVRStarted = false;
        Logger::Write("OpenVR runtime lease released");
    }
    // This VR owner has stopped all consumer calls. Other devices can still
    // retain shared-runtime leases; detachment/drain is not a compositor fence.
    m_Overlay = nullptr;
    InvalidateD3DResources();
    if (m_D3DVR) m_D3DVR->Release();
    if (m_D3DDevice) m_D3DDevice->Release();
    m_D3DVR = nullptr;
    m_D3DDevice = nullptr;
}

void VR::CreateExperimentalHUDOverlay()
{
    const auto created = m_Overlay->CreateOverlay("Portal2VR.HUD.Experimental", "Portal 2 VR Enriched HUD",
                                                   &m_HUDHandle);
    if (created != vr::VROverlayError_None) {
        Logger::Write(std::string("Experimental HUD overlay unavailable: ") +
            m_Overlay->GetOverlayErrorNameFromEnum(created));
        m_HUDHandle = vr::k_ulOverlayHandleInvalid;
        return;
    }
    vr::HmdMatrix34_t transform{};
    transform.m[0][0] = transform.m[1][1] = transform.m[2][2] = 1.0f;
    transform.m[1][3] = m_Config.hudVerticalOffsetMeters;
    transform.m[2][3] = -m_Config.hudDistanceMeters; // OpenVR HMD forward is -Z
    const auto inputError = m_Overlay->SetOverlayInputMethod(m_HUDHandle, vr::VROverlayInputMethod_None);
    const auto transformError = m_Overlay->SetOverlayTransformTrackedDeviceRelative(
        m_HUDHandle, vr::k_unTrackedDeviceIndex_Hmd, &transform);
    const auto widthError = m_Overlay->SetOverlayWidthInMeters(m_HUDHandle, m_Config.hudWidthMeters);
    if (inputError != vr::VROverlayError_None || transformError != vr::VROverlayError_None ||
        widthError != vr::VROverlayError_None) {
        Logger::Write("Experimental HUD overlay geometry/input setup failed: " +
            std::to_string(inputError) + "," + std::to_string(transformError) + "," +
            std::to_string(widthError));
        m_Overlay->DestroyOverlay(m_HUDHandle);
        m_HUDHandle = vr::k_ulOverlayHandleInvalid;
        return;
    }
    // Captions and hints stay readable over the binocular window (order 0).
    const auto sortError = m_Overlay->SetOverlaySortOrder(m_HUDHandle, 1);
    if (sortError != vr::VROverlayError_None)
        Logger::Write("Experimental HUD overlay sort order failed: " + std::to_string(sortError));
    Logger::Write("EXPERIMENTAL HUD overlay created: " +
        std::to_string(m_Config.hudWidthMeters) + "m wide, " +
        std::to_string(m_Config.hudDistanceMeters) +
        "m from HMD, vertical=" + std::to_string(m_Config.hudVerticalOffsetMeters) +
        "m; VGUI capture/alpha/subtitles require runtime verification");
}

bool VR::SetActionManifest(const char *fileName)
{
    std::error_code pathError;
    const auto path = std::filesystem::absolute(
        std::filesystem::path("VR") / "SteamVRActionManifest" / fileName, pathError).string();
    if (pathError) {
        Game::errorMsg("Unable to resolve the OpenVR action manifest path.");
        return false;
    }
    const auto manifestError = m_Input->SetActionManifestPath(path.c_str());
    if (manifestError != vr::VRInputError_None) {
        Game::errorMsg(("SetActionManifestPath failed (OpenVR error " +
            std::to_string(manifestError) + "): " + path).c_str());
        return false;
    }
    Logger::Write("OpenVR action manifest OK: " + path);

#define GET_ACTION(member, name) do { \
    const auto result = m_Input->GetActionHandle("/actions/main/in/" name, &member); \
    if (result != vr::VRInputError_None || member == vr::k_ulInvalidActionHandle) { \
        Game::errorMsg((std::string("OpenVR action handle failed: " name " (error ") + \
            std::to_string(result) + ")").c_str()); return false; \
    } \
    Logger::Write("OpenVR action handle OK: " name); \
} while (false)
    GET_ACTION(m_ActionActivateVR, "ActivateVR");
    GET_ACTION(m_ActionJump, "Jump");
    GET_ACTION(m_ActionPrimaryAttack, "PrimaryAttack");
    GET_ACTION(m_ActionReload, "Reload");
    GET_ACTION(m_ActionUse, "Use");
    GET_ACTION(m_ActionWalk, "Walk");
    GET_ACTION(m_ActionTurn, "Turn");
    GET_ACTION(m_ActionSecondaryAttack, "SecondaryAttack");
    GET_ACTION(m_ActionNextItem, "NextItem");
    GET_ACTION(m_ActionPrevItem, "PrevItem");
    GET_ACTION(m_ActionResetPosition, "ResetPosition");
    GET_ACTION(m_ActionCrouch, "Crouch");
    GET_ACTION(m_ActionFlashlight, "Flashlight");
    GET_ACTION(m_MenuSelect, "MenuSelect");
    GET_ACTION(m_MenuBack, "MenuBack");
    GET_ACTION(m_MenuUp, "MenuUp");
    GET_ACTION(m_MenuDown, "MenuDown");
    GET_ACTION(m_MenuLeft, "MenuLeft");
    GET_ACTION(m_MenuRight, "MenuRight");
    GET_ACTION(m_Spray, "Spray");
    GET_ACTION(m_Scoreboard, "Scoreboard");
    GET_ACTION(m_ShowHUD, "ShowHUD");
    GET_ACTION(m_Pause, "Pause");
    GET_ACTION(m_ActionZoom, "Zoom");
#undef GET_ACTION

    const auto setError = m_Input->GetActionSetHandle("/actions/main", &m_ActionSet);
    if (setError != vr::VRInputError_None || m_ActionSet == vr::k_ulInvalidActionSetHandle) {
        Game::errorMsg(("OpenVR main action set handle failed (error " +
            std::to_string(setError) + ").").c_str());
        return false;
    }
    m_ActiveActionSet = {};
    m_ActiveActionSet.ulActionSet = m_ActionSet;

    Logger::Write("OpenVR main action set OK");
    const auto baseError = m_Input->GetActionSetHandle("/actions/base", &m_HapticActionSet);
    const auto leftError = m_Input->GetActionHandle("/actions/base/out/vibration_left", &m_HapticLeft);
    const auto rightError = m_Input->GetActionHandle("/actions/base/out/vibration_right", &m_HapticRight);
    m_HapticOutputsAvailable = baseError == vr::VRInputError_None &&
        leftError == vr::VRInputError_None && rightError == vr::VRInputError_None &&
        m_HapticActionSet != vr::k_ulInvalidActionSetHandle &&
        m_HapticLeft != vr::k_ulInvalidActionHandle &&
        m_HapticRight != vr::k_ulInvalidActionHandle;
    if (m_HapticOutputsAvailable) {
        m_ActiveHapticActionSet.ulActionSet = m_HapticActionSet;
        Logger::Write("Optional OpenVR haptic output actions available");
    } else {
        Logger::Write("Optional OpenVR haptic actions unavailable (set/left/right errors " +
            std::to_string(baseError) + "/" + std::to_string(leftError) + "/" +
            std::to_string(rightError) + "); haptics disabled");
    }
    return true;
}

bool VR::InstallApplicationManifest(const char *fileName)
{
    auto applications = vr::VRApplications();
    if (!applications) {
        Game::errorMsg("OpenVR applications interface unavailable.");
        return false;
    }
    std::error_code pathError;
    const auto path = std::filesystem::absolute(std::filesystem::path("VR") / fileName, pathError).string();
    if (pathError) {
        Game::errorMsg("Unable to resolve the OpenVR application manifest path.");
        return false;
    }
    const auto result = applications->AddApplicationManifest(path.c_str());
    if (result != vr::VRApplicationError_None) {
        Game::errorMsg((std::string("OpenVR application manifest failed: ") +
            applications->GetApplicationsErrorNameFromEnum(result) + " (" + path + ")").c_str());
        return false;
    }
    Logger::Write("OpenVR application manifest OK: " + path);
    return true;
}

void VR::SetScreenSizeOverride(bool bState) {
    bool isOverriding = m_Game->m_VguiSurface->IsScreenSizeOverrideActive();

    if (bState && !isOverriding || !bState && isOverriding) {
        int iOldWidth, iOldHeight;
        m_Game->m_VguiSurface->GetScreenSize(iOldWidth, iOldHeight);
        m_Game->m_VguiSurface->ForceScreenSizeOverride(bState, m_RenderWidth, m_RenderHeight);
       /*int x = 0, y = 0, w = m_RenderWidth, h = m_RenderHeight;

        if (m_Game->m_ClientMode->GetViewport())
            m_Game->m_ClientMode->AdjustEngineViewport(x, y, w, h);*/

        if (bState) {
            /*IMatRenderContext* renderContext = m_Game->m_MaterialSystem->GetRenderContext();
            renderContext->Viewport(0, 0, m_RenderWidth, m_RenderHeight);
            renderContext->Release();*/
        }

        m_Game->m_VguiSurface->OnScreenSizeChanged(iOldWidth, iOldHeight);
    }
}

void VR::Update()
{
    if (!m_IsInitialized || !m_Game->m_Initialized)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now >= m_NextConfigCheck) {
        m_NextConfigCheck = now + std::chrono::seconds(1);
        std::error_code error;
        const auto modified = std::filesystem::last_write_time("VR\\config.txt", error);
        std::error_code settingsError;
        auto settingsModified = std::filesystem::last_write_time(VrSettings::kOverridesPath, settingsError);
        if (settingsError) settingsModified = {}; // a deleted overrides file is a change too
        if ((!error && modified != m_ConfigLastModified) || settingsModified != m_SettingsLastModified) {
            if (!error) m_ConfigLastModified = modified;
            m_SettingsLastModified = settingsModified;
            ParseConfigFile();
        }
        ApplyHeldObjectTuning();
    }
    PerfBeginFrame(now);
    PollHeldTrace(now);

    if (now >= m_NextRoomscaleSummary) {
        m_NextRoomscaleSummary = now + std::chrono::seconds(30);
        const auto summary = m_RoomscaleObserver.TakeSummary();
        if (m_Config.verboseDiagnostics && summary.steps)
            Logger::Write("Roomscale observe: " + std::to_string(summary.steps) +
                " physical intents, " + std::to_string(summary.distanceUnits) +
                " requested Source units (not accepted movement)");
        const auto motor = m_RoomscaleMotor.TakeSummary();
        if (m_Config.verboseDiagnostics &&
            (motor.requestedCommands || motor.manualCommands || motor.maximumErrorUnits > 0.5f))
            Logger::Write("Roomscale active experimental: " + std::to_string(motor.requestedCommands) +
                " movement requests, " + std::to_string(motor.manualCommands) +
                " manual-input commands suppressed, maxAnchorError=" +
                std::to_string(motor.maximumErrorUnits) +
                " units; feedback is Source view origin, accepted hull movement unverified");
    }

    if (m_IsVREnabled && m_D3DVR)
    {
        bool inGame = m_Game->m_EngineClient->IsInGame();

        //SetScreenSizeOverride(inGame);

        // Prevents crashing at menu
        if (!inGame)
        {
            IMatRenderContext *rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
            if (rndrContext) {
                rndrContext->SetRenderTarget(NULL);
                rndrContext->Release();
            }

            m_Game->m_CachedArmsModel = false;
        } 
        if (m_ResourceLifecycle.ObserveGameplay(inGame)) {
            // Preserve the workshop refresh, once per return from gameplay.
            m_CreatedVRTextures = false;
            Logger::Write("VR resources: gameplay ended; one compatibility refresh requested");
        }
    }

    const auto submitStart = std::chrono::steady_clock::now();
    SubmitVRTextures();
    PerfRecord(PerfStats::Metric::Submit, PerfStats::ElapsedMs(submitStart));
    const bool actionsReady = UpdatePosesAndActions();
    GetPoses();
    if (!actionsReady) {
        m_PrevFrameTime = std::chrono::steady_clock::now();
        UpdateTracking();
        ReleaseHeldActions();
        ReleaseMenuMouse();
        m_PortalShotHapticGate.Clear();
        m_GameHapticQueue.Clear();
        m_HoldTracker.Suspend(std::chrono::steady_clock::now());
        return;
    }
    if (!m_Game->m_VguiSurface->IsCursorVisible())
        ProcessViewActions();
    UpdateTracking();
    DispatchPortalShotHaptic(actionsReady);
    DispatchGameHaptics(actionsReady);

    if (m_Game->m_VguiSurface->IsCursorVisible()) {
        m_PrevFrameTime = std::chrono::steady_clock::now();
        ReleaseHeldActions();
        ProcessMenuInput();
    } else {
        ReleaseMenuMouse();
        ProcessInput();
    }
}

void VR::PerfReset()
{
    m_PerfWindow.Clear();
    m_PerfWindowStart = {};
    m_PerfLastUpdateStart = {};
    m_PerfPendingDrainMs = m_PerfLastWaitPosesMs = m_PerfLastPairMs = m_PerfLastSubmitMs = 0.0;
    m_PerfWorstFrame = {};
    m_PerfFrames = m_PerfStereoFrames = m_PerfInGameFrames = m_PerfMenuFrames = m_PerfLevelLoads = 0;
    m_PerfLongFrames = m_PerfFastFrames = m_PerfLongBusyFrames = 0;
    m_PerfHaveCounts = false;
    m_PerfCompiler.Clear();
    m_PerfFrameCompilerBusy = false;
}

void VR::PerfRecord(PerfStats::Metric metric, double milliseconds)
{
    if (!m_Config.perfLog) return;
    m_PerfWindow.Add(metric, milliseconds);
    // Parts of the current frame interval, for the worst-frame breakdown.
    if (metric == PerfStats::Metric::RenderStereo) m_PerfLastPairMs = milliseconds;
    else if (metric == PerfStats::Metric::Submit) m_PerfLastSubmitMs = milliseconds;
}

void VR::PerfNotePresent(double presentMs, double drainMs, bool fastSubmit)
{
    PerfRecord(PerfStats::Metric::Present, presentMs);
    PerfRecord(PerfStats::Metric::Drain, drainMs);
    m_PerfPendingDrainMs = drainMs;
    if (fastSubmit && m_Config.perfLog) ++m_PerfFastFrames;
}

void VR::PerfNoteCompiler(const PerfStats::CompilerCounts &counts)
{
    if (m_Config.perfLog && m_PerfCompiler.Note(counts))
        m_PerfFrameCompilerBusy = true;
}

unsigned int VR::FastSubmitSurfaces(IDirect3DSurface9 *(&surfaces)[4]) const
{
    // Menus submit the back buffer through an overlay and MSAA eyes submit
    // their resolve image; both keep the device drain.
    if (m_Config.gpuDrainEachFrame || !m_RenderedNewFrame || !m_CreatedVRTextures ||
        m_AntiAliasing != 0 || !m_D9LeftEyeSurface || !m_D9RightEyeSurface)
        return 0;
    unsigned int count = 0;
    surfaces[count++] = m_D9LeftEyeSurface;
    surfaces[count++] = m_D9RightEyeSurface;
    if (m_Config.experimentalHudOverlay && m_D9HUDSurface)
        surfaces[count++] = m_D9HUDSurface;
    if (m_ZoomRendered && m_ZoomMasked && m_D9ZoomSurface)
        surfaces[count++] = m_D9ZoomSurface;
    return count;
}

void VR::PerfBeginFrame(std::chrono::steady_clock::time_point now)
{
    if (!m_Config.perfLog) {
        if (m_PerfWindowStart != std::chrono::steady_clock::time_point{})
            PerfReset();
        return;
    }
    if (m_PerfWindowStart == std::chrono::steady_clock::time_point{}) {
        m_PerfWindowStart = now;
        m_PerfHaveCounts = PerfReadCompositorCounts(m_PerfCountsStart);
        if (m_System)
            m_PerfDisplayHz = m_System->GetFloatTrackedDeviceProperty(
                vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);
        Logger::Write("Perf log: " + Logger::SiblingPath("portal2vr_perf.log") + " every " +
                      std::to_string(m_Config.perfLogIntervalSeconds) + " s");
    } else if (m_PerfLastUpdateStart != std::chrono::steady_clock::time_point{}) {
        // The interval since the previous Update holds that Update's
        // WaitGetPoses and this frame's GPU drain; the rest is game-thread work.
        const double frameMs = PerfStats::ElapsedMs(m_PerfLastUpdateStart, now);
        const PerfStats::FrameBreakdown frame{frameMs,
            PerfStats::MainWorkMs(frameMs, m_PerfPendingDrainMs, m_PerfLastWaitPosesMs),
            m_PerfLastPairMs, m_PerfPendingDrainMs, m_PerfLastWaitPosesMs, m_PerfLastSubmitMs};
        m_PerfWindow.Add(PerfStats::Metric::Frame, frame.frame);
        m_PerfWindow.Add(PerfStats::Metric::MainWork, frame.main);
        if (frame.frame > m_PerfWorstFrame.frame) m_PerfWorstFrame = frame;
        if (PerfStats::IsLongFrame(frame.frame, m_PerfDisplayHz)) {
            ++m_PerfLongFrames;
            if (m_PerfFrameCompilerBusy) ++m_PerfLongBusyFrames;
        }
    }
    m_PerfPendingDrainMs = m_PerfLastWaitPosesMs = m_PerfLastPairMs = m_PerfLastSubmitMs = 0.0;
    m_PerfFrameCompilerBusy = false;
    m_PerfLastUpdateStart = now;

    ++m_PerfFrames;
    if (m_RenderedNewFrame) ++m_PerfStereoFrames;
    const bool inGame = m_Game->m_EngineClient->IsInGame();
    if (inGame) ++m_PerfInGameFrames;
    if (inGame && !m_PerfWasInGame) ++m_PerfLevelLoads;
    m_PerfWasInGame = inGame;
    if (m_Game->m_VguiSurface->IsCursorVisible()) ++m_PerfMenuFrames;

    if (PerfStats::ElapsedMs(m_PerfWindowStart, now) >= m_Config.perfLogIntervalSeconds * 1000.0)
        PerfFlush(now);
}

void VR::PerfReadCompositorTiming()
{
    if (!m_Config.perfLog || !vr::VRCompositor()) return;
    // One frame ago is the newest frame whose GPU times are complete.
    vr::Compositor_FrameTiming timing{};
    timing.m_nSize = sizeof(timing);
    if (!vr::VRCompositor()->GetFrameTiming(&timing, 1) ||
        timing.m_nFrameIndex == m_PerfLastTimingFrameIndex)
        return;
    m_PerfLastTimingFrameIndex = timing.m_nFrameIndex;
    m_PerfWindow.Add(PerfStats::Metric::GpuApp, timing.m_flPreSubmitGpuMs + timing.m_flPostSubmitGpuMs);
    m_PerfWindow.Add(PerfStats::Metric::GpuTotal, timing.m_flTotalRenderGpuMs);
    m_PerfWindow.Add(PerfStats::Metric::CompositorIdle, timing.m_flCompositorIdleCpuMs);
}

bool VR::PerfReadCompositorCounts(PerfStats::CompositorCounts &counts)
{
    if (!vr::VRCompositor()) return false;
    vr::Compositor_CumulativeStats stats{};
    vr::VRCompositor()->GetCumulativeStats(&stats, sizeof(stats));
    counts = {stats.m_nNumFramePresents, stats.m_nNumDroppedFrames,
              stats.m_nNumReprojectedFrames, stats.m_nNumTimedOut};
    return true;
}

void VR::PerfFlush(std::chrono::steady_clock::time_point now)
{
    const double seconds = PerfStats::ElapsedMs(m_PerfWindowStart, now) / 1000.0;
    if (m_System) // the refresh rate can change between sessions of the runtime
        m_PerfDisplayHz = m_System->GetFloatTrackedDeviceProperty(
            vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);
    PerfStats::CompositorCounts countsNow{};
    const bool haveCounts = PerfReadCompositorCounts(countsNow);
    const PerfStats::CompositorCounts delta = haveCounts && m_PerfHaveCounts
        ? PerfStats::CountsSince(m_PerfCountsStart, countsNow) : PerfStats::CompositorCounts{};

    char header[352];
    std::snprintf(header, sizeof(header),
        "win=%.1fs frames=%u fps=%.1f stereo=%u fast=%u ingame=%u menu=%u loads=%u long=%u eye=%ux%u hz=%.0f aa=%u"
        " pos=%.0f,%.0f,%.0f presents=%u reproj=%u dropped=%u timeout=%u",
        seconds, m_PerfFrames, seconds > 0.0 ? m_PerfFrames / seconds : 0.0, m_PerfStereoFrames,
        m_PerfFastFrames, m_PerfInGameFrames, m_PerfMenuFrames, m_PerfLevelLoads, m_PerfLongFrames, m_RenderWidth,
        m_RenderHeight, m_PerfDisplayHz, m_AntiAliasing, m_SetupOrigin.x, m_SetupOrigin.y,
        m_SetupOrigin.z, delta.presents, delta.reprojected, delta.dropped, delta.timedOut);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    const std::string addressSpace = GlobalMemoryStatusEx(&memory)
        ? PerfStats::FormatAddressSpace(memory.ullTotalVirtual, memory.ullAvailVirtual) : std::string();
    const std::string abSide = m_Config.perfAbSeconds > 0 ? (m_PerfAb.SideB() ? " ab=1" : " ab=0") : "";
    Logger::WriteSibling("portal2vr_perf.log", header + abSide + m_PerfCompiler.Format(m_PerfLongBusyFrames) +
                                                   addressSpace + PerfStats::FormatMetrics(m_PerfWindow) +
                                                   PerfStats::FormatBreakdown(m_PerfWorstFrame));

    m_PerfWindow.Clear();
    m_PerfWindowStart = now;
    m_PerfWorstFrame = {};
    m_PerfFrames = m_PerfStereoFrames = m_PerfInGameFrames = m_PerfMenuFrames = m_PerfLevelLoads = 0;
    m_PerfLongFrames = m_PerfFastFrames = m_PerfLongBusyFrames = 0;
    m_PerfCompiler.StartWindow();
    m_PerfCountsStart = countsNow;
    m_PerfHaveCounts = haveCounts;
    PerfAdvanceAb();
}

void VR::PerfAdvanceAb()
{
    if (m_Config.perfAbSeconds <= 0) return;
    const int windowsPerSide = (std::max)(1, m_Config.perfAbSeconds / m_Config.perfLogIntervalSeconds);
    if (!m_PerfAb.Next(windowsPerSide)) return;
    const std::string &commands = m_PerfAb.SideB() ? m_Config.perfAbCommandB : m_Config.perfAbCommandA;
    if (!commands.empty()) m_Game->ClientCmd_Unrestricted(commands.c_str());
}

void VR::SubmitFrameTiming()
{
    if (!m_FrameTimingPending) return;
    m_FrameTimingPending = false;
    // This performs a vkQueueSubmit, so it needs the queue just like Submit.
    const VRSubmissionScope queue(m_D3DVR);
    const auto error = vr::VRCompositor()->SubmitExplicitTimingData();
    if (error != vr::VRCompositorError_None && error != m_LastFrameTimingError)
        Logger::Write("SubmitExplicitTimingData failed: " + std::to_string(error));
    m_LastFrameTimingError = error;
}

bool VR::DetachBackBufferOverlay(bool hide)
{
    m_RenderConditions.SetVerbose(m_Config.verboseDiagnostics, GetTickCount64());
    if (!m_Overlay || m_MainMenuHandle == vr::k_ulOverlayHandleInvalid) return true;
    const VRSubmissionScope queue(m_D3DVR);
    // Capture rotation clears the old texture while retaining visible menu
    // heading/placement. Actual invalidation also hides the overlay.
    vr::EVROverlayError error = vr::VROverlayError_None;
    DetachVRMenuTexture(hide, [&] { m_Overlay->HideOverlay(m_MainMenuHandle); }, [&] {
        error = m_Overlay->ClearOverlayTexture(m_MainMenuHandle);
        return error == vr::VROverlayError_None;
    });
    if (m_RenderConditions.Observe(RenderCondition::MenuDetach, error != vr::VROverlayError_None,
            {static_cast<std::uint64_t>(error)}) != RenderConditionChange::None)
        Logger::Write("VR menu texture detach: error=" + std::to_string(error));
    return error == vr::VROverlayError_None;
}

bool VR::InvalidateD3DResources(bool deviceReset)
{
    m_RenderConditions.SetVerbose(m_Config.verboseDiagnostics, GetTickCount64());
    if (deviceReset) {
        m_ResourceLifecycle.OnDeviceReset();
        m_RenderTargetRetry.ResetForDevice();
        m_RenderConditions.NewGeneration();
        Logger::Write("VR resources invalidated for device lifecycle " +
            std::to_string(m_ResourceLifecycle.DeviceGeneration()));
    }
    m_CreatedVRTextures = false;
    m_RenderedNewFrame = false;
    m_RenderedHud = false;
    m_HUDBoundsReady = false;
    m_ZoomRendered = m_ZoomMasked = false;
    return RetireVRResources([&] {
        const bool menuDetached = DetachBackBufferOverlay();
        bool hudDetached = true, zoomDetached = true;
        if (m_Overlay && m_ZoomHandle != vr::k_ulOverlayHandleInvalid) {
            const VRSubmissionScope queue(m_D3DVR);
            m_Overlay->HideOverlay(m_ZoomHandle);
            const auto zoomError = m_Overlay->ClearOverlayTexture(m_ZoomHandle);
            zoomDetached = zoomError == vr::VROverlayError_None;
            if (m_RenderConditions.Observe(RenderCondition::ZoomDetach, !zoomDetached,
                    {static_cast<std::uint64_t>(zoomError)}) != RenderConditionChange::None)
                Logger::Write("Binocular zoom texture detach: error=" + std::to_string(zoomError));
        }
        if (m_Overlay && m_HUDHandle != vr::k_ulOverlayHandleInvalid) {
            const VRSubmissionScope queue(m_D3DVR);
            m_Overlay->HideOverlay(m_HUDHandle);
            const auto hudError = m_Overlay->ClearOverlayTexture(m_HUDHandle);
            hudDetached = hudError == vr::VROverlayError_None;
            if (m_RenderConditions.Observe(RenderCondition::HudDetach, !hudDetached,
                    {static_cast<std::uint64_t>(hudError)}) != RenderConditionChange::None)
                Logger::Write("VR HUD texture detach: error=" + std::to_string(hudError));
        }
        if (m_OpenVRStarted && vr::VRCompositor()) {
            const VRSubmissionScope queue(m_D3DVR);
            vr::VRCompositor()->ClearLastSubmittedFrame();
        }
        return menuDetached && hudDetached && zoomDetached;
    }, [&] {
        const HRESULT result = m_D3DVR ? m_D3DVR->WaitDeviceIdle() : D3D_OK;
        if (m_RenderConditions.Observe(RenderCondition::ResourceDrain, FAILED(result),
                {static_cast<std::uint32_t>(result)}) != RenderConditionChange::None)
            Logger::Write("VR resource queue drain: hr=" + HRESULTText(result));
        return SUCCEEDED(result);
    }, [&] {
    auto releaseSurface = [](IDirect3DSurface9*& surface) {
        if (surface) {
            surface->Release();
            surface = nullptr;
        }
    };
    releaseSurface(m_D9LeftEyeSurface);
    releaseSurface(m_D9RightEyeSurface);
    releaseSurface(m_D9HUDSurface);
    releaseSurface(m_D9BlankSurface);
    releaseSurface(m_D9ZoomSurface);
    m_VKLeftEye.m_VRTexture.handle = nullptr;
    m_VKRightEye.m_VRTexture.handle = nullptr;
    m_VKHUD.m_VRTexture.handle = nullptr;
    m_VKBlankTexture.m_VRTexture.handle = nullptr;
    m_VKZoom.m_VRTexture.handle = nullptr;
    m_VKBackBuffer.m_VRTexture.handle = nullptr;
    // Named Source textures are borrowed material-system objects. Only the
    // GetSurfaceLevel references above belong to the mod.
    m_LeftEyeTexture = m_RightEyeTexture = m_HUDTexture = m_BlankTexture = m_ZoomTexture = nullptr;
    if (deviceReset && m_D3DVR) m_D3DVR->DiscardBackBufferAfterDrain();
    });
}

bool VR::RefreshBackBuffer(SharedTextureHolder& holder)
{
    m_RenderConditions.SetVerbose(m_Config.verboseDiagnostics, GetTickCount64());
    holder = {};
    const bool bridgeAvailable = m_D3DVR != nullptr;
    if (m_RenderConditions.Observe(RenderCondition::Bridge, !bridgeAvailable) != RenderConditionChange::None)
        Logger::Write(bridgeAvailable ? "DXVK VR bridge recovered" : "VR textures unavailable: no DXVK VR bridge");
    if (!bridgeAvailable) return false;
    const HRESULT result = m_D3DVR->GetBackBufferData(&holder);
    const auto& data = holder.m_VulkanData;
    if (m_RenderConditions.Observe(RenderCondition::BackBufferCapture, FAILED(result),
            {static_cast<std::uint32_t>(result)}) != RenderConditionChange::None)
        Logger::Write("VR back buffer descriptor: hr=" + HRESULTText(result));
    if (m_RenderConditions.Observe(RenderCondition::BackBufferImage, !data.m_nImage,
            {data.m_nImage != 0}) != RenderConditionChange::None)
        Logger::Write(data.m_nImage ? "VR back buffer image recovered" : "VR back buffer image unavailable");
    const bool dimensionsValid = data.m_nWidth && data.m_nHeight;
    if (m_RenderConditions.Observe(RenderCondition::BackBufferDimensions, !dimensionsValid,
            {data.m_nWidth, data.m_nHeight}) != RenderConditionChange::None)
        Logger::Write("VR back buffer dimensions: " + std::to_string(data.m_nWidth) + "x" + std::to_string(data.m_nHeight));
    const bool deviceDataValid = data.m_pDevice && data.m_pPhysicalDevice && data.m_pInstance &&
        data.m_pQueue && data.m_nSampleCount;
    if (m_RenderConditions.Observe(RenderCondition::BackBufferDevice, !deviceDataValid,
            {data.m_pDevice != nullptr, data.m_pPhysicalDevice != nullptr, data.m_pInstance != nullptr,
             data.m_pQueue != nullptr, data.m_nSampleCount}) != RenderConditionChange::None)
        Logger::Write(deviceDataValid ? "VR back buffer device descriptor recovered" : "VR back buffer device descriptor incomplete");
    return SUCCEEDED(result) && TextureSubmissionReady(holder);
}

void VR::CreateVRTextures()
{
    const auto now = GetTickCount64();
    m_RenderConditions.SetVerbose(m_Config.verboseDiagnostics, now);
    if (m_CreatedVRTextures) return;
    if (m_RenderConditions.Observe(RenderCondition::RetryBudget, m_RenderTargetRetry.Exhausted()) != RenderConditionChange::None)
        Logger::Write(m_RenderTargetRetry.Exhausted() ? "VR render target retry budget exhausted; reset/restart required" :
            "VR render target retry budget recovered");
    SharedTextureHolder currentBackBuffer;
    if (!RefreshBackBuffer(currentBackBuffer) || !m_RenderTargetRetry.CanAttempt(now, true))
        return;
    // Preserve retry history when replacing partial resources from an attempt.
    const auto retryState = m_RenderTargetRetry;
    if (!InvalidateD3DResources(false)) {
        m_RenderTargetRetry.RecordFailure(now);
        return;
    }
    m_RenderTargetRetry = retryState;

    int windowWidth = 0, windowHeight = 0;

    IMatRenderContext* rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
    if (m_RenderConditions.Observe(RenderCondition::RenderContext, rndrContext == nullptr) != RenderConditionChange::None)
        Logger::Write(rndrContext ? "VR material render context recovered" : "VR textures unavailable: material render context missing");
    if (rndrContext) {
        rndrContext->GetWindowSize(windowWidth, windowHeight);
        rndrContext->Release();
    } else {
        m_RenderTargetRetry.RecordFailure(now);
        return;
    }

    if (m_RenderTargetDiagnostics.Allocation(m_RenderWidth, m_RenderHeight, m_Config.verboseDiagnostics))
        Logger::Write("Creating VR render targets: " + std::to_string(m_RenderWidth) +
                      "x" + std::to_string(m_RenderHeight));
    if (windowWidth > 0 && windowHeight > 0 && m_RenderTargetDiagnostics.Projection(
            windowWidth, windowHeight, m_RenderWidth, m_RenderHeight, m_Aspect, m_Fov,
            m_Config.verboseDiagnostics))
        Logger::Write("Projection geometry: Source window=" +
            std::to_string(windowWidth) + "x" + std::to_string(windowHeight) +
            " aspect=" + std::to_string(static_cast<float>(windowWidth) / windowHeight) +
            " VR eye=" + std::to_string(m_RenderWidth) + "x" +
            std::to_string(m_RenderHeight) + " projectionAspect=" +
            std::to_string(m_Aspect) + " horizontalFov=" + std::to_string(m_Fov) +
            "; native viewmodel aspect requires in-game verification");

    m_Game->m_MaterialSystem->isGameRunning = false;
    m_Game->m_MaterialSystem->BeginRenderTargetAllocation();
    m_Game->m_MaterialSystem->isGameRunning = true;

    m_CreatingTextureID = Texture_LeftEye;
    m_LeftEyeTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("leftEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);
    
    m_CreatingTextureID = Texture_RightEye;
    m_RightEyeTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("rightEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);

    m_CreatingTextureID = Texture_HUD;
    const ImageFormat hudFormat = m_Config.experimentalHudOverlay ?
        IMAGE_FORMAT_BGRA8888 : m_Game->m_MaterialSystem->GetBackBufferFormat();
    m_HUDTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("vrHUD", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, hudFormat, MATERIAL_RT_DEPTH_SHARED, TEXTUREFLAGS_NOMIP);
    
    m_CreatingTextureID = Texture_Blank;
    m_BlankTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("blankTexture", 512, 512, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SHARED, TEXTUREFLAGS_NOMIP);

    // Optional: stereo never waits on it. Alpha carries the lens outline.
    m_CreatingTextureID = Texture_Zoom;
    m_ZoomTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("vrZoom", BinocularZoom::kTextureWidth, BinocularZoom::kTextureHeight, RT_SIZE_NO_CHANGE, IMAGE_FORMAT_BGRA8888, MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);

    m_CreatingTextureID = Texture_None;

    m_Game->m_MaterialSystem->EndRenderTargetAllocation();

    if (m_Config.experimentalHudOverlay && m_Overlay &&
        m_HUDHandle != vr::k_ulOverlayHandleInvalid) {
        const auto crop = HudCapture::WindowTextureCrop(
            static_cast<int>(m_RenderWidth), static_cast<int>(m_RenderHeight),
            windowWidth, windowHeight);
        if (!crop) {
            Logger::Write("Experimental HUD: cannot map Source window " +
                std::to_string(windowWidth) + "x" + std::to_string(windowHeight) +
                " into HUD target " + std::to_string(m_RenderWidth) + "x" +
                std::to_string(m_RenderHeight) + "; overlay stays hidden");
        } else {
            const vr::VRTextureBounds_t bounds{0.0f, 0.0f, crop->uMax, crop->vMax};
            const auto boundsError = m_Overlay->SetOverlayTextureBounds(m_HUDHandle, &bounds);
            m_HUDBoundsReady = boundsError == vr::VROverlayError_None;
            if (m_RenderTargetDiagnostics.HudBounds(windowWidth, windowHeight, m_RenderWidth,
                    m_RenderHeight, crop->uMax, crop->vMax, boundsError, m_Config.verboseDiagnostics))
                Logger::Write("Experimental HUD texture bounds: window=" +
                std::to_string(windowWidth) + "x" + std::to_string(windowHeight) +
                " target=" + std::to_string(m_RenderWidth) + "x" +
                std::to_string(m_RenderHeight) + " uMax=" + std::to_string(crop->uMax) +
                " vMax=" + std::to_string(crop->vMax) +
                " clamped=" + std::to_string(windowWidth > static_cast<int>(m_RenderWidth) ||
                    windowHeight > static_cast<int>(m_RenderHeight)) +
                " error=" + std::to_string(boundsError));
        }
    }

    if (m_Config.experimentalHudOverlay &&
        (!m_HUDTexture || !m_VKHUD.m_VRTexture.handle))
        Logger::Write("Experimental HUD: render target or Vulkan share unavailable; overlay stays hidden");
    Logger::Write("Binocular zoom target " + std::to_string(BinocularZoom::kTextureWidth) + "x" +
            std::to_string(BinocularZoom::kTextureHeight) + ": texture=" + std::to_string(m_ZoomTexture != nullptr) +
            " surface=" + std::to_string(m_D9ZoomSurface != nullptr) +
            " shared=" + std::to_string(m_VKZoom.m_VRTexture.handle != nullptr) +
            (m_ZoomTexture && m_VKZoom.m_VRTexture.handle ? "" : "; zoom unavailable"));

    const RenderTargetReadiness readiness{
        {m_LeftEyeTexture != nullptr, m_D9LeftEyeSurface != nullptr,
         m_VKLeftEye.m_VRTexture.handle != nullptr},
        {m_RightEyeTexture != nullptr, m_D9RightEyeSurface != nullptr,
         m_VKRightEye.m_VRTexture.handle != nullptr},
        {m_BlankTexture != nullptr, m_D9BlankSurface != nullptr,
         m_VKBlankTexture.m_VRTexture.handle != nullptr}};
    m_CreatedVRTextures = readiness.Ready() && TextureSubmissionReady(m_VKLeftEye) &&
        TextureSubmissionReady(m_VKRightEye) && TextureSubmissionReady(m_VKBlankTexture);
    if (m_RenderConditions.Observe(RenderCondition::Allocation, !m_CreatedVRTextures,
            {readiness.left.Ready(), readiness.right.Ready(), readiness.blank.Ready()}, true) != RenderConditionChange::None)
        Logger::Write("VR render target readiness: left=" + std::to_string(readiness.left.Ready()) +
            " right=" + std::to_string(readiness.right.Ready()) + " blank=" + std::to_string(readiness.blank.Ready()));
    if (!m_CreatedVRTextures) {
        m_RenderTargetRetry.RecordFailure(now);
        Logger::Write("VR render target creation failed: left=" +
            std::to_string(readiness.left.Ready()) + " right=" +
            std::to_string(readiness.right.Ready()) + " blank=" +
            std::to_string(readiness.blank.Ready()) +
            (m_RenderTargetRetry.Exhausted() ?
                "; retry budget exhausted; stereo disabled until device reset/restart" :
                "; stereo bypassed; retry delayed at least one second"));
        const auto failedRetry = m_RenderTargetRetry;
        InvalidateD3DResources(false);
        m_RenderTargetRetry = failedRetry;
    } else {
        // Newly allocated texture initializers must complete before the first
        // Vulkan submission. Stable frames retain the existing Present wait.
        const HRESULT result = m_D3DVR->WaitDeviceIdle();
        if (m_RenderConditions.Observe(RenderCondition::ResourceDrain, FAILED(result),
                {static_cast<std::uint32_t>(result)}) != RenderConditionChange::None)
            Logger::Write("VR allocation queue drain: hr=" + HRESULTText(result));
        m_CreatedVRTextures = CompleteVRTextureCreation(m_RenderTargetRetry, now, SUCCEEDED(result));
    }
}

void VR::SubmitVRTextures()
{
    // Menu frames have no stereo RenderView to start the GPU clock.
    SubmitFrameTiming();
    // The bridge pins the image captured before Present rotates backbuffers.
    // Reacquire its descriptor every submission; never reuse a reset-era handle.
    if (!RefreshBackBuffer(m_VKBackBuffer)) {
        DetachBackBufferOverlay();
        if (m_Overlay) {
            m_Overlay->HideOverlay(m_MainMenuHandle);
            if (m_HUDHandle != vr::k_ulOverlayHandleInvalid)
                m_Overlay->HideOverlay(m_HUDHandle);
            if (m_ZoomHandle != vr::k_ulOverlayHandleInvalid)
                m_Overlay->HideOverlay(m_ZoomHandle);
        }
        m_RenderedNewFrame = false;
        m_RenderedHud = false;
        m_ZoomRendered = m_ZoomMasked = false;
        ReleaseMenuMouse();
        return;
    }
    SubmitExperimentalHUDOverlay();
    SubmitZoomOverlay();
    m_RenderedHud = false;
    if (!m_RenderedNewFrame)
    {
        if (!m_CreatedVRTextures)
            CreateVRTextures();

        if (!m_CreatedVRTextures || !m_BlankTexture || !TextureSubmissionReady(m_VKBlankTexture))
            return;
        if (!RefreshBackBuffer(m_VKBackBuffer)) {
            m_Overlay->HideOverlay(m_MainMenuHandle);
            ReleaseMenuMouse();
            return;
        }

        const bool inGame = m_Game->m_EngineClient->IsInGame();
        int windowWidth = 0, windowHeight = 0;
        IMatRenderContext *context = m_Game->m_MaterialSystem->GetRenderContext();
        if (context) {
            context->GetWindowSize(windowWidth, windowHeight);
            context->Release();
        }
        const auto &backBuffer = m_VKBackBuffer.m_VulkanData;
        const auto maxDimension = static_cast<uint32_t>((std::numeric_limits<int>::max)());
        const auto mapping = backBuffer.m_nWidth <= maxDimension && backBuffer.m_nHeight <= maxDimension ?
            HudCapture::MenuTextureMapping(static_cast<int>(backBuffer.m_nWidth),
                static_cast<int>(backBuffer.m_nHeight), windowWidth, windowHeight, inGame) : std::nullopt;
        if (!mapping) {
            m_Overlay->HideOverlay(m_MainMenuHandle);
            m_MenuOverlayPlacement.Invalidate();
            ReleaseMenuMouse();
            return;
        }
        // Translation follows physical HMD; heading locks until reopening.
        const VRSubmissionScope queue(m_D3DVR);
        const bool menuPositioned = RepositionOverlays();
        const vr::VRTextureBounds_t bounds{0, 0, mapping->bounds.uMax, mapping->bounds.vMax};
        const auto aspectError = m_Overlay->SetOverlayTexelAspect(m_MainMenuHandle, mapping->texelAspect);
        const auto boundsError = vr::VROverlay()->SetOverlayTextureBounds(m_MainMenuHandle, &bounds);
        const auto textureError = vr::VROverlay()->SetOverlayTexture(m_MainMenuHandle, &m_VKBackBuffer.m_VRTexture);
        const vr::HmdVector2_t mouseScale{static_cast<float>(backBuffer.m_nWidth),
                                        static_cast<float>(backBuffer.m_nHeight)};
        const auto mouseError = m_Overlay->SetOverlayMouseScale(m_MainMenuHandle, &mouseScale);
        const bool menuReady = menuPositioned && aspectError == vr::VROverlayError_None &&
            boundsError == vr::VROverlayError_None && textureError == vr::VROverlayError_None &&
            mouseError == vr::VROverlayError_None;
        const auto showError = menuReady ?
            vr::VROverlay()->ShowOverlay(m_MainMenuHandle) :
            vr::VROverlay()->HideOverlay(m_MainMenuHandle);

        //if (!m_Game->m_EngineClient->IsInGame())
        {
            const auto leftError = vr::VRCompositor()->Submit(vr::Eye_Left, &m_VKBlankTexture.m_VRTexture, NULL, vr::Submit_Default);
            const auto rightError = vr::VRCompositor()->Submit(vr::Eye_Right, &m_VKBlankTexture.m_VRTexture, NULL, vr::Submit_Default);
            if (m_RenderConditions.Observe(RenderCondition::MenuSubmit,
                    !menuReady || showError != vr::VROverlayError_None || leftError != vr::VRCompositorError_None || rightError != vr::VRCompositorError_None,
                    {static_cast<std::uint64_t>(aspectError), static_cast<std::uint64_t>(boundsError),
                     static_cast<std::uint64_t>(textureError), static_cast<std::uint64_t>(mouseError),
                     static_cast<std::uint64_t>(showError), static_cast<std::uint64_t>(leftError),
                     static_cast<std::uint64_t>(rightError), menuPositioned}, true) != RenderConditionChange::None)
                Logger::Write("VR menu submit: inGame=" + std::to_string(inGame) +
                    " backBuffer=" + std::to_string(m_VKBackBuffer.m_VRTexture.handle != nullptr) +
                    " blank=" + std::to_string(m_VKBlankTexture.m_VRTexture.handle != nullptr) +
                    " overlay=" + std::to_string(aspectError) + "," + std::to_string(boundsError) +
                    "," + std::to_string(textureError) + "," + std::to_string(showError) +
                    " compositor=" + std::to_string(leftError) + "," + std::to_string(rightError));
        }

        return;
    }
    if (!m_CreatedVRTextures || !TextureSubmissionReady(m_VKLeftEye) || !TextureSubmissionReady(m_VKRightEye)) {
        m_RenderedNewFrame = m_RenderedHud = false;
        return;
    }
    const VRSubmissionScope queue(m_D3DVR);
    vr::VROverlay()->HideOverlay(m_MainMenuHandle);
    m_MenuOverlayPlacement.Invalidate();

    const auto leftError = vr::VRCompositor()->Submit(vr::Eye_Left, &m_VKLeftEye.m_VRTexture, &(m_TextureBounds)[0], vr::Submit_Default);
    const auto rightError = vr::VRCompositor()->Submit(vr::Eye_Right, &m_VKRightEye.m_VRTexture, &(m_TextureBounds)[1], vr::Submit_Default);
    if (m_RenderConditions.Observe(RenderCondition::StereoSubmit,
            leftError != vr::VRCompositorError_None || rightError != vr::VRCompositorError_None,
            {static_cast<std::uint64_t>(leftError), static_cast<std::uint64_t>(rightError)}, true) != RenderConditionChange::None)
        Logger::Write("VR stereo submit: left=" + std::to_string(m_VKLeftEye.m_VRTexture.handle != nullptr) +
            " right=" + std::to_string(m_VKRightEye.m_VRTexture.handle != nullptr) +
            " compositor=" + std::to_string(leftError) + "," + std::to_string(rightError));

    m_RenderedNewFrame = false;
}

void VR::SubmitExperimentalHUDOverlay()
{
    m_RenderConditions.SetVerbose(m_Config.verboseDiagnostics, GetTickCount64());
    if (!m_Overlay || m_HUDHandle == vr::k_ulOverlayHandleInvalid)
        return;
    if (m_RenderedHud && !m_HUDCaptureLogged) {
        Logger::Write("Experimental HUD: first capture target ready for overlay submission (painted contents unverified)");
        m_HUDCaptureLogged = true;
    }
    if (m_RenderedHud)
        m_HUDMissingCaptureFrames = 0;
    else if (m_RenderedNewFrame && m_CreatedVRTextures &&
             m_Game->m_Hooks->m_HudCaptureHooksReady &&
             ++m_HUDMissingCaptureFrames == 120)
        Logger::Write("Experimental HUD: no HUD capture target after 120 stereo frames; overlay remains hidden");
    const bool canShow = m_Config.experimentalHudOverlay && m_HUDBoundsReady &&
        m_RenderedNewFrame && m_RenderedHud && m_CreatedVRTextures &&
        m_HmdPose.valid && !m_Game->m_VguiSurface->IsCursorVisible() && m_HUDTexture &&
        TextureSubmissionReady(m_VKHUD);
    if (!canShow || !PlaceExperimentalHUDOverlay()) {
        if (m_Overlay->IsOverlayVisible(m_HUDHandle))
            m_Overlay->HideOverlay(m_HUDHandle);
        // Reappear straight ahead after menus and loads.
        m_CaptionFollow.Reset();
        return;
    }
    const VRSubmissionScope queue(m_D3DVR);
    const auto textureError = m_Overlay->SetOverlayTexture(m_HUDHandle, &m_VKHUD.m_VRTexture);
    const auto showError = textureError == vr::VROverlayError_None ?
        m_Overlay->ShowOverlay(m_HUDHandle) : textureError;
    const bool submissionFailed = textureError != vr::VROverlayError_None || showError != vr::VROverlayError_None;
    if (m_RenderConditions.Observe(RenderCondition::HudSubmit, submissionFailed,
            {static_cast<std::uint64_t>(textureError), static_cast<std::uint64_t>(showError)}, true) != RenderConditionChange::None)
        Logger::Write("Experimental HUD overlay submission: texture=" + std::to_string(textureError) +
            " visibility=" + std::to_string(showError) + " (pixels/alpha/subtitles unverified)");
    if (submissionFailed) {
        m_Overlay->HideOverlay(m_HUDHandle);
    }
}

bool VR::PlaceExperimentalHUDOverlay()
{
    if (!m_Config.hudLazyFollow) {
        if (m_HUDHeadLocked)
            return true;
        vr::HmdMatrix34_t transform{};
        transform.m[0][0] = transform.m[1][1] = transform.m[2][2] = 1.0f;
        transform.m[1][3] = m_Config.hudVerticalOffsetMeters;
        transform.m[2][3] = -m_Config.hudDistanceMeters;
        m_HUDHeadLocked = m_Overlay->SetOverlayTransformTrackedDeviceRelative(
            m_HUDHandle, vr::k_unTrackedDeviceIndex_Hmd, &transform) == vr::VROverlayError_None;
        m_CaptionFollow.Reset();
        m_HUDFollowPose.reset();
        return m_HUDHeadLocked;
    }

    const auto &hmd = m_Poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking.m;
    const auto now = std::chrono::steady_clock::now();
    const float dt = (std::min)(std::chrono::duration<float>(now - m_HUDFollowLast).count(), 0.1f);
    m_HUDFollowLast = now;
    const auto pose = m_CaptionFollow.Update(hmd, dt, {m_Config.hudFollowDeadzoneDegrees,
        m_Config.hudFollowSpeed, m_Config.hudDistanceMeters, m_Config.hudVerticalOffsetMeters});
    if (!pose)
        return false; // no usable heading yet, or a non-finite pose
    vr::HmdMatrix34_t transform{};
    std::memcpy(transform.m, pose->m, sizeof(transform.m));
    const auto error = m_Overlay->SetOverlayTransformAbsolute(
        m_HUDHandle, vr::VRCompositor()->GetTrackingSpace(), &transform);
    if (error != vr::VROverlayError_None) {
        if (m_RenderDiagnostics.First(RenderDiagnosticEvent::HudFollowFailed))
            Logger::Write("Caption overlay lazy follow: transform error " + std::to_string(error));
        return false;
    }
    m_HUDHeadLocked = false;
    m_HUDFollowPose = pose;
    return true;
}

std::optional<InstructorHints::Glyph> VR::HintGlyphForBinding(const char *binding)
{
    using InstructorHints::Action;
    const Action action = InstructorHints::ActionForBinding(binding);
    vr::VRActionHandle_t handle = vr::k_ulInvalidActionHandle;
    switch (action) {
    case Action::Jump: handle = m_ActionJump; break;
    case Action::Use: handle = m_ActionUse; break;
    case Action::Crouch: handle = m_ActionCrouch; break;
    case Action::PrimaryAttack: handle = m_ActionPrimaryAttack; break;
    case Action::SecondaryAttack: handle = m_ActionSecondaryAttack; break;
    case Action::Reload: handle = m_ActionReload; break;
    case Action::Zoom: handle = m_ActionZoom; break;
    default: return std::nullopt;
    }
    if (!m_IsVREnabled || !m_Input || handle == vr::k_ulInvalidActionHandle)
        return std::nullopt;

    // Bindings can change in the SteamVR dashboard while the game runs.
    auto &cache = m_HintGlyphs[static_cast<std::size_t>(action)];
    const auto now = std::chrono::steady_clock::now();
    if (cache.checked && now - cache.at < std::chrono::seconds(3))
        return cache.glyph;
    vr::InputBindingInfo_t bindings[4]{};
    uint32_t count = 0;
    const auto error = m_Input->GetActionBindingInfo(handle, bindings, sizeof(bindings[0]),
        static_cast<uint32_t>(std::size(bindings)), &count);
    std::optional<InstructorHints::Glyph> glyph;
    std::string source = "none";
    if (error == vr::VRInputError_None) {
        for (uint32_t i = 0; i < count && i < std::size(bindings) && !glyph; ++i) {
            glyph = InstructorHints::GlyphForInput(bindings[i].rchDevicePathName, bindings[i].rchInputPathName,
                bindings[i].rchSlotName);
            source = std::string(bindings[i].rchDevicePathName) + bindings[i].rchInputPathName + " " +
                bindings[i].rchModeName + "/" + bindings[i].rchSlotName;
        }
    }
    if (!cache.checked || cache.glyph != glyph)
        Logger::Write(std::string("Instructor hint: ") + binding + " -> " + source + " (error " +
            std::to_string(error) + ", " + std::to_string(count) + " binding(s)); " +
            (glyph ? std::string("VR icon ") + InstructorHints::GlyphName(*glyph) : std::string("native key icon")));
    cache = {true, now, glyph};
    return glyph;
}

VR::HintProjection VR::ProjectHintToHud(const Vector &world, int windowWidth, int windowHeight)
{
    const auto &hmdPose = m_Poses[vr::k_unTrackedDeviceIndex_Hmd];
    if (!m_IsVREnabled || !m_TrackingOutputValid || !m_HUDBoundsReady || !hmdPose.bPoseIsValid)
        return {};
    const auto &hmd = hmdPose.mDeviceToAbsoluteTracking.m;
    float overlay[3][4];
    if (m_Config.hudLazyFollow) {
        if (!m_HUDFollowPose)
            return {};
        std::memcpy(overlay, m_HUDFollowPose->m, sizeof(overlay));
    } else {
        // Head-locked at (0, HUDVerticalOffsetMeters, -HUDDistanceMeters) in HMD space.
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column)
                overlay[row][column] = hmd[row][column];
            overlay[row][3] = hmd[row][3] + hmd[row][1] * m_Config.hudVerticalOffsetMeters -
                hmd[row][2] * m_Config.hudDistanceMeters;
        }
    }
    const Vector origin = GetViewOrigin(m_SetupOrigin);
    const InstructorHints::HeadFrame head{{origin.x, origin.y, origin.z},
        {m_HmdForward.x, m_HmdForward.y, m_HmdForward.z}, {m_HmdRight.x, m_HmdRight.y, m_HmdRight.z},
        {m_HmdUp.x, m_HmdUp.y, m_HmdUp.z}, m_VRScale};
    return {true, InstructorHints::ProjectToOverlay({world.x, world.y, world.z}, head, hmd, overlay,
        m_Config.hudWidthMeters, windowWidth, windowHeight)};
}

void VR::BoostHUDAlpha(IDirect3DDevice9 *device)
{
    float remaining = m_Config.hudCaptionOpacityBoost;
    if (!device || !m_Config.experimentalHudOverlay || !m_RenderedHud || !m_D9HUDSurface ||
        remaining < 1.01f)
        return;

    // VGUI blends into a target cleared to alpha 0, so a translucent caption
    // box lands with a lower alpha than it has on screen. Scale only the
    // alpha channel: dst.a += src.a * dst.a, at most x2 per pass; untouched
    // texels stay fully transparent. Source caches device state, so every
    // state touched here is restored before returning.
    IDirect3DStateBlock9 *saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) {
        if (m_RenderDiagnostics.First(RenderDiagnosticEvent::HudAlphaBoostFailed))
            Logger::Write("Caption opacity boost: state block unavailable; boost skipped");
        return;
    }
    IDirect3DSurface9 *oldTarget = nullptr, *oldDepth = nullptr;
    device->GetRenderTarget(0, &oldTarget);
    device->GetDepthStencilSurface(&oldDepth);
    D3DSURFACE_DESC desc{};
    m_D9HUDSurface->GetDesc(&desc);

    device->SetRenderTarget(0, m_D9HUDSurface);
    device->SetDepthStencilSurface(nullptr);
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    struct Vertex { float x, y, z, rhw; DWORD color; };
    const float w = static_cast<float>(desc.Width) - 0.5f, h = static_cast<float>(desc.Height) - 0.5f;
    Vertex quad[4] = {{-0.5f, -0.5f, 0, 1, 0}, {w, -0.5f, 0, 1, 0},
                      {-0.5f, h, 0, 1, 0}, {w, h, 0, 1, 0}};
    HRESULT drawResult = D3D_OK;
    while (remaining >= 1.01f && SUCCEEDED(drawResult)) {
        const float factor = (std::min)(remaining, 2.0f);
        remaining /= factor;
        const DWORD alpha = static_cast<DWORD>(std::lround((factor - 1.0f) * 255.0f));
        for (auto &vertex : quad)
            vertex.color = alpha << 24;
        drawResult = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
    }

    device->SetRenderTarget(0, oldTarget);
    device->SetDepthStencilSurface(oldDepth);
    saved->Apply(); // after the targets: SetRenderTarget resets the viewport
    saved->Release();
    if (oldTarget) oldTarget->Release();
    if (oldDepth) oldDepth->Release();
    if (!m_HUDAlphaBoostLogged) {
        m_HUDAlphaBoostLogged = true;
        Logger::Write("Caption opacity boost x" + std::to_string(m_Config.hudCaptionOpacityBoost) +
            " applied to " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) +
            " HUD surface: hr=" + std::to_string(drawResult));
    }
}

void VR::CreateZoomOverlay()
{
    const auto created = m_Overlay->CreateOverlay("Portal2VR.Zoom", "Portal 2 VR Enriched Binoculars", &m_ZoomHandle);
    if (created != vr::VROverlayError_None) {
        Logger::Write(std::string("Binocular zoom overlay unavailable: ") +
            m_Overlay->GetOverlayErrorNameFromEnum(created));
        m_ZoomHandle = vr::k_ulOverlayHandleInvalid;
        return;
    }
    const vr::VRTextureBounds_t bounds{0.0f, 0.0f, 1.0f, 1.0f};
    const auto inputError = m_Overlay->SetOverlayInputMethod(m_ZoomHandle, vr::VROverlayInputMethod_None);
    const auto boundsError = m_Overlay->SetOverlayTextureBounds(m_ZoomHandle, &bounds);
    const auto sortError = m_Overlay->SetOverlaySortOrder(m_ZoomHandle, 0);
    if (inputError != vr::VROverlayError_None || boundsError != vr::VROverlayError_None ||
        sortError != vr::VROverlayError_None) {
        Logger::Write("Binocular zoom overlay setup failed: " + std::to_string(inputError) + "," +
            std::to_string(boundsError) + "," + std::to_string(sortError));
        m_Overlay->DestroyOverlay(m_ZoomHandle);
        m_ZoomHandle = vr::k_ulOverlayHandleInvalid;
        return;
    }
    Logger::Write("Binocular zoom overlay created");
}

bool VR::ZoomViewWanted() const
{
    return m_ZoomHold.Active() && m_Config.binocularZoom && m_ZoomHandle != vr::k_ulOverlayHandleInvalid &&
        m_ZoomTexture && m_D9ZoomSurface && TextureSubmissionReady(m_VKZoom);
}

float VR::ZoomFovDegrees() const
{
    return BinocularZoom::RenderFovDegrees(m_Config.zoomWindowWidthMeters, m_Config.zoomDistanceMeters,
        m_Config.zoomMagnification);
}

void VR::NoteZoomRendered(double milliseconds)
{
    m_ZoomRendered = true;
    m_ZoomMasked = false;
    ++m_ZoomFrames;
    m_ZoomRenderMs += milliseconds;
    m_ZoomWorstMs = (std::max)(m_ZoomWorstMs, milliseconds);
}

void VR::MaskZoomView(IDirect3DDevice9 *device)
{
    m_ZoomMasked = false;
    if (!device || !m_ZoomRendered || !m_D9ZoomSurface)
        return;

    // The scene leaves arbitrary alpha in the target. Rewrite alpha to the
    // lens outline and darken the rim, with the same state discipline as
    // BoostHUDAlpha: Source caches device state, so all of it is restored.
    IDirect3DStateBlock9 *saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved) {
        if (m_RenderDiagnostics.First(RenderDiagnosticEvent::ZoomStateBlockUnavailable))
            Logger::Write("Binocular zoom: state block unavailable; window stays hidden");
        return;
    }
    D3DSURFACE_DESC desc{};
    m_D9ZoomSurface->GetDesc(&desc);
    if (m_ZoomMask.lenses.empty())
        m_ZoomMask = BinocularZoom::MaskFor(static_cast<int>(desc.Width), static_cast<int>(desc.Height));
    IDirect3DSurface9 *oldTarget = nullptr, *oldDepth = nullptr;
    device->GetRenderTarget(0, &oldTarget);
    device->GetDepthStencilSurface(&oldDepth);

    device->SetRenderTarget(0, m_D9ZoomSurface);
    device->SetDepthStencilSurface(nullptr);
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    HRESULT result = D3D_OK;
    const auto draw = [&](const std::vector<BinocularZoom::Vertex> &triangles) {
        if (SUCCEEDED(result) && !triangles.empty())
            result = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(triangles.size() / 3),
                triangles.data(), sizeof(triangles[0]));
    };
    // 1. Transparent everywhere.
    device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    const float w = static_cast<float>(desc.Width) - 0.5f, h = static_cast<float>(desc.Height) - 0.5f;
    const BinocularZoom::Vertex clear[6] = {{-0.5f, -0.5f, 0, 1, 0}, {w, -0.5f, 0, 1, 0}, {-0.5f, h, 0, 1, 0},
                                            {w, -0.5f, 0, 1, 0}, {w, h, 0, 1, 0}, {-0.5f, h, 0, 1, 0}};
    draw(std::vector<BinocularZoom::Vertex>(std::begin(clear), std::end(clear)));
    // 2. Opaque lenses; MAX keeps their overlap and soft edges right.
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MAX);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    draw(m_ZoomMask.lenses);
    // 3. Dark rim, each lens clipped to its own side of the seam.
    device->SetRenderState(D3DRS_COLORWRITEENABLE,
        D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    const LONG seam = static_cast<LONG>(desc.Width / 2);
    const RECT left{0, 0, seam, static_cast<LONG>(desc.Height)};
    const RECT right{seam, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    device->SetScissorRect(&left);
    draw(m_ZoomMask.leftShade);
    device->SetScissorRect(&right);
    draw(m_ZoomMask.rightShade);

    device->SetRenderTarget(0, oldTarget);
    device->SetDepthStencilSurface(oldDepth);
    saved->Apply(); // after the targets: SetRenderTarget resets the viewport
    saved->Release();
    if (oldTarget) oldTarget->Release();
    if (oldDepth) oldDepth->Release();
    m_ZoomMasked = SUCCEEDED(result);
    if (m_RenderDiagnostics.First(m_ZoomMasked ? RenderDiagnosticEvent::ZoomMaskApplied :
            RenderDiagnosticEvent::ZoomMaskFailed))
        Logger::Write("Binocular zoom lens mask on " + std::to_string(desc.Width) + "x" +
            std::to_string(desc.Height) + " surface (format " + std::to_string(desc.Format) +
            "): hr=" + HRESULTText(result) + (m_ZoomMasked ? "" : "; window stays hidden"));
}

void VR::SubmitZoomOverlay()
{
    const bool show = m_Overlay && m_ZoomHandle != vr::k_ulOverlayHandleInvalid && m_ZoomRendered &&
        m_ZoomMasked && m_RenderedNewFrame && m_CreatedVRTextures && m_ZoomHold.Active() &&
        TextureSubmissionReady(m_VKZoom) && !m_Game->m_VguiSurface->IsCursorVisible();
    m_ZoomRendered = m_ZoomMasked = false;
    if (!m_Overlay || m_ZoomHandle == vr::k_ulOverlayHandleInvalid)
        return;
    if (!show) {
        if (m_Overlay->IsOverlayVisible(m_ZoomHandle))
            m_Overlay->HideOverlay(m_ZoomHandle);
        return;
    }
    const VRSubmissionScope queue(m_D3DVR);
    vr::EVROverlayError placeError = vr::VROverlayError_None, widthError = vr::VROverlayError_None;
    if (!m_Overlay->IsOverlayVisible(m_ZoomHandle)) {
        // Placed when it opens, so config reloads apply on the next look.
        vr::HmdMatrix34_t transform{};
        transform.m[0][0] = transform.m[1][1] = transform.m[2][2] = 1.0f;
        transform.m[2][3] = -m_Config.zoomDistanceMeters; // OpenVR HMD forward is -Z
        placeError = m_Overlay->SetOverlayTransformTrackedDeviceRelative(
            m_ZoomHandle, vr::k_unTrackedDeviceIndex_Hmd, &transform);
        widthError = m_Overlay->SetOverlayWidthInMeters(m_ZoomHandle, m_Config.zoomWindowWidthMeters);
    }
    const auto textureError = m_Overlay->SetOverlayTexture(m_ZoomHandle, &m_VKZoom.m_VRTexture);
    const bool ready = placeError == vr::VROverlayError_None && widthError == vr::VROverlayError_None &&
        textureError == vr::VROverlayError_None;
    const auto showError = ready ? m_Overlay->ShowOverlay(m_ZoomHandle) : textureError;
    const bool failed = !ready || showError != vr::VROverlayError_None;
    if (m_RenderConditions.Observe(RenderCondition::ZoomSubmit, failed,
            {static_cast<std::uint64_t>(placeError), static_cast<std::uint64_t>(widthError),
             static_cast<std::uint64_t>(textureError), static_cast<std::uint64_t>(showError)}, true) !=
            RenderConditionChange::None)
        Logger::Write("Binocular zoom overlay submission: place=" + std::to_string(placeError) +
            " width=" + std::to_string(widthError) + " texture=" + std::to_string(textureError) +
            " visibility=" + std::to_string(showError));
    if (failed)
        m_Overlay->HideOverlay(m_ZoomHandle);
}

void VR::ProcessZoomInput()
{
    if (!m_ZoomBindingLogged) {
        // SteamVR bindings saved before this action existed leave it unbound.
        m_ZoomBindingLogged = true;
        vr::InputBindingInfo_t bindings[2]{};
        uint32_t count = 0;
        const auto error = m_Input->GetActionBindingInfo(m_ActionZoom, bindings, sizeof(bindings[0]),
            static_cast<uint32_t>(std::size(bindings)), &count);
        std::string where;
        for (uint32_t i = 0; i < count && i < std::size(bindings); ++i)
            where += std::string(i ? ", " : "") + bindings[i].rchDevicePathName + bindings[i].rchInputPathName +
                " (" + bindings[i].rchModeName + ")";
        Logger::Write("Binocular zoom action: " + (where.empty() ? std::string("unbound") : where) +
            " (error " + std::to_string(error) + ")" +
            (m_Config.binocularZoom ? "" : "; BinocularZoom=false"));
    }
    vr::InputDigitalActionData_t click{};
    const auto clickError = m_Input->GetDigitalActionData(m_ActionZoom, &click, sizeof(click),
        vr::k_ulInvalidInputValueHandle);
    const bool pressed = m_Config.binocularZoom && clickError == vr::VRInputError_None &&
        click.bActive && click.bState;
    vr::InputAnalogActionData_t stick{};
    if (!GetAnalogActionData(m_ActionTurn, stick))
        stick = {};
    switch (m_ZoomHold.Update(pressed, stick.x, stick.y)) {
    case BinocularZoom::Hold::Change::Started:
        m_ZoomFrames = 0;
        m_ZoomRenderMs = m_ZoomWorstMs = 0.0;
        m_ZoomStart = std::chrono::steady_clock::now();
        // Portal 2's own zoom: no visible effect in the headset, but the
        // instructor lesson for it completes.
        m_Game->ClientCmd_Unrestricted("+zoom");
        Logger::Write("Binocular zoom on: x" + std::to_string(m_Config.zoomMagnification) + ", " +
            std::to_string(m_Config.zoomWindowWidthMeters) + " m window at " +
            std::to_string(m_Config.zoomDistanceMeters) + " m, view fov " + std::to_string(ZoomFovDegrees()) +
            " deg");
        break;
    case BinocularZoom::Hold::Change::Ignored:
        if (m_RenderDiagnostics.First(RenderDiagnosticEvent::ZoomClickWhileTurning))
            Logger::Write("Binocular zoom: right stick click ignored while the stick is pushed to " +
                std::to_string(stick.x) + "," + std::to_string(stick.y) + " (turning); later ones are not logged");
        break;
    case BinocularZoom::Hold::Change::Ended:
        EndZoom("released");
        break;
    default:
        break;
    }
}

void VR::EndZoom(const char *reason)
{
    m_Game->ClientCmd_Unrestricted("-zoom");
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_ZoomStart).count();
    char line[224];
    std::snprintf(line, sizeof(line),
        "Binocular zoom off (%s) after %.1f s: %u views, render avg %.2f ms, worst %.2f ms",
        reason, seconds, m_ZoomFrames, m_ZoomFrames ? m_ZoomRenderMs / m_ZoomFrames : 0.0, m_ZoomWorstMs);
    Logger::Write(line);
}

void VR::GetPoseData(vr::TrackedDevicePose_t &poseRaw, TrackedDevicePoseData &poseOut)
{
    poseOut.valid = poseRaw.bPoseIsValid &&
        IsUsableTrackedPose(poseRaw.mDeviceToAbsoluteTracking.m,
                            poseRaw.vVelocity.v, poseRaw.vAngularVelocity.v);
    if (!poseOut.valid) {
        poseOut.TrackedDevicePos = { 0, 0, 0 };
        poseOut.TrackedDeviceVel = { 0, 0, 0 };
        poseOut.TrackedDeviceAng = { 0, 0, 0 };
        poseOut.TrackedDeviceAngVel = { 0, 0, 0 };
        return;
    }
    if (poseRaw.bPoseIsValid) 
    {
        vr::HmdMatrix34_t mat = poseRaw.mDeviceToAbsoluteTracking;
        Vector pos;
        Vector vel;
        QAngle ang;
        QAngle angvel;
        pos = TrackingSpace::OpenVrToSourceMeters({mat.m[0][3], mat.m[1][3], mat.m[2][3]});
        ang.x = asin(std::clamp(mat.m[1][2], -1.0f, 1.0f)) * (180.0 / 3.141592654);
        ang.y = atan2f(mat.m[0][2], mat.m[2][2]) * (180.0 / 3.141592654);
        ang.z = atan2f(-mat.m[1][0], mat.m[1][1]) * (180.0 / 3.141592654);
        vel = TrackingSpace::OpenVrToSourceMeters({poseRaw.vVelocity.v[0],
            poseRaw.vVelocity.v[1], poseRaw.vVelocity.v[2]});
        angvel.x = -poseRaw.vAngularVelocity.v[2] * (180.0 / 3.141592654);
        angvel.y = -poseRaw.vAngularVelocity.v[0] * (180.0 / 3.141592654);
        angvel.z = poseRaw.vAngularVelocity.v[1] * (180.0 / 3.141592654);

        poseOut.TrackedDevicePos = pos;
        poseOut.TrackedDeviceVel = vel;
        poseOut.TrackedDeviceAng = ang;
        poseOut.TrackedDeviceAngVel = angvel;
    }
}

bool VR::RepositionOverlays()
{
    if (!m_HmdPose.valid) {
        m_MenuOverlayPlacement.Invalidate();
        return false;
    }
    const auto &hmdMat = m_Poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
    const bool visible = m_Overlay->IsOverlayVisible(m_MainMenuHandle);
    const bool reanchor = m_MenuOverlayPlacement.ShouldAttempt(visible, true);
    const auto pose = m_MenuOverlayPlacement.UpdatePose(hmdMat.m, visible);
    if (!pose)
        return false;

    int windowWidth = 0, windowHeight = 0;
    IMatRenderContext *menuContext = m_Game->m_MaterialSystem->GetRenderContext();
    if (menuContext) {
        menuContext->GetWindowSize(windowWidth, windowHeight);
        menuContext->Release();
    }

    vr::HmdMatrix34_t menuTransform{};
    std::memcpy(menuTransform.m, pose->m, sizeof(menuTransform.m));

    vr::ETrackingUniverseOrigin trackingOrigin = vr::VRCompositor()->GetTrackingSpace();

    // Reposition main menu overlay
    const auto renderWidth = m_VKBackBuffer.m_VulkanData.m_nWidth;
    const auto renderHeight = m_VKBackBuffer.m_VulkanData.m_nHeight;
    const auto maximum = static_cast<uint32_t>((std::numeric_limits<int>::max)());
    const auto crop = renderWidth <= maximum && renderHeight <= maximum ?
        HudCapture::WindowTextureCrop(static_cast<int>(renderWidth), static_cast<int>(renderHeight),
            windowWidth, windowHeight) : std::nullopt;
    if (!crop) {
        m_MenuOverlayPlacement.Invalidate();
        return false;
    }

    float widthRatio = crop->uMax;
    float heightRatio = crop->vMax;
    menuTransform.m[0][0] *= widthRatio;
    menuTransform.m[2][0] *= widthRatio;
    menuTransform.m[1][1] *= heightRatio;

    const auto transformError = vr::VROverlay()->SetOverlayTransformAbsolute(
        m_MainMenuHandle, trackingOrigin, &menuTransform);
    const auto widthError = vr::VROverlay()->SetOverlayWidthInMeters(
        m_MainMenuHandle, 1.5 * (1.0 / heightRatio));
    const bool positioned = transformError == vr::VROverlayError_None &&
        widthError == vr::VROverlayError_None;
    m_MenuOverlayPlacement.RecordResult(positioned);
    const auto now = std::chrono::steady_clock::now();
    const bool firstResult = m_RenderDiagnostics.First(positioned ?
        RenderDiagnosticEvent::OverlayPlacementSucceeded : RenderDiagnosticEvent::OverlayPlacementFailed);
    if (firstResult || (positioned && reanchor && now >= m_NextMenuPlacementLog)) {
        Logger::Write("VR menu placement: transform=" + std::to_string(transformError) +
            " width=" + std::to_string(widthError) +
            " physicalHmd=" + std::to_string(hmdMat.m[0][3]) + "," +
            std::to_string(hmdMat.m[1][3]) + "," + std::to_string(hmdMat.m[2][3]) +
            " (translation follows HMD; heading locked until reopening)");
        m_NextMenuPlacementLog = now + std::chrono::seconds(5);
    }
    return positioned;
}

void VR::GetPoses() 
{
    const bool hadHmd = m_HmdPose.valid;
    const bool hadLeft = m_LeftControllerPose.valid;
    const bool hadRight = m_RightControllerPose.valid;
    vr::TrackedDevicePose_t hmdPose = m_Poses[vr::k_unTrackedDeviceIndex_Hmd];

    vr::TrackedDeviceIndex_t leftControllerIndex = m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
    vr::TrackedDeviceIndex_t rightControllerIndex = m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);

    if (m_LeftHanded)
        std::swap(leftControllerIndex, rightControllerIndex);

    vr::TrackedDevicePose_t leftControllerPose{};
    vr::TrackedDevicePose_t rightControllerPose{};
    if (IsUsableTrackedDeviceIndex(leftControllerIndex, vr::k_unMaxTrackedDeviceCount,
                                   vr::k_unTrackedDeviceIndexInvalid))
        leftControllerPose = m_Poses[leftControllerIndex];
    if (IsUsableTrackedDeviceIndex(rightControllerIndex, vr::k_unMaxTrackedDeviceCount,
                                   vr::k_unTrackedDeviceIndexInvalid))
        rightControllerPose = m_Poses[rightControllerIndex];

    GetPoseData(hmdPose, m_HmdPose);
    GetPoseData(leftControllerPose, m_LeftControllerPose);
    GetPoseData(rightControllerPose, m_RightControllerPose);
    if (!m_HmdPose.valid) {
        m_LeftControllerPose = {};
        m_RightControllerPose = {};
    }
    if (hadHmd != m_HmdPose.valid)
        Logger::Write(std::string("HMD tracking ") + (m_HmdPose.valid ? "valid" : "lost"));
    if (hadLeft != m_LeftControllerPose.valid)
        Logger::Write(std::string("Left controller tracking ") +
                      (m_LeftControllerPose.valid ? "valid, index " + std::to_string(leftControllerIndex) : "lost"));
    if (hadRight != m_RightControllerPose.valid)
        Logger::Write(std::string("Right controller tracking ") +
                      (m_RightControllerPose.valid ? "valid, index " + std::to_string(rightControllerIndex) : "lost"));
}

bool VR::UpdatePosesAndActions()
{
    vr::EVRCompositorError poseError;
    const auto waitStart = std::chrono::steady_clock::now();
    {
        const VRSubmissionScope queue(m_D3DVR);
        poseError = vr::VRCompositor()->WaitGetPoses(m_Poses, vr::k_unMaxTrackedDeviceCount, NULL, 0);
    }
    m_PerfLastWaitPosesMs = PerfStats::ElapsedMs(waitStart);
    PerfRecord(PerfStats::Metric::WaitPoses, m_PerfLastWaitPosesMs);
    if (poseError == vr::VRCompositorError_None)
        PerfReadCompositorTiming();
    if (poseError != vr::VRCompositorError_None) {
        if (m_LastPoseError != poseError)
            Logger::Write("WaitGetPoses failed: " + std::to_string(poseError));
        m_LastPoseError = poseError;
        std::fill(std::begin(m_Poses), std::end(m_Poses), vr::TrackedDevicePose_t{});
        m_HmdPose.valid = m_LeftControllerPose.valid = m_RightControllerPose.valid = false;
    } else {
        m_LastPoseError = 0;
        ++m_PoseFetchSequence;
        m_FrameTimingPending = m_ExplicitTiming;
    }
    const bool useHaptics = (m_Config.experimentalPortalShotHaptics || GameHapticsEnabled()) &&
        m_HapticOutputsAvailable;
    vr::VRActiveActionSet_t activeSets[] = { m_ActiveActionSet, m_ActiveHapticActionSet };
    auto inputError = m_Input->UpdateActionState(activeSets,
        sizeof(vr::VRActiveActionSet_t), useHaptics ? 2 : 1);
    if (inputError != vr::VRInputError_None && useHaptics) {
        Logger::Write("OpenVR haptic action set update failed (error " +
            std::to_string(inputError) + "); disabling optional haptics");
        m_HapticOutputsAvailable = false;
        inputError = m_Input->UpdateActionState(&m_ActiveActionSet,
            sizeof(vr::VRActiveActionSet_t), 1);
    }
    if (inputError != vr::VRInputError_None) {
        if (m_LastInputError != inputError)
            Logger::Write("UpdateActionState failed: " + std::to_string(inputError));
        m_LastInputError = inputError;
    } else {
        m_LastInputError = 0;
    }
    return poseError == vr::VRCompositorError_None && inputError == vr::VRInputError_None;
}

void VR::QueuePortalShotHaptic()
{
    m_PortalShotHapticGate.Queue(std::chrono::steady_clock::now());
}

void VR::DispatchPortalShotHaptic(bool actionsReady)
{
    const bool gameplay = m_IsVREnabled && m_Game->m_EngineClient->IsInGame() &&
        !m_Game->m_VguiSurface->IsCursorVisible();
    if (!Haptics::CanDeliverShot(m_Config.experimentalPortalShotHaptics, actionsReady,
            gameplay, m_TrackingOutputValid, m_RightControllerPose.valid,
            m_HapticOutputsAvailable)) {
        m_PortalShotHapticGate.Clear();
        return;
    }
    // The forwarded native PORTALGUN rumble already covers this shot.
    if (GameHapticsEnabled() && m_Config.hapticsNativeRumble && m_NativeRumbleHookActive) {
        if (!m_PortalShotSupersededLogged)
            Logger::Write("ExperimentalPortalShotHaptics superseded by HapticsNativeRumble");
        m_PortalShotSupersededLogged = true;
        m_PortalShotHapticGate.Clear();
        return;
    }
    // Only an eligible attempt starts the pulse-spacing window.
    if (!m_PortalShotHapticGate.Consume())
        return;
    const auto hand = Haptics::OutputHand(m_LeftHanded);
    const auto action = hand == Haptics::Hand::Left ? m_HapticLeft : m_HapticRight;
    const auto result = m_Input->TriggerHapticVibrationAction(action, 0.0f,
        m_Config.portalShotHapticDurationSeconds, 150.0f,
        m_Config.portalShotHapticAmplitude, vr::k_ulInvalidInputValueHandle);
    if (result != vr::VRInputError_None) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_NextHapticErrorLog) {
            Logger::Write("OpenVR portal-shot haptic failed (error " + std::to_string(result) + ")");
            m_NextHapticErrorLog = now + std::chrono::seconds(5);
        }
    }
}

bool VR::GameHapticsEnabled() const
{
    return m_Config.haptics && m_Config.hapticsIntensity > 0.0f;
}

// Called from the client Rumble user-message path; logs every effect so the
// events Portal 2 emits can be read back from portal2vr.log.
void VR::QueueNativeRumble(int slot, int index, int data, int flags)
{
    const auto now = std::chrono::steady_clock::now();
    const auto pattern = Haptics::NativeRumblePattern(index, data, flags);
    const bool forward = slot == 0 && GameHapticsEnabled() && m_Config.hapticsNativeRumble;
    if (now - m_RumbleLogWindow >= std::chrono::seconds(1)) {
        m_RumbleLogWindow = now;
        m_RumbleLogLines = 0;
    }
    if (m_RumbleLogLines < 20) {
        ++m_RumbleLogLines;
        Logger::Write("Native rumble: slot=" + std::to_string(slot) +
            " effect=" + std::to_string(index) + " data=" + std::to_string(data) +
            " flags=" + std::to_string(flags) +
            (!pattern.count ? " (stop/update, no pulse)" : !pattern.mapped ? " (unmapped, generic pulse)" : "") +
            (forward ? "" : " (not forwarded)"));
    }
    if (forward)
        m_GameHapticQueue.Push(index, pattern, now);
}

void VR::QueueHapticEvent(Haptics::Event event)
{
    if (!GameHapticsEnabled()) return;
    const bool held = event == Haptics::Event::Pickup || event == Haptics::Event::Drop;
    if (held ? !m_Config.hapticsHeldObject : !m_Config.hapticsButtons) return;
    m_GameHapticQueue.Push(1000 + static_cast<int>(event), Haptics::EventPattern(event),
        std::chrono::steady_clock::now());
}

void VR::NoteHeldObjectUpdate()
{
    m_HoldTracker.Update(std::chrono::steady_clock::now());
}

// Pooled entity strings; "?" keeps a log line readable if one is ever missing.
static std::string HeldEntityText(const void *entity, std::size_t offset)
{
    constexpr std::size_t kMaxLength = 96;
    const char *text = entity ? HeldObject::Field<const char *>(entity, offset) : nullptr;
    if (!VerifiedPortalSymbols::Detail::Readable(text, kMaxLength, false)) return "?";
    return std::string(text, strnlen(text, kMaxLength));
}

bool VR::BeginHeldAnchor(void *controller)
{
    const auto *offsets = m_Game->m_Offsets;
    const auto &layout = offsets->m_HeldObjectLayout;
    const void *entity = !layout.entityList ? nullptr : HeldObject::EntityFromHandle(
        HeldObject::Field<std::uintptr_t>(reinterpret_cast<const void *>(layout.entityList), 0),
        HeldObject::Field<std::uint32_t>(controller, HeldObject::kAttachedEntity));
    if (!entity || !offsets->Weapon_ShootPosition.address || !m_Config.heldObjectAnchor ||
        !m_IsVREnabled || !m_TrackingOutputValid || !m_RightControllerPose.valid ||
        !HeldObject::Field<std::uint8_t>(controller, HeldObject::kIgnoreRelativePitch)) {
        m_HeldAnchor.Release(controller);
        return false;
    }

    const QAngle gun = GetRightControllerAbsAngle();
    m_HeldAnchor.Apply(controller, gun.x, gun.z);

    // UpdateObject reads a roll it never saw on a flat screen as a player lying on one side,
    // and lowers the ray. The roll reaches the object through the attached angles instead.
    const float pitch = std::remainder(gun.x, 360.0f);
    m_HeldAnchorEye.x = std::clamp(pitch, -HeldObject::kAnchorEyePitch, HeldObject::kAnchorEyePitch);
    m_HeldAnchorEye.y = gun.y;
    m_HeldAnchorEye.z = 0;
    m_HeldAnchorStart = GetRightControllerAbsPos();
    m_HeldAnchorActive = true;
    return true;
}

void VR::EndHeldAnchor()
{
    m_HeldAnchorActive = false;
}

void VR::NoteGrabController(const void *controller, float maxError, bool kept, bool viewModel)
{
    const auto &layout = m_Game->m_Offsets->m_HeldObjectLayout;
    if (!m_Config.heldObjectLog || !layout.entityList || !controller) return;

    const auto now = std::chrono::steady_clock::now();
    std::string line, sample;
    {
        std::lock_guard<std::mutex> lock(m_HeldTraceMutex);
        // A forced drop in UpdateObjectVM returns through UpdateObject as well.
        if (!kept && !m_HeldTrace.Active()) return;
        auto state = HeldObject::ReadGrabState(controller);
        const bool anchored = !viewModel && m_HeldAnchor.Applied();
        if (anchored) state.attachedAngles = m_HeldAnchor.BaseAngles();
        const void *entity = HeldObject::EntityFromHandle(
            HeldObject::Field<std::uintptr_t>(reinterpret_cast<const void *>(layout.entityList), 0),
            state.attachedHandle);
        if (!m_HeldTrace.Active()) {
            m_HeldTrace.Begin(now);
            line = HeldObject::FormatPickup(HeldEntityText(entity, HeldObject::kClassName),
                HeldEntityText(entity, HeldObject::kModelName), maxError, state,
                entity ? HeldObject::Field<float>(entity, HeldObject::kCollisionRadius) : 0.0f,
                m_Config.heldObjectColumn && !anchored, m_Config.laserCubeAimAssist, anchored);
        }
        const bool sampleDue = m_HeldTrace.Tick(now, state.error, viewModel);
        if (!kept) {
            sample = HeldObject::FormatDrop(HeldObject::DropReason::Forced, m_HeldTrace.End());
        } else if (sampleDue) {
            const Vector hand = GetRightControllerAbsPos();
            const QAngle handAngles = GetRightControllerAbsAngle();
            sample = HeldObject::FormatSample(m_HeldTrace.Elapsed(now), {hand.x, hand.y, hand.z},
                {handAngles.x, handAngles.y, handAngles.z},
                {m_HmdAngAbs.x, m_HmdAngAbs.y, m_HmdAngAbs.z},
                entity ? HeldObject::Field<HeldObject::Vec3>(entity, HeldObject::kAbsOrigin) : HeldObject::Vec3{},
                entity ? HeldObject::Field<HeldObject::Vec3>(entity, HeldObject::kAbsAngles) : HeldObject::Vec3{},
                state);
        }
    }
    if (!line.empty()) Logger::WriteSibling(HeldObject::kLogFile, line);
    if (!sample.empty()) Logger::WriteSibling(HeldObject::kLogFile, sample);
}

void VR::PollHeldTrace(std::chrono::steady_clock::time_point now)
{
    const bool ticking = m_Game->m_EngineClient->IsInGame() && !m_Game->m_VguiSurface->IsCursorVisible();
    std::string line;
    {
        std::lock_guard<std::mutex> lock(m_HeldTraceMutex);
        if (m_HeldTrace.Released(now, ticking))
            line = HeldObject::FormatDrop(HeldObject::DropReason::Released, m_HeldTrace.End());
    }
    if (!line.empty()) Logger::WriteSibling(HeldObject::kLogFile, line);
}

void VR::ApplyHeldObjectTuning()
{
    const auto &layout = m_Game->m_Offsets->m_HeldObjectLayout;
    const auto apply = [this](std::uintptr_t conVar, const char *name, bool enabled) {
        if (HeldObject::SetConVarInt(conVar, name, enabled ? 1 : 0) && m_Config.heldObjectLog)
            Logger::WriteSibling(HeldObject::kLogFile,
                std::string("tuning ") + name + "=" + (enabled ? "1" : "0"));
    };
    apply(layout.laserAutoaimCvar, "sv_laser_cube_autoaim", m_Config.laserCubeAimAssist);
    // The vertical plane would pull an anchored object off the gun's axis.
    const bool column = m_Config.heldObjectColumn && !m_Config.heldObjectAnchor;
    apply(layout.holdColumnCvar, "player_hold_object_in_column", column);
    apply(layout.clientHoldColumnCvar, "player_hold_object_in_column", column);
}

void VR::DispatchGameHaptics(bool actionsReady)
{
    const auto now = std::chrono::steady_clock::now();
    const bool gameplay = m_IsVREnabled && m_Game->m_EngineClient->IsInGame() &&
        !m_Game->m_VguiSurface->IsCursorVisible();
    if (!GameHapticsEnabled() || !actionsReady || !gameplay || !m_TrackingOutputValid ||
        !m_HapticOutputsAvailable) {
        m_GameHapticQueue.Clear();
        m_HoldTracker.Suspend(now);
        return;
    }
    switch (m_HoldTracker.Poll(now)) {
    case Haptics::HoldTracker::Change::Pickup:
        QueueHapticEvent(Haptics::Event::Pickup);
        m_NextHoldPulse = now + std::chrono::milliseconds(80);
        break;
    case Haptics::HoldTracker::Change::Drop:
        QueueHapticEvent(Haptics::Event::Drop);
        break;
    default:
        break;
    }
    std::array<Haptics::Pattern, Haptics::EventQueue::kCapacity> patterns;
    const auto count = m_GameHapticQueue.Drain(patterns);
    for (std::size_t i = 0; i < count; ++i) {
        for (int p = 0; p < patterns[i].count; ++p)
            TriggerHapticPulse(patterns[i].pulses[p]);
    }
    if (m_Config.hapticsHeldObject && m_HoldTracker.Holding() && now >= m_NextHoldPulse) {
        m_NextHoldPulse = now + Haptics::kHoldPulseInterval;
        TriggerHapticPulse({Haptics::Target::GunHand, 0.0f, Haptics::kHoldPulseSeconds,
            90.0f, Haptics::kHoldAmplitude});
    }
}

void VR::TriggerHapticPulse(const Haptics::Pulse &requested)
{
    const auto pulse = Haptics::ScaledPulse(requested, m_Config.hapticsIntensity);
    if (pulse.amplitude <= 0.0f) return;
    const auto gun = Haptics::OutputHand(m_LeftHanded);
    const auto off = gun == Haptics::Hand::Left ? Haptics::Hand::Right : Haptics::Hand::Left;
    const auto fire = [&](Haptics::Hand hand) {
        const bool valid = hand == Haptics::Hand::Left ? m_LeftControllerPose.valid : m_RightControllerPose.valid;
        if (!valid) return;
        const auto action = hand == Haptics::Hand::Left ? m_HapticLeft : m_HapticRight;
        const auto result = m_Input->TriggerHapticVibrationAction(action, pulse.delaySeconds,
            pulse.durationSeconds, pulse.frequency, pulse.amplitude, vr::k_ulInvalidInputValueHandle);
        if (result != vr::VRInputError_None) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= m_NextHapticErrorLog) {
                Logger::Write("OpenVR game haptic failed (error " + std::to_string(result) + ")");
                m_NextHapticErrorLog = now + std::chrono::seconds(5);
            }
        }
    };
    if (pulse.target != Haptics::Target::OffHand) fire(gun);
    if (pulse.target != Haptics::Target::GunHand) fire(off);
}

void VR::GetViewParameters() 
{
    vr::HmdMatrix34_t eyeToHeadLeft = m_System->GetEyeToHeadTransform(vr::Eye_Left);
    vr::HmdMatrix34_t eyeToHeadRight = m_System->GetEyeToHeadTransform(vr::Eye_Right);
    m_EyeToHeadTransformPosLeft.x = eyeToHeadLeft.m[0][3];
    m_EyeToHeadTransformPosLeft.y = eyeToHeadLeft.m[1][3];
    m_EyeToHeadTransformPosLeft.z = eyeToHeadLeft.m[2][3];

    m_EyeToHeadTransformPosRight.x = eyeToHeadRight.m[0][3];
    m_EyeToHeadTransformPosRight.y = eyeToHeadRight.m[1][3];
    m_EyeToHeadTransformPosRight.z = eyeToHeadRight.m[2][3];
}

bool VR::PressedDigitalAction(vr::VRActionHandle_t &actionHandle, bool checkIfActionChanged)
{
    vr::InputDigitalActionData_t digitalActionData{};
    vr::EVRInputError result = m_Input->GetDigitalActionData(actionHandle, &digitalActionData, sizeof(digitalActionData), vr::k_ulInvalidInputValueHandle);
    
    if (result == vr::VRInputError_None && digitalActionData.bActive)
    {
        if (checkIfActionChanged)
            return DigitalButtonState::PressEdge(true, digitalActionData.bChanged, digitalActionData.bState);
        else
            return digitalActionData.bState;
    }

    return false;
}

void VR::ProcessHeldAction(vr::VRActionHandle_t actionHandle, DigitalButtonState &state,
                           const char *pressCommand, const char *releaseCommand)
{
    vr::InputDigitalActionData_t data{};
    const auto result = m_Input->GetDigitalActionData(actionHandle, &data, sizeof(data),
                                                      vr::k_ulInvalidInputValueHandle);
    const bool active = result == vr::VRInputError_None && data.bActive;
    if (const char *command = state.HeldCommand(active, data.bChanged, data.bState,
                                                 pressCommand, releaseCommand))
        m_Game->ClientCmd_Unrestricted(command);
}

void VR::InvalidateTrackingOutput()
{
    m_RenderedNewFrame = m_RenderedHud = false;
    m_TrackingOutputValid = m_LeftControllerOutputValid = false;
    m_HmdPose.valid = m_LeftControllerPose.valid = m_RightControllerPose.valid = false;
    std::fill(std::begin(m_Poses), std::end(m_Poses), vr::TrackedDevicePose_t{});
    m_LeftControllerPosRel = m_RightControllerPosRel = {0.0f, 0.0f, 0.0f};
    m_PrevFrameTime = std::chrono::steady_clock::now();
    m_HmdLostSinceLastValid = true;
    m_RoomscaleMotor.Reset();
    m_PortalCoordinator.CancelPending();
    m_RoomscaleObserver.OnPose(false, {}, m_PoseFetchSequence, 0.0f, 1.0f, true);
    m_PortalShotHapticGate.Clear();
    m_GameHapticQueue.Clear();
    ResetMuzzleSample();
}

void VR::SuspendInputForRenderFailure()
{
    // CPU/input cleanup only: no OpenVR consumer calls, graphics submission,
    // allocation or queue locks while the producer drain is unavailable.
    SuspendVRInputAfterRenderFailure([&] { InvalidateTrackingOutput(); },
        [&] { ReleaseHeldActions(); }, [&] { ReleaseMenuMouse(); });
}

void VR::ReleaseHeldActions()
{
    const struct {
        DigitalButtonState *state;
        const char *releaseCommand;
    } actions[] = {
        { &m_PrimaryAttackState, "-attack" }, { &m_SecondaryAttackState, "-attack2" },
        { &m_JumpState, "-jump" }, { &m_CrouchState, "-duck" },
        { &m_UseState, "-use" }, { &m_ReloadState, "-reload" }
    };
    for (const auto &action : actions)
        if (const char *command = action.state->HeldCommand(false, false, false, "", action.releaseCommand))
            m_Game->ClientCmd_Unrestricted(command);
    if (m_ZoomHold.Cancel())
        EndZoom("menu or input lost");
}

bool VR::GetAnalogActionData(vr::VRActionHandle_t &actionHandle, vr::InputAnalogActionData_t &analogDataOut)
{
    vr::EVRInputError result = m_Input->GetAnalogActionData(actionHandle, &analogDataOut, sizeof(analogDataOut), vr::k_ulInvalidInputValueHandle);

    if (result == vr::VRInputError_None)
        return true;

    return false;
}

void VR::SendMenuMouse(UiInput::MouseTransition transition)
{
    if (transition == UiInput::MouseTransition::None)
        return;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = transition == UiInput::MouseTransition::Press ?
        MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    const bool sent = SendInput(1, &input, sizeof(input)) == 1;
    m_MenuPointerState.ConfirmSent(transition, sent);
    if (!sent) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_NextMenuInputErrorLog) {
            Logger::Write("Menu mouse SendInput failed: " + std::to_string(GetLastError()));
            m_NextMenuInputErrorLog = now + std::chrono::seconds(5);
        }
    }
}

void VR::ReleaseMenuMouse()
{
    SendMenuMouse(m_MenuPointerState.LoseFocus());
}

void VR::ProcessMenuInput()
{
    // An overlay mouse-up event is delivered only once. Retry a failed synthetic
    // release even if the controller is still hovering and no new event arrives.
    SendMenuMouse(m_MenuPointerState.PendingRelease());
    const auto overlay = m_MainMenuHandle;
    bool hovering = m_Overlay->IsOverlayVisible(overlay) &&
        (CheckOverlayIntersectionForController(overlay, vr::TrackedControllerRole_LeftHand) ||
         CheckOverlayIntersectionForController(overlay, vr::TrackedControllerRole_RightHand));
    int windowWidth = 0, windowHeight = 0;
    IMatRenderContext *context = m_Game->m_MaterialSystem->GetRenderContext();
    if (context) {
        context->GetWindowSize(windowWidth, windowHeight);
        context->Release();
    }
    hovering = hovering && windowWidth > 0 && windowHeight > 0 &&
        m_VKBackBuffer.m_VRTexture.handle;
    m_Overlay->SetOverlayFlag(overlay, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, hovering);
    if (!hovering) {
        ReleaseMenuMouse();
        m_OptionsMenu.PointerLeave();
    }
    const bool inGame = m_Game->m_EngineClient->IsInGame();
    vr::VREvent_t event{};
    // Drain releases even when a controller leaves the overlay after a press.
    while (m_Overlay->PollNextOverlayEvent(overlay, &event, sizeof(event))) {
        switch (event.eventType) {
        case vr::VREvent_MouseMove:
            if (hovering) {
                const auto point = UiInput::MapMenuPointer(event.data.mouse.x, event.data.mouse.y,
                    m_VKBackBuffer.m_VulkanData.m_nWidth, m_VKBackBuffer.m_VulkanData.m_nHeight,
                    windowWidth, windowHeight, inGame);
                // While the VR page is open the native cursor stays parked on the
                // empty row under it, so Options buttons below never roll over.
                if (point && (!m_OptionsMenu.PointerMove(point->x, point->y) || !m_OptionsMenu.PageOpen()))
                    m_Game->m_VguiInput->SetCursorPos(point->x, point->y);
            }
            break;
        case vr::VREvent_MouseButtonDown:
            if (hovering && !m_OptionsMenu.PointerPress())
                SendMenuMouse(m_MenuPointerState.Press());
            break;
        case vr::VREvent_MouseButtonUp:
            SendMenuMouse(m_MenuPointerState.Release());
            break;
        case vr::VREvent_ScrollDiscrete:
            if (hovering && !m_OptionsMenu.Scroll(static_cast<int>(event.data.scroll.ydelta)))
                m_Game->m_VguiInput->InternalMouseWheeled((int)event.data.scroll.ydelta);
            break;
        }
    }
    if (m_OptionsMenu.PageOpen()) {
        // The VR page is modal: controller navigation never reaches the native menu.
        using Nav = VrOptionsMenu::Nav;
        const bool back = PressedDigitalAction(m_MenuBack, true);
        const bool pause = PressedDigitalAction(m_Pause, true);
        if (back || pause) m_OptionsMenu.Navigate(Nav::Back);
        if (hovering)
            return;
        if (PressedDigitalAction(m_MenuSelect, true)) m_OptionsMenu.Navigate(Nav::Select);
        if (PressedDigitalAction(m_MenuUp, true)) m_OptionsMenu.Navigate(Nav::Up);
        if (PressedDigitalAction(m_MenuDown, true)) m_OptionsMenu.Navigate(Nav::Down);
        if (PressedDigitalAction(m_MenuLeft, true)) m_OptionsMenu.Navigate(Nav::Left);
        if (PressedDigitalAction(m_MenuRight, true)) m_OptionsMenu.Navigate(Nav::Right);
        return;
    }
    if (hovering)
        return;

    const auto sendKey = [this](WORD key) {
        INPUT inputs[2]{};
        inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = inputs[1].ki.wVk = key;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT sent = SendInput(2, inputs, sizeof(INPUT));
        if (sent == 1)
            SendInput(1, &inputs[1], sizeof(INPUT)); // best-effort key release
        if (sent != 2) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= m_NextMenuInputErrorLog) {
                Logger::Write("Menu keyboard SendInput failed: " + std::to_string(GetLastError()));
                m_NextMenuInputErrorLog = now + std::chrono::seconds(5);
            }
        }
    };
    if (PressedDigitalAction(m_MenuSelect, true)) sendKey(VK_RETURN);
    const bool back = PressedDigitalAction(m_MenuBack, true);
    const bool pause = PressedDigitalAction(m_Pause, true);
    if (back || pause) sendKey(VK_ESCAPE);
    if (PressedDigitalAction(m_MenuUp, true)) sendKey(VK_UP);
    if (PressedDigitalAction(m_MenuDown, true)) sendKey(VK_DOWN);
    if (PressedDigitalAction(m_MenuLeft, true)) sendKey(VK_LEFT);
    if (PressedDigitalAction(m_MenuRight, true)) sendKey(VK_RIGHT);
}

void VR::ProcessViewActions()
{
    if (!m_IsVREnabled) {
        m_PrevFrameTime = std::chrono::steady_clock::now();
        return;
    }
    using duration = std::chrono::duration<float, std::milli>;
    const auto currentTime = std::chrono::steady_clock::now();
    const float deltaTime = duration(currentTime - m_PrevFrameTime).count();
    m_PrevFrameTime = currentTime;

    if (PressedDigitalAction(m_ActionResetPosition, true))
        ResetPosition();
    if (!m_HmdPose.valid)
        return;

    vr::InputAnalogActionData_t analogActionData{};
    if (!GetAnalogActionData(m_ActionTurn, analogActionData))
        return;

    float deltaYaw = 0.0f;
    if (m_SnapTurning) {
        if (!m_PressedTurn && analogActionData.x > 0.5f) {
            deltaYaw = -m_SnapTurnAngle;
            m_PressedTurn = true;
        } else if (!m_PressedTurn && analogActionData.x < -0.5f) {
            deltaYaw = m_SnapTurnAngle;
            m_PressedTurn = true;
        } else if (analogActionData.x > -0.3f && analogActionData.x < 0.3f) {
            m_PressedTurn = false;
        }
    } else {
        constexpr float deadzone = 0.2f;
        if (std::fabs(analogActionData.x) > deadzone) {
            const float normalized = (std::fabs(analogActionData.x) - deadzone) / (1.0f - deadzone);
            deltaYaw = -std::copysign(m_TurnSpeed * deltaTime * normalized, analogActionData.x);
        }
    }

    if (deltaYaw != 0.0f) {
        m_Playspace.yawDegrees = m_RotationOffset.y;
        m_Playspace.TurnAboutHmd(deltaYaw, m_HmdPose.TrackedDevicePos, m_LastEyeHeightUnits);
        m_RotationOffset.y = m_Playspace.yawDegrees;
        m_RotationOffset.y -= 360.0f * std::floor(m_RotationOffset.y / 360.0f);
        m_Playspace.yawDegrees = m_RotationOffset.y;
    }
}

void VR::ProcessInput()
{
    if (!m_IsVREnabled) {
        ReleaseHeldActions();
        return;
    }

    //vr::VROverlay()->SetOverlayFlag(m_HUDHandle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);

    ProcessHeldAction(m_ActionPrimaryAttack, m_PrimaryAttackState, "+attack", "-attack");
    ProcessHeldAction(m_ActionSecondaryAttack, m_SecondaryAttackState, "+attack2", "-attack2");
    ProcessHeldAction(m_ActionJump, m_JumpState, "+jump", "-jump");
    ProcessHeldAction(m_ActionCrouch, m_CrouchState, "+duck", "-duck");
    ProcessHeldAction(m_ActionUse, m_UseState, "+use", "-use");
    ProcessHeldAction(m_ActionReload, m_ReloadState, "+reload", "-reload");
    ProcessZoomInput();

    if (PressedDigitalAction(m_ActionPrevItem, true))
    {
        m_Game->ClientCmd_Unrestricted("invprev");
    }
    else if (PressedDigitalAction(m_ActionNextItem, true))
    {
        m_Game->ClientCmd_Unrestricted("invnext");
    }

    if (PressedDigitalAction(m_ActionFlashlight, true))
    {
        m_Game->ClientCmd_Unrestricted("impulse 100");
    }

    if (PressedDigitalAction(m_Spray, true))
    {
        m_Game->ClientCmd_Unrestricted("impulse 201");
    }
    
    /*bool isControllerVertical = m_RightControllerAngAbs.x > 60 || m_RightControllerAngAbs.x < -45;
    if ((PressedDigitalAction(m_ShowHUD) || PressedDigitalAction(m_Scoreboard) || isControllerVertical || m_HudAlwaysVisible)
        && m_RenderedHud)
    {
        if (!vr::VROverlay()->IsOverlayVisible(m_HUDHandle) || m_HudAlwaysVisible)
            RepositionOverlays();

        if (PressedDigitalAction(m_Scoreboard))
            m_Game->ClientCmd_Unrestricted("+showscores");
        else
            m_Game->ClientCmd_Unrestricted("-showscores");

        vr::VROverlay()->ShowOverlay(m_HUDHandle);
    }
    else
    {
        vr::VROverlay()->HideOverlay(m_HUDHandle);
    }*/

    m_RenderedHud = false;

    if (PressedDigitalAction(m_Pause, true))
    {
        m_MenuOverlayPlacement.Invalidate();
        m_Game->ClientCmd_Unrestricted("gameui_activate");
    }
}

VMatrix VR::VMatrixFromHmdMatrix(const vr::HmdMatrix34_t &hmdMat)
{
    // VMatrix has a different implicit coordinate system than HmdMatrix34_t, but this function does not convert between them
    VMatrix vMat(
        hmdMat.m[0][0], hmdMat.m[1][0], hmdMat.m[2][0], 0.0f,
        hmdMat.m[0][1], hmdMat.m[1][1], hmdMat.m[2][1], 0.0f,
        hmdMat.m[0][2], hmdMat.m[1][2], hmdMat.m[2][2], 0.0f,
        hmdMat.m[0][3], hmdMat.m[1][3], hmdMat.m[2][3], 1.0f
    );

    return vMat;
}

vr::HmdMatrix34_t VR::VMatrixToHmdMatrix(const VMatrix &vMat)
{
    vr::HmdMatrix34_t hmdMat = {0};

    hmdMat.m[0][0] = vMat.m[0][0];
    hmdMat.m[1][0] = vMat.m[0][1];
    hmdMat.m[2][0] = vMat.m[0][2];

    hmdMat.m[0][1] = vMat.m[1][0];
    hmdMat.m[1][1] = vMat.m[1][1];
    hmdMat.m[2][1] = vMat.m[1][2];

    hmdMat.m[0][2] = vMat.m[2][0];
    hmdMat.m[1][2] = vMat.m[2][1];
    hmdMat.m[2][2] = vMat.m[2][2];

    hmdMat.m[0][3] = vMat.m[3][0];
    hmdMat.m[1][3] = vMat.m[3][1];
    hmdMat.m[2][3] = vMat.m[3][2];

    return hmdMat;
}

vr::HmdMatrix34_t VR::GetControllerTipMatrix(vr::ETrackedControllerRole controllerRole)
{
    vr::VRInputValueHandle_t inputValue = vr::k_ulInvalidInputValueHandle;
    const auto deviceIndex = m_System->GetTrackedDeviceIndexForControllerRole(controllerRole);

    if (controllerRole == vr::TrackedControllerRole_RightHand)
    {
        m_Input->GetInputSourceHandle("/user/hand/right", &inputValue);
    }
    else if (controllerRole == vr::TrackedControllerRole_LeftHand)
    {
        m_Input->GetInputSourceHandle("/user/hand/left", &inputValue);
    }

    if (m_RenderModels && inputValue != vr::k_ulInvalidInputValueHandle &&
        IsUsableTrackedDeviceIndex(deviceIndex, vr::k_unMaxTrackedDeviceCount,
                                   vr::k_unTrackedDeviceIndexInvalid) && m_Poses[deviceIndex].bPoseIsValid)
    {
        char buffer[vr::k_unMaxPropertyStringSize]{};

        m_System->GetStringTrackedDeviceProperty(deviceIndex, vr::Prop_RenderModelName_String,
                                                 buffer, vr::k_unMaxPropertyStringSize);

        vr::RenderModel_ControllerMode_State_t controllerState = {0};
        vr::RenderModel_ComponentState_t componentState = {0};

        if (buffer[0] && m_RenderModels->GetComponentStateForDevicePath(buffer, vr::k_pch_Controller_Component_Tip, inputValue, &controllerState, &componentState))
        {
            return componentState.mTrackingToComponentLocal;
        }
    }

    // Not a hand controller role or tip lookup failed, return identity
    const vr::HmdMatrix34_t identity = 
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    return identity;
}

bool VR::CheckOverlayIntersectionForController(vr::VROverlayHandle_t overlayHandle, vr::ETrackedControllerRole controllerRole)
{
    vr::TrackedDeviceIndex_t deviceIndex = m_System->GetTrackedDeviceIndexForControllerRole(controllerRole);

    if (!IsUsableTrackedDeviceIndex(deviceIndex, vr::k_unMaxTrackedDeviceCount,
                                    vr::k_unTrackedDeviceIndexInvalid))
        return false;

    vr::TrackedDevicePose_t &controllerPose = m_Poses[deviceIndex];

    if (!controllerPose.bPoseIsValid)
        return false;

    VMatrix controllerVMatrix = VMatrixFromHmdMatrix(controllerPose.mDeviceToAbsoluteTracking);
    VMatrix tipVMatrix        = VMatrixFromHmdMatrix(GetControllerTipMatrix(controllerRole));
    tipVMatrix.MatrixMul(controllerVMatrix, controllerVMatrix);

    vr::VROverlayIntersectionParams_t  params  = {0};
    vr::VROverlayIntersectionResults_t results = {0};

    params.eOrigin    = vr::VRCompositor()->GetTrackingSpace();
    params.vSource    = { controllerVMatrix.m[3][0],  controllerVMatrix.m[3][1],  controllerVMatrix.m[3][2]};
    params.vDirection = {-controllerVMatrix.m[2][0], -controllerVMatrix.m[2][1], -controllerVMatrix.m[2][2]};

    return m_Overlay->ComputeOverlayIntersection(overlayHandle, &params, &results);
}

QAngle VR::GetRightControllerAbsAngle()
{
    return m_RightControllerAngAbs;
}

QAngle& VR::GetRightControllerAbsAngleConst()
{
    return m_RightControllerAngAbs;
}

Vector VR::GetRightControllerAbsPos()
{
    return TrackingSpace::ControllerWorldOrigin(m_SetupOrigin, m_RightControllerPosRel,
        GetHmdViewOffset(), m_6DOF);
}

Vector VR::GetRecommendedViewmodelAbsPos()
{
    Vector viewmodelPos = GetRightControllerAbsPos();
    viewmodelPos -= m_ViewmodelForward * m_ViewmodelPosOffset.x;
    viewmodelPos -= m_ViewmodelRight * m_ViewmodelPosOffset.y;
    viewmodelPos -= m_ViewmodelUp * m_ViewmodelPosOffset.z;

    return viewmodelPos;
}

QAngle VR::GetRecommendedViewmodelAbsAngle()
{
    QAngle result{};

    QAngle::VectorAngles(m_ViewmodelForward, m_ViewmodelUp, result);

    return result;
}

void VR::UpdateHMDAngles() {
    QAngle hmdAngLocal = m_HmdPose.TrackedDeviceAng;

    //hmdAngLocal += m_RotationOffset;
    hmdAngLocal.x += m_RotationOffset.x;
    hmdAngLocal.y += m_RotationOffset.y;
    hmdAngLocal.z += m_RotationOffset.z;

    //hmdAngLocal.Normalize();

    QAngle::AngleVectors(hmdAngLocal, &m_HmdForward, &m_HmdRight, &m_HmdUp);

    //hmdAngLocal.x = (hmdAngLocal.x > 180 ? 180)
    hmdAngLocal.Normalize();

    m_HmdAngAbs = hmdAngLocal;
}

void VR::ResetPosition()
{
    if (m_HmdPose.valid) {
        if (m_VRScale != m_Config.vrScale ||
            m_Playspace.heightOffsetMeters != m_Config.heightOffsetMeters) {
            m_VRScale = m_Config.vrScale;
            m_Playspace.scale = m_VRScale;
            m_Playspace.heightOffsetMeters = m_Config.heightOffsetMeters;
            Logger::Write("Applied staged VRScale/HeightOffsetMeters at recenter");
        }
        m_Playspace.Recenter(m_HmdPose.TrackedDevicePos);
        m_PhysicalCrouch.Recalibrate();
        m_Center = m_HmdPose.TrackedDevicePos;
        if (m_Playspace.mode == TrackingSpace::TrackingMode::Standing && m_6DOF) {
            if (const auto anchor = TrackingSpace::StandingEyeAnchorUnits(
                    m_HmdPose.TrackedDevicePos.z, m_VRScale)) {
                m_LastEyeHeightUnits = *anchor;
                m_HasEyeHeight = true;
                m_EyeHeightWasInvalid = false;
                Logger::Write("Standing height reanchored to tracked HMD; Source avatar eye offset unverified");
            }
        }
        m_HmdLostSinceLastValid = false;
        ResetRoomscale(true);
        if (ExperimentalPortalOrientation()) {
            const Vector baseOffset = m_Playspace.HmdOffsetUnits(
                m_HmdPose.TrackedDevicePos, m_LastEyeHeightUnits);
            m_PortalRigAnchor.Reanchor(baseOffset, baseOffset);
            m_PortalCoordinator.CancelPending();
        }
    }
}

float VR::UpdatePhysicalCrouch(C_BasePlayer *player)
{
    constexpr int kDucking = 1 << 1;  // FL_DUCKING
    // A dip deeper than the first value that ended above the second one is logged as a near miss.
    constexpr float kNearMissDropMeters = 0.12f;
    constexpr float kDipEndedDropMeters = 0.08f;
    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = m_PhysicalCrouchLastUpdate == std::chrono::steady_clock::time_point{} ? 0.0f :
        std::min(std::chrono::duration<float>(now - m_PhysicalCrouchLastUpdate).count(), 0.1f);
    m_PhysicalCrouchLastUpdate = now;

    const auto &fields = m_Game->m_Offsets->m_PlayerFields;
    if (!m_Config.physicalCrouch || !m_6DOF || !fields.viewOffsetZ || !fields.flags || !player) {
        m_PhysicalCrouch.Reset();
        return 0.0f;
    }
    const bool eligible = m_Game->m_EngineClient->IsInGame() && !m_Game->m_VguiSurface->IsCursorVisible();
    const bool wasDucking = m_PhysicalCrouch.Ducking();
    const float headMeters = m_HmdPose.TrackedDevicePos.z;
    const float headDrop = eligible ? m_PhysicalCrouch.UpdateHeadDrop(headMeters, deltaSeconds) : 0.0f;
    const bool ducking = m_PhysicalCrouch.UpdateIntent(eligible, headDrop,
        m_Config.physicalCrouchDropMeters, deltaSeconds);

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(player);
    const float viewOffsetZ = *reinterpret_cast<const float *>(bytes + fields.viewOffsetZ);
    const int flags = *reinterpret_cast<const int *>(bytes + fields.flags);

    if (std::isfinite(headDrop))
        m_PhysicalCrouchPeakDrop = std::max(m_PhysicalCrouchPeakDrop, headDrop);
    if (ducking != wasDucking) {
        if (ducking) {
            m_PhysicalCrouchDuckTime = now;
            m_PhysicalCrouchDuckedThisDip = true;
        } else {
            m_PhysicalCrouchSettleTime = now;
            m_PhysicalCrouchMaxEyeStep = 0.0f;
        }
        Logger::Write(std::string(ducking ? "Physical crouch: ducking" : "Physical crouch: standing") +
            " head=" + std::to_string(headMeters) + " standing=" + std::to_string(m_PhysicalCrouch.StandingHead()) +
            " drop=" + std::to_string(headDrop) + " peakDrop=" + std::to_string(m_PhysicalCrouchPeakDrop) +
            " threshold=" + std::to_string(m_Config.physicalCrouchDropMeters) +
            " eyeZ=" + std::to_string(viewOffsetZ) + " flags=" + std::to_string(flags));
    }
    const bool sourceDucked = (flags & kDucking) != 0;
    if (sourceDucked != m_PhysicalCrouchSourceDucked) {
        m_PhysicalCrouchSourceDucked = sourceDucked;
        Logger::Write(std::string("Physical crouch: Source ") + (sourceDucked ? "ducked" : "unducked") +
            " " + std::to_string(std::chrono::duration<float>(now - m_PhysicalCrouchDuckTime).count()) +
            "s after intent; holding=" + std::to_string(ducking) + " eyeZ=" + std::to_string(viewOffsetZ));
    }
    if (eligible && headDrop < kDipEndedDropMeters) {
        if (!m_PhysicalCrouchDuckedThisDip && m_PhysicalCrouchPeakDrop >= kNearMissDropMeters)
            Logger::Write("Physical crouch: near miss peakDrop=" + std::to_string(m_PhysicalCrouchPeakDrop) +
                " threshold=" + std::to_string(m_Config.physicalCrouchDropMeters) +
                " standing=" + std::to_string(m_PhysicalCrouch.StandingHead()));
        m_PhysicalCrouchPeakDrop = 0.0f;
        m_PhysicalCrouchDuckedThisDip = false;
    }

    m_PhysicalCrouchHeadOffset = m_HmdPosRelative.z;
    return m_PhysicalCrouch.Compensation();
}

// Called from RenderView, not UpdateTracking: VR::Update runs after the previous frame, and
// the compensation has to match the interpolated view offset this frame renders with.
void VR::UpdatePhysicalCrouchCamera()
{
    constexpr int kOnGround = 1 << 0; // FL_ONGROUND
    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds = m_PhysicalCrouchCameraUpdate == std::chrono::steady_clock::time_point{} ? 0.0f :
        std::min(std::chrono::duration<float>(now - m_PhysicalCrouchCameraUpdate).count(), 0.1f);
    m_PhysicalCrouchCameraUpdate = now;

    const auto &fields = m_Game->m_Offsets->m_PlayerFields;
    const int index = m_Game->m_EngineClient->GetLocalPlayer();
    auto *player = index > 0 ? (C_BasePlayer*)m_Game->GetClientEntity(index) : nullptr;
    if (!m_Config.physicalCrouch || !m_6DOF || !m_TrackingOutputValid || !fields.viewOffsetZ ||
        !fields.flags || !player || player != m_EyeHeightPlayerEntity)
        return;

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(player);
    const float viewOffsetZ = *reinterpret_cast<const float *>(bytes + fields.viewOffsetZ);
    const int flags = *reinterpret_cast<const int *>(bytes + fields.flags);
    const bool wasSettling = m_PhysicalCrouch.CameraSettling();
    const float previous = m_PhysicalCrouch.Compensation();
    const float compensation = m_PhysicalCrouch.UpdateCompensation(viewOffsetZ, m_PhysicalCrouchHeadOffset,
        m_Config.physicalCrouchDropMeters * m_VRScale, (flags & kOnGround) != 0, deltaSeconds);
    m_HmdPosRelative.z += compensation - previous;

    const float cameraEye = viewOffsetZ + compensation;
    if (m_PhysicalCrouch.CameraSettling()) {
        m_PhysicalCrouchMaxEyeStep = std::max(m_PhysicalCrouchMaxEyeStep,
            std::fabs(cameraEye - m_PhysicalCrouchLastEye));
    } else if (wasSettling) {
        Logger::Write("Physical crouch: camera settled in " +
            std::to_string(std::chrono::duration<float>(now - m_PhysicalCrouchSettleTime).count()) +
            "s maxFrameStep=" + std::to_string(m_PhysicalCrouchMaxEyeStep) + " units");
    }
    m_PhysicalCrouchLastEye = cameraEye;
}

Vector VR::GetMovementForward()
{
    const TrackingSpace::DeviceDirection hmd{m_TrackingOutputValid, m_HmdForward};
    const TrackingSpace::DeviceDirection left{m_LeftControllerOutputValid, m_LeftControllerForward};
    const TrackingSpace::DeviceDirection right{m_RightControllerPose.valid, m_RightControllerForward};
    bool fallback = false;
    if (m_Config.movementDirection == TrackingSpace::MovementDirection::LeftController)
        fallback = !TrackingSpace::HorizontalDirection(left).has_value();
    else if (m_Config.movementDirection == TrackingSpace::MovementDirection::RightController)
        fallback = !TrackingSpace::HorizontalDirection(right).has_value();
    if (fallback != m_MovementFallbackActive) {
        Logger::Write(fallback ? "Movement direction: controller unavailable; using HMD" :
                                 "Movement direction: configured controller available");
        m_MovementFallbackActive = fallback;
    }
    return TrackingSpace::SelectMovementForward(m_Config.movementDirection, hmd, left, right);
}

void VR::UpdateTracking()
{
    if (!m_HmdPose.valid || !m_RightControllerPose.valid ||
        !m_Game->m_EngineClient->IsInGame() || m_Game->m_VguiSurface->IsCursorVisible())
        ResetMuzzleSample();
    m_TrackingOutputValid = false;
    m_LeftControllerOutputValid = false;
    m_LeftControllerPosRel = {0.0f, 0.0f, 0.0f};
    m_RightControllerPosRel = {0.0f, 0.0f, 0.0f};

    if (m_Config.roomscaleMode == RoomscaleMotion::Mode::ActiveExperimental) {
        if (!m_Game->m_EngineClient->IsInGame())
            ResetRoomscale(false, true);
        else if (m_Game->m_VguiSurface->IsCursorVisible())
            m_RoomscaleMotor.Reset();
    }

    if (ExperimentalPortalOrientation() && !m_Game->m_EngineClient->IsInGame())
        ResetPortalOrientation();

    if (!m_HmdPose.valid) {
        m_RoomscaleMotor.Reset();
        m_PortalCoordinator.CancelPending();
        if (m_RoomscaleObserver.OnPose(false, {}, m_PoseFetchSequence, 0.0f, 1.0f, true) ==
            RoomscaleMotion::Observation::TrackingLost) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= m_NextRoomscaleTrackingLog) {
                Logger::Write("Roomscale observe: HMD tracking lost; pending physical intent discarded");
                m_NextRoomscaleTrackingLog = now + std::chrono::seconds(5);
            }
        }
        m_HmdLostSinceLastValid = true;
        if (m_Game->m_Offsets->m_LaserAvailable) {
            const int index = m_Game->m_EngineClient->GetLocalPlayer();
            C_Portal_Player* player = (C_Portal_Player*)m_Game->GetClientEntity(index);
            if (player && player->m_PointLaser) {
                player->m_PointLaser->StopEmission(false, true, false);
                player->m_PointLaser = NULL;
            }
        }
        return;
    }

    const int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
    C_BasePlayer* localPlayer = playerIndex > 0 ?
        (C_BasePlayer*)m_Game->GetClientEntity(playerIndex) : nullptr;
    if (!localPlayer) {
        ResetMuzzleSample();
        ResetRoomscale(false, true);
        ResetPortalOrientation();
        m_PhysicalCrouch.Reset();
        m_EyeHeightPlayerEntity = nullptr;
        m_HasEyeHeight = false;
        m_HasLastHmdOffset = false;
        return;
    }

    if (playerIndex != m_EyeHeightPlayerIndex || localPlayer != m_EyeHeightPlayerEntity) {
        ResetRoomscale(false, true);
        ResetPortalOrientation();
        m_PhysicalCrouch.Reset();
        m_EyeHeightPlayerIndex = playerIndex;
        m_EyeHeightPlayerEntity = localPlayer;
        m_HasEyeHeight = false;
        m_EyeHeightWasInvalid = false;
        m_HasLastHmdOffset = false;
    }
    if (m_Playspace.mode == TrackingSpace::TrackingMode::Standing && m_6DOF && !m_HasEyeHeight) {
        const auto anchor = TrackingSpace::StandingEyeAnchorUnits(
            m_HmdPose.TrackedDevicePos.z, m_VRScale);
        if (anchor) {
            m_LastEyeHeightUnits = *anchor;
            m_HasEyeHeight = true;
            Logger::Write("Standing height anchored to tracked HMD at player entry; Source avatar eye offset unverified");
            m_EyeHeightWasInvalid = false;
        } else {
            if (!m_EyeHeightWasInvalid)
                Logger::Write("Standing height unavailable: invalid HMD floor height; deferring standing view");
            m_EyeHeightWasInvalid = true;
            ResetRoomscale();
            return;
        }
    }

    // HMD tracking
    Vector hmdPosLocal = m_HmdPose.TrackedDevicePos;
    Vector hmdPosCentered = hmdPosLocal - m_Playspace.centerMeters;

    m_HmdPosRelativeRaw = hmdPosCentered;

    //std::cout << "HMD - X: " << hmdWorldPos.x << ", Y: " << hmdWorldPos.y << ", Z: " << hmdWorldPos.z << "\n";

    m_Playspace.yawDegrees = m_RotationOffset.y;
    m_Playspace.scale = m_VRScale;
    if (m_HmdLostSinceLastValid) {
        if (m_HasLastHmdOffset) {
            m_Playspace.PreserveOffsetOnRecovery(hmdPosLocal, m_LastHmdOffsetUnits,
                                                  m_LastEyeHeightUnits);
            Logger::Write("HMD tracking recovered; preserving previous view offset until recenter");
        }
        m_HmdLostSinceLastValid = false;
    }
    UpdateHMDAngles();

    m_HmdPosRelative = m_Playspace.HmdOffsetUnits(hmdPosLocal, m_LastEyeHeightUnits);
    m_LastHmdOffsetUnits = m_HmdPosRelative;
    m_HasLastHmdOffset = true;
    m_HmdPosRelative.z += UpdatePhysicalCrouch(localPlayer);
    m_TrackingOutputValid = true;
    const auto roomscaleStatus = m_RoomscaleObserver.OnPose(
        true, hmdPosLocal, m_PoseFetchSequence, m_Playspace.yawDegrees, m_Playspace.scale,
        m_Game->m_EngineClient->IsInGame() && !m_Game->m_VguiSurface->IsCursorVisible());
    if (roomscaleStatus == RoomscaleMotion::Observation::MappingChanged ||
        roomscaleStatus == RoomscaleMotion::Observation::Discontinuity ||
        roomscaleStatus == RoomscaleMotion::Observation::InvalidSample)
        m_RoomscaleMotor.Reset();
    if (roomscaleStatus == RoomscaleMotion::Observation::TrackingRecovered) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_NextRoomscaleTrackingLog) {
            Logger::Write("Roomscale observe: HMD tracking recovered; using fresh movement baseline");
            m_NextRoomscaleTrackingLog = now + std::chrono::seconds(5);
        }
    }
    else if (roomscaleStatus == RoomscaleMotion::Observation::Discontinuity ||
             roomscaleStatus == RoomscaleMotion::Observation::InvalidSample) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_NextRoomscaleAnomalyLog) {
            Logger::Write(roomscaleStatus == RoomscaleMotion::Observation::Discontinuity ?
                "Roomscale observe: pose discontinuity; pending physical intent discarded" :
                "Roomscale observe: invalid pose/mapping; physical intent discarded");
            m_NextRoomscaleAnomalyLog = now + std::chrono::seconds(5);
        }
    }

    if ((!m_RightControllerPose.valid || m_AimMode != 2) &&
        m_Game->m_Offsets->m_LaserAvailable) {
        C_Portal_Player* portalPlayer = (C_Portal_Player*)localPlayer;
        if (portalPlayer->m_PointLaser) {
            portalPlayer->m_PointLaser->StopEmission(false, true, false);
            portalPlayer->m_PointLaser = NULL;
        }
    }

    // Check if camera is clipping inside wall
    /*CGameTrace trace;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    Vector extendedHmdPos = m_HmdPosAbs - m_SetupOrigin;
    VectorNormalize(extendedHmdPos);
    extendedHmdPos = m_HmdPosAbs + (extendedHmdPos * 10);
    ray.Init(m_SetupOrigin, extendedHmdPos);

    m_Game->m_EngineTrace->TraceRay(ray, STANDARD_TRACE_MASK, &tracefilter, &trace);
    if (trace.fraction < 1 && trace.fraction > 0)
    {
        Vector distanceInsideWall = trace.endpos - extendedHmdPos;
        m_CameraAnchor += distanceInsideWall;
        m_HmdPosAbs = m_CameraAnchor - Vector(0, 0, 64) + m_HmdPosLocalInWorld;
    }

    // Reset camera if it somehow gets too far
    m_SetupOriginToHMD = m_HmdPosAbs - m_SetupOrigin;
    if (VectorLength(m_SetupOriginToHMD) > 150)
        ResetPosition();

    m_HmdPosAbsPrev = m_HmdPosAbs;
    m_SetupOriginPrev = m_SetupOrigin;*/

    GetViewParameters();
    m_Ipd = m_EyeToHeadTransformPosRight.x * 2;
    m_EyeZ = m_EyeToHeadTransformPosRight.z;

    // Hand tracking
    if (const auto leftOffset = m_Playspace.ControllerRelativeOffsetUnits(
            m_LeftControllerPose.valid, m_LeftControllerPose.TrackedDevicePos,
            hmdPosLocal, m_LastEyeHeightUnits)) {
        m_LeftControllerPosRel = *leftOffset;
        m_LeftControllerOutputValid = true;
        QAngle leftControllerAng = m_LeftControllerPose.TrackedDeviceAng;
        leftControllerAng.x += m_RotationOffset.x;
        leftControllerAng.y += m_RotationOffset.y;
        leftControllerAng.z += m_RotationOffset.z;
        QAngle::AngleVectors(leftControllerAng, &m_LeftControllerForward,
                             &m_LeftControllerRight, &m_LeftControllerUp);
        m_LeftControllerForward = VectorRotate(m_LeftControllerForward, m_LeftControllerRight,
                                                m_Config.controllerPitchDegrees);
        m_LeftControllerUp = VectorRotate(m_LeftControllerUp, m_LeftControllerRight,
                                           m_Config.controllerPitchDegrees);
        QAngle::VectorAngles(m_LeftControllerForward, m_LeftControllerUp, m_LeftControllerAngAbs);
    }

    const auto rightOffset = m_Playspace.ControllerRelativeOffsetUnits(
        m_RightControllerPose.valid, m_RightControllerPose.TrackedDevicePos,
        hmdPosLocal, m_LastEyeHeightUnits);
    if (!rightOffset) {
        ApplyPortalRigToDerivedPose();
        return;
    }

    QAngle rightControllerAngLocal = m_RightControllerPose.TrackedDeviceAng;

    m_RightControllerPosRel = *rightOffset;

    //rightControllerAngLocal += m_RotationOffset;
    rightControllerAngLocal.x += m_RotationOffset.x;
    rightControllerAngLocal.y += m_RotationOffset.y;
    rightControllerAngLocal.z += m_RotationOffset.z;

    // Wrap angle from -180 to 180
    //rightControllerAngLocal.Normalize();

    QAngle::AngleVectors(rightControllerAngLocal, &m_RightControllerForward, &m_RightControllerRight, &m_RightControllerUp);

    const float offset = m_Config.controllerPitchDegrees;

    // Adjust controller angle downward
    m_RightControllerForward = VectorRotate(m_RightControllerForward, m_RightControllerRight, offset);
    m_RightControllerUp = VectorRotate(m_RightControllerUp, m_RightControllerRight, offset);

    // controller angles
    QAngle::VectorAngles(m_RightControllerForward, m_RightControllerUp, m_RightControllerAngAbs);
    m_RightControllerAngAbs.Normalize();

    // Apply both hardcoded and custom (from config) viewmodel offsets here:
    m_ViewmodelPosOffset = ViewmodelBasePositionOffset(m_Config.experimentalViewmodelAlignment) +
        m_ViewmodelPosCustomOffset;
    m_ViewmodelAngOffset = m_ViewmodelAngCustomOffset;

    m_ViewmodelForward = m_RightControllerForward;
    m_ViewmodelUp = m_RightControllerUp;
    m_ViewmodelRight = m_RightControllerRight;

    // Viewmodel yaw offset
    m_ViewmodelForward = VectorRotate(m_ViewmodelForward, m_ViewmodelUp, m_ViewmodelAngOffset.y);
    m_ViewmodelRight = VectorRotate(m_ViewmodelRight, m_ViewmodelUp, m_ViewmodelAngOffset.y);

    // Viewmodel pitch offset
    m_ViewmodelForward = VectorRotate(m_ViewmodelForward, m_ViewmodelRight, m_ViewmodelAngOffset.x);
    m_ViewmodelUp = VectorRotate(m_ViewmodelUp, m_ViewmodelRight, m_ViewmodelAngOffset.x);

    // Viewmodel roll offset
    m_ViewmodelRight = VectorRotate(m_ViewmodelRight, m_ViewmodelForward, m_ViewmodelAngOffset.z);
    m_ViewmodelUp = VectorRotate(m_ViewmodelUp, m_ViewmodelForward, m_ViewmodelAngOffset.z);

    ApplyPortalRigToDerivedPose();
    if (!RoomscaleEnabled() || !RoomscaleEligible())
        UpdateAimFeedback(localPlayer);
}

void VR::ResetMuzzleSample()
{
    m_MuzzleSample.Reset();
    Hooks::m_NativeBeamRenderProbe.SetEffect(0);
}

void VR::CaptureViewmodelMuzzle(const Vector &world, const Vector &modelOrigin, const QAngle &modelAngles)
{
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (m_MuzzleSample.Capture(world, modelOrigin, modelAngles, now) &&
        !m_MuzzleSampleLogged.exchange(true)) {
        Logger::Write("Native viewmodel muzzle sampled: world=" + std::to_string(world.x) + "," +
            std::to_string(world.y) + "," + std::to_string(world.z) +
            "; model-local sample rebased each aim update; hardware alignment unverified");
    }
}

std::optional<Vector> VR::GetAimBeamOrigin()
{
    if (!m_Config.aimFromViewmodelMuzzle)
        return GetRightControllerAbsPos();
    std::optional<Vector> origin;
    if (Hooks::CanAlignViewmodel() && m_Game->m_Hooks->m_MuzzleSamplingReady) {
        const double now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        origin = m_MuzzleSample.Origin(GetRecommendedViewmodelAbsPos(), GetRecommendedViewmodelAbsAngle(),
                                      now);
    }
    if (!origin && !m_MuzzleWaitingLogged) {
        Logger::Write("Aim beam waiting for valid local viewmodel muzzle sample; controller-origin fallback disabled");
        m_MuzzleWaitingLogged = true;
    }
    return origin;
}

void VR::UpdateAimFeedback(C_BasePlayer *localPlayer)
{
    Hooks::m_NativeBeamRenderProbe.SetEffect(0);
    if (!localPlayer || !m_TrackingOutputValid || !m_RightControllerPose.valid) {
        ResetMuzzleSample();
        return;
    }
    bool aimTraceHit = false;
    m_AimPos = Trace((uint32_t*)localPlayer, aimTraceHit);
    if (AimFeedback::ShouldInspectActiveWeaponForAim(
            m_AimMode, m_Game->m_Offsets->m_LaserAvailable,
            m_Config.experimentalWorldAimMarker, m_Game->m_DebugOverlay != nullptr)) {
        C_Portal_Player* portalPlayer = (C_Portal_Player*)localPlayer;
        auto activeWeaponAddr = (*(int(__thiscall**)(void*))(*(uintptr_t*)portalPlayer + 968))(portalPlayer);
        const auto playerKey = reinterpret_cast<std::uintptr_t>(localPlayer);
        const auto weaponKey = static_cast<std::uintptr_t>(activeWeaponAddr);
        if (m_Game->m_VguiSurface->IsCursorVisible())
            m_MuzzleSample.Reset();
        else
            m_MuzzleSample.SelectIdentity(playerKey, weaponKey);
        const auto beamOrigin = GetAimBeamOrigin();
        const bool worldMarker = AimFeedback::ShouldUseWorldAimMarker(
            m_AimMode, m_Config.experimentalWorldAimMarker,
            m_Game->m_DebugOverlay != nullptr, m_RightControllerPose.valid,
            activeWeaponAddr != 0, m_Game->m_VguiSurface->IsCursorVisible());
        if (worldMarker && beamOrigin) {
            const auto geometry = AimFeedback::PrepareWorldAimGeometry(
                *beamOrigin, m_AimPos);
            if (geometry) {
                // The beam is only a pointing aid. Source owns the reticle
                // artwork and status; do not draw a competing impact plus.
                constexpr int r = 64, g = 200, b = 255;
                const auto now = std::chrono::steady_clock::now();
                const float frameInterval = m_LastWorldAimMarkerUpdate ==
                    std::chrono::steady_clock::time_point{} ? 0.0f :
                    std::chrono::duration<float>(now - m_LastWorldAimMarkerUpdate).count();
                const float lifetime = AimFeedback::WorldAimOverlayLifetime(frameInterval);
                m_LastWorldAimMarkerUpdate = now;
                m_Game->m_DebugOverlay->AddLineOverlay(*beamOrigin, geometry->beamEnd,
                    r, g, b, false, lifetime);
                if (!m_WorldAimMarkerLogged) {
                    Logger::Write("Experimental world aim marker submitted to Source debug overlay; "
                        "actual stereo visibility and shot alignment require VR testing");
                    m_WorldAimMarkerLogged = true;
                }
                if (!m_WorldAimMarkerCadenceLogged && frameInterval >= 0.005f) {
                    Logger::Write("Experimental world aim marker cadence: updateInterval=" +
                        std::to_string(frameInterval) + "s overlayLifetime=" +
                        std::to_string(lifetime) + "s (not HMD refresh rate)");
                    m_WorldAimMarkerCadenceLogged = true;
                }
            }
        }
        const bool requestLaser = !worldMarker && beamOrigin && AimFeedback::ShouldRequestLaser(
            m_AimMode, m_Game->m_Offsets->m_LaserAvailable,
            m_RightControllerPose.valid, activeWeaponAddr != 0,
            m_Game->m_VguiSurface->IsCursorVisible());
        if (requestLaser) {
            CWeaponPortalBase* activeWeapon = (CWeaponPortalBase*)activeWeaponAddr;
            if (!portalPlayer->m_PointLaser) {
                if (!m_LaserRequestLogged) {
                    Logger::Write("Controller laser creation requested; crosshair paint state is no longer a prerequisite");
                    m_LaserRequestLogged = true;
                }
                m_Game->m_Hooks->CreateNativeAimPointer(localPlayer, m_AimPos);
            }
            // Apply control points on the creation frame too. Source may use a
            // player-owned ABSORIGIN branch: the native updater skips CP0 after
            // initialization instead of replacing it with EyePosition each frame.
            if (portalPlayer->m_PointLaser) {
                Hooks::m_NativeBeamRenderProbe.SetEffect(
                    reinterpret_cast<std::uintptr_t>(portalPlayer->m_PointLaser));
                if (!m_LaserParticleObserved) {
                    Logger::Write("Controller laser particle pointer observed; actual visibility unverified");
                    m_LaserParticleObserved = true;
                }
                const auto portalColor = NativeBeam::ControlPointColor(activeWeapon->m_iLastFiredPortal,
                    m_Game->m_singlePlayerPortalColors);
                portalPlayer->m_PointLaser->SetControlPoint(0, *beamOrigin);
                portalPlayer->m_PointLaser->SetControlPoint(1, m_AimPos);
                portalPlayer->m_PointLaser->SetControlPoint(2, portalColor);
                if (m_RenderDiagnostics.First(RenderDiagnosticEvent::LaserControlPoints)) {
                    Logger::Write(std::string("Controller laser control points updated: originMode=") +
                        (m_Config.aimFromViewmodelMuzzle ? "viewmodelMuzzle" : "controller") + " originCP0=" +
                        std::to_string(beamOrigin->x) + "," + std::to_string(beamOrigin->y) +
                        "," + std::to_string(beamOrigin->z) + " targetCP1=" +
                        std::to_string(m_AimPos.x) + "," + std::to_string(m_AimPos.y) +
                        "," + std::to_string(m_AimPos.z) +
                        " colorCP2=" + std::to_string(portalColor.x) + "," +
                        std::to_string(portalColor.y) + "," + std::to_string(portalColor.z) +
                        "; originCP0 and targetCP1 updated; actual visibility unverified");
                }
            }
        } else if (m_Game->m_Offsets->m_LaserAvailable && portalPlayer->m_PointLaser) {
            portalPlayer->m_PointLaser->StopEmission(false, true, false);
            portalPlayer->m_PointLaser = NULL;
        }
    } else {
        ResetMuzzleSample();
    }
}

void VR::ObserveRoomscaleCommand(int commandNumber)
{
    if (m_Config.roomscaleMode == RoomscaleMotion::Mode::Off)
        return;
    if (!m_Game->m_EngineClient->IsInGame()) {
        m_RoomscaleObserver.Reset(true);
        return;
    }
    const int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
    const bool gameplayEligible = m_IsVREnabled && m_HmdPose.valid &&
        m_Game->m_EngineClient->IsInGame() && !m_Game->m_VguiSurface->IsCursorVisible() &&
        playerIndex > 0 && m_Game->GetClientEntity(playerIndex) != nullptr;
    (void)m_RoomscaleObserver.OnCommand(commandNumber, gameplayEligible);
}

bool VR::RoomscaleEnabled() const
{
    return m_Config.roomscaleMode == RoomscaleMotion::Mode::ActiveExperimental &&
        m_6DOF && !ExperimentalPortalOrientation();
}

bool VR::RoomscaleEligible() const
{
    const int index = m_Game->m_EngineClient->GetLocalPlayer();
    return RoomscaleMotion::Eligibility{
        m_6DOF, m_IsVREnabled && m_TrackingOutputValid && m_HmdPose.valid,
        !ExperimentalPortalOrientation(), m_Game->m_EngineClient->IsInGame(),
        m_Game->m_VguiSurface->IsCursorVisible(),
        index > 0 && m_Game->GetClientEntity(index) != nullptr}.Allowed();
}

void VR::ResetRoomscale(bool recenter, bool newCommandStream)
{
    m_RoomscaleObserver.Reset(newCommandStream);
    m_RoomscaleMotor.Reset(recenter, newCommandStream);
}

Vector VR::GetHmdViewOffset()
{
    return RoomscaleEnabled() ? m_RoomscaleMotor.ViewOffset(m_HmdPosRelative) : m_HmdPosRelative;
}

void VR::UpdateRoomscaleRenderAnchor(const Vector &sourceAnchor)
{
    if (m_Config.roomscaleMode != RoomscaleMotion::Mode::ActiveExperimental)
        return;
    const bool eligible = RoomscaleEligible();
    const auto now = std::chrono::steady_clock::now();
    if (!m_LastRoomscaleEligibility || *m_LastRoomscaleEligibility != eligible) {
        if (now >= m_NextRoomscaleEligibilityLog) {
            Logger::Write(eligible ?
                "Roomscale active experimental: render feedback ready; view-anchor proxy, collision behavior unverified" :
                "Roomscale active experimental suspended: requires 6DOF, LegacyYaw, valid HMD and local gameplay without cursor");
            m_NextRoomscaleEligibilityLog = now + std::chrono::seconds(5);
        }
        m_LastRoomscaleEligibility = eligible;
    }
    if (!eligible) {
        m_RoomscaleMotor.Reset(false, !m_Game->m_EngineClient->IsInGame());
        return;
    }
    const int index = m_Game->m_EngineClient->GetLocalPlayer();
    auto *player = (C_BasePlayer*)m_Game->GetClientEntity(index);
    m_SetupOrigin = sourceAnchor;
    (void)m_RoomscaleMotor.OnRender(sourceAnchor, m_HmdPosRelative,
        m_PoseFetchSequence, reinterpret_cast<std::uintptr_t>(player), m_VRScale, now);
    // Rebuild the beam/trace after compensation, before either eye uses it.
    UpdateAimFeedback(player);
}

std::optional<TrackingSpace::MoveAxes> VR::GetRoomscaleCommand(int commandNumber, bool manualMovement)
{
    if (m_Config.roomscaleMode != RoomscaleMotion::Mode::ActiveExperimental)
        return std::nullopt;
    if (!RoomscaleEligible()) {
        m_RoomscaleMotor.Reset(false, !m_Game->m_EngineClient->IsInGame());
        return std::nullopt;
    }
    return m_RoomscaleMotor.OnCommand(commandNumber, m_HmdForward, manualMovement,
                                     std::chrono::steady_clock::now());
}

bool VR::ExperimentalPortalOrientation() const
{
    return m_ActivePortalMode != PortalOrientation::Mode::LegacyYaw;
}

void VR::QueuePortalTraversal(std::uintptr_t playerKey, std::uintptr_t portalKey,
                              const std::optional<PortalOrientation::Rotation> &rotation)
{
    if (!ExperimentalPortalOrientation())
        return;
    if (!rotation) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_NextPortalEventLog) {
            Logger::Write("Experimental portal orientation: invalid portal transform; event ignored");
            m_NextPortalEventLog = now + std::chrono::seconds(5);
        }
        return;
    }
    const auto queued = m_PortalCoordinator.Queue(playerKey, portalKey, *rotation);
    if (queued == PortalOrientation::QueueResult::Queued)
        Logger::Write("Experimental portal orientation: local crossing queued");
    else if (queued == PortalOrientation::QueueResult::Full) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_NextPortalEventLog) {
            Logger::Write("Experimental portal orientation: event queue full; crossing ignored");
            m_NextPortalEventLog = now + std::chrono::seconds(5);
        }
    }
}

void VR::ResetPortalOrientation()
{
    m_PortalCoordinator.Reset();
    m_PortalRigAnchor.Reset();
    m_PortalEffectiveRotation = PortalOrientation::Rotation::Identity();
}

void VR::ApplyPendingPortalOrientation(const Vector &renderOrigin)
{
    if (!ExperimentalPortalOrientation())
        return;
    if (!m_Game->m_EngineClient->IsInGame()) {
        ResetPortalOrientation();
        m_TrackingOutputValid = false;
        return;
    }
    const int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
    C_BasePlayer* player = playerIndex > 0 ?
        (C_BasePlayer*)m_Game->GetClientEntity(playerIndex) : nullptr;
    if (!player) {
        ResetPortalOrientation();
        m_TrackingOutputValid = false;
        return;
    }
    if (playerIndex != m_EyeHeightPlayerIndex || player != m_EyeHeightPlayerEntity) {
        ResetPortalOrientation();
        m_SetupOrigin = renderOrigin;
        UpdateTracking();
        return;
    }
    if (!m_HmdPose.valid || !m_TrackingOutputValid ||
        m_Game->m_VguiSurface->IsCursorVisible()) {
        m_PortalCoordinator.CancelPending();
        return;
    }
    const auto frame = m_PortalCoordinator.Drain(reinterpret_cast<std::uintptr_t>(player));
    if (frame.playerChanged) {
        ResetPortalOrientation();
        m_SetupOrigin = renderOrigin;
        UpdateTracking();
        return;
    }
    if (!frame.applied)
        return;
    const Vector baseOffset = m_Playspace.HmdOffsetUnits(
        m_HmdPose.TrackedDevicePos, m_LastEyeHeightUnits);
    m_PortalRigAnchor.Reanchor(baseOffset, m_HmdPosRelative);
    m_PortalEffectiveRotation = frame.effective;
    ResetRoomscale();
    m_SetupOrigin = renderOrigin; // traces rebuilt below must use the teleported render origin
    UpdateTracking(); // rebuild head and hands from the same pose before either eye is rendered
    Logger::Write("Experimental portal orientation: applied " +
        std::to_string(frame.applied) + " crossing(s) before stereo render");
}

void VR::ApplyPortalRigToDerivedPose()
{
    if (!ExperimentalPortalOrientation() || !m_TrackingOutputValid)
        return;
    const auto &rotation = m_PortalEffectiveRotation;
    m_HmdPosRelative = m_PortalRigAnchor.MapHmd(m_HmdPosRelative, rotation);
    m_HmdForward = rotation.Rotate(m_HmdForward);
    m_HmdRight = rotation.Rotate(m_HmdRight);
    m_HmdUp = rotation.Rotate(m_HmdUp);
    QAngle::VectorAngles(m_HmdForward, m_HmdUp, m_HmdAngAbs);
    m_HmdAngAbs.Normalize();
    if (m_LeftControllerOutputValid) {
        m_LeftControllerPosRel = m_PortalRigAnchor.MapRelative(m_LeftControllerPosRel, rotation);
        m_LeftControllerForward = rotation.Rotate(m_LeftControllerForward);
        m_LeftControllerRight = rotation.Rotate(m_LeftControllerRight);
        m_LeftControllerUp = rotation.Rotate(m_LeftControllerUp);
        QAngle::VectorAngles(m_LeftControllerForward, m_LeftControllerUp,
                             m_LeftControllerAngAbs);
        m_LeftControllerAngAbs.Normalize();
    }
    if (m_RightControllerPose.valid) {
        m_RightControllerPosRel = m_PortalRigAnchor.MapRelative(m_RightControllerPosRel, rotation);
        m_RightControllerForward = rotation.Rotate(m_RightControllerForward);
        m_RightControllerRight = rotation.Rotate(m_RightControllerRight);
        m_RightControllerUp = rotation.Rotate(m_RightControllerUp);
        QAngle::VectorAngles(m_RightControllerForward, m_RightControllerUp,
                             m_RightControllerAngAbs);
        m_RightControllerAngAbs.Normalize();
        m_ViewmodelForward = rotation.Rotate(m_ViewmodelForward);
        m_ViewmodelRight = rotation.Rotate(m_ViewmodelRight);
        m_ViewmodelUp = rotation.Rotate(m_ViewmodelUp);
    }
}

Vector VR::GetViewAngle()
{
    return Vector( m_HmdAngAbs.x, m_HmdAngAbs.y, m_HmdAngAbs.z );
}

Vector VR::GetViewOrigin(Vector setupOrigin)
{
    Vector center = setupOrigin;

    if (m_6DOF)
        center += GetHmdViewOffset();

    return center + (m_HmdForward * -(m_EyeZ * m_VRScale));
}

Vector VR::GetViewOriginLeft(Vector setupOrigin)
{
    Vector viewOriginLeft = GetViewOrigin(setupOrigin);
    viewOriginLeft -= m_HmdRight * ((m_Ipd * m_IpdScale * m_VRScale) / 2);

    return viewOriginLeft;
}

Vector VR::GetViewOriginRight(Vector setupOrigin)
{
    Vector viewOriginRight = GetViewOrigin(setupOrigin);
    viewOriginRight += m_HmdRight * ((m_Ipd * m_IpdScale * m_VRScale) / 2);

    return viewOriginRight;
}

Vector VR::Trace(uint32_t* localPlayer, bool &didHit) {
    Vector vecStart = GetRightControllerAbsPos();
    Vector vecEnd = vecStart + m_RightControllerForward * MAX_TRACE_LENGTH;

    CGameTrace trace;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    ray.Init(vecStart, vecEnd);

    m_Game->m_EngineTrace->TraceRay(ray, MASK_SHOT | MASK_SHOT_HULL, &tracefilter, &trace);

    didHit = trace.DidHit();
    return trace.endpos;
}

void AngleMatrix(const QAngle& angles, matrix3x4_t& matrix)
{
    float sr, sp, sy, cr, cp, cy;

    SinCos(DEG2RAD(angles[YAW]), &sy, &cy);
    SinCos(DEG2RAD(angles[PITCH]), &sp, &cp);
    SinCos(DEG2RAD(angles[ROLL]), &sr, &cr);

    // matrix = (YAW * PITCH) * ROLL
    matrix[0][0] = cp * cy;
    matrix[1][0] = cp * sy;
    matrix[2][0] = -sp;

    // NOTE: Do not optimize this to reduce multiplies! optimizer bug will screw this up.
    matrix[0][1] = sr * sp * cy + cr * -sy;
    matrix[1][1] = sr * sp * sy + cr * cy;
    matrix[2][1] = sr * cp;
    matrix[0][2] = (cr * sp * cy + -sr * -sy);
    matrix[1][2] = (cr * sp * sy + -sr * cy);
    matrix[2][2] = cr * cp;

    matrix[0][3] = 0.0f;
    matrix[1][3] = 0.0f;
    matrix[2][3] = 0.0f;
}

void MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out)
{
    memcpy(out.Base(), in.Base(), sizeof(float) * 3 * 4);
}


/*
================
R_ConcatTransforms
================
*/

void ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
{
    if (&in1 == &out)
    {
        matrix3x4_t in1b;
        MatrixCopy(in1, in1b);
        ConcatTransforms(in1b, in2, out);
        return;
    }
    if (&in2 == &out)
    {
        matrix3x4_t in2b;
        MatrixCopy(in2, in2b);
        ConcatTransforms(in1, in2b, out);
        return;
    }
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
        in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
        in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
        in1[0][2] * in2[2][2];
    out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
        in1[0][2] * in2[2][3] + in1[0][3];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
        in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
        in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
        in1[1][2] * in2[2][2];
    out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
        in1[1][2] * in2[2][3] + in1[1][3];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
        in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
        in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
        in1[2][2] * in2[2][2];
    out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
        in1[2][2] * in2[2][3] + in1[2][3];
}

void MatrixAngles(const matrix3x4_t& matrix, float* angles)
{
    float forward[3];
    float left[3];
    float up[3];

    //
    // Extract the basis vectors from the matrix. Since we only need the Z
    // component of the up vector, we don't get X and Y.
    //
    forward[0] = matrix[0][0];
    forward[1] = matrix[1][0];
    forward[2] = matrix[2][0];
    left[0] = matrix[0][1];
    left[1] = matrix[1][1];
    left[2] = matrix[2][1];
    up[2] = matrix[2][2];

    float xyDist = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);

    // enough here to get angles?
    if (xyDist > 0.001f)
    {
        // (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
        angles[1] = RAD2DEG(atan2f(forward[1], forward[0]));

        // (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
        angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

        // (roll)	z = ATAN( left.z, up.z );
        angles[2] = RAD2DEG(atan2f(left[2], up[2]));
    }
    else	// forward is mostly Z, gimbal lock-
    {
        // (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
        angles[1] = RAD2DEG(atan2f(-left[0], left[1]));

        // (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
        angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

        // Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
        angles[2] = 0;
    }
}

inline void MatrixAngles(const matrix3x4_t& matrix, QAngle& angles)
{
    MatrixAngles(matrix, &angles.x);
}



// transform a set of angles in the input space of parentMatrix to the output space
QAngle TransformAnglesToWorldSpace(const QAngle& angles, const matrix3x4_t& parentMatrix)
{
    matrix3x4_t angToParent, angToWorld;
    AngleMatrix(angles, angToParent);
    ConcatTransforms(parentMatrix, angToParent, angToWorld);
    QAngle out;
    MatrixAngles(angToWorld, out);
    return out;
}


Vector VR::TraceEye(uint32_t* localPlayer, Vector cameraPos, Vector eyePos, QAngle& eyeAngle) {
    CGameTrace trTestObstructionsNearPortals;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    ray.Init(cameraPos, eyePos);
    m_Game->m_EngineTrace->TraceRay(ray, MASK_SHOT | MASK_SHOT_HULL, &tracefilter, &trTestObstructionsNearPortals);

    float flWallHitFraction = trTestObstructionsNearPortals.fraction + 0.01f;
    CPortal_Base2D* pPortal = (CPortal_Base2D*)m_Game->m_Hooks->UTIL_Portal_FirstAlongRay(ray, flWallHitFraction);

    if (trTestObstructionsNearPortals.DidHit() && pPortal) {
        float flRayHitFraction = m_Game->m_Hooks->UTIL_IntersectRayWithPortal(ray, pPortal);
        //Vector vNewEye;
        Vector vHitPoint = ray.m_Start + ray.m_Delta * flRayHitFraction;
        //vNewEye = m_Game->m_Hooks->UTIL_Portal_PointTransform(pPortal->MatrixThisToLinked(), vHitPoint, vNewEye);

        //VMatrix matrix = *(VMatrix*)((uintptr_t)pPortal + 0x4C4);
        VMatrix matrix = pPortal->MatrixThisToLinked();

   
        /*QAngle newAngle;
        m_Game->m_Hooks->UTIL_Portal_AngleTransform(matrix, eyeAngle, newAngle);*/
        eyeAngle = TransformAnglesToWorldSpace(eyeAngle, matrix.As3x4());

        return matrix * vHitPoint;

        //return pPortal->MatrixThisToLinked() * vHitPoint;
    }

    return eyePos;
}

void VR::ParseConfigFile()
{
    std::ifstream configStream("VR\\config.txt");
    if (!configStream) {
        Logger::Write("VR/config.txt unavailable; keeping previous/default configuration");
        return;
    }
    std::ostringstream baseConfig;
    baseConfig << configStream.rdbuf();
    m_SettingsOverrides.clear();
    if (std::ifstream overridesStream{VrSettings::kOverridesPath})
        m_SettingsOverrides = VrSettings::ParseOverrides(overridesStream);
    if (!m_SettingsOverrides.empty())
        Logger::Write("Config: " + std::to_string(m_SettingsOverrides.size()) +
            " in-game VR option(s) from VR/ingame_settings.txt override config.txt");
    std::istringstream composed(VrSettings::ComposeConfigText(baseConfig.str(), m_SettingsOverrides));
    auto parsed = ApplyRuntimeConfig(m_Config, ParseConfig(composed, m_Config), m_IsInitialized);
    for (const auto &error : parsed.errors) Logger::Write("Config: " + error);
    for (const auto &note : parsed.notes) Logger::Write("Config: " + note);
    if (m_IsInitialized && m_VRScale != parsed.value.vrScale)
        Logger::Write("Config: VRScale change staged until recenter");
    if (m_IsInitialized && m_Playspace.heightOffsetMeters != parsed.value.heightOffsetMeters)
        Logger::Write("Config: HeightOffsetMeters change staged until recenter");
    m_Config = parsed.value;
    if (!m_Config.experimentalPortalShotHaptics)
        m_PortalShotHapticGate.Clear();
    if (!m_IsInitialized) {
        m_ActivePortalMode = m_Config.portalOrientationMode;
        m_PortalCoordinator.SetMode(m_ActivePortalMode);
        if (ExperimentalPortalOrientation())
            Logger::Write("EXPERIMENTAL portal orientation enabled; hardware alignment is unverified");
    }
    if (m_RoomscaleObserver.SetMode(m_Config.roomscaleMode)) {
        m_RoomscaleMotor.Reset(true);
        Logger::Write(m_Config.roomscaleMode == RoomscaleMotion::Mode::ActiveExperimental ?
            "RoomscaleMode=ActiveExperimental: CUserCmd movement prototype; LegacyYaw + 6DOF required; hardware/collision unverified" :
            m_Config.roomscaleMode == RoomscaleMotion::Mode::Observe ?
            "RoomscaleMode=Observe: diagnostics only; physical movement is disabled" :
            "RoomscaleMode=Off: roomscale diagnostics disabled");
    }
    m_SnapTurning = m_Config.snapTurning;
    m_SnapTurnAngle = m_Config.snapTurnAngle;
    if (!m_Config.physicalCrouch)
        m_PhysicalCrouch.Reset();
    m_TurnSpeed = m_Config.turnSpeed;
    m_LeftHanded = m_Config.leftHanded;
    if (!m_IsInitialized)
        m_VRScale = m_Config.vrScale;
    if (!m_IsInitialized) {
        m_Playspace.mode = m_Config.trackingMode;
        m_Playspace.heightOffsetMeters = m_Config.heightOffsetMeters;
        m_Playspace.scale = m_VRScale;
    }
    m_IpdScale = m_Config.ipdScale;
    const bool wasSixDof = m_6DOF;
    m_6DOF = m_Config.sixDof;
    if (m_Playspace.mode == TrackingSpace::TrackingMode::Standing &&
        m_6DOF && !wasSixDof)
        m_HasEyeHeight = false;
    const bool standingHeightInactive =
        m_Playspace.mode == TrackingSpace::TrackingMode::Standing && !m_6DOF;
    if (standingHeightInactive != m_StandingHeightInactiveLogged) {
        Logger::Write(standingHeightInactive ?
            "Config: Standing height and HeightOffsetMeters are inactive while 6DOF=false" :
            "Config: Standing height placement active again");
        m_StandingHeightInactiveLogged = standingHeightInactive;
    }
    m_AimMode = m_Config.aimMode;
    if (m_Config.experimentalWorldAimMarker && !m_Game->m_DebugOverlay)
        Logger::Write("Experimental world aim marker unavailable: VDebugOverlay004 missing; legacy particle retained");
    m_AntiAliasing = m_Config.antiAliasing;
    m_RenderWindow = m_Config.renderWindow;
    m_ViewmodelPosCustomOffset = {m_Config.viewmodelPosOffset[0], m_Config.viewmodelPosOffset[1], m_Config.viewmodelPosOffset[2]};
    m_ViewmodelAngCustomOffset = {m_Config.viewmodelAngOffset[0], m_Config.viewmodelAngOffset[1], m_Config.viewmodelAngOffset[2]};
    const Vector &viewmodelBase = ViewmodelBasePositionOffset(m_Config.experimentalViewmodelAlignment);
    Logger::Write("Config applied: VerboseDiagnostics=" + std::to_string(m_Config.verboseDiagnostics) +
        " TurnSpeed=" + std::to_string(m_TurnSpeed) +
        " SnapTurnAngle=" + std::to_string(m_SnapTurnAngle) +
        " VRScale=" + std::to_string(m_VRScale) +
        " IPDScale=" + std::to_string(m_IpdScale) +
        " AimMode=" + std::to_string(m_AimMode) +
        " ExperimentalWorldAimMarker=" +
        std::to_string(m_Config.experimentalWorldAimMarker) +
        " ExperimentalStereoReticle=" + std::to_string(m_Config.experimentalStereoReticle) +
        " ReticleDistanceScaling=" + std::to_string(m_Config.reticleDistanceScaling) +
        " AntiAliasing=" + std::to_string(m_AntiAliasing) +
        " GpuDrainEachFrame=" + std::to_string(m_Config.gpuDrainEachFrame) +
        " ExplicitTiming=" + std::to_string(m_Config.explicitTiming) +
        " PerfAbSeconds=" + std::to_string(m_Config.perfAbSeconds) +
        " ExperimentalHUDOverlay=" + std::to_string(m_Config.experimentalHudOverlay) +
        " ExperimentalViewmodelAlignment=" + std::to_string(m_Config.experimentalViewmodelAlignment) +
        " AimFromViewmodelMuzzle=" + std::to_string(m_Config.aimFromViewmodelMuzzle) +
        " ExperimentalPortalShotHaptics=" +
        std::to_string(m_Config.experimentalPortalShotHaptics) +
        " Haptics=" + std::to_string(m_Config.haptics) +
        " HapticsIntensity=" + std::to_string(m_Config.hapticsIntensity) +
        " HapticsNativeRumble=" + std::to_string(m_Config.hapticsNativeRumble) +
        " HapticsHeldObject=" + std::to_string(m_Config.hapticsHeldObject) +
        " HapticsButtons=" + std::to_string(m_Config.hapticsButtons) +
        " HeldObjectLog=" + std::to_string(m_Config.heldObjectLog) +
        " LaserCubeAimAssist=" + std::to_string(m_Config.laserCubeAimAssist) +
        " HeldObjectColumn=" + std::to_string(m_Config.heldObjectColumn) +
        " HeldObjectAnchor=" + std::to_string(m_Config.heldObjectAnchor) +
        " ViewmodelPosCustomOffset=" +
        std::to_string(m_ViewmodelPosCustomOffset.x) + "," +
        std::to_string(m_ViewmodelPosCustomOffset.y) + "," +
        std::to_string(m_ViewmodelPosCustomOffset.z) +
        " EffectiveViewmodelPosOffset=" +
        std::to_string(viewmodelBase.x + m_ViewmodelPosCustomOffset.x) + "," +
        std::to_string(viewmodelBase.y + m_ViewmodelPosCustomOffset.y) + "," +
        std::to_string(viewmodelBase.z + m_ViewmodelPosCustomOffset.z) +
        (m_Config.experimentalViewmodelAlignment ? " (aligned grip base)" : " (legacy base)"));
}

bool VR::WriteMenuSettings(const VrSettings::Overrides &overrides)
{
    const char *path = VrSettings::kOverridesPath;
    bool written = false;
    if (overrides.empty()) {
        written = DeleteFileA(path) || GetLastError() == ERROR_FILE_NOT_FOUND;
    } else {
        // Replace atomically so a crash never leaves a truncated overrides file.
        const std::string temporary = std::string(path) + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream << VrSettings::SerializeOverrides(overrides);
            written = static_cast<bool>(stream.flush());
        }
        written = written && MoveFileExA(temporary.c_str(), path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!written) DeleteFileA(temporary.c_str());
    }
    if (!written) {
        Logger::Write("In-game VR options: writing VR/ingame_settings.txt failed (" +
            std::to_string(GetLastError()) + "); setting not applied");
        return false;
    }
    std::error_code error;
    m_SettingsLastModified = std::filesystem::last_write_time(path, error);
    if (error) m_SettingsLastModified = {};
    return true;
}

bool VR::ApplyMenuSetting(const std::string &key, const std::string &value)
{
    if (!VrSettings::IsMenuKey(key)) return false;
    const auto previous = m_SettingsOverrides;
    auto next = previous;
    next[key] = value;
    if (!WriteMenuSettings(next)) return false;
    ParseConfigFile();
    // ApplyRuntimeConfig keeps the previous value when a dependent setting is rejected.
    for (const auto &page : VrSettings::Pages())
        for (const auto &item : page.items)
            if (item.key && key == item.key && item.read && item.read(m_Config) != value) {
                Logger::Write("In-game VR options: " + key + "=" + value + " rejected; previous value kept");
                if (WriteMenuSettings(previous)) ParseConfigFile();
                return false;
            }
    Logger::Write("In-game VR options: " + key + "=" + value + " applied");
    return true;
}

void VR::ResetMenuSettings()
{
    if (WriteMenuSettings({})) {
        ParseConfigFile();
        Logger::Write("In-game VR options: Use Defaults removed VR/ingame_settings.txt; config.txt values active");
    }
}
