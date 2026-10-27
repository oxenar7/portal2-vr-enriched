#include "../L4D2VR/digital_input.h"
#include "../L4D2VR/tracked_device.h"
#include "../L4D2VR/sigscanner.h"
#include "../L4D2VR/config.h"
#include "../L4D2VR/tracking_space.h"
#include "../L4D2VR/roomscale_motion.h"
#include "../L4D2VR/roomscale_motor.h"
#include "../L4D2VR/portal_orientation.h"
#include "../L4D2VR/ui_input.h"
#include "../L4D2VR/haptics.h"
#include "../L4D2VR/viewport_readiness.h"
#include "../L4D2VR/render_target_readiness.h"
#include "../L4D2VR/runtime_publication.h"
#include "../L4D2VR/render_diagnostics.h"
#include "../L4D2VR/menu_overlay_placement.h"
#include "../L4D2VR/hud_capture.h"
#include "../L4D2VR/aim_feedback.h"
#include "../L4D2VR/render_context_abi.h"
#include "../L4D2VR/reticle_telemetry.h"
#include "../L4D2VR/viewmodel_alignment.h"
#include "../L4D2VR/muzzle_origin.h"
#include "../L4D2VR/native_reticle.h"
#include "../L4D2VR/native_beam.h"
#include "../L4D2VR/shared_runtime_session.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>

static int failures = 0;

struct TestReticleCanvas
{
    NativeReticle::ClipRect rect{0, 0, 1280, 720};
    void SetClipRect(const NativeReticle::ClipRect &value) { rect = value; }
};

struct TestViewportEngine
{
    bool inGame = false;
    int calls = 0;
    bool IsInGame() { ++calls; return inGame; }
};

struct TestViewportGame
{
    TestViewportEngine *m_EngineClient = nullptr;
    struct TestViewportVr *m_VR = nullptr;
};

struct TestViewportVr
{
    bool m_IsInitialized = false;
};

struct TestPublishedGame
{
    bool m_Initialized = false;
};

