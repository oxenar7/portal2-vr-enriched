#include "../L4D2VR/config.h"
#include "../L4D2VR/eye_hud.h"
#include "../L4D2VR/caption_follow.h"
#include "../L4D2VR/instructor_hints.h"
#include "../L4D2VR/binocular_zoom.h"
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

static void expect(bool value, const char *message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
struct Canvas {
    EyeHud::Rect rect{0, 0, 2528, 2704};
    void Viewport(int x, int y, int width, int height) { rect = {x, y, width, height}; }
};
static void nestedPaint(Canvas &canvas) {
    EyeHud::ViewportScope<Canvas> outer(canvas, canvas.rect, {10, 20, 1000, 900});
    {
        EyeHud::ViewportScope<Canvas> inner(canvas, canvas.rect, {30, 40, 500, 400});
        expect(canvas.rect.x == 30 && canvas.rect.width == 500, "inner paint uses its viewport");
    }
    expect(canvas.rect.x == 10 && canvas.rect.width == 1000, "nested paint restores outer viewport");
    return; // Early returns also restore the caller's viewport.
}

// Head at (0, 1.6, 0), looking along yaw/pitch in degrees.
static void headPose(float (&hmd)[3][4], float yawDegrees, float pitchDegrees) {
    const float radians = 3.14159265f / 180.0f;
    const float cy = std::cos(yawDegrees * radians), sy = std::sin(yawDegrees * radians);
    const float cp = std::cos(pitchDegrees * radians), sp = std::sin(pitchDegrees * radians);
    const float pose[3][4] = {{cy, sy * sp, sy * cp, 0}, {0, cp, -sp, 1.6f}, {-sy, cy * sp, cy * cp, 0}};
    std::memcpy(hmd, pose, sizeof(pose));
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void captionFollowTests() {
    const CaptionFollow::Settings settings{10.0f, 8.0f, 1.25f, -0.15f};
    const float dt = 1.0f / 120.0f;
    float hmd[3][4];
    CaptionFollow follow;
    headPose(hmd, 0, 0);
    auto first = follow.Update(hmd, dt, settings);
    expect(first && near(first->m[0][3], 0) && near(first->m[1][3], 1.45f) && near(first->m[2][3], -1.25f),
           "first placement is straight ahead, below the eye line");

    headPose(hmd, 8, 8);
    auto held = follow.Update(hmd, dt, settings);
    expect(held && near(held->m[0][3], 0) && near(held->m[1][3], 1.45f), "gaze inside the deadzone leaves the panel put");

    headPose(hmd, 0, 30);
    std::optional<CaptionFollow::Pose> up;
    for (int frame = 0; frame < 30; ++frame)
        up = follow.Update(hmd, dt, settings);
    expect(up && up->m[1][3] > 1.45f + 0.8f * 1.25f * 0.5f, "looking up lifts the panel within a quarter second");
    for (int frame = 0; frame < 240; ++frame)
        up = follow.Update(hmd, dt, settings);
    const float gazeHeight = 1.6f + 1.25f * 0.5f; // sin(30) along the panel distance
    expect(up && up->m[1][3] < gazeHeight && up->m[1][3] > gazeHeight - 0.2f, "panel settles slightly below the gaze");
    expect(up && std::fabs(up->m[1][2] + 0.5f) < 0.04f, "panel tilts to face the head, settling within 2 degrees");

    CaptionFollow wrap;
    headPose(hmd, 175, 0);
    wrap.Update(hmd, dt, settings);
    headPose(hmd, -170, 0);
    std::optional<CaptionFollow::Pose> around;
    for (int frame = 0; frame < 240; ++frame)
        around = wrap.Update(hmd, dt, settings);
    const float sinYaw = std::sin(-170.0f * 3.14159265f / 180.0f);
    expect(around && std::fabs(around->m[0][3] + 1.25f * sinYaw) < 0.05f, "yaw takes the short way across 180 degrees");

    CaptionFollow vertical;
    headPose(hmd, 0, 89);
    expect(!vertical.Update(hmd, dt, settings), "no first placement without a usable heading");
    headPose(hmd, 0, 0);
    hmd[0][0] = std::numeric_limits<float>::quiet_NaN();
    expect(!follow.Update(hmd, dt, settings), "non-finite pose rejected");
}

static void instructorHintTests() {
    using namespace InstructorHints;
    expect(ActionForBinding("+use") == Action::Use && ActionForBinding(" +JUMP;+use") == Action::Jump,
           "hint bindings name the first command, any case");
    expect(ActionForBinding("+attack2") == Action::SecondaryAttack && ActionForBinding("+attack") == Action::PrimaryAttack,
           "+attack2 is not +attack");
    expect(ActionForBinding("+zoom") == Action::Zoom, "+zoom is the binocular zoom action");
    expect(ActionForBinding("use") == Action::Use && ActionForBinding("Attack2") == Action::SecondaryAttack,
           "the game's own lessons name commands without the +");
    expect(ActionForBinding("-use") == Action::None && ActionForBinding("+") == Action::None,
           "only a leading + is optional");
    expect(ActionForBinding("+speed") == Action::None && ActionForBinding(nullptr) == Action::None,
           "commands without a VR action keep the key icon");
    using HintIcons::Id;
    expect(GlyphForInput("/user/hand/right", "/input/joystick", "click") == Id::RightStickPress,
           "the zoom click shows the right stick pressed");
    expect(GlyphForInput("/user/hand/left", "/input/joystick", "north") == Id::LeftStickUp &&
           GlyphForInput("/user/hand/right", "/input/thumbstick", "WEST") == Id::RightStickLeft &&
           GlyphForInput("/user/hand/right", "/input/joystick", "south") == Id::RightStickDown &&
           GlyphForInput("/user/hand/left", "/input/joystick", "east") == Id::LeftStickRight,
           "dpad slots show the stick arrows");
    expect(GlyphForInput("/user/hand/right", "/input/joystick", "position") == Id::RightStick &&
           GlyphForInput("/user/hand/left", "/input/joystick") == Id::LeftStick, "stick motion shows the plain stick");

    const auto a = GlyphForInput("/user/hand/right", "/input/a", "click");
    expect(a == Id::A && GlyphForInput("/user/hand/left", "/input/Y") == Id::Y &&
           GlyphForInput("/user/hand/right", "/input/b") == Id::B && GlyphForInput("/user/hand/left", "/input/x") == Id::X,
           "face buttons show their letter");
    expect(GlyphForInput("/user/hand/left", "/input/grip", "click") == Id::LeftGrip &&
           GlyphForInput("/user/hand/right", "/input/grip") == Id::RightGrip &&
           GlyphForInput("/user/hand/left", "/input/trigger", "click") == Id::LeftTrigger &&
           GlyphForInput("/user/hand/right", "/input/trigger", "pull") == Id::RightTrigger,
           "grips and triggers show their hand");
    expect(GlyphForInput("/user/hand/left", "/input/system") == Id::Menu &&
           GlyphForInput("/user/hand/right", "/input/system") == Id::System, "left menu, right Oculus button");
    expect(!GlyphForInput("/user/head", "/input/a") && !GlyphForInput("/user/hand/left", "/input/trackpad") &&
           !GlyphForInput("/user/hand/left", "/input/") && !GlyphForInput(nullptr, "/input/a") &&
           !GlyphForInput("/user/hand/left", "/input/q"), "other devices and inputs keep the key icon");

    TargetTable targets;
    int first = 0, second = 0;
    targets.SetBinding(&first, "+use");
    targets.SetGlyph(&first, a);
    targets.SetBinding(&first, "+use");
    expect(targets.GlyphFor(&first) == a && std::strcmp(targets.Binding(&first), "+use") == 0,
           "the same binding keeps the chosen glyph");
    targets.SetBinding(&first, "+jump");
    expect(!targets.GlyphFor(&first), "a new binding drops the old glyph");
    expect(!targets.Binding(&second) && !targets.GlyphFor(&second), "unknown targets have nothing");
    std::vector<int> many(TargetTable::kCapacity + 4);
    for (int &target : many)
        targets.SetBinding(&target, "+duck");
    expect(std::strcmp(targets.Binding(&many.back()), "+duck") == 0 && !targets.Binding(&first),
           "a full table recycles the oldest target");

    // Head 1.6 m up in tracking space, facing -Z; Source world x forward, y left.
    const float hmd[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 1.6f}, {0, 0, 1, 0}};
    const float overlay[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 1.45f}, {0, 0, 1, -1.25f}};
    const HeadFrame head{{0, 0, 64}, {1, 0, 0}, {0, -1, 0}, {0, 0, 1}, 40.0f};
    const auto ahead = ProjectToOverlay({400, 0, 64}, head, hmd, overlay, 1.65f, 1280, 720);
    // Eye level lies 0.15 m above the panel centre: (0.5 - 0.15 / 0.928) * 720.
    expect(ahead && ahead->x == 640 && ahead->y == 244, "a target straight ahead lands above the panel centre");
    const auto right = ProjectToOverlay({400, -40, 64}, head, hmd, overlay, 1.65f, 1280, 720);
    expect(right && right->x == 737 && right->y == 244, "one metre right at ten metres moves 97 pixels right");
    const auto low = ProjectToOverlay({40, 0, 24}, head, hmd, overlay, 1.65f, 1280, 720);
    expect(low && low->x == 640 && low->y > 720, "a target at the feet lies below the panel");
    expect(!ProjectToOverlay({-400, 0, 64}, head, hmd, overlay, 1.65f, 1280, 720), "a target behind has no position");
    expect(!ProjectToOverlay({400, 0, 64}, head, hmd, overlay, 1.65f, 0, 720), "no window, no projection");
    const auto side = ProjectToOverlay({1, -400, 64}, head, hmd, overlay, 1.65f, 1280, 720);
    expect(side && side->x > 1280 * 10, "a target to the side lands far off the panel, still to the right");

    // The generated icon table: every icon inside the canvas, pixel data back to back.
    std::uint32_t offset = 0;
    bool packed = true;
    for (const auto &icon : HintIcons::kIcons) {
        packed = packed && icon.offset == offset && icon.width > 0 && icon.height > 0 &&
                 icon.left + icon.width <= HintIcons::kCanvas && icon.top + icon.height <= HintIcons::kCanvas;
        offset += icon.width * icon.height * 2u;
    }
    expect(packed && offset == sizeof(HintIcons::kPixels) &&
           std::size(HintIcons::kIcons) == static_cast<std::size_t>(Id::Count), "icon table matches its pixels");
    expect(std::strcmp(GlyphName(Id::RightStickPress), "RightStickPress") == 0 &&
           std::strcmp(GlyphName(Id::Count), "?") == 0, "icons are named in the log");

    const auto texel = [](const std::vector<std::uint8_t> &image, int size, float x, float y) {
        return &image[(static_cast<int>(y * size) * size + static_cast<int>(x * size)) * 4];
    };
    for (const int size : {76, 74, 40, 150}) {
        const auto iconA = RenderGlyph(Id::A, size);
        expect(iconA.size() == static_cast<std::size_t>(size) * size * 4u, "RGBA texture of the requested size");
        expect(texel(iconA, size, 0.02f, 0.02f)[3] == 0 && texel(iconA, size, 0.98f, 0.5f)[3] == 0,
               "outside the button stays transparent");
        // Left of the letter, inside the white disc.
        expect(texel(iconA, size, 0.25f, 0.5f)[0] > 200 && texel(iconA, size, 0.25f, 0.5f)[3] > 240,
               "the button face is opaque white");
        // The A's crossbar sits a little below the centre.
        bool ink = false;
        for (float y = 0.5f; y < 0.62f; y += 0.01f)
            ink = ink || (texel(iconA, size, 0.5f, y)[0] < 90 && texel(iconA, size, 0.5f, y)[3] > 240);
        expect(ink, "the letter stays dark and opaque");
    }
    expect(RenderGlyph(Id::Count, 8) == std::vector<std::uint8_t>(8 * 8 * 4, 0) && RenderGlyph(Id::A, 0).empty(),
           "unknown icons and empty sizes draw nothing");
    expect(GlyphKey(Id::LeftGrip) != GlyphKey(Id::RightGrip), "each icon gets its own texture");
}