static void expectCommand(const char* actual, const char* expected, const char* caseName)
{
    if ((actual == nullptr) != (expected == nullptr) ||
        (actual && std::strcmp(actual, expected) != 0)) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

static void expect(bool actual, bool expected, const char* caseName)
{
    if (actual != expected) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

static void expectInt(int actual, int expected, const char* caseName)
{
    if (actual != expected) {
        std::cerr << caseName << " failed: " << actual << " != " << expected << "\n";
        ++failures;
    }
}

static void expectNear(float actual, float expected, const char* caseName)
{
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 0.0001f) {
        std::cerr << caseName << " failed: " << actual << " != " << expected << "\n";
        ++failures;
    }
}

static void expectVectorNear(const Vector &actual, const Vector &expected, const char* caseName)
{
    expectNear(actual.x, expected.x, caseName);
    expectNear(actual.y, expected.y, caseName);
    expectNear(actual.z, expected.z, caseName);
}

int main()
{
    MuzzleOrigin::Sample muzzle;
    expect(muzzle.Origin({200,50,60}, {0,90,0}, 1, 2, 1.0).has_value(), false,
           "missing muzzle sample cannot fall back to controller origin");
    expect(muzzle.Capture({110,18,31}, {100,20,30}, {0,0,0}, 1, 2, 1.0), true,
           "native muzzle is captured relative to actual model pose");
    const auto rebasedMuzzle = muzzle.Origin({200,50,60}, {0,90,0}, 1, 2, 1.01);
    expect(rebasedMuzzle.has_value(), true, "fresh muzzle sample follows current model pose");
    if (rebasedMuzzle) expectVectorNear(*rebasedMuzzle, {202,60,61},
           "muzzle is rotated and translated, not left at previous world position");
    expect(muzzle.Origin({200,50,60}, {0,90,0}, 3, 2, 1.01).has_value(), false,
           "another player cannot use local muzzle sample");
    expect(muzzle.Origin({200,50,60}, {0,90,0}, 1, 3, 1.01).has_value(), false,
           "changed weapon cannot use prior muzzle sample");
    expect(muzzle.Origin({200,50,60}, {0,90,0}, 1, 2, 1.3).has_value(), false,
           "stale animation sample is rejected");
    expect(muzzle.Origin({200,50,60}, {0,90,0}, 1, 2, 0.9).has_value(), false,
           "future sample is rejected");
    const float invalidMuzzleFloat = std::numeric_limits<float>::quiet_NaN();
    expect(muzzle.Origin({invalidMuzzleFloat,0,0}, {0,0,0}, 1, 2, 1.01).has_value(), false,
           "nonfinite current model origin is rejected");
    expect(muzzle.Origin({0,0,0}, {0,invalidMuzzleFloat,0}, 1, 2, 1.01).has_value(), false,
           "nonfinite current model rotation is rejected");
    expect(muzzle.Capture({invalidMuzzleFloat,0,0}, {0,0,0}, {0,0,0}, 1, 2, 1.1), false,
           "invalid native sample is rejected");
    expect(muzzle.Origin({0,0,0}, {0,0,0}, 1, 2, 1.11).has_value(), false,
           "bad capture invalidates previously valid data");
    expect(muzzle.Capture({257,0,0}, {0,0,0}, {0,0,0}, 1, 2, 1.2), false,
           "implausible attachment offset is rejected");
    expect(muzzle.Capture({1,0,0}, {0,0,0}, {0,0,0}, 0, 2, 1.2), false,
           "missing player identity is rejected");
    expect(muzzle.Capture({1,0,0}, {0,0,0}, {0,0,0}, 1, 0, 1.2), false,
           "missing weapon identity is rejected");
    expect(muzzle.Capture({1,0,0}, {0,0,0}, {0,0,0}, 1, 2, 1.2), true,
           "sample recovers after validation failure");
    muzzle.Invalidate();
    expect(muzzle.Origin({0,0,0}, {0,0,0}, 1, 2, 1.21).has_value(), false,
           "tracking/menu invalidation clears muzzle sample");
    MuzzleOrigin::State muzzleState;
    expect(muzzleState.Capture({1,0,0}, {0,0,0}, {0,0,0}, 2.0), false,
           "unbound sampling state cannot capture");
    muzzleState.SelectIdentity(1, 2);
    expect(muzzleState.Capture({1,0,0}, {0,0,0}, {0,0,0}, 2.0), true,
           "bound muzzle state captures under one synchronized identity");
    muzzleState.SelectIdentity(1, 2);
    expect(muzzleState.Origin({0,0,0}, {0,0,0}, 2.01).has_value(), true,
           "unchanged identity preserves fresh sample");
    muzzleState.SelectIdentity(1, 3);
    expect(muzzleState.Origin({0,0,0}, {0,0,0}, 2.01).has_value(), false,
           "identity update invalidates sample atomically");
    expect(muzzleState.Capture({1,0,0}, {0,0,0}, {0,0,0}, 2.02), true,
           "new weapon can supply fresh sample");
    muzzleState.Reset();
    expect(muzzleState.Capture({1,0,0}, {0,0,0}, {0,0,0}, 2.03), false,
           "reset clears identity as well as sample");

    float scopedAspect = 0.0f;
    expectNear(ViewmodelAlignment::AspectOr(scopedAspect, 16.0f / 9.0f),
               16.0f / 9.0f, "desktop aspect remains native outside viewmodel draw");
    {
        ViewmodelAlignment::ProjectionScope leftEye(scopedAspect, 1.074f);
        expectNear(ViewmodelAlignment::AspectOr(scopedAspect, 16.0f / 9.0f),
                   1.074f, "viewmodel projection uses VR eye aspect, not window aspect");
        {
            ViewmodelAlignment::ProjectionScope nestedView(scopedAspect, 1.2f);
            expectNear(scopedAspect, 1.2f, "nested view uses its own projection aspect");
        }
        expectNear(scopedAspect, 1.074f, "nested view restores the outer eye projection");
    }
    expectNear(scopedAspect, 0.0f, "viewmodel draw restores desktop projection state");
    expectNear(ViewmodelAlignment::AspectOr(std::numeric_limits<float>::quiet_NaN(),
                                          16.0f / 9.0f), 16.0f / 9.0f,
               "nonfinite override cannot corrupt native projection");
    ViewmodelAlignment::Eligibility modelEligibility{true, true, true, true, true, true, false};
    expect(modelEligibility.Allowed(), true, "complete local tracking permits viewmodel correction");
    modelEligibility.ready = false;
    expect(modelEligibility.Allowed(), false, "missing optional hook group preserves legacy viewmodel");
    modelEligibility.ready = true;
    modelEligibility.handValid = false;
    expect(modelEligibility.Allowed(), false, "lost firing hand cannot override model pose or effects");
    modelEligibility.handValid = true;
    modelEligibility.cursorVisible = true;
    expect(modelEligibility.Allowed(), false, "pause menu cannot apply controller viewmodel correction");
    modelEligibility.cursorVisible = false;
    modelEligibility.published = false;
    expect(modelEligibility.Allowed(), false, "initializing runtime cannot apply viewmodel correction");
    ReticleTelemetry reticleTelemetry;
    using ReticleEye = ReticleTelemetry::Eye;
    using ReticleIcon = ReticleTelemetry::Icon;
    using ReticleResult = ReticleTelemetry::Result;
    expect(reticleTelemetry.Record(ReticleEye::Left, ReticleIcon::LeftInvalid,
               0, ReticleResult::Drawn, 1000).has_value(), false,
           "reticle telemetry does not log each icon draw");
    expect(reticleTelemetry.Record(ReticleEye::Left, ReticleIcon::LeftValid,
               128, ReticleResult::Drawn, 1500).has_value(), false,
           "reticle telemetry retains partial alpha until the window closes");
    expect(reticleTelemetry.Record(ReticleEye::Right, ReticleIcon::RightValid,
               255, ReticleResult::Drawn, 2000).has_value(), true,
           "reticle telemetry emits a bounded one-second summary");
    const auto reticleWindow = reticleTelemetry.LastWindow();
    expectInt(reticleWindow.eyes[0].icons[0].zeroAlpha, 1,
              "zero-alpha left portal draw is counted");
    expectInt(reticleWindow.eyes[0].icons[1].partialAlpha, 1,
              "partial-alpha left portal draw is counted");
    expectInt(reticleWindow.eyes[1].icons[3].opaqueAlpha, 1,
              "opaque right portal draw is counted in the correct eye");
    expectInt(reticleWindow.eyes[0].drawn, 2,
              "left eye draw count excludes the right eye");
    expect(reticleTelemetry.Record(ReticleEye::Right, ReticleIcon::RightInvalid,
               -1, ReticleResult::ProjectedOutside, 2010).has_value(), false,
           "reticle telemetry starts a fresh window after emission");
    expectInt(reticleTelemetry.CurrentWindow().eyes[1].outside, 1,
              "projection skips are separate from atlas draws");

    std::atomic<TestPublishedGame*> publishedGame{nullptr};
    TestPublishedGame initializingGame;
    expect(Portal2VRRuntime::IsPublished(publishedGame, &initializingGame), false,
           "render hook bypasses VR before game publication");
    expect(Portal2VRRuntime::PublishInitialized(publishedGame, &initializingGame), false,
           "partially initialized game is not published to renderer");
    expect(publishedGame.load() == nullptr, true,
           "renderer sees no game after initialization failure");
    initializingGame.m_Initialized = true;
    expect(Portal2VRRuntime::PublishInitialized(publishedGame, &initializingGame), true,
           "initialized game is published to renderer");
    expect(publishedGame.load() == &initializingGame, true,
           "renderer sees the initialized game");
    expect(Portal2VRRuntime::IsPublished(publishedGame, &initializingGame), true,
           "render hook may use VR after game publication");

    RenderTargetReadiness targets{{true, true, true}, {true, true, true},
                                  {true, true, true}};
    expect(targets.Ready(), true, "complete stereo and menu targets are ready");
    targets.left.shared = false;
    expect(targets.Ready(), false, "missing left Vulkan share blocks stereo");
    targets.left.shared = true;
    targets.right.surface = false;
    expect(targets.Ready(), false, "missing right D3D surface blocks stereo");
    targets.right.surface = true;
    targets.blank.texture = false;
    expect(targets.Ready(), false, "missing menu texture blocks target readiness");

    expect(Portal2VRViewport::CanUseRecommendedVRSize(false, 1920, 1080), false,
           "failed OpenVR initialization preserves game backbuffer size");
    expect(Portal2VRViewport::CanUseRecommendedVRSize(true, 0, 1080), false,
           "zero OpenVR width preserves game backbuffer size");
    expect(Portal2VRViewport::CanUseRecommendedVRSize(true, 1920, 0), false,
           "zero OpenVR height preserves game backbuffer size");
    expect(Portal2VRViewport::CanUseRecommendedVRSize(true, 1920, 1080), true,
           "valid OpenVR size may override game backbuffer size");

    RenderDiagnosticGate renderDiagnostics;
    expect(renderDiagnostics.First(RenderDiagnosticEvent::MenuSubmission), true,
           "first menu submission is logged");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::MenuSubmission), false,
           "repeated menu submissions are not logged");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::StereoSubmission), true,
           "first stereo submission is logged independently");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::StereoSubmission), false,
           "repeated stereo submissions are not logged");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::HudPaintEligible), true,
           "first eligible HUD paint diagnostic is logged");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::HudPaintEligible), false,
           "eligible HUD paint diagnostic does not spam each frame");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::HudPushInPaint), true,
           "render-target push diagnostic is independent from paint diagnostic");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::CrosshairHudDraw), true,
           "first crosshair draw in the HUD target is logged independently");
    expect(renderDiagnostics.First(RenderDiagnosticEvent::CrosshairHudDraw), false,
           "crosshair HUD draw diagnostic does not spam each frame");

    RenderTargetDiagnosticGate targetDiagnostics;
    int allocations = 0, projections = 0, bounds = 0;
    for (int i = 0; i < 1944; ++i) {
        allocations += targetDiagnostics.Allocation(2528, 2704, false);
        projections += targetDiagnostics.Projection(1280, 720, 2528, 2704, 1.2f, 100.0f, false);
        bounds += targetDiagnostics.HudBounds(1280, 720, 2528, 2704, 0.5f, 0.25f, 0, false);
    }
    expectInt(allocations, 1, "unchanged menu allocations log once in normal mode");
    expectInt(projections, 1, "unchanged projection geometry logs once in normal mode");
    expectInt(bounds, 1, "unchanged successful HUD bounds log once in normal mode");
    expect(targetDiagnostics.Allocation(3000, 2704, false), true, "changed target dimensions log");
    expect(targetDiagnostics.Allocation(3000, 2704, false), false, "changed dimensions then stay quiet");
    expect(targetDiagnostics.Projection(1920, 1080, 2528, 2704, 1.2f, 100.0f, false), true,
           "Source window resize logs new projection geometry");
    expect(targetDiagnostics.Projection(1920, 1080, 2528, 2704, 1.2f, 110.0f, false), true,
           "FOV change logs even when dimensions are unchanged");
    expect(targetDiagnostics.HudBounds(1280, 720, 2528, 2704, 0.6f, 0.25f, 0, false), true,
           "changed HUD crop logs");
    for (int i = 0; i < 3; ++i) {
        expect(targetDiagnostics.Allocation(3000, 2704, true), true, "verbose logs repeated allocations");
        expect(targetDiagnostics.Projection(1920, 1080, 2528, 2704, 1.2f, 110.0f, true), true,
               "verbose logs repeated projection geometry");
        expect(targetDiagnostics.HudBounds(1280, 720, 2528, 2704, 0.6f, 0.25f, 0, true), true,
               "verbose logs repeated HUD bounds");
        expect(targetDiagnostics.HudBounds(1280, 720, 2528, 2704, 0.6f, 0.25f, 12, false), true,
               "repeated HUD bounds failures remain visible without verbose");
    }
    expect(targetDiagnostics.Allocation(3000, 2704, false), false, "disabling verbose resumes suppression");
    expect(targetDiagnostics.HudBounds(1280, 720, 2528, 2704, 0.6f, 0.25f, 0, false), true,
           "HUD bounds recovery logs once");
    expect(targetDiagnostics.HudBounds(1280, 720, 2528, 2704, 0.6f, 0.25f, 0, false), false,
           "recovered HUD bounds stay quiet");

    MenuOverlayPlacement menuPlacement;
    expect(menuPlacement.ShouldAttempt(false, false), false,
           "menu cannot be positioned before the first valid HMD pose");
    expect(menuPlacement.ShouldAttempt(true, true), true,
           "visible menu retries placement after HMD pose becomes valid");
    menuPlacement.RecordResult(false);
    expect(menuPlacement.ShouldAttempt(true, true), true,
           "failed menu placement is retried while visible");
    menuPlacement.RecordResult(true);
    expect(menuPlacement.ShouldAttempt(true, true), false,
           "positioned visible menu does not recapture heading on every frame");
    expect(menuPlacement.ShouldAttempt(false, true), true,
           "hidden menu is positioned again before returning");
    const float menuHmd[3][4] = {{1,0,0,1}, {0,1,0,1.7f}, {0,0,1,2}};
    const auto openedMenu = menuPlacement.UpdatePose(menuHmd, false);
    expect(openedMenu.has_value(), true, "menu opens from the current tracking pose");
    if (openedMenu) {
        expectNear(openedMenu->m[0][3], 1.0f, "menu opens at current HMD x");
        expectNear(openedMenu->m[1][3], 1.45f, "menu opens slightly below eye height");
        expectNear(openedMenu->m[2][3], -1.0f, "menu opens three meters ahead in OpenVR space");
    }
    menuPlacement.RecordResult(true);
    // Move two meters and turn the head 90 degrees while the panel is open.
    const float movedMenuHmd[3][4] = {{0,0,1,3}, {0,1,0,1.7f}, {-1,0,0,2}};
    const auto followedMenu = menuPlacement.UpdatePose(movedMenuHmd, true);
    expect(followedMenu.has_value(), true, "visible menu follows physical translation");
    if (followedMenu) {
        expectNear(followedMenu->m[0][3], 3.0f, "visible menu follows current HMD x");
        expectNear(followedMenu->m[2][3], -1.0f, "visible menu keeps its opening heading");
        expectNear(followedMenu->m[0][0], 1.0f, "visible menu does not rotate with head turns");
    }
    menuPlacement.Invalidate();
    const auto reopenedMenu = menuPlacement.UpdatePose(movedMenuHmd, true);
    expect(reopenedMenu.has_value(), true, "invalidated visible menu captures a fresh opening pose");
    if (reopenedMenu) {
        expectNear(reopenedMenu->m[0][3], 0.0f, "reopening adopts the new facing direction");
        expectNear(reopenedMenu->m[2][3], 2.0f, "reopening uses the current tracking position");
    }
    float badMenuHmd[3][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}};
    badMenuHmd[0][3] = std::numeric_limits<float>::quiet_NaN();
    expect(menuPlacement.UpdatePose(badMenuHmd, true).has_value(), false,
           "invalid tracking cannot send a nonfinite overlay transform");
    const auto recoveredMenu = menuPlacement.UpdatePose(movedMenuHmd, true);
    expect(recoveredMenu.has_value(), true, "menu recovers when finite tracking returns");
    if (recoveredMenu)
        expectNear(recoveredMenu->m[0][3], 0.0f, "tracking recovery captures current heading");
    menuPlacement.Invalidate();
    const float upwardMenuHmd[3][4] = {{1,0,0,1}, {0,0,1,1.7f}, {0,-1,0,2}};
    const auto upwardMenu = menuPlacement.UpdatePose(upwardMenuHmd, false);
    expect(upwardMenu.has_value(), true, "vertical gaze still yields a usable menu pose");
    if (upwardMenu) {
        expectNear(upwardMenu->m[0][0], 1.0f, "vertical gaze uses a nondegenerate yaw fallback");
        expectNear(upwardMenu->m[2][3], -1.0f, "vertical gaze keeps menu in front of tracking origin heading");
    }

    TestViewportEngine viewportEngine;
    TestViewportGame viewportGame;
    TestViewportVr viewportVr;
    expect(Portal2VRViewport::CanOverrideMenuViewport<TestViewportGame>(nullptr), false,
           "viewport override skips absent game");
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), false,
           "viewport override skips game before engine interface resolves");
    viewportGame.m_EngineClient = &viewportEngine;
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), false,
           "viewport override skips game before VR is initialized");
    expect(viewportEngine.calls == 0, true,
           "viewport override never queries engine during partial initialization");
    viewportGame.m_VR = &viewportVr;
    expect(Portal2VRViewport::HasInitializedVR(&viewportGame), false,
           "DXVK reset skips an incomplete VR object");
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), false,
           "viewport override skips VR object before initialization completes");
    viewportVr.m_IsInitialized = true;
    expect(Portal2VRViewport::HasInitializedVR(&viewportGame), true,
           "DXVK reset may use dimensions after VR initialization completes");
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), true,
           "viewport override remains active in menu once dependencies are ready");
    viewportEngine.inGame = true;
    expect(Portal2VRViewport::CanOverrideMenuViewport(&viewportGame), false,
           "viewport override stays off during gameplay");

    const VMatrix quarterTurn{0,-1,0,12, 1,0,0,-5, 0,0,1,3, 0,0,0,1};
    const auto turn = PortalOrientation::Rotation::FromVMatrix(quarterTurn);
    expect(turn.has_value(), true, "rigid translated portal matrix accepted");
    if (turn) {
        expectVectorNear(turn->Rotate({1,0,0}), {0,1,0}, "90 degree portal rotation uses Source axes");
        expectVectorNear(turn->Inverse().Rotate(turn->Rotate({2,3,4})), {2,3,4},
                         "portal transform followed by inverse restores direction");
        const auto twice = turn->Compose(*turn);
        expectVectorNear(twice.Rotate({1,0,0}), {-1,0,0}, "two 90 degree crossings compose to 180");
        expectVectorNear(turn->Rotate({0.3f,0.1f,1.6f}) - turn->Rotate({0.1f,0.1f,1.6f}),
                         {0,0.2f,0}, "head and controller share relative portal rotation");
        expectVectorNear(turn->YawOnly().Rotate({1,0,0}), {0,1,0},
                         "yaw-only wall portal preserves horizontal heading");
    }
    const VMatrix floorExit{0,0,-1,0, 0,1,0,0, 1,0,0,0, 0,0,0,1};
    const auto floorTurn = PortalOrientation::Rotation::FromVMatrix(floorExit);
    expect(floorTurn.has_value(), true, "floor portal pitch matrix accepted");
    if (floorTurn) {
        expectVectorNear(floorTurn->Rotate({1,0,0}), {0,0,1},
                         "wall-to-floor portal can point forward vertically");
        expectVectorNear(floorTurn->YawOnly().Rotate({1,0,0}), {1,0,0},
                         "vertical-forward yaw fallback uses portal left axis");
        expectVectorNear(floorTurn->PreserveHorizon().Rotate({1,0,0}), {0,0,1},
                         "horizon mode retains floor-portal pitch");
    }
    const VMatrix rollPortal{1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1};
    const auto rollTurn = PortalOrientation::Rotation::FromVMatrix(rollPortal);
    expect(rollTurn.has_value(), true, "rolled portal matrix accepted");
    if (rollTurn)
        expectVectorNear(rollTurn->PreserveHorizon().Rotate({0,0,1}), {0,0,1},
                         "horizon mode removes portal-induced roll without head tracking input");
    const VMatrix reflected{-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    expect(PortalOrientation::Rotation::FromVMatrix(reflected).has_value(), false,
           "reflected portal matrix is not a rigid rotation");
    const VMatrix scaled{2,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    expect(PortalOrientation::Rotation::FromVMatrix(scaled).has_value(), false,
           "scaled portal matrix is rejected");
    PortalOrientation::Coordinator portalEvents;
    expect(portalEvents.Queue(1, 10, *turn) == PortalOrientation::QueueResult::Disabled,
           true, "legacy default does not consume experimental portal events");
    portalEvents.SetMode(PortalOrientation::Mode::FullRotation);
    expect(portalEvents.Queue(1, 10, *turn) == PortalOrientation::QueueResult::Queued,
           true, "first local portal event queued");
    expect(portalEvents.Queue(1, 10, *turn) == PortalOrientation::QueueResult::Duplicate,
           true, "duplicate callback in one render interval is ignored");
    const auto firstPortalFrame = portalEvents.Drain(1);
    expect(firstPortalFrame.applied == 1, true, "one portal event applied at frame boundary");
    expectVectorNear(firstPortalFrame.effective.Rotate({1,0,0}), {0,1,0},
                     "full rotation uses linked portal orientation");
    expect(portalEvents.Queue(1, 10, *turn) == PortalOrientation::QueueResult::Duplicate,
           true, "same crossing prediction replay in next frame is ignored");
    portalEvents.Queue(1, 20, turn->Inverse());
    const auto returnPortalFrame = portalEvents.Drain(1);
    expect(returnPortalFrame.applied == 1, true, "reverse crossing applies once");
    expectVectorNear(returnPortalFrame.effective.Rotate({1,0,0}), {1,0,0},
                     "crossing linked portal back restores heading");
    portalEvents.Queue(1, 10, *turn);
    portalEvents.Queue(1, 20, *turn);
    const auto consecutivePortalFrame = portalEvents.Drain(1);
    expect(consecutivePortalFrame.applied == 2, true,
           "two distinct portal crossings before one frame compose in arrival order");
    expectVectorNear(consecutivePortalFrame.effective.Rotate({1,0,0}), {-1,0,0},
                     "two queued quarter turns compose to 180 degrees");
    PortalOrientation::Coordinator rapidReturns;
    rapidReturns.SetMode(PortalOrientation::Mode::FullRotation);
    rapidReturns.Queue(1, 10, *turn);
    rapidReturns.Drain(1);
    rapidReturns.Queue(1, 20, *turn);
    rapidReturns.Queue(1, 10, *turn);
    expect(rapidReturns.Drain(1).applied == 2, true,
           "distinct intervening crossing permits quick return through prior portal");
    portalEvents.Queue(1, 30, *turn);
    const auto otherPlayerFrame = portalEvents.Drain(2);
    expect(otherPlayerFrame.applied == 0, true,
           "player replacement discards pending portal event");
    expect(otherPlayerFrame.playerChanged, true,
           "player replacement explicitly reports rig reset even without applied event");
    expectVectorNear(otherPlayerFrame.effective.Rotate({1,0,0}), {1,0,0},
                     "player replacement resets old world alignment");
    portalEvents.SetMode(PortalOrientation::Mode::YawOnly);
    portalEvents.Queue(2, 40, *floorTurn);
    const auto yawPortalFrame = portalEvents.Drain(2);
    expectVectorNear(yawPortalFrame.effective.Rotate({1,0,0}), {1,0,0},
                     "yaw-only floor crossing uses stable left-axis fallback");
    portalEvents.SetMode(PortalOrientation::Mode::PreserveHorizon);
    portalEvents.Queue(2, 50, *rollTurn);
    const auto horizonPortalFrame = portalEvents.Drain(2);
    expectVectorNear(horizonPortalFrame.effective.Rotate({0,0,1}), {0,0,1},
                     "horizon mode removes portal roll for entire rig");
    portalEvents.Queue(2, 60, *turn);
    portalEvents.CancelPending();
    expect(portalEvents.Drain(2).applied == 0, true,
           "recenter or tracking loss cancels a pending portal event");
    PortalOrientation::Coordinator resumedEvents;
    resumedEvents.SetMode(PortalOrientation::Mode::FullRotation);
    resumedEvents.Queue(1, 30, *turn);
    resumedEvents.Drain(1);
    resumedEvents.CancelPending();
    expect(resumedEvents.Queue(1, 30, *turn) == PortalOrientation::QueueResult::Queued,
           true, "focus loss clears old portal duplicate window");
    PortalOrientation::RigAnchor portalRig;
    portalRig.Reanchor({10,20,30}, {10,20,30});
    const Vector afterPortalHead = portalRig.MapHmd({11,20,30}, *turn);
    expectVectorNear(afterPortalHead, {10,21,30},
                     "head displacement after portal follows linked world heading");
    expectVectorNear(portalRig.MapRelative({0.2f,0,0}, *turn), {0,0.2f,0},
                     "hand displacement uses same portal mapping as head");
    portalRig.Reanchor({0,0,0}, {0,0,0});
    expectVectorNear(portalRig.MapHmd({0,0,0}, *turn), {0,0,0},
                     "explicit recenter resets the virtual head offset");
    expectVectorNear(portalRig.MapHmd({1,0,0}, *turn), {0,1,0},
                     "post-recenter physical delta retains portal orientation");

    UiInput::MenuPointerState menuMouse;
    expect(menuMouse.Press() == UiInput::MouseTransition::Press, true,
           "menu press creates one synthetic down event");
    menuMouse.ConfirmSent(UiInput::MouseTransition::Press, true);
    expect(menuMouse.Press() == UiInput::MouseTransition::None, true,
           "held menu selection does not repeat mouse down");
    expect(menuMouse.LoseFocus() == UiInput::MouseTransition::Release, true,
           "lost hover or menu focus releases a held synthetic click");
    menuMouse.ConfirmSent(UiInput::MouseTransition::Release, true);
    expect(menuMouse.Release() == UiInput::MouseTransition::None, true,
           "late mouse up after focus loss is ignored");
    expect(menuMouse.Press() == UiInput::MouseTransition::Press, true,
           "next menu click still works after focus recovery");
    menuMouse.ConfirmSent(UiInput::MouseTransition::Press, true);
    expect(menuMouse.Release() == UiInput::MouseTransition::Release, true,
           "ordinary release emits one up event");
    menuMouse.ConfirmSent(UiInput::MouseTransition::Release, true);
    expect(menuMouse.PendingRelease() == UiInput::MouseTransition::None, true,
           "successful mouse up leaves no pending release");

    UiInput::MenuPointerState failedMenuMouse;
    expect(failedMenuMouse.Press() == UiInput::MouseTransition::Press, true,
           "failed mouse down is attempted once");
    failedMenuMouse.ConfirmSent(UiInput::MouseTransition::Press, false);
    expect(failedMenuMouse.Release() == UiInput::MouseTransition::None, true,
           "failed mouse down does not create a phantom held click");
    expect(failedMenuMouse.Press() == UiInput::MouseTransition::Press, true,
           "a later click may retry after failed mouse down");
    failedMenuMouse.ConfirmSent(UiInput::MouseTransition::Press, true);
    expect(failedMenuMouse.Release() == UiInput::MouseTransition::Release, true,
           "mouse up is requested after a delivered down");
    failedMenuMouse.ConfirmSent(UiInput::MouseTransition::Release, false);
    expect(failedMenuMouse.PendingRelease() == UiInput::MouseTransition::Release, true,
           "failed mouse up remains pending for next frame");
    expect(failedMenuMouse.Press() == UiInput::MouseTransition::None, true,
           "new down is blocked until pending mouse up succeeds");
    failedMenuMouse.ConfirmSent(failedMenuMouse.PendingRelease(), true);
    expect(failedMenuMouse.PendingRelease() == UiInput::MouseTransition::None, true,
           "successful retry clears pending mouse up");
    expect(failedMenuMouse.Press() == UiInput::MouseTransition::Press, true,
           "normal clicks resume after mouse up retry");
    const auto mainTopLeft = UiInput::MapMenuPointer(0, 1080, 1920, 1080, 1280, 720, false);
    expect(mainTopLeft && mainTopLeft->x == 0 && mainTopLeft->y == 0, true,
           "main-menu overlay top-left maps to desktop top-left");
    const auto mainBottomRight = UiInput::MapMenuPointer(1920, 0, 1920, 1080, 1280, 720, false);
    expect(mainBottomRight && mainBottomRight->x == 1279 && mainBottomRight->y == 719, true,
           "main-menu overlay edges clamp to desktop bounds");
    const auto pausePoint = UiInput::MapMenuPointer(100, 1000, 1920, 1200, 800, 600, true);
    expect(pausePoint && pausePoint->x == 100 && pausePoint->y == 200, true,
           "pause-menu crop retains existing render-texture pointer convention");
    expect(UiInput::MapMenuPointer(std::numeric_limits<float>::quiet_NaN(), 0,
                                  1920, 1080, 800, 600, false).has_value(), false,
           "nonfinite overlay coordinates cannot reach VGUI");

    RoomscaleMotion::Eligibility roomscaleGate{true, true, true, true, false, true};
    expect(RoomscaleMotion::HasManualInput(0, 0, 0, false), false,
           "idle command permits physical movement");
    expect(RoomscaleMotion::HasManualInput(0, 0, 0, true), true,
           "directional button without float movement still suppresses motor");
    expect(RoomscaleMotion::HasManualInput(0, -20, 0, false), true,
           "stick or keyboard movement suppresses motor");
    expect(RoomscaleMotion::HasManualInput(std::numeric_limits<float>::quiet_NaN(), 0, 0, false), true,
           "malformed input cannot receive additional motor movement");
    expect(roomscaleGate.Allowed(), true, "valid LegacyYaw gameplay permits experimental motor");
    auto rejectedGate = roomscaleGate;
    rejectedGate.sixDof = false;
    expect(rejectedGate.Allowed(), false, "motor requires positional tracking mode");
    rejectedGate = roomscaleGate; rejectedGate.trackingValid = false;
    expect(rejectedGate.Allowed(), false, "motor requires a valid HMD pose");
    rejectedGate = roomscaleGate; rejectedGate.legacyYaw = false;
    expect(rejectedGate.Allowed(), false, "motor cannot combine with unverified 3D portal orientation");
    rejectedGate = roomscaleGate; rejectedGate.gameplay = false;
    expect(rejectedGate.Allowed(), false, "motor is disabled outside a level");
    rejectedGate = roomscaleGate; rejectedGate.cursorVisible = true;
    expect(rejectedGate.Allowed(), false, "motor is disabled in pause menus");
    rejectedGate = roomscaleGate; rejectedGate.hasPlayer = false;
    expect(rejectedGate.Allowed(), false, "motor is disabled without a local player");

    // A body echo must remove the same displacement from the visual offset.
    // Wrong-sign compensation or open-loop commands fail these fixtures.
    const auto motorTime = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);
    RoomscaleMotion::Motor motor;
    motor.OnRender({100, 200, 64}, {0, 0, 0}, 1, 1, 43.2f, motorTime);
    expect(motor.OnCommand(1, {1, 0, 0}, false, motorTime).has_value(), false,
           "roomscale baseline does not request movement");
    auto motorView = motor.OnRender({100, 200, 64}, {4, 0, 2}, 2, 1, 43.2f, motorTime);
    auto motorMove = motor.OnCommand(2, {1, 0, 0}, false, motorTime);
    expect(motorMove.has_value(), true, "physical step requests bounded Source movement");
    if (motorMove) {
        expectNear(motorMove->forward, 64.0f, "motor applies positional feedback gain");
        expectNear(motorMove->side, 0.0f, "motor does not add a perpendicular step");
    }
    expectNear(motorView.x, 4.0f, "camera follows head before body accepts step");
    expectNear(motorView.z, 2.0f, "motor preserves vertical HMD motion");
    expect(motor.OnCommand(2, {1, 0, 0}, false, motorTime).has_value(), false,
           "motor does not inject twice into a repeated command");
    motorView = motor.OnRender({104, 200, 64}, {4, 0, 2}, 2, 1, 43.2f, motorTime);
    expectNear(motorView.x, 0.0f, "accepted body displacement is not added to head twice");
    expect(motor.OnCommand(3, {1, 0, 0}, false, motorTime).has_value(), false,
           "accepted movement stops correction commands");
    motor.Reset();
    motorView = motor.OnRender({500, 500, 64}, {4, 0, 2}, 3, 1, 43.2f, motorTime);
    expectNear(motorView.x, 0.0f, "portal reset preserves compensated camera offset");
    expect(motor.OnCommand(4, {1, 0, 0}, false, motorTime).has_value(), false,
           "portal reset discards old movement target");
    motor.Reset(true);
    motorView = motor.OnRender({500, 500, 64}, {0, 0, 0}, 4, 1, 43.2f, motorTime);
    expectNear(motorView.x, 0.0f, "explicit recenter clears visual compensation");

    RoomscaleMotion::Motor blockedMotor;
    blockedMotor.OnRender({0, 0, 64}, {0, 0, 0}, 1, 1, 43.2f, motorTime);
    blockedMotor.OnRender({0, 0, 64}, {10, 0, 0}, 2, 1, 43.2f, motorTime);
    motorMove = blockedMotor.OnCommand(1, {1, 0, 0}, false, motorTime);
    expect(motorMove.has_value(), true, "wall-blocked step remains a movement request");
    motorView = blockedMotor.OnRender({0, 0, 64}, {20, 0, 0}, 3, 1, 43.2f, motorTime);
    expectNear(motorView.x, 8.0f, "wall-blocked camera residual is bounded");
    motorMove = blockedMotor.OnCommand(2, {0, 1, 0}, false, motorTime);
    if (motorMove) {
        expectNear(motorMove->forward, 0.0f, "movement projection respects view yaw");
        expectNear(motorMove->side, 175.0f, "movement request has bounded speed");
    } else expect(false, true, "large residual requests movement");
    blockedMotor.OnRender({0, 0, 64}, {10, 0, 0}, 4, 1, 43.2f, motorTime);
    blockedMotor.OnRender({0, 0, 64}, {0, 0, 0}, 5, 1, 43.2f, motorTime);
    expect(blockedMotor.OnCommand(3, {1, 0, 0}, false, motorTime).has_value(), false,
           "return after blocked step does not move body away from original position");
    blockedMotor.OnRender({0, 0, 64}, {0.2f, 0.2f, 0}, 6, 1, 43.2f, motorTime);
    expect(blockedMotor.OnCommand(4, {1, 0, 0}, false, motorTime).has_value(), false,
           "sub-centimeter tracking noise does not drive body");
    blockedMotor.OnRender({0, 0, 64}, {10, 0, 0}, 7, 1, 43.2f, motorTime);
    blockedMotor.Reset();
    blockedMotor.OnRender({100, 0, 64}, {10, 0, 0}, 8, 1, 43.2f, motorTime);
    motorView = blockedMotor.OnRender({100, 0, 64}, {20, 0, 0}, 9, 1, 43.2f, motorTime);
    expectNear(motorView.x, 8.0f, "repeated reset cannot stack wall lean beyond limit");

    RoomscaleMotion::Motor manualMotor;
    manualMotor.OnRender({0, 0, 64}, {0, 0, 0}, 1, 1, 43.2f, motorTime);
    manualMotor.OnRender({0, 0, 64}, {4, 0, 0}, 2, 1, 43.2f, motorTime);
    expect(manualMotor.OnCommand(1, {1, 0, 0}, true, motorTime).has_value(), false,
           "stick input suppresses roomscale request");
    manualMotor.OnRender({10, 0, 64}, {4, 0, 0}, 2, 1, 43.2f, motorTime);
    expect(manualMotor.OnCommand(2, {1, 0, 0}, false, motorTime).has_value(), false,
           "roomscale does not undo completed stick movement");
    manualMotor.OnRender({10, 0, 64}, {6, 0, 0}, 3, 1, 43.2f, motorTime);
    expect(manualMotor.OnCommand(3, {1, 0, 0}, false, motorTime).has_value(), true,
           "fresh physical movement resumes after stick release");
    expect(manualMotor.OnCommand(4, {1, 0, 0}, false,
                                 motorTime + std::chrono::milliseconds(251)).has_value(), false,
           "missing render feedback prevents stale movement");
    manualMotor.OnRender({10, 0, 64}, {6, 0, 0}, 3, 1, 43.2f, motorTime);
    expect(manualMotor.OnCommand(1, {1, 0, 0}, false, motorTime).has_value(), false,
           "command stream restart reanchors before issuing movement");

    RoomscaleMotion::Motor externalMotor;
    externalMotor.OnRender({0, 0, 64}, {0, 0, 0}, 1, 1, 43.2f, motorTime);
    externalMotor.OnCommand(1, {1, 0, 0}, false, motorTime);
    externalMotor.OnRender({2, 0, 64}, {0, 0, 0}, 2, 1, 43.2f, motorTime);
    expect(externalMotor.OnCommand(2, {1, 0, 0}, false, motorTime).has_value(), false,
           "idle external anchor motion is not counteracted");
    externalMotor.OnRender({2, 0, 64}, {4, 0, 0}, 3, 1, 43.2f, motorTime);
    motorView = externalMotor.OnRender({100, 0, 64}, {4, 0, 0}, 3, 1, 43.2f, motorTime);
    expect(externalMotor.OnCommand(3, {1, 0, 0}, false, motorTime).has_value(), false,
           "large Source anchor discontinuity cancels old target");
    externalMotor.OnRender({100, 0, 64}, {100, 0, 0}, 4, 1, 43.2f, motorTime);
    expect(externalMotor.OnCommand(4, {1, 0, 0}, false, motorTime).has_value(), false,
           "tracking relocalization is not requested as walking");
    externalMotor.OnRender({std::numeric_limits<float>::quiet_NaN(), 0, 64},
                           {100, 0, 0}, 5, 1, 43.2f, motorTime);
    expect(externalMotor.OnCommand(5, {1, 0, 0}, false, motorTime).has_value(), false,
           "nonfinite anchor disables movement");

    RoomscaleMotion::Motor scaledMotor;
    scaledMotor.OnRender({0, 0, 64}, {10, 0, 0}, 1, 1, 43.2f, motorTime);
    scaledMotor.OnRender({0, 0, 64}, {11.111111f, 0, 0}, 2, 1, 48.0f, motorTime);
    expect(scaledMotor.OnCommand(1, {1, 0, 0}, false, motorTime).has_value(), false,
           "scale transition is not interpreted as physical movement");
    RoomscaleMotion::Motor stalledMotor;
    stalledMotor.OnRender({0, 0, 64}, {0, 0, 0}, 1, 1, 43.2f, motorTime);
    stalledMotor.OnRender({0, 0, 64}, {4, 0, 0}, 2, 1, 43.2f, motorTime);
    stalledMotor.OnCommand(1, {1, 0, 0}, false, motorTime);
    stalledMotor.OnRender({0, 0, 64}, {4, 0, 0}, 2, 1, 43.2f,
                         motorTime + std::chrono::milliseconds(200));
    stalledMotor.OnRender({0, 0, 64}, {4, 0, 0}, 2, 1, 43.2f,
                         motorTime + std::chrono::milliseconds(300));
    expect(stalledMotor.OnCommand(2, {1, 0, 0}, false,
                                 motorTime + std::chrono::milliseconds(300)).has_value(), false,
           "repeated render cannot refresh an expired HMD pose");
    RoomscaleMotion::Motor preservedMotor;
    preservedMotor.OnRender({0, 0, 64}, {0, 0, 0}, 1, 1, 43.2f, motorTime);
    preservedMotor.OnRender({0, 0, 64}, {4, 0, 0}, 2, 1, 43.2f, motorTime);
    preservedMotor.Reset();
    motorView = preservedMotor.OnRender({100, 0, 64}, {4, 0, 2}, 3, 1, 43.2f, motorTime);
    expectNear(motorView.x, 4.0f, "reset preserves a nonzero compensated view offset");
    expectVectorNear(preservedMotor.ViewOffset({40, 50, 3}), {4, 0, 3},
                     "head and controller consumers share compensated XY with current vertical offset");

    RoomscaleMotion::Motor replayMotor;
    replayMotor.OnRender({0, 0, 64}, {0, 0, 0}, 1, 1, 43.2f, motorTime);
    replayMotor.OnRender({0, 0, 64}, {4, 0, 0}, 2, 1, 43.2f, motorTime);
    replayMotor.OnCommand(100, {1, 0, 0}, false, motorTime);
    replayMotor.OnCommand(99, {1, 0, 0}, false, motorTime);
    replayMotor.OnRender({0, 0, 64}, {4, 0, 0}, 3, 1, 43.2f, motorTime);
    replayMotor.OnRender({0, 0, 64}, {6, 0, 0}, 4, 1, 43.2f, motorTime);
    expect(replayMotor.OnCommand(100, {1, 0, 0}, false, motorTime).has_value(), false,
           "older callback cannot make an already injected command eligible again");
    replayMotor.Reset(false, true);
    replayMotor.OnRender({0, 0, 64}, {6, 0, 0}, 5, 1, 43.2f, motorTime);
    expect(replayMotor.OnCommand(1, {1, 0, 0}, false, motorTime).has_value(), false,
           "explicit new level stream starts with a fresh target");
    replayMotor.OnRender({0, 0, 64}, {8, 0, 0}, 6, 1, 43.2f, motorTime);
    expect(replayMotor.OnCommand(2, {1, 0, 0}, false, motorTime).has_value(), true,
           "new level stream can request fresh movement using restarted command numbers");

    RoomscaleMotion::StepAccumulator roomscale;
    roomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    expect(roomscale.Consume(100).has_value(), false, "first HMD sample establishes baseline");
    roomscale.Observe(true, {0.1f, 0.0f, 1.7f}, 2, 0.0f, 43.2f);
    const auto roomscaleStep = roomscale.Consume(101);
    expect(roomscaleStep.has_value(), true, "small fresh HMD step becomes one move intent");
    RoomscaleMotion::StepAccumulator newLevelObservation;
    newLevelObservation.Observe(true, {0, 0, 1.6f}, 1, 0, 43.2f);
    newLevelObservation.Observe(true, {0.1f, 0, 1.6f}, 2, 0, 43.2f);
    newLevelObservation.Consume(500);
    newLevelObservation.Reset(true);
    newLevelObservation.Observe(true, {0, 0, 1.6f}, 3, 0, 43.2f);
    newLevelObservation.Observe(true, {0.1f, 0, 1.6f}, 4, 0, 43.2f);
    expect(newLevelObservation.Consume(1).has_value(), true,
           "observer reset allows diagnostic commands from a new level stream");
    if (roomscaleStep)
        expectVectorNear(*roomscaleStep, {4.32f, 0.0f, 0.0f}, "physical step is horizontal Source units");
    expect(roomscale.Consume(101).has_value(), false, "same command does not repeat physical step");
    expect(roomscale.Consume(102).has_value(), false, "new command without pose does not repeat step");
    roomscale.Reset();
    roomscale.Observe(true, {1.0f, 0.0f, 1.6f}, 3, 0.0f, 43.2f);
    roomscale.Observe(true, {1.1f, 0.0f, 1.6f}, 4, 0.0f, 43.2f);
    expect(roomscale.Consume(102).has_value(), false,
           "recenter cannot replay an already consumed command number");

    RoomscaleMotion::StepAccumulator recoveredRoomscale;
    recoveredRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    recoveredRoomscale.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(recoveredRoomscale.Observe(false, {}, 3, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::TrackingLost, true, "tracking loss is reported once");
    expect(recoveredRoomscale.Consume(103).has_value(), false, "tracking loss drops unconsumed step");
    expect(recoveredRoomscale.Observe(false, {}, 4, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::NoStep, true, "repeated invalid pose is quiet");
    expect(recoveredRoomscale.Observe(true, {2.0f, 0.0f, 1.6f}, 5, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::TrackingRecovered, true, "recovery rebases without teleporting");
    expect(recoveredRoomscale.Consume(104).has_value(), false, "recovery sample has no movement");

    RoomscaleMotion::StepAccumulator discontinuousRoomscale;
    discontinuousRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    discontinuousRoomscale.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(discontinuousRoomscale.Observe(true, {1.1f, 0.0f, 1.6f}, 3, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::Discontinuity, true, "large tracking jump is rejected");
    expect(discontinuousRoomscale.Consume(1).has_value(), false, "jump clears earlier queued step");
    discontinuousRoomscale.Observe(true, {1.2f, 0.0f, 1.6f}, 4, 0.0f, 43.2f);
    const auto postJumpStep = discontinuousRoomscale.Consume(2);
    expect(postJumpStep.has_value(), true, "fresh step after discontinuity is accepted");
    if (postJumpStep)
        expectVectorNear(*postJumpStep, {4.32f, 0.0f, 0.0f}, "post-jump baseline is fresh");

    RoomscaleMotion::StepAccumulator changedMapping;
    changedMapping.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    changedMapping.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(changedMapping.Observe(true, {0.1f, 0.0f, 1.6f}, 3, 90.0f, 43.2f) ==
           RoomscaleMotion::Observation::MappingChanged, true, "artificial turn rebases pending movement");
    expect(changedMapping.Consume(1).has_value(), false, "turn is not a physical step");
    changedMapping.Observe(true, {0.1f, 0.1f, 1.6f}, 4, 90.0f, 43.2f);
    const auto turnedStep = changedMapping.Consume(2);
    expect(turnedStep.has_value(), true, "physical step after turn remains available");
    if (turnedStep)
        expectVectorNear(*turnedStep, {-4.32f, 0.0f, 0.0f}, "post-turn step uses new mapping");
    changedMapping.Reset();
    changedMapping.Observe(true, {9.0f, 9.0f, 1.6f}, 5, 90.0f, 43.2f);
    expect(changedMapping.Consume(3).has_value(), false, "recenter never generates catch-up step");

    RoomscaleMotion::StepAccumulator invalidRoomscale;
    invalidRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    expect(invalidRoomscale.Observe(true, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.6f},
                                    2, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::InvalidSample, true, "non-finite pose is rejected");
    expect(invalidRoomscale.Consume(1).has_value(), false, "invalid pose has no movement");

    RoomscaleMotion::StepAccumulator accumulatedRoomscale;
    accumulatedRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.05f, 0.0f, 1.65f}, 2, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.1f, 0.2f, 1.7f}, 3, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.2f, 0.2f, 1.7f}, 3, 0.0f, 43.2f);
    expect(accumulatedRoomscale.Consume(0).has_value(), false, "zero command does not consume physical step");
    const auto accumulatedStep = accumulatedRoomscale.Consume(1);
    expect(accumulatedStep.has_value(), true, "pose samples accumulate before command");
    if (accumulatedStep)
        expectVectorNear(*accumulatedStep, {4.32f, 8.64f, 0.0f},
                         "stale pose sequence ignored and vertical motion excluded");
    accumulatedRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 4, 0.0f, 43.2f);
    const auto returnStep = accumulatedRoomscale.Consume(2);
    expect(returnStep.has_value(), true, "return walk produces opposite physical intent");
    if (returnStep)
        expectVectorNear(*returnStep, {-4.32f, -8.64f, 0.0f},
                         "return walk cancels open-space intent mathematically");

    const Vector openVrPosition{1.0f, 2.0f, 3.0f};
    const Vector sourceMeters = TrackingSpace::OpenVrToSourceMeters(openVrPosition);
    expectVectorNear(sourceMeters, {-3.0f, -1.0f, 2.0f}, "OpenVR to Source axes");
    expectVectorNear(TrackingSpace::SourceToOpenVrMeters(sourceMeters), openVrPosition,
                     "Source to OpenVR inverse axes");
    expectVectorNear(TrackingSpace::RotateYawDegrees({1.0f, 0.0f, 2.0f}, 90.0f),
                     {0.0f, 1.0f, 2.0f}, "yaw 90 degrees");
    expectVectorNear(TrackingSpace::RotateYawDegrees({1.0f, 0.0f, 2.0f}, 180.0f),
                     {-1.0f, 0.0f, 2.0f}, "yaw 180 degrees");

    TrackingSpace::PlayspaceState seated;
    seated.scale = 50.0f;
    seated.yawDegrees = 90.0f;
    seated.Recenter({2.0f, 3.0f, 1.0f});
    expectVectorNear(seated.HmdOffsetUnits({2.0f, 3.0f, 1.0f}, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "seated recenter at nonzero yaw");
    const Vector seatedHmd{3.0f, 3.0f, 1.2f};
    expectVectorNear(seated.HmdOffsetUnits(seatedHmd, 70.0f),
                     {0.0f, 50.0f, 10.0f}, "seated HMD displacement at scale 50");
    expectVectorNear(seated.ControllerOffsetUnits({3.0f, 4.0f, 1.2f}, seatedHmd, 70.0f),
                     {-50.0f, 50.0f, 10.0f}, "controller uses HMD playspace transform");
    const auto leftOnly = seated.ControllerRelativeOffsetUnits(true,
        {3.0f, 4.0f, 1.2f}, seatedHmd, 70.0f);
    const auto rightLost = seated.ControllerRelativeOffsetUnits(false,
        {4.0f, 3.0f, 1.2f}, seatedHmd, 70.0f);
    expect(leftOnly.has_value(), true, "left position valid without right pose");
    if (leftOnly) expectVectorNear(*leftOnly, {-50.0f, 0.0f, 0.0f}, "left relative playspace offset");
    expect(rightLost.has_value(), false, "lost right position invalid independently");
    const auto rightReconnected = seated.ControllerRelativeOffsetUnits(true,
        {4.0f, 3.0f, 1.2f}, seatedHmd, 70.0f);
    expect(rightReconnected.has_value(), true, "right position recovers from fresh pose");
    if (rightReconnected) expectVectorNear(*rightReconnected,
        {0.0f, 50.0f, 0.0f}, "right relative offset after reconnect");

    TrackingSpace::PlayspaceState standing;
    standing.mode = TrackingSpace::TrackingMode::Standing;
    standing.scale = 50.0f;
    standing.heightOffsetMeters = 0.1f;
    standing.Recenter({2.0f, 3.0f, 1.7f});
    expectVectorNear(standing.HmdOffsetUnits({2.0f, 3.0f, 1.7f}, 70.0f),
                     {0.0f, 0.0f, 20.0f}, "standing floor height uses measured eye height");
    expectVectorNear(standing.HmdOffsetUnits({2.0f, 3.0f, 1.8f}, 70.0f),
                     {0.0f, 0.0f, 25.0f}, "standing HMD height change");
    const auto measuredEyeHeight = TrackingSpace::EyeHeightUnits(72.0f, 5.0f);
    expect(measuredEyeHeight.has_value(), true, "finite Source eye height available");
    if (measuredEyeHeight) expectNear(*measuredEyeHeight, 67.0f, "Source eye height difference");
    expect(TrackingSpace::EyeHeightUnits(std::numeric_limits<float>::quiet_NaN(), 5.0f).has_value(),
           false, "NaN eye position rejected");
    expect(TrackingSpace::EyeHeightUnits(72.0f, std::numeric_limits<float>::infinity()).has_value(),
           false, "infinite player origin rejected");
    const auto standingAnchor = TrackingSpace::StandingEyeAnchorUnits(1.7f, 43.2f);
    expect(standingAnchor.has_value(), true, "standing anchor accepts a tracked HMD height");
    if (standingAnchor)
        expectNear(*standingAnchor, 73.44f, "standing anchor uses the tracking height");
    expect(TrackingSpace::StandingEyeAnchorUnits(0.0f, 43.2f).has_value(), false,
           "standing anchor rejects an uncalibrated floor height");
    expect(TrackingSpace::StandingEyeAnchorUnits(1.7f, std::numeric_limits<float>::infinity()).has_value(),
           false, "standing anchor rejects an invalid scale");
    expectVectorNear(TrackingSpace::ControllerWorldOrigin({10.0f, 20.0f, 30.0f},
        {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, true),
        {15.0f, 27.0f, 39.0f}, "controller world origin shares the trace origin");
    expectVectorNear(TrackingSpace::ControllerWorldOrigin({10.0f, 20.0f, 30.0f},
        {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, false),
        {11.0f, 22.0f, 33.0f}, "controller world origin respects disabled 6DOF");

    expect(HudCapture::ShouldRedirectTarget(true, true, true, true, false, false), true,
           "opt-in VGUI paint redirects one render target");
    expect(HudCapture::ShouldRedirectTarget(false, true, true, true, false, false), false,
           "default VGUI paint leaves render targets alone");
    expect(HudCapture::ShouldRedirectTarget(true, true, true, false, false, false), false,
           "unrelated render-target pushes are not redirected");
    expect(HudCapture::ShouldRedirectTarget(true, true, true, true, true, false), false,
           "menu paint does not redirect the HUD");
    expect(HudCapture::ShouldRedirectTarget(true, true, true, true, false, true), false,
           "only the first target push per paint is redirected");
    expect(HudCapture::CanCapturePaint(true, true, true, true, true, true, false), true,
           "eligible in-game VGUI paint may capture HUD");
    expect(HudCapture::CanCapturePaint(true, true, true, true, false, true, false), false,
           "VGUI paint before stereo frame cannot capture HUD");
    expect(HudCapture::CanCapturePaint(true, true, true, false, true, true, false), false,
           "missing HUD shared target blocks paint capture");
    expect(HudCapture::CanCapturePaint(true, true, true, true, true, false, false), false,
           "menu VGUI paint cannot capture HUD");
    expect(HudCapture::CanCapturePaint(true, true, true, true, true, true, true), false,
           "visible cursor blocks paint capture");
    const auto croppedHud = HudCapture::WindowTextureCrop(2528, 2704, 1280, 720);
    expect(croppedHud.has_value(), true,
           "window-sized VGUI region can be mapped within the HUD texture");
    if (croppedHud) {
        expectNear(croppedHud->uMax, 0.506329f,
                   "HUD overlay crops to the Source window width");
        expectNear(croppedHud->vMax, 0.266272f,
                   "HUD overlay crops to the Source window height");
    }
    expect(HudCapture::WindowTextureCrop(2528, 2704, 0, 720).has_value(), false,
           "HUD crop rejects an unavailable window size");
    const auto oversizedHud = HudCapture::WindowTextureCrop(1280, 720, 2528, 2704);
    expect(oversizedHud.has_value(), true,
           "oversized windows retain the full HUD texture rather than hiding subtitles");
    if (oversizedHud) {
        expectNear(oversizedHud->uMax, 1.0f, "oversized HUD crop clamps horizontal bounds");
        expectNear(oversizedHud->vMax, 1.0f, "oversized HUD crop clamps vertical bounds");
    }
    HudCapture::RouteState hudRoute;
    expect(hudRoute.ShouldCaptureExplicitly(true, true, false), false,
           "HUD capture initially waits for Source target push");
    expect(hudRoute.ObserveRedirectPaint(false, false), true,
           "eligible paint without a nested target push arms explicit capture");
    expect(hudRoute.AllowsRedirect(), false,
           "explicit capture does not also redirect nested Source pushes");
    expect(hudRoute.ShouldCaptureExplicitly(true, false, false), false,
           "explicit HUD capture skips non-UI paint passes");
    expect(hudRoute.ShouldCaptureExplicitly(true, true, true), false,
           "explicit HUD capture runs at most once before submission");
    expect(hudRoute.ShouldCaptureExplicitly(true, true, false), true,
           "next eligible UI paint captures into the HUD target");
    expect(hudRoute.ObserveExplicitPaint(true), true,
           "unexpected nested target push disables unsafe explicit capture");
    expect(hudRoute.ShouldCaptureExplicitly(true, true, false), false,
           "disabled capture does not reuse an ambiguous render target");
    unsigned nestedPushDepth = 2;
    unsigned nestedPops = 0;
    HudCapture::UnwindNestedTargets(nestedPushDepth, [&] { ++nestedPops; });
    expectInt(nestedPops, 2,
           "explicit capture unwinds unmatched nested pushes before its outer target");
    expectInt(nestedPushDepth, 0,
           "nested target depth is reset only after matching pops");
    HudCapture::UnwindNestedTargets(nestedPushDepth, [&] { ++nestedPops; });
    expectInt(nestedPops, 2,
           "balanced explicit capture needs no nested target pops");
    expect(HudCapture::ShouldForwardPaintPop(true, 0), false,
           "unmatched Source pop cannot remove the explicit HUD target");
    expect(HudCapture::ShouldForwardPaintPop(true, 1), true,
           "Source pop can remove a Source target nested inside the HUD target");
    expect(HudCapture::ShouldForwardPaintPop(false, 0), true,
           "normal paint keeps Source pop behavior unchanged");
    HudCapture::RouteState sourcePushRoute;
    expect(sourcePushRoute.ObserveRedirectPaint(true, false), false,
           "Source target push preserves original redirect strategy");
    expect(sourcePushRoute.AllowsRedirect(), true,
           "original redirect remains available when Source pushes a target");
    expect(AimFeedback::ShouldRequestLaser(2, true, true, true, false), true,
           "laser request does not depend on Source crosshair paint");
    expect(AimFeedback::ShouldRequestLaser(1, true, true, true, false), false,
           "head-aim mode does not request controller laser");
    expect(AimFeedback::ShouldUseWorldAimMarker(2, true, true, true, true, false), true,
           "opted-in world marker follows a tracked right controller with a weapon");
    expect(AimFeedback::ShouldUseWorldAimMarker(2, false, true, true, true, false), false,
           "default controller aim does not change its legacy laser rendering");
    expect(AimFeedback::ShouldUseWorldAimMarker(1, true, true, true, true, false), false,
           "head-aim mode does not draw a controller line");
    expect(AimFeedback::ShouldUseWorldAimMarker(2, true, false, true, true, false), false,
           "missing Source debug overlay cannot request a world marker");
    expect(AimFeedback::ShouldUseWorldAimMarker(2, true, true, true, false, false), false,
           "putting away the portal gun removes the world marker");
    expect(AimFeedback::ShouldUseWorldAimMarker(2, true, true, false, true, false), false,
           "lost controller tracking removes the world marker");
    expect(AimFeedback::ShouldUseWorldAimMarker(2, true, true, true, true, true), false,
           "a visible game menu removes the world marker");
    expect(AimFeedback::ShouldInspectActiveWeaponForAim(2, false, false, false), false,
           "default aim skips weapon lookup when legacy laser symbols are absent");
    expect(AimFeedback::ShouldInspectActiveWeaponForAim(2, true, false, false), true,
           "legacy laser still inspects the active weapon when available");
    expect(AimFeedback::ShouldInspectActiveWeaponForAim(2, false, true, true), true,
           "enabled world marker inspects the active weapon with its overlay available");
    expect(AimFeedback::ShouldInspectActiveWeaponForAim(2, false, true, false), false,
           "missing overlay does not trigger an unsafe weapon lookup");
    const auto wallAim = AimFeedback::PrepareWorldAimGeometry(
        Vector{10, 20, 30}, Vector{110, 20, 30});
    expect(wallAim.has_value(), true, "finite wall hit produces aim geometry");
    if (wallAim) {
        expectVectorNear(wallAim->beamEnd, {110, 20, 30},
                         "nearby beam stays at the controller trace endpoint");
    }
    const auto distantAim = AimFeedback::PrepareWorldAimGeometry(
        Vector{10, 20, 30}, Vector{10010, 20, 30});
    expect(distantAim.has_value(), true, "long trace keeps a directional beam");
    if (distantAim) {
        expectVectorNear(distantAim->beamEnd, {10010, 20, 30},
                         "long trace beam reaches the actual shot trace endpoint");
    }
    expect(AimFeedback::PrepareWorldAimGeometry(Vector{0, 0, 0}, Vector{0, 0, 0}).has_value(), false,
           "zero-length aim trace has no beam");
    expect(AimFeedback::PrepareWorldAimGeometry(Vector{0, 0, 0},
        Vector{std::numeric_limits<float>::quiet_NaN(), 0, 0}).has_value(), false,
        "nonfinite aim trace has no beam");
    expectNear(AimFeedback::WorldAimOverlayLifetime(1.0f / 120.0f), 1.25f / 120.0f,
               "120 Hz game cadence limits aim history to roughly one frame");
    expectNear(AimFeedback::WorldAimOverlayLifetime(1.0f / 60.0f), 1.25f / 60.0f,
               "overlay lifetime follows actual game frame cadence rather than HMD refresh");
    expectNear(AimFeedback::WorldAimOverlayLifetime(0.2f), 0.05f,
               "a stalled frame cannot leave a long trail of old aim samples");
    expectNear(AimFeedback::WorldAimOverlayLifetime(0.0f), 1.25f / 90.0f,
               "first frame uses a short fallback interval");
    const Vector eyeForward{1, 0, 0};
    const Vector eyeRight{0, -1, 0};
    const Vector eyeUp{0, 0, 1};
    const auto eyeCenter = AimFeedback::ProjectWorldToEye(
        Vector{10, 0, 0}, Vector{0, 0, 0}, eyeForward, eyeRight, eyeUp,
        90.0f, 1.0f, 1000, 1000);
    expect(eyeCenter.has_value(), true, "world aim endpoint in front of an eye is projectable");
    if (eyeCenter) {
        expectNear(eyeCenter->x, 500.0f, "center hit projects to eye viewport center X");
        expectNear(eyeCenter->y, 500.0f, "center hit projects to eye viewport center Y");
    }
    const auto leftEye = AimFeedback::ProjectWorldToEye(
        Vector{10, 0, 0}, Vector{0, 0.1f, 0}, eyeForward, eyeRight, eyeUp,
        90.0f, 1.0f, 1000, 1000);
    const auto rightEye = AimFeedback::ProjectWorldToEye(
        Vector{10, 0, 0}, Vector{0, -0.1f, 0}, eyeForward, eyeRight, eyeUp,
        90.0f, 1.0f, 1000, 1000);
    expect(leftEye.has_value() && rightEye.has_value(), true,
           "both eye origins can independently project the same world hit");
    if (leftEye && rightEye) {
        expectNear(leftEye->x, 505.0f, "left eye projects the hit to its own screen coordinate");
        expectNear(rightEye->x, 495.0f, "right eye projects the hit to its own screen coordinate");
    }
    expect(AimFeedback::ProjectWorldToEye(Vector{-10, 0, 0}, Vector{0, 0, 0},
        eyeForward, eyeRight, eyeUp, 90.0f, 1.0f, 1000, 1000).has_value(), false,
        "hit behind the eye is not redrawn as a misleading reticle");
    expect(AimFeedback::IsCenteredReticleSprite(612, 318, 56, 84, 1280, 720), true,
           "native center reticle sprite remains eligible for stereo placement");
    expect(AimFeedback::IsCenteredReticleSprite(10, 10, 56, 84, 1280, 720), false,
           "unrelated HUD icons are not relocated to the aim endpoint");
    expect(AimFeedback::IsCenteredReticleSprite(500, 300, 400, 80, 1280, 720), false,
           "large center HUD panels are not mistaken for the reticle");
    expect(AimFeedback::IsReticleIconName("crosshair", "Crosshairs"), true,
           "Portal 2 weapon crosshair icon is recognized by its Source name");
    expect(AimFeedback::IsReticleIconName("portal_left", "hud/portal_crosshairs"), true,
           "portal-status texture is recognized by its Source material name");
    expect(AimFeedback::IsReticleIconName("crosshair", "sprites/qi_center"), true,
           "quick-info center icon remains eligible when used by Portal 2");
    expect(AimFeedback::IsReticleIconName("qi_center", "sprites/other"), true,
           "QuickInfo short name remains eligible when texture filename differs");
    expect(AimFeedback::IsReticleIconName("health", "sprites/health"), false,
           "unrelated centered Source HUD icon cannot be moved with the reticle");
    expect(AimFeedback::IsReticleIconName("", ""), false,
           "unreadable Source icon identity is not treated as a reticle");
    expect(AimFeedback::IsPortalStatusIconName("portal_left", "hud/portal_crosshairs"), true,
           "portal HUD texture is tracked separately from the weapon crosshair");
    expect(AimFeedback::IsPortalStatusIconName("quickinfo", "sprites/qi_center"), true,
           "QuickInfo center texture is tracked as a possible status route");
    expect(AimFeedback::IsPortalStatusIconName("crosshair", "Crosshairs"), false,
           "ordinary weapon crosshair is not mistaken for portal status");
    const auto atlasRect = AimFeedback::SourceHudAtlasRect(
        {0.5f / 256.0f, 0.5f / 256.0f, 43.5f / 256.0f, 63.5f / 256.0f},
        0, 44, 0, 64, 256, 256, 44, 64);
    expect(atlasRect.has_value(), true,
           "original Portal HUD subrectangle is accepted for direct eye rendering");
    if (atlasRect) {
        expectNear(atlasRect->x0, 0.0f, "atlas left texel is preserved");
        expectNear(atlasRect->y0, 0.0f, "atlas top texel is preserved");
        expectNear(atlasRect->x1, 43.0f, "atlas right texel is preserved");
        expectNear(atlasRect->y1, 63.0f, "atlas bottom texel is preserved");
    }
    expect(AimFeedback::SourceHudAtlasRect(
        {0.5f / 256.0f, 0.5f / 256.0f, 43.5f / 256.0f, 63.5f / 256.0f},
        0, 44, 0, 64, 256, 256, 44, 65).has_value(), false,
        "unexpected HUD sprite dimensions reject an unverified Source layout");
    expect(AimFeedback::SourceHudAtlasRect(
        {0.4f, 0.5f / 256.0f, 43.5f / 256.0f, 63.5f / 256.0f},
        0, 44, 0, 64, 256, 256, 44, 64).has_value(), false,
        "inconsistent texture coordinates cannot sample the wrong portal icon");
    const Portal2MaterialAbi::Probe queuedContext{
        0x50000000u, 0x6A4466CAu, 0x14D000u,
        0x5009BEF4u, 0x50027710u, 0x50025190u, 0x50027C40u};
    expect(Portal2MaterialAbi::Classify(queuedContext) == Portal2MaterialAbi::Kind::Queued,
           true, "verified queued context permits the stereo atlas draw");
    const Portal2MaterialAbi::Probe immediateContext{
        0x50000000u, 0x6A4466CAu, 0x14D000u,
        0x5009ED4Cu, 0x5002D8B0u, 0x5002CAF0u, 0x5002A510u};
    expect(Portal2MaterialAbi::Classify(immediateContext) == Portal2MaterialAbi::Kind::Immediate,
           true, "verified immediate context permits the stereo atlas draw");
    auto wrongViewportSlot = queuedContext;
    wrongViewportSlot.getViewport = 0x50016980u;
    expect(Portal2MaterialAbi::Classify(wrongViewportSlot) == Portal2MaterialAbi::Kind::Unsupported,
           true, "one-argument slot 40 cannot be mistaken for four-output GetViewport");
    auto changedEngineBuild = queuedContext;
    changedEngineBuild.timestamp = 0x6A4466CBu;
    expect(Portal2MaterialAbi::Classify(changedEngineBuild) == Portal2MaterialAbi::Kind::Unsupported,
           true, "unknown material-system build disables direct atlas rendering");
    const auto stereoReticle = AimFeedback::ProjectReticleSpriteToEye(
        Vector{10, 0, 0}, Vector{0, 0.1f, 0}, eyeForward, eyeRight, eyeUp,
        90.0f, 1.0f, 1000, 1000, 612, 318, 56, 84, 1280, 720);
    expect(stereoReticle.has_value(), true,
           "native center HUD icon can be repositioned for one eye");
    if (stereoReticle) {
        expectInt(stereoReticle->x, 477,
               "left-eye reticle follows the projected point in the eye target");
        expectInt(stereoReticle->y, 458,
               "left-eye reticle uses eye-target coordinates");
    }
    const auto nativeWindowReticle = AimFeedback::ProjectReticleSpriteToEye(
        Vector{10, 0, 0}, Vector{0, 0, 0}, eyeForward, eyeRight, eyeUp,
        90.0f, 1.0f, 2528, 2704, 612, 318, 56, 84, 1280, 720);
    expect(nativeWindowReticle.has_value(), true,
           "eye-size and window-size mismatch still projects the native reticle");
    if (nativeWindowReticle) {
        expectInt(nativeWindowReticle->x, 1236,
               "eye-center hit moves the native reticle to eye-target center X");
        expectInt(nativeWindowReticle->y, 1310,
               "eye-center hit moves the native reticle to eye-target center Y");
    }
    const auto eyeCanvasReticle = AimFeedback::ProjectReticleSpriteToEye(
        Vector{10, 0, 0}, Vector{0, 0, 0}, eyeForward, eyeRight, eyeUp,
        90.0f, 1.0f, 2528, 2704, 1236, 1310, 56, 84, 1280, 720);
    expect(eyeCanvasReticle.has_value(), true,
           "eye-sized Source HUD layout does not get rejected as an unrelated icon");
    if (eyeCanvasReticle) {
        expectInt(eyeCanvasReticle->x, 1236, "eye-sized source keeps its original center offset X");
        expectInt(eyeCanvasReticle->y, 1310, "eye-sized source keeps its original center offset Y");
    }
    TestReticleCanvas nativeCanvas;
    expectNear(NativeReticle::DistanceScale(true, 43.2f, 43.2f), 1.0f,
               "near targets retain the original readable reticle size");
    expectNear(NativeReticle::DistanceScale(true, 237.6f, 43.2f), 0.9f,
               "mid-distance reticle shrinks gently rather than inverse-distance disappearing");
    expectNear(NativeReticle::DistanceScale(true, 43200.0f, 43.2f), 0.8f,
               "distant targets cannot shrink the native portal status below eighty percent");
    expectNear(NativeReticle::DistanceScale(false, 432.0f, 43.2f), 1.0f,
               "disabled scaling keeps fixed reticle size");
    expectNear(NativeReticle::DistanceScale(true, -1.0f, 43.2f), 1.0f,
               "invalid target distance preserves fixed size");
    expectNear(NativeReticle::DistanceScale(true, 432.0f, 0.0f), 1.0f,
               "invalid world scale cannot divide by zero");
    expectNear(NativeReticle::DistanceScale(true, std::numeric_limits<float>::quiet_NaN(), 43.2f),
               1.0f, "nonfinite distance cannot corrupt native geometry");
    const auto scaledReticle = NativeReticle::ScaleLayout({1236, 1310}, {1264.0f, 1352.0f}, 56, 84, 0.8f);
    expect(scaledReticle.has_value(), true, "native reticle layout supports bounded distance scaling");
    if (scaledReticle) {
        expectInt(scaledReticle->position.x, 1242, "reticle scales toward its projected hit, not its own corner");
        expectInt(scaledReticle->position.y, 1318, "scaled reticle stays at the hit vertically");
        expectInt(scaledReticle->width, 45, "native sprite width scales without changing atlas UVs");
        expectInt(scaledReticle->height, 67, "native sprite height scales without changing aspect");
    }
    const auto fixedReticle = NativeReticle::ScaleLayout({1236, 1310}, {1264.0f, 1352.0f}, 56, 84, 1.0f);
    expect(fixedReticle.has_value(), true, "fixed-size rollback keeps original native layout");
    if (fixedReticle) {
        expectInt(fixedReticle->position.x, 1236, "fixed-size native sprite position unchanged");
        expectInt(fixedReticle->width, 56, "fixed-size native sprite dimensions unchanged");
    }
    expect(NativeReticle::ScaleLayout({1236, 1310}, {1264, 1352}, 56, 84, 0.0f).has_value(), false,
           "invalid reticle scale cannot send zero-sized geometry to Source");
    expect(NativeReticle::ScaleLayout({1236, 1310}, {1264, 1352}, INT_MAX, 84, 0.8f).has_value(), false,
           "unrelated oversized HUD texture cannot overflow scaled sprite dimensions");
    {
        NativeReticle::ClipScope<TestReticleCanvas> scope(nativeCanvas,
            nativeCanvas.rect, {0, 0, 2528, 2704});
        expect(nativeCanvas.rect.Contains(1236, 1310, 56, 84), true,
               "native Source reticle is not clipped to the desktop panel");
    }
    expect(nativeCanvas.rect.Contains(1236, 1310, 56, 84), false,
           "reticle scope restores the native clip for captions and other HUD panels");
    expectInt(nativeCanvas.rect.right, 1280, "original HUD clip width restored");
    expectInt(nativeCanvas.rect.bottom, 720, "original HUD clip height restored");
    const auto translatedReticle = NativeReticle::SurfacePosition({1236, 1310}, 15, -7);
    expect(translatedReticle.has_value(), true, "native panel translation can be removed safely");
    if (translatedReticle) {
        expectInt(translatedReticle->x, 1221, "native Surface adds X translation only once");
        expectInt(translatedReticle->y, 1317, "native Surface adds Y translation only once");
    }
    expect(NativeReticle::SurfacePosition({INT_MAX, 0}, -1, 0).has_value(), false,
           "native Surface coordinate rebasing cannot overflow");
    const NativeReticle::SurfaceProbe knownSurface{
        0x50000000, 0x6A4466CE, 0x198000, 0x500C4ED4, 0x5000AA10, 0x5000B8C0};
    expect(NativeReticle::Supported(knownSurface), true,
           "installed Source surface ABI allows the scoped native reticle route");
    auto changedSurface = knownSurface;
    ++changedSurface.timestamp;
    expect(NativeReticle::Supported(changedSurface), false,
           "unknown Source surface build must not use internal clipping calls");
    changedSurface = knownSurface;
    changedSurface.drawTexturedSubRect = 0x5000AB40;
    expect(NativeReticle::Supported(changedSurface), false,
           "different texture-draw ABI cannot use the native reticle fix");
    std::uintptr_t beamLookupCaller = 0;
    const Vector nativePortalColors[3] = {{255, 255, 255}, {64, 160, 255}, {255, 160, 32}};
    expectVectorNear(NativeBeam::ControlPointColor(1, nativePortalColors), {64, 160, 255},
                     "native beam color must not be silently halved before PCF remapping");
    expectVectorNear(NativeBeam::ControlPointColor(2, nativePortalColors), {255, 160, 32},
                     "orange beam retains native full-intensity RGB");
    expectVectorNear(NativeBeam::ControlPointColor(-1, nativePortalColors), {255, 255, 255},
                     "invalid negative portal color cannot index before the palette");
    expectVectorNear(NativeBeam::ControlPointColor(100, nativePortalColors), {255, 160, 32},
                     "invalid positive portal color cannot index past the palette");
    expect(NativeBeam::UsePlayerOwnedFallback(beamLookupCaller, 0x102808B3, "muzzle"), false,
           "normal native attachment lookups are never changed");
    {
        NativeBeam::CreationScope scope(beamLookupCaller, 0x102808B3);
        expect(NativeBeam::UsePlayerOwnedFallback(beamLookupCaller, 0x102808B3, "muzzle"), true,
               "only VR beam creation selects Source's player-owned fallback branch");
        expect(NativeBeam::UsePlayerOwnedFallback(beamLookupCaller, 0x1028095C, "muzzle"), false,
               "nested model lookups retain normal muzzle attachments");
        expect(NativeBeam::UsePlayerOwnedFallback(beamLookupCaller, 0x102808B3, "light"), false,
               "gun light attachments are never overridden");
        expect(NativeBeam::UsePlayerOwnedFallback(beamLookupCaller, 0x102808B3, nullptr), false,
               "null attachment names are not dereferenced");
        {
            NativeBeam::CreationScope nested(beamLookupCaller, 0x102908B3);
            expect(NativeBeam::UsePlayerOwnedFallback(beamLookupCaller, 0x102908B3, "muzzle"), true,
                   "nested scope selects its own native caller");
        }
        expect(NativeBeam::UsePlayerOwnedFallback(beamLookupCaller, 0x102808B3, "muzzle"), true,
               "nested creation restores the previous scope");
    }
    expect(NativeBeam::UsePlayerOwnedFallback(beamLookupCaller, 0x102808B3, "muzzle"), false,
           "beam creation immediately restores normal attachment behavior");
    expectInt(NativeBeam::FactoryAttachment(0x102808B3, 0x10280900, "robot_point_beam", 6, -1), 0,
              "our beam must not retain the native eyes-follow CP0 binding");
    expectInt(NativeBeam::FactoryAttachment(0, 0x10280900, "robot_point_beam", 6, -1), 6,
              "native non-VR creation keeps its eyes-follow binding");
    expectInt(NativeBeam::FactoryAttachment(0x102808B3, 0x10280901, "robot_point_beam", 6, -1), 6,
              "unrelated particle creation callers are never changed");
    expectInt(NativeBeam::FactoryAttachment(0x102808B3, 0x10280900, "portalgun_glow", 6, -1), 6,
              "portal-gun glow and other systems retain their native attachments");
    expectInt(NativeBeam::FactoryAttachment(0x102808B3, 0x10280900, nullptr, 6, -1), 6,
              "null particle name cannot be dereferenced");
    expectInt(NativeBeam::FactoryAttachment(0x102808B3, 0x10280900, "robot_point_beam", 5, 1), 5,
              "normal point-follow effects cannot be changed by the CP0 policy");
    expectInt(NativeBeam::FactoryAttachment(0x102808B3, 0x10280900, "robot_point_beam", 6, 0), 6,
              "an unexpected attachment index cannot enable the manual CP0 route");
    expectInt(NativeBeam::FactoryAttachment(UINTPTR_MAX, 0x4C, "robot_point_beam", 6, -1), 6,
              "wrapped return-address arithmetic cannot admit unrelated creation");
    NativeBeam::RenderProbe beamProbe;
    expect(beamProbe.ObserveDraw(0x1008, 1), false, "unknown particle cannot be logged as our beam");
    beamProbe.SetEffect(0x1000);
    expect(beamProbe.ObserveDraw(0x1000, 1), false, "particle primary object is not its renderable subobject");
    expect(beamProbe.ObserveDraw(0x1008, 1), true, "first beam draw in left-eye scope is recorded");
    expect(beamProbe.ObserveDraw(0x1008, 1), false, "beam render diagnostic never logs every frame");
    expect(beamProbe.ObserveDraw(0x1008, 2), true, "right-eye draw evidence is independent");
    expect(beamProbe.ObserveDraw(0x1008, 0), true, "queued/outside-stereo draws are classified separately");
    for (int i = 0; i < 119; ++i)
        expect(beamProbe.StereoPairSummary().has_value(), false, "summary waits for a bounded render observation window");
    const auto beamSummary = beamProbe.StereoPairSummary();
    expect(beamSummary.has_value(), true, "beam render summary is emitted once after 120 stereo pairs");
    if (beamSummary) expectInt(*beamSummary, 7, "summary retains left/right/outside-scope evidence");
    expect(beamProbe.StereoPairSummary().has_value(), false, "beam render summaries are not periodic spam");
    beamProbe.SetEffect(0);
    expect(beamProbe.ObserveDraw(0x1008, 2), false, "cleared effect cannot identify reused particle storage");
    NativeBeam::RenderProbe undrawnBeam;
    expect(undrawnBeam.StereoPairSummary().has_value(), false, "no observation timeout before an effect exists");
    undrawnBeam.SetEffect(UINTPTR_MAX);
    expect(undrawnBeam.ObserveDraw(7, 1), false, "overflowed renderable offset cannot match a particle");
    undrawnBeam.SetEffect(0x1000);
    for (int i = 0; i < 119; ++i) (void)undrawnBeam.StereoPairSummary();
    const auto missingDraw = undrawnBeam.StereoPairSummary();
    expect(missingDraw.has_value(), true, "missing DrawModel is reported rather than hidden by the creation log");
    if (missingDraw) expectInt(*missingDraw, 0, "unobserved native rendering is never reported as working");
    expect(AimFeedback::ClassifyReticleCanvas(1078, 1255, 56, 84, 1280, 720) ==
               AimFeedback::ReticleCanvasPosition::OutsideY, true,
           "eye-sized reticle below the reported Source canvas is diagnosed separately");
    expect(AimFeedback::ClassifyReticleCanvas(1799, 25, 56, 84, 1280, 720) ==
               AimFeedback::ReticleCanvasPosition::OutsideX, true,
           "reticle beside the reported Source canvas is diagnosed separately");
    expect(AimFeedback::ClassifyReticleCanvas(1236, 1310, 56, 84, 2528, 2704) ==
               AimFeedback::ReticleCanvasPosition::Inside, true,
           "eye-sized canvas accepts the projected center reticle");
    expect(AimFeedback::ClassifyReticleCanvas(1236, 1310, 56, 84, 0, 2704) ==
               AimFeedback::ReticleCanvasPosition::Invalid, true,
           "invalid reported VGUI size is not mistaken for clipping");
    expect(AimFeedback::ProjectReticleSpriteToEye(
        Vector{10, 0, 0}, Vector{0, 0, 0}, eyeForward, eyeRight, eyeUp,
        90.0f, 1.0f, 1000, 1000, 10, 10, 56, 84, 1280, 720).has_value(), false,
        "unrelated native HUD texture cannot be moved to the controller hit");
    const auto projectedCrosshair = AimFeedback::ProjectedCrosshairPosition(
        false, 711.811584f, 1847.891113f, 612, 318, 1280, 720, 2528, 2704);
    expect(projectedCrosshair.has_value(), true,
           "on-screen controller aim produces a crosshair position");
    if (projectedCrosshair) {
        expectInt(projectedCrosshair->x, 683,
               "crosshair projection preserves the Source sprite offset");
        expectInt(projectedCrosshair->y, 1805,
               "crosshair projection uses the VR viewport height");
    }
    const auto croppedCrosshair = AimFeedback::ProjectedCrosshairPosition(
        false, 711.811584f, 1847.891113f, 612, 318,
        1280, 720, 2528, 2704, 1280, 720);
    expect(croppedCrosshair.has_value(), true,
           "crosshair remains inside the cropped Source window overlay");
    if (croppedCrosshair) {
        expectInt(croppedCrosshair->x, 332,
               "HUD crosshair X is scaled from the eye target to the cropped window");
        expectInt(croppedCrosshair->y, 450,
               "HUD crosshair Y is scaled from the eye target to the cropped window");
    }
    expect(AimFeedback::ProjectedCrosshairPosition(true, 2141487616.0f, 21570732032.0f,
        612, 318, 1280, 720, 2528, 2704).has_value(), false,
        "clipped Source projection never becomes an overflowing sprite coordinate");
    expect(AimFeedback::ProjectedCrosshairPosition(false,
        std::numeric_limits<float>::quiet_NaN(), 100.0f,
        612, 318, 1280, 720, 2528, 2704).has_value(), false,
        "non-finite crosshair projection is rejected");
    expect(AimFeedback::ProjectedCrosshairPosition(false, 2800.0f, 100.0f,
        612, 318, 1280, 720, 2528, 2704).has_value(), false,
        "crosshair outside the VR viewport is not drawn at a misleading position");
    expect(AimFeedback::ShouldRequestLaser(2, true, false, true, false), false,
           "tracking loss stops controller laser requests");
    expect(AimFeedback::ShouldRequestLaser(2, true, true, true, true), false,
           "menus do not request controller laser");

    TrackingSpace::PlayspaceState turning;
    turning.scale = 50.0f;
    const Vector turnHmd{0.4f, 0.2f, 1.2f};
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "turn baseline head offset");
    turning.TurnAboutHmd(90.0f, turnHmd, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "first turn holds head pivot");
    expectVectorNear(turning.ControllerOffsetUnits({0.6f, 0.2f, 1.2f}, turnHmd, 70.0f),
                     {20.0f, 20.0f, 60.0f}, "controller rotates around head");
    turning.TurnAboutHmd(90.0f, turnHmd, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "repeated turns hold head pivot");
    turning.Recenter(turnHmd);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "recenter clears turn translation");
    const Vector lastRenderedOffset{12.0f, -8.0f, 5.0f};
    turning.PreserveOffsetOnRecovery({0.8f, -0.3f, 1.1f}, lastRenderedOffset, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits({0.8f, -0.3f, 1.1f}, 70.0f),
                     lastRenderedOffset, "tracking recovery holds last rendered offset");
    turning.Recenter({0.8f, -0.3f, 1.1f});
    expectVectorNear(turning.HmdOffsetUnits({0.8f, -0.3f, 1.1f}, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "recenter clears recovery compensation");
    expectNear(standing.centerMeters.z, 0.0f, "standing recenter keeps floor origin");

    const TrackingSpace::DeviceDirection hmdDirection{true, {1.0f, 0.0f, 0.0f}};
    const TrackingSpace::DeviceDirection leftDirection{true, {0.0f, 1.0f, 0.5f}};
    const TrackingSpace::DeviceDirection rightDirection{true, {-1.0f, 0.0f, 0.0f}};
    const TrackingSpace::DeviceDirection lostDirection{false, {0.0f, -1.0f, 0.0f}};
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::Hmd,
                     hmdDirection, leftDirection, rightDirection), {1.0f, 0.0f, 0.0f},
                     "default HMD movement direction");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, leftDirection, rightDirection), {0.0f, 1.0f, 0.0f},
                     "left movement ignores pitch");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, leftDirection, rightDirection), {-1.0f, 0.0f, 0.0f},
                     "right controller movement direction");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, lostDirection, rightDirection), {-1.0f, 0.0f, 0.0f},
                     "right direction does not require left controller");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, lostDirection, rightDirection), {1.0f, 0.0f, 0.0f},
                     "lost left controller falls back to HMD");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, leftDirection, {true, {0.001f, 0.0f, 1.0f}}),
                     {1.0f, 0.0f, 0.0f}, "near vertical controller falls back");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, leftDirection, lostDirection), {0.0f, 1.0f, 0.0f},
                     "left controller reconnect uses fresh pose");
    const auto defaultAxes = TrackingSpace::RebaseAnalogToView(0.25f, 0.8f,
                                                               hmdDirection.forward, hmdDirection.forward);
    expectNear(defaultAxes.forward, 0.8f, "HMD forward magnitude unchanged");
    expectNear(defaultAxes.side, 0.25f, "HMD side magnitude unchanged");
    const auto leftAxes = TrackingSpace::RebaseAnalogToView(0.25f, 0.8f,
                                                             leftDirection.forward, hmdDirection.forward);
    expectNear(leftAxes.forward, 0.25f, "left facing stick side becomes forward");
    expectNear(leftAxes.side, -0.8f, "left facing stick forward becomes side");

    DigitalButtonState attack;
    expectCommand(attack.HeldCommand(true, false, false, "+attack", "-attack"), nullptr, "released to released");
    expectCommand(attack.HeldCommand(true, true, true, "+attack", "-attack"), "+attack", "released to pressed");
    expectCommand(attack.HeldCommand(true, false, true, "+attack", "-attack"), nullptr, "pressed to pressed");
    expectCommand(attack.HeldCommand(true, true, false, "+attack", "-attack"), "-attack", "pressed to released");
    expectCommand(attack.HeldCommand(true, false, false, "+attack", "-attack"), nullptr, "released stays released");
    expectCommand(attack.HeldCommand(true, true, true, "+attack", "-attack"), "+attack", "press before tracking loss");
    expectCommand(attack.HeldCommand(false, false, false, "+attack", "-attack"), "-attack", "inactive action releases held command");
    expectCommand(attack.HeldCommand(false, false, false, "+attack", "-attack"), nullptr, "inactive action does not repeat release");

    expect(DigitalButtonState::PressEdge(true, false, false), false, "one shot released");
    expect(DigitalButtonState::PressEdge(true, true, true), true, "one shot press");
    expect(DigitalButtonState::PressEdge(true, false, true), false, "one shot hold");
    expect(DigitalButtonState::PressEdge(true, true, false), false, "one shot release");
    expect(DigitalButtonState::PressEdge(false, true, true), false, "inactive one shot");

    expect(IsUsableTrackedDeviceIndex(0, 64, 0xffffffffu), true, "HMD index is in range");
    expect(IsUsableTrackedDeviceIndex(63, 64, 0xffffffffu), true, "last tracked index is in range");
    expect(IsUsableTrackedDeviceIndex(64, 64, 0xffffffffu), false, "out of range tracked index");
    expect(IsUsableTrackedDeviceIndex(0xffffffffu, 64, 0xffffffffu), false, "invalid controller role index");

    const uint8_t image[] = { 0x11, 0x22, 0x33, 0x44 };
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x33, 0x44 }) == 2, true, "signature at image end");
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x33, 0x55 }) == -1, true, "signature mismatch");
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x44, 0x55 }) == -1, true, "no scan beyond image");

    ConfigSnapshot previous;
    previous.turnSpeed = 0.3f;
    previous.antiAliasing = 4;
    std::istringstream malformed("TurnSpeed=bogus\nVRScale=0\nIPDScale=nan\nAimMode=9\nAntiAliasing=3\nSnapTurning=maybe\nLeftHanded=true\n");
    const auto parsed = ParseConfig(malformed, previous);
    expect(parsed.value.turnSpeed == 0.3f, true, "bad turn speed preserves previous");
    expect(parsed.value.vrScale == 43.2f, true, "out of range scale preserves previous");
    expect(parsed.value.ipdScale == 1.0f, true, "nonfinite IPD preserves previous");
    expect(parsed.value.aimMode == 2, true, "bad aim mode preserves previous");
    expect(parsed.value.antiAliasing == 4, true, "unsupported AA preserves previous");
    expect(parsed.value.snapTurning == false, true, "bad boolean preserves previous");
    expect(parsed.value.leftHanded == true, true, "other valid field accepted");
    expect(parsed.errors.size() == 6, true, "all malformed config fields reported");
    std::istringstream good("TurnSpeed = 0.5 # comment\nSnapTurning=true\nAntiAliasing=8\nViewmodelPosCustomOffsetX=-2.5\n");
    const auto valid = ParseConfig(good, previous);
    expect(valid.errors.empty(), true, "valid config has no errors");
    expect(valid.value.turnSpeed == 0.5f, true, "trimmed float accepted");
    expect(valid.value.antiAliasing == 8, true, "valid AA accepted");
    expect(valid.value.viewmodelPosOffset[0] == -2.5f, true, "viewmodel offset accepted");
    std::istringstream modelOptions("ExperimentalViewmodelAlignment=true\n");
    expect(ConfigSnapshot{}.aimFromViewmodelMuzzle, false, "muzzle-origin beam is opt-in");
    std::istringstream muzzleOptions("AimFromViewmodelMuzzle=true\n");
    const auto muzzleConfig = ParseConfig(muzzleOptions, ConfigSnapshot{});
    expect(muzzleConfig.value.aimFromViewmodelMuzzle, true, "muzzle-origin config parses");
    std::istringstream badMuzzleOptions("AimFromViewmodelMuzzle=maybe\n");
    const auto retainedMuzzleConfig = ParseConfig(badMuzzleOptions, muzzleConfig.value);
    expect(retainedMuzzleConfig.value.aimFromViewmodelMuzzle, true,
           "invalid muzzle toggle preserves previous value");
    expect(retainedMuzzleConfig.errors.size() == 1, true, "invalid muzzle toggle is diagnosed");
    const auto modelConfig = ParseConfig(modelOptions, previous);
    expect(modelConfig.value.experimentalViewmodelAlignment, true,
           "viewmodel projection and pose correction require explicit opt-in");
    std::istringstream badModelOptions("ExperimentalViewmodelAlignment=maybe\n");
    const auto retainedModelConfig = ParseConfig(badModelOptions, modelConfig.value);
    expect(retainedModelConfig.value.experimentalViewmodelAlignment, true,
           "invalid viewmodel alignment toggle preserves previous value");
    expectInt(retainedModelConfig.errors.size(), 1,
              "invalid viewmodel alignment toggle is diagnosed");
    std::istringstream trailing("TurnSpeed=0.5junk\nRenderWindow=2\n");
    const auto rejected = ParseConfig(trailing, previous);
    expect(rejected.value.turnSpeed == previous.turnSpeed, true, "trailing numeric garbage rejected");
    expect(rejected.value.renderWindow == previous.renderWindow, true, "invalid render toggle rejected");

    ConfigSnapshot m2Defaults;
    expect(m2Defaults.trackingMode == TrackingSpace::TrackingMode::Seated, true,
           "legacy config defaults to seated tracking");
    expect(m2Defaults.movementDirection == TrackingSpace::MovementDirection::Hmd, true,
           "legacy config defaults to HMD locomotion");
    expectNear(m2Defaults.heightOffsetMeters, 0.0f, "default height offset");
    expectNear(m2Defaults.controllerPitchDegrees, -30.0f, "default controller pitch");
    expect(m2Defaults.roomscaleMode == RoomscaleMotion::Mode::Off, true,
           "roomscale observation defaults off");
    expect(m2Defaults.portalOrientationMode == PortalOrientation::Mode::LegacyYaw, true,
           "portal orientation keeps legacy yaw by default");
    expect(m2Defaults.experimentalHudOverlay, false,
           "unverified HUD overlay is disabled by default");
    expect(m2Defaults.experimentalWorldAimMarker, false,
           "unverified world-space aim marker is disabled by default");
    std::istringstream aimMarkerOption("ExperimentalWorldAimMarker=true\n");
    const auto enabledAimMarker = ParseConfig(aimMarkerOption, m2Defaults);
    expect(enabledAimMarker.value.experimentalWorldAimMarker, true,
           "world-space aim marker accepts explicit opt-in");
    std::istringstream badAimMarker("ExperimentalWorldAimMarker=maybe\n");
    const auto retainedAimMarker = ParseConfig(badAimMarker, enabledAimMarker.value);
    expect(retainedAimMarker.value.experimentalWorldAimMarker, true,
           "invalid world-space aim marker option keeps prior value");
    expect(retainedAimMarker.errors.size() == 1, true,
           "invalid world-space aim marker option is diagnosed");
    std::istringstream hudOptions("ExperimentalHUDOverlay=true\nHUDDistanceMeters=1.6\n"
                                  "HUDWidthMeters=1.4\nHUDVerticalOffsetMeters=-0.25\n");
    const auto hudConfig = ParseConfig(hudOptions, m2Defaults);
    expect(hudConfig.errors.empty(), true, "bounded experimental HUD settings parse");
    expect(hudConfig.value.experimentalHudOverlay, true, "HUD overlay requires explicit opt-in");
    expectNear(hudConfig.value.hudDistanceMeters, 1.6f, "HUD distance parsed");
    expectNear(hudConfig.value.hudWidthMeters, 1.4f, "HUD width parsed");
    expectNear(hudConfig.value.hudVerticalOffsetMeters, -0.25f, "HUD vertical offset parsed");
    std::istringstream hudBad("ExperimentalHUDOverlay=maybe\nHUDDistanceMeters=0\n"
                              "HUDWidthMeters=10\nHUDVerticalOffsetMeters=nan\n");
    const auto hudFallback = ParseConfig(hudBad, hudConfig.value);
    expect(hudFallback.errors.size() == 4, true, "invalid HUD fields are all diagnosed");
    expect(hudFallback.value.experimentalHudOverlay, true,
           "invalid HUD toggle retains prior valid value");
    expectNear(hudFallback.value.hudDistanceMeters, 1.6f, "invalid HUD distance retains prior");
    expectNear(hudFallback.value.hudWidthMeters, 1.4f, "invalid HUD width retains prior");
    expectNear(hudFallback.value.hudVerticalOffsetMeters, -0.25f,
               "invalid HUD vertical offset retains prior");
    expect(m2Defaults.experimentalPortalShotHaptics, false,
           "unverified portal-shot haptics default off");
    std::istringstream hapticOptions("ExperimentalPortalShotHaptics=true\n"
                                    "PortalShotHapticAmplitude=0.6\n"
                                    "PortalShotHapticDurationSeconds=0.08\n");
    const auto hapticConfig = ParseConfig(hapticOptions, m2Defaults);
    expect(hapticConfig.errors.empty(), true, "bounded portal-shot haptic settings parse");
    expect(hapticConfig.value.experimentalPortalShotHaptics, true,
           "haptic output requires explicit opt-in");
    expectNear(hapticConfig.value.portalShotHapticAmplitude, 0.6f, "haptic amplitude parsed");
    expectNear(hapticConfig.value.portalShotHapticDurationSeconds, 0.08f,
               "haptic duration parsed");
    std::istringstream hapticBad("ExperimentalPortalShotHaptics=maybe\n"
                                "PortalShotHapticAmplitude=1.2\n"
                                "PortalShotHapticDurationSeconds=nan\n");
    const auto hapticFallback = ParseConfig(hapticBad, hapticConfig.value);
    expect(hapticFallback.errors.size() == 3, true, "invalid haptic fields are diagnosed");
    expect(hapticFallback.value.experimentalPortalShotHaptics, true,
           "invalid haptic toggle retains prior value");
    expectNear(hapticFallback.value.portalShotHapticAmplitude, 0.6f,
               "out-of-range amplitude retains prior value");
    expectNear(hapticFallback.value.portalShotHapticDurationSeconds, 0.08f,
               "nonfinite duration retains prior value");

    Haptics::ShotGate shotGate;
    const auto shotStart = std::chrono::steady_clock::time_point{} + std::chrono::seconds(1);
    expect(shotGate.Queue(shotStart), true, "first shot queues one pulse");
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(20)), false,
           "duplicate callback cannot queue a second pending pulse");
    expect(shotGate.Consume(), true, "queued shot is delivered once");
    expect(shotGate.Consume(), false, "consumed shot cannot be delivered twice");
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(50)), false,
           "repeat shot inside 100 ms cooldown is suppressed");
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(100)), true,
           "shot at cooldown boundary is accepted");
    shotGate.Clear();
    expect(shotGate.Consume(), false, "menu or tracking loss discards pending shot");
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(101)), true,
           "discarded shot cannot suppress the first valid shot after recovery");
    shotGate.Clear();
    expect(shotGate.Queue(shotStart + std::chrono::milliseconds(200)), true,
           "new shot after discarded event and cooldown is accepted");
    Haptics::ShotGate delayedShots;
    delayedShots.Queue(shotStart);
    expect(delayedShots.Consume(shotStart + std::chrono::milliseconds(500)), true,
           "delayed first shot may deliver after a stalled frame");
    delayedShots.Queue(shotStart + std::chrono::milliseconds(501));
    expect(delayedShots.Consume(shotStart + std::chrono::milliseconds(501)), false,
           "a second pulse cannot follow immediately after a delayed pulse");
    expect(Haptics::OutputHand(false) == Haptics::Hand::Right, true,
           "right-handed aiming vibrates physical right hand");
    expect(Haptics::OutputHand(true) == Haptics::Hand::Left, true,
           "left-handed aiming vibrates physical left hand");
    expect(Haptics::CanDeliverShot(true, true, true, true, true, true), true,
           "ready local gameplay delivers a queued haptic shot");
    expect(Haptics::CanDeliverShot(false, true, true, true, true, true), false,
           "disabled haptics never reach OpenVR");
    expect(Haptics::CanDeliverShot(true, false, true, true, true, true), false,
           "failed action update drops pending haptics");
    expect(Haptics::CanDeliverShot(true, true, false, true, true, true), false,
           "menu focus drops pending haptics");
    expect(Haptics::CanDeliverShot(true, true, true, false, true, true), false,
           "invalid tracking drops pending haptics");
    expect(Haptics::CanDeliverShot(true, true, true, true, false, true), false,
           "disconnected firing controller drops pending haptics");
    expect(Haptics::CanDeliverShot(true, true, true, true, true, false), false,
           "missing output action drops pending haptics");
    expect(Haptics::IsLocalShot(1, 1), true,
           "local portal-gun owner qualifies as shot source");
    expect(Haptics::IsLocalShot(1, 2), false,
           "another player's portal gun cannot vibrate the local hand");
    expect(Haptics::IsLocalShot(0, 0), false,
           "missing local player cannot qualify as shot source");
    expect(Haptics::IsLocalShot(1, -1), false,
           "weapon without a resolved owner cannot qualify as shot source");

    expect(m2Defaults.haptics && m2Defaults.hapticsNativeRumble &&
           m2Defaults.hapticsHeldObject && m2Defaults.hapticsButtons, true,
           "game haptics default on");
    expectNear(m2Defaults.hapticsIntensity, 1.0f, "game haptics default full intensity");
    std::istringstream gameHapticOptions("Haptics=false\nHapticsIntensity=0.5\n"
                                         "HapticsNativeRumble=false\nHapticsHeldObject=false\n"
                                         "HapticsButtons=false\n");
    const auto gameHaptics = ParseConfig(gameHapticOptions, m2Defaults);
    expect(gameHaptics.errors.empty(), true, "game haptic settings parse");
    expect(gameHaptics.value.haptics || gameHaptics.value.hapticsNativeRumble ||
           gameHaptics.value.hapticsHeldObject || gameHaptics.value.hapticsButtons, false,
           "game haptic toggles parse");
    expectNear(gameHaptics.value.hapticsIntensity, 0.5f, "game haptic intensity parsed");
    std::istringstream gameHapticBad("Haptics=on\nHapticsIntensity=1.5\n");
    const auto gameHapticFallback = ParseConfig(gameHapticBad, gameHaptics.value);
    expect(gameHapticFallback.errors.size() == 2, true, "invalid game haptic fields diagnosed");
    expectNear(gameHapticFallback.value.hapticsIntensity, 0.5f,
               "out-of-range intensity retains prior value");

    const auto shot = Haptics::NativeRumblePattern(Haptics::kRumblePortalgunLeft, 0, 0);
    expect(shot.count == 1 && shot.mapped && shot.pulses[0].target == Haptics::Target::GunHand, true,
           "native portal-gun rumble pulses the gun hand once");
    const auto failure = Haptics::NativeRumblePattern(Haptics::kRumblePlacementFailure, 0, 0);
    expect(failure.count == 2 && failure.pulses[1].delaySeconds > 0.0f, true,
           "failed placement is a delayed double pulse");
    expect(Haptics::NativeRumblePattern(Haptics::kRumbleFallLong, 0, 0).pulses[0].target ==
           Haptics::Target::BothHands, true, "landing vibrates both hands");
    expectInt(Haptics::NativeRumblePattern(Haptics::kRumbleStopAll, 0, 0).count, 0,
              "stop-all emits no pulse");
    expectInt(Haptics::NativeRumblePattern(Haptics::kRumbleFallShort, 0, Haptics::kRumbleStop).count, 0,
              "stop flag emits no pulse");
    expectInt(Haptics::NativeRumblePattern(Haptics::kRumbleFallShort, 50, Haptics::kRumbleUpdateScale).count, 0,
              "scale update of a running effect emits no pulse");
    const auto unknown = Haptics::NativeRumblePattern(14, 0, 0);
    expect(unknown.count == 1 && !unknown.mapped, true, "unknown effect gets a generic pulse");
    const auto scaledFall = Haptics::NativeRumblePattern(Haptics::kRumbleFallLong, 50,
                                                     Haptics::kRumbleInitialScale);
    expectNear(scaledFall.pulses[0].amplitude,
               Haptics::NativeRumblePattern(Haptics::kRumbleFallLong, 0, 0).pulses[0].amplitude * 0.5f,
               "initial-scale data scales amplitude");
    const auto bounded = Haptics::ScaledPulse({Haptics::Target::GunHand, 9.0f, 9.0f, 9999.0f, 0.8f}, 2.0f);
    expectNear(bounded.amplitude, 0.8f, "intensity above one is clamped");
    expect(bounded.durationSeconds <= 0.25f && bounded.delaySeconds <= 0.25f &&
           bounded.frequency <= 320.0f, true, "pulse timing is bounded");
    expectNear(Haptics::ScaledPulse({Haptics::Target::GunHand, 0, 0.05f, 100, 0.8f},
               std::numeric_limits<float>::quiet_NaN()).amplitude, 0.0f,
               "nonfinite intensity silences the pulse");

    Haptics::EventQueue gameQueue;
    std::array<Haptics::Pattern, Haptics::EventQueue::kCapacity> drained;
    const auto pickup = Haptics::EventPattern(Haptics::Event::Pickup);
    expect(gameQueue.Push(1, pickup, shotStart), true, "first game event queues");
    expect(gameQueue.Push(1, pickup, shotStart + std::chrono::milliseconds(30)), false,
           "duplicate game event inside 60 ms is dropped");
    expect(gameQueue.Push(2, pickup, shotStart + std::chrono::milliseconds(30)), true,
           "different game event is not suppressed");
    expect(gameQueue.Push(1, pickup, shotStart + std::chrono::milliseconds(60)), true,
           "repeat game event after the window queues");
    expect(gameQueue.Push(3, {}, shotStart), false, "empty pattern never queues");
    expectInt(static_cast<int>(gameQueue.Drain(drained)), 3, "queued game events drain once");
    expectInt(static_cast<int>(gameQueue.Drain(drained)), 0, "drained queue is empty");
    for (int i = 0; i < 20; ++i)
        gameQueue.Push(100 + i, pickup, shotStart + std::chrono::seconds(2));
    expectInt(static_cast<int>(gameQueue.Drain(drained)), static_cast<int>(Haptics::EventQueue::kCapacity),
              "queue overflow is bounded");

    Haptics::HoldTracker hold;
    expect(hold.Poll(shotStart) == Haptics::HoldTracker::Change::None, true,
           "no held object before any grab update");
    hold.Update(shotStart);
    expect(hold.Poll(shotStart + std::chrono::milliseconds(10)) == Haptics::HoldTracker::Change::Pickup, true,
           "first grab update reports pickup");
    hold.Update(shotStart + std::chrono::milliseconds(100));
    expect(hold.Poll(shotStart + std::chrono::milliseconds(200)) == Haptics::HoldTracker::Change::None, true,
           "continuous grab updates keep holding");
    hold.Suspend(shotStart + std::chrono::seconds(5));
    expect(hold.Poll(shotStart + std::chrono::milliseconds(5100)) == Haptics::HoldTracker::Change::None, true,
           "paused game ticks do not report a drop");
    expect(hold.Poll(shotStart + std::chrono::seconds(6)) == Haptics::HoldTracker::Change::Drop, true,
           "missing grab updates report one drop");
    expect(hold.Poll(shotStart + std::chrono::seconds(7)) == Haptics::HoldTracker::Change::None, true,
           "drop is reported once");

    float poseMatrix[3][4] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}};
    float poseVelocity[3] = {0,0,0};
    float poseAngularVelocity[3] = {0,0,0};
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), true,
           "finite tracked pose is usable");
    poseMatrix[0][3] = std::numeric_limits<float>::quiet_NaN();
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), false,
           "nonfinite tracking position is rejected");
    poseMatrix[0][3] = 0;
    poseMatrix[1][2] = 1.01f;
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), false,
           "out-of-domain tracking rotation is rejected");
    poseMatrix[1][2] = 1.00001f;
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), true,
           "small matrix rounding error is accepted for clamped asin");
    poseMatrix[1][2] = 0;
    poseAngularVelocity[2] = std::numeric_limits<float>::infinity();
    expect(IsUsableTrackedPose(poseMatrix, poseVelocity, poseAngularVelocity), false,
           "nonfinite angular velocity is rejected");

    std::istringstream portalModeFull("PortalOrientationMode=FullRotation\n");
    const auto fullModeConfig = ParseConfig(portalModeFull, m2Defaults);
    expect(fullModeConfig.value.portalOrientationMode == PortalOrientation::Mode::FullRotation,
           true, "full portal rotation requires explicit config");
    std::istringstream portalModeHorizon("PortalOrientationMode=PreserveHorizon\n");
    const auto horizonModeConfig = ParseConfig(portalModeHorizon, fullModeConfig.value);
    expect(horizonModeConfig.value.portalOrientationMode == PortalOrientation::Mode::PreserveHorizon,
           true, "horizon-preserving mode parses");
    std::istringstream portalModeBad("PortalOrientationMode=Magic\n");
    const auto badPortalMode = ParseConfig(portalModeBad, horizonModeConfig.value);
    expect(badPortalMode.value.portalOrientationMode == PortalOrientation::Mode::PreserveHorizon,
           true, "invalid portal mode retains prior valid mode");
    expect(badPortalMode.errors.empty(), false, "invalid portal mode is diagnosed");
    std::istringstream roomscaleObserve("RoomscaleMode=Observe\n");
    const auto observedConfig = ParseConfig(roomscaleObserve, m2Defaults);
    expect(observedConfig.errors.empty(), true, "roomscale observe config is valid");
    expect(observedConfig.value.roomscaleMode == RoomscaleMotion::Mode::Observe, true,
           "roomscale observe config selects no-motion diagnostics");
    std::istringstream roomscaleActive("RoomscaleMode=ActiveExperimental\n");
    const auto activeConfig = ParseConfig(roomscaleActive, m2Defaults);
    expect(activeConfig.errors.empty(), true, "explicit experimental roomscale config is valid");
    expect(activeConfig.value.roomscaleMode == RoomscaleMotion::Mode::ActiveExperimental, true,
           "active roomscale requires explicit experimental mode name");
    std::istringstream roomscaleBad("RoomscaleMode=Active\n");
    const auto badRoomscaleConfig = ParseConfig(roomscaleBad, observedConfig.value);
    expect(badRoomscaleConfig.value.roomscaleMode == RoomscaleMotion::Mode::Observe, true,
           "invalid roomscale config retains previous mode");
    expect(badRoomscaleConfig.errors.size() == 1, true,
           "unsupported roomscale activation is diagnosed");
    RoomscaleMotion::Observer diagnostic;
    diagnostic.OnPose(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f, true);
    diagnostic.OnPose(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(1, true).has_value(), false, "off roomscale mode ignores poses and commands");
    expect(diagnostic.SetMode(RoomscaleMotion::Mode::Observe), true, "observe mode transition reported");
    diagnostic.OnPose(true, {5.0f, 0.0f, 1.6f}, 3, 0.0f, 43.2f, true);
    diagnostic.OnPose(true, {5.1f, 0.0f, 1.6f}, 4, 0.0f, 43.2f, true);
    diagnostic.OnPose(true, {9.0f, 0.0f, 1.6f}, 4, 0.0f, 43.2f, true);
    const auto observedStep = diagnostic.OnCommand(2, true);
    expect(observedStep.has_value(), true, "observe mode computes diagnostic intent");
    if (observedStep)
        expectVectorNear(*observedStep, {4.32f, 0.0f, 0.0f}, "observe mode reports unmoved intent");
    const auto observedSummary = diagnostic.TakeSummary();
    expect(observedSummary.steps == 1, true, "diagnostic summary counts consumed steps");
    expectNear(observedSummary.distanceUnits, 4.32f, "diagnostic summary measures intended distance");
    expect(diagnostic.TakeSummary().steps == 0, true, "diagnostic summary drains once");
    diagnostic.OnPose(true, {5.2f, 0.0f, 1.6f}, 5, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(3, false).has_value(), false,
           "menu or missing gameplay gate discards pending intent");
    diagnostic.OnPose(true, {8.0f, 0.0f, 1.6f}, 6, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(4, true).has_value(), false,
           "return from menu establishes a fresh baseline");
    diagnostic.OnPose(true, {8.1f, 0.0f, 1.6f}, 7, 0.0f, 43.2f, false);
    expect(diagnostic.OnCommand(5, true).has_value(), false,
           "poses observed in menus do not queue movement");
    diagnostic.OnPose(false, {}, 8, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(6, true).has_value(), false,
           "invalid HMD pose blocks command consumption");
    expect(diagnostic.SetMode(RoomscaleMotion::Mode::Off), true, "disabling observation reported");
    diagnostic.OnPose(true, {5.2f, 0.0f, 1.6f}, 9, 0.0f, 43.2f, true);
    expect(diagnostic.OnCommand(7, true).has_value(), false, "disabled observer emits no movement intent");
    std::istringstream m2Options("TrackingMode=Standing\nMovementDirection=LeftController\n"
                                 "HeightOffsetMeters=0.25\nControllerPitchDegrees=15\n");
    const auto m2Valid = ParseConfig(m2Options, m2Defaults);
    expect(m2Valid.errors.empty(), true, "valid M2 config has no errors");
    expect(m2Valid.value.trackingMode == TrackingSpace::TrackingMode::Standing, true,
           "standing mode parsed");
    expect(m2Valid.value.movementDirection == TrackingSpace::MovementDirection::LeftController,
           true, "left controller movement parsed");
    expectNear(m2Valid.value.heightOffsetMeters, 0.25f, "height offset parsed");
    expectNear(m2Valid.value.controllerPitchDegrees, 15.0f, "controller pitch parsed");
    std::istringstream m2Right("MovementDirection=RightController\nHeightOffsetMeters=-0.5\n"
                               "ControllerPitchDegrees=-60\n");
    const auto m2Boundary = ParseConfig(m2Right, m2Defaults);
    expect(m2Boundary.errors.empty(), true, "M2 lower bounds valid");
    expect(m2Boundary.value.movementDirection == TrackingSpace::MovementDirection::RightController,
           true, "right controller movement parsed");
    std::istringstream m2Bad("TrackingMode=Floor\nMovementDirection=Neither\n"
                             "HeightOffsetMeters=nan\nControllerPitchDegrees=61\nSeatedMode=true\n");
    const auto m2Rejected = ParseConfig(m2Bad, m2Valid.value);
    expect(m2Rejected.value.trackingMode == m2Valid.value.trackingMode, true,
           "invalid tracking mode retains previous");
    expect(m2Rejected.value.movementDirection == m2Valid.value.movementDirection, true,
           "invalid movement direction retains previous");
    expectNear(m2Rejected.value.heightOffsetMeters, 0.25f,
               "invalid height offset retains previous");
    expectNear(m2Rejected.value.controllerPitchDegrees, 15.0f,
               "invalid controller pitch retains previous");
    expect(m2Rejected.errors.size() == 5, true, "invalid M2 entries and legacy key reported");

    std::istringstream badReticleOptions("ExperimentalStereoReticle=maybe\nReticleDistanceScaling=0\n");
    expectInt(static_cast<int>(ParseConfig(badReticleOptions, ConfigSnapshot{}).errors.size()), 2,
              "invalid reticle renderer/scaling options must not silently change the working native path");
    std::istringstream fixedAtlasReticle("ExperimentalStereoReticle=true\nReticleDistanceScaling=false\n");
    const auto oldReticle = ParseConfig(fixedAtlasReticle, ConfigSnapshot{});
    expect(oldReticle.value.experimentalStereoReticle, true, "atlas rollback can be explicitly selected");
    expect(oldReticle.value.reticleDistanceScaling, false, "distance scaling can be explicitly disabled");
    std::istringstream malformedReticleReload("ExperimentalStereoReticle=1\nReticleDistanceScaling=1\n");
    const auto keptReticle = ParseConfig(malformedReticleReload, oldReticle.value);
    expect(keptReticle.value.experimentalStereoReticle, true, "malformed renderer reload retains previous choice");
    expect(keptReticle.value.reticleDistanceScaling, false, "malformed scaling reload retains fixed-size choice");

    std::istringstream badVerbose("VerboseDiagnostics=maybe\n");
    expectInt(static_cast<int>(ParseConfig(badVerbose, ConfigSnapshot{}).errors.size()), 1,
              "malformed verbose diagnostics cannot silently enable continuous logging");
    expect(ConfigSnapshot{}.verboseDiagnostics, false, "periodic diagnostic spam is off by default");
    std::istringstream enableVerbose("VerboseDiagnostics=true\n");
    const auto verbose = ParseConfig(enableVerbose, ConfigSnapshot{});
    expect(verbose.value.verboseDiagnostics, true, "detailed summaries remain available on explicit request");
    std::istringstream invalidVerbose("VerboseDiagnostics=1\n");
    expect(ParseConfig(invalidVerbose, verbose.value).value.verboseDiagnostics, true,
           "malformed diagnostics reload retains the last valid choice");

    const auto menuCrop = HudCapture::MenuTextureMapping(2528, 2704, 1280, 720, true);
    expect(menuCrop.has_value(), true, "menu mapping accepts a valid cropped window");
    if (menuCrop) {
        expectNear(menuCrop->bounds.uMax, 0.506329f, "menu uses submitted texture width");
        expectNear(menuCrop->bounds.vMax, 0.266272f, "menu uses submitted texture height");
        expectNear(menuCrop->texelAspect, 0.525888f, "menu texel aspect preserves crop proportions");
    }
    for (const auto &size : {std::array<int, 4>{0, 720, 1280, 720},
                            std::array<int, 4>{1280, 0, 1280, 720},
                            std::array<int, 4>{1280, 720, 0, 720},
                            std::array<int, 4>{1280, 720, 1280, 0},
                            std::array<int, 4>{1280, 720, -1, 720},
                            std::array<int, 4>{-1, 720, 1280, 720}}) {
        expect(HudCapture::MenuTextureMapping(size[0], size[1], size[2], size[3], true).has_value(),
               false, "unavailable dimensions never produce OpenVR menu geometry");
        expect(HudCapture::MenuTextureMapping(size[0], size[1], size[2], size[3], false).has_value(),
               false, "main menu also hides for unavailable window or texture dimensions");
    }
    const auto oversizedMenu = HudCapture::MenuTextureMapping(1280, 720, 2560, 360, true);
    if (oversizedMenu) {
        expectNear(oversizedMenu->bounds.uMax, 1.0f, "oversized menu width clamps to texture");
        expectNear(oversizedMenu->bounds.vMax, 0.5f, "menu height retains valid partial crop");
        expectNear(oversizedMenu->texelAspect, 0.5f, "menu aspect follows clamped bounds");
    } else expect(false, true, "oversized valid menu stays available");
    const auto fullMenu = HudCapture::MenuTextureMapping(2528, 2704, 1280, 720, false);
    if (fullMenu) {
        expectNear(fullMenu->bounds.uMax, 1.0f, "main menu uses entire submitted backbuffer");
        expectNear(fullMenu->texelAspect, 1.0f, "main menu retains square texel aspect");
    } else expect(false, true, "valid main menu remains available");
    const auto extremeMenu = HudCapture::MenuTextureMapping(
        (std::numeric_limits<int>::max)(), 1, 1, (std::numeric_limits<int>::max)(), true);
    expect(extremeMenu && std::isfinite(extremeMenu->texelAspect) && extremeMenu->texelAspect > 0,
           true, "extreme positive menu dimensions produce finite positive aspect");

    RenderTargetRetryState targetRetry;
    expect(targetRetry.CanAttempt(0, false), false, "unavailable device does not allocate or consume retry budget");
    expect(targetRetry.CanAttempt(0, true), true, "initial target allocation can proceed");
    targetRetry.RecordFailure(0);
    expect(targetRetry.CanAttempt(999, true), false, "failed targets do not retry every frame");
    expect(targetRetry.CanAttempt(1000, true), true, "transient target failure gets a delayed retry");
    targetRetry.RecordFailure(1000);
    targetRetry.RecordFailure(2000);
    expect(targetRetry.CanAttempt(100000, true), false, "persistent target failure exhausts bounded retry budget");
    targetRetry.ResetForDevice();
    expect(targetRetry.CanAttempt(100000, false), false, "new unavailable device still cannot allocate");
    expect(targetRetry.CanAttempt(100000, true), true, "new device lifecycle rearms target recovery");
    targetRetry.RecordFailure(100000);
    targetRetry.RecordSuccess();
    expect(targetRetry.CanAttempt(100001, true), true, "successful allocation resets transient failure budget");

    SharedRuntimeSession<int> sharedSession;
    int sessionObject = 7, sessionInitializations = 0, sessionShutdowns = 0;
    const auto startSession = [&]() { ++sessionInitializations; return &sessionObject; };
    const auto stopSession = [&]() { ++sessionShutdowns; };
    expect(sharedSession.Acquire(startSession) == &sessionObject, true, "first runtime lease initializes session");
    expect(sharedSession.Acquire(startSession) == &sessionObject, true, "second lease shares same runtime session");
    expectInt(sessionInitializations, 1, "D3D bootstrap and VR runtime never initialize OpenVR twice");
    sharedSession.Release(stopSession);
    expectInt(sessionShutdowns, 0, "bootstrap release does not shut down adopted session");
    sharedSession.Release(stopSession);
    expectInt(sessionShutdowns, 1, "last runtime owner shuts OpenVR down exactly once");
    sharedSession.Release(stopSession);
    expectInt(sessionShutdowns, 1, "extra release cannot duplicate runtime shutdown");
    expect(sharedSession.Acquire([]() -> int * { return nullptr; }) == nullptr, true,
           "failed OpenVR bootstrap publishes no session");
    sharedSession.Release(stopSession);
    expectInt(sessionShutdowns, 1, "failed initialization has no shutdown ownership");
    expect(sharedSession.Acquire(startSession) == &sessionObject, true, "runtime can recover after failed bootstrap");
    expectInt(sessionInitializations, 2, "fresh lifecycle initializes once after previous shutdown");
    sharedSession.Release(stopSession);

    if (failures) return 1;
    std::cout << "stabilization tests passed\n";
    return 0;
}