static void binocularZoomTests() {
    using namespace BinocularZoom;
    // A 0.8 m window at 1 m spans 2 * atan(0.4) = 43.6 degrees; x3 shows 15.2.
    expect(std::fabs(RenderFovDegrees(0.8f, 1.0f, 3.0f) - 15.19f) < 0.05f, "magnification narrows the view");
    expect(std::fabs(RenderFovDegrees(0.8f, 1.0f, 1.0f) - 43.60f) < 0.05f, "x1 matches the window's own angle");

    const Outline outline = OutlineFor(kTextureWidth, kTextureHeight);
    expect(outline.radius == 320.0f && outline.leftX == 320.0f && outline.rightX == 704.0f &&
           outline.seamX == 512.0f, "two lenses fill the target and meet in the middle");

    const Mask mask = MaskFor(kTextureWidth, kTextureHeight);
    expect(!mask.lenses.empty() && mask.lenses.size() % 3 == 0 && mask.leftShade.size() % 3 == 0 &&
           mask.rightShade.size() == mask.leftShade.size(), "whole triangles for both lenses");
    bool inside = true, outerClear = true;
    for (const Vertex &vertex : mask.lenses) {
        inside = inside && vertex.x >= -0.6f && vertex.x <= kTextureWidth - 0.4f &&
                 vertex.y >= -0.6f && vertex.y <= kTextureHeight - 0.4f;
        const float dx = vertex.x + 0.5f - (vertex.x < outline.seamX ? outline.leftX : outline.rightX);
        const float dy = vertex.y + 0.5f - outline.centerY;
        if (std::sqrt(dx * dx + dy * dy) > outline.radius - 0.01f)
            outerClear = outerClear && (vertex.color >> 24) == 0;
    }
    expect(inside, "lens geometry stays on the target");
    expect(outerClear, "the lens rim fades to transparent");
    expect((mask.lenses.front().color >> 24) == 255, "lens centres are opaque");
    bool leftOnLeft = true;
    for (const Vertex &vertex : mask.leftShade) {
        const float dx = vertex.x + 0.5f - outline.leftX, dy = vertex.y + 0.5f - outline.centerY;
        const float distance = std::sqrt(dx * dx + dy * dy);
        leftOnLeft = leftOnLeft && distance > outline.radius * (1.0f - kShadeWidth) - 0.5f &&
                     distance < outline.radius + 0.5f;
    }
    expect(leftOnLeft, "the left shade is a band inside the left lens rim");

    Hold hold;
    expect(hold.Update(true, 0.1f, -0.2f) == Hold::Change::Started && hold.Active(), "a centred click starts the zoom");
    expect(hold.Update(true, 0.9f, 0.0f) == Hold::Change::None && hold.Active(), "moving the stick while held keeps it");
    expect(hold.Update(false, 0, 0) == Hold::Change::Ended && !hold.Active(), "release ends the zoom");
    expect(hold.Update(true, 0.8f, 0.0f) == Hold::Change::Ignored && !hold.Active(), "a click while turning is ignored");
    expect(hold.Update(false, 0, 0) == Hold::Change::None, "releasing an ignored click is quiet");
    hold.Update(true, 0, 0);
    expect(hold.Cancel() && !hold.Active() && !hold.Cancel(), "a menu cancels once");
    expect(hold.Update(true, 0, 0) == Hold::Change::Started, "a click still held after the menu zooms again");

    const ConfigSnapshot defaults;
    expect(defaults.binocularZoom && defaults.zoomMagnification == 3.0f && defaults.zoomWindowWidthMeters == 0.8f &&
           defaults.zoomDistanceMeters == 1.0f, "zoom defaults");
    std::istringstream zoomConfig("BinocularZoom=false\nZoomMagnification=5\nZoomWindowWidthMeters=2\n");
    const auto zoomParsed = ParseConfig(zoomConfig, defaults);
    expect(!zoomParsed.value.binocularZoom && zoomParsed.value.zoomMagnification == 5.0f &&
           zoomParsed.value.zoomWindowWidthMeters == 0.8f && zoomParsed.errors.size() == 1,
           "zoom settings parse, out-of-range width rejected");
}

int main()
{
    captionFollowTests();
    instructorHintTests();
    binocularZoomTests();
    std::istringstream invalid("HUDInEyeScale=nan\nHUDInEyeVerticalOffset=0.5\nShowSourceCrosshair=invalid\n");
    const auto parsed = ParseConfig(invalid, ConfigSnapshot{});
    if (parsed.errors.size() != 3) {
        std::cerr << "FAIL: malformed eye HUD and crosshair settings must be rejected\n";
        return EXIT_FAILURE;
    }
    const ConfigSnapshot defaults;
    expect(!defaults.hudInEyeCentered && defaults.showSourceCrosshair, "optional centering off and crosshair visible");
    expect(defaults.hudInEyeScale == .45f && defaults.hudInEyeVerticalOffset == .05f, "HUD geometry defaults");
    std::istringstream conflict("HUDInEyeCentered=true\nAimMode=2\n");
    const auto safe = ParseConfig(conflict, defaults);
    expect(!safe.value.hudInEyeCentered && !safe.notes.empty(), "incompatible startup centering safely disabled and reported");
    std::istringstream valid("HUDInEyeCentered=true\nAimMode=1\nHUDInEyeScale=0.2\nHUDInEyeVerticalOffset=-0.4\nShowSourceCrosshair=false\n");
    auto centered = ParseConfig(valid, defaults);
    expect(centered.errors.empty() && centered.value.hudInEyeCentered && !centered.value.showSourceCrosshair, "valid legacy configuration accepted");
    const auto reloaded = ApplyRuntimeConfig(defaults, centered, true);
    expect(!reloaded.value.hudInEyeCentered && !reloaded.notes.empty(), "enabling HUD hook requires restart");
    std::istringstream geometry("HUDInEyeScale=1\nHUDInEyeVerticalOffset=0.4\n");
    const auto active = ApplyRuntimeConfig(centered.value, ParseConfig(geometry, centered.value), true);
    expect(active.value.hudInEyeScale == 1 && active.value.hudInEyeVerticalOffset == .4f, "installed HUD geometry can reload");
    std::istringstream captionConflict("HUDInEyeCentered=true\nAimMode=1\nExperimentalHUDOverlay=true\n");
    const auto captionSafe = ParseConfig(captionConflict, defaults);
    expect(!captionSafe.value.hudInEyeCentered && captionSafe.value.experimentalHudOverlay, "startup fallback retains caption settings");
    std::istringstream modeConflict("AimMode=2\n");
    const auto modeSafe = ApplyRuntimeConfig(centered.value, ParseConfig(modeConflict, centered.value), true);
    expect(modeSafe.value.hudInEyeCentered && modeSafe.value.aimMode == 1 && !modeSafe.errors.empty(), "incompatible active reload preserves all prior settings");
    for (const char *text : {"HUDInEyeScale=0.19", "HUDInEyeScale=1.01", "HUDInEyeScale=inf",
        "HUDInEyeVerticalOffset=-0.41", "HUDInEyeVerticalOffset=0.41", "HUDInEyeVerticalOffset=nan"}) {
        std::istringstream malformed(text);
        const auto rejected = ApplyRuntimeConfig(centered.value, ParseConfig(malformed, centered.value), true);
        expect(!rejected.errors.empty() && rejected.value.hudInEyeScale == centered.value.hudInEyeScale &&
            rejected.value.hudInEyeVerticalOffset == centered.value.hudInEyeVerticalOffset, "malformed geometry reload preserves active config");
    }

    EyeHud::GameplayScope scope{true, true, true, true, true, false, true, true, true};
    expect(EyeHud::CanCenter(scope, true, false, 1), "verified legacy eye can center");
    expect(!EyeHud::CanCenter(scope, false, false, 1), "centering is opt in");
    expect(!EyeHud::CanCenter(scope, true, true, 1), "caption overlay keeps its layout");
    expect(!EyeHud::CanCenter(scope, true, false, 2), "native reticle keeps full eye viewport");
    constexpr int uiPanels = 1, inGamePanels = 2, cursor = 4;
    expect(EyeHud::IsGameplayOnlyPaint(inGamePanels, inGamePanels), "gameplay-only paint remains eligible");
    expect(!EyeHud::IsGameplayOnlyPaint(uiPanels | inGamePanels, inGamePanels), "mixed UI/gameplay paint retains original viewport");
    expect(!EyeHud::IsGameplayOnlyPaint(cursor | inGamePanels, inGamePanels), "mixed cursor/gameplay paint retains original viewport");
    expect(!EyeHud::IsGameplayOnlyPaint(uiPanels | cursor | inGamePanels, inGamePanels), "all mixed paint flags retain original viewport");
    expect(!EyeHud::IsGameplayOnlyPaint(uiPanels, inGamePanels), "UI-only paint bypasses centering");
    expect(!EyeHud::IsGameplayOnlyPaint(cursor, inGamePanels), "cursor-only paint bypasses centering");
    expect(!EyeHud::IsGameplayOnlyPaint(0, inGamePanels), "empty paint bypasses centering");
    expect(!EyeHud::IsGameplayOnlyPaint(8 | inGamePanels, inGamePanels), "unknown paint bits bypass centering");
    for (int field = 0; field < 9; ++field) {
        auto bypass = scope;
        bool *fields[] = {&bypass.published, &bypass.vrEnabled, &bypass.trackingValid,
            &bypass.texturesReady, &bypass.inGame, &bypass.cursorVisible, &bypass.activeEye,
            &bypass.matchingEyeTarget, &bypass.verifiedContext};
        *fields[field] = field == 5;
        expect(!EyeHud::CanCenter(bypass, true, false, 1), "every unsupported state bypasses centering");
        expect(!EyeHud::ShouldHideFlatCrosshair(bypass, false, 1, "crosshair", "hud/crosshair"), "every unsupported state retains crosshair");
    }
    expect(EyeHud::ShouldHideFlatCrosshair(scope, false, 1, "CrossHair", "hud/flat"), "generic flat crosshair can hide");
    expect(!EyeHud::ShouldHideFlatCrosshair(scope, true, 1, "crosshair", ""), "visible crosshair default preserved");
    expect(!EyeHud::ShouldHideFlatCrosshair(scope, false, 2, "crosshair", ""), "native mode never filters artwork");
    expect(!EyeHud::ShouldHideFlatCrosshair(scope, false, 1, "portal_crosshair_left_invalid", ""), "portal status always retained");
    expect(!EyeHud::ShouldHideFlatCrosshair(scope, false, 0, "crosshair", "hud/portal_crosshair"), "portal status texture always retained");
    expect(!EyeHud::ShouldHideFlatCrosshair(scope, false, 1, "crosshair", "hud/qi_center"), "Portal center status retained");
    expect(!EyeHud::ShouldHideFlatCrosshair(scope, false, 1, "subtitle", "hud/caption"), "unrecognized HUD artwork retained");
    const auto rect = EyeHud::CenteredRect(2528, 2704, 1280, 720, .45f, .05f);
    expect(rect && rect->x == 695 && rect->y == 1167 && rect->width == 1137 && rect->height == 639, "centered viewport preserves window aspect and offset");
    expect(!EyeHud::CenteredRect(0, 2704, 1280, 720, .45f, .05f), "zero eye rejected");
    expect(!EyeHud::CenteredRect(2528, 2704, 0, 720, .45f, .05f), "zero window rejected");
    expect(!EyeHud::CenteredRect(1000, 1000, 1000, 1000, 1.f, .4f), "out of eye offset rejected");
    expect(!EyeHud::CenteredRect(INT_MAX, 1000, 1, INT_MAX, 1.f, 0.f), "height overflow rejected");
    expect(!EyeHud::CenteredRect(1000, 1000, 1000, 1000, std::numeric_limits<float>::quiet_NaN(), 0), "NaN rejected");
    expect(!EyeHud::CenteredRect(1000, 1000, 1000, 1000, .19f, 0), "scale outside contract rejected");
    const auto full = EyeHud::CenteredRect(1000, 1000, 1000, 1000, 1, 0);
    expect(full && full->x == 0 && full->y == 0 && full->width == 1000 && full->height == 1000, "maximum scale fits identical aspect");
    const auto lower = EyeHud::CenteredRect(1000, 1000, 1000, 1000, .2f, -.4f);
    expect(lower && lower->y == 0, "minimum scale and negative offset boundary fits");
    expect(!EyeHud::CenteredRect(1000, 1000, 1000, 1000, std::numeric_limits<float>::infinity(), 0), "infinite scale rejected");
    expect(!EyeHud::CenteredRect(1000, 1000, 1000, 1000, .45f, std::numeric_limits<float>::infinity()), "infinite offset rejected");
    Canvas canvas;
    nestedPaint(canvas);
    expect(canvas.rect.x == 0 && canvas.rect.y == 0 && canvas.rect.width == 2528 && canvas.rect.height == 2704, "early return restores full caller viewport");
    try {
        EyeHud::ViewportScope<Canvas> scope(canvas, canvas.rect, {10, 20, 1000, 900});
        throw 1;
    } catch (int) {}
    expect(canvas.rect.width == 2528 && canvas.rect.height == 2704, "exception unwind restores caller viewport");
    std::cout << "PCVR HUD tests passed\n";
}
