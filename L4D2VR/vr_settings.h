#pragma once

// In-menu VR options: setting table, value stepping, the in-game overrides
// file and Portal 2 dialog geometry. No engine or Windows dependencies.

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <istream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace VrSettings {

enum class Kind { Toggle, Choice, Number, Link };
enum class Unit { Plain, Degrees, Percent, Meters, Multiplier };

struct Choice {
    const char *value;
    const char *label;
};

struct Item {
    const char *key = nullptr; // config key; null for links
    const char *label = "";
    Kind kind = Kind::Toggle;
    std::string (*read)(const ConfigSnapshot &) = nullptr;
    bool (*enabled)(const ConfigSnapshot &) = nullptr;
    std::vector<Choice> choices;
    std::vector<float> presets;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float step = 0.0f;
    Unit unit = Unit::Plain;
    bool appliesOnRecenter = false;
    int link = -1;
};

enum class FooterAction { Back, UseDefaults, Recenter };

struct FooterButton {
    const char *label;
    FooterAction action;
};

struct Page {
    const char *title;
    std::vector<Item> items;
    std::vector<FooterButton> footer;
};

inline const char *kOverridesPath = "VR\\ingame_settings.txt";

inline std::string Bool(bool value) { return value ? "true" : "false"; }

inline std::string Number(float value)
{
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.6g", value);
    return text;
}

inline Item Toggle(const char *key, const char *label, std::string (*read)(const ConfigSnapshot &),
                   bool (*enabled)(const ConfigSnapshot &) = nullptr)
{
    Item item;
    item.key = key;
    item.label = label;
    item.kind = Kind::Toggle;
    item.read = read;
    item.enabled = enabled;
    return item;
}

inline Item Numeric(const char *key, const char *label, std::string (*read)(const ConfigSnapshot &),
                    float minimum, float maximum, float step, Unit unit,
                    bool (*enabled)(const ConfigSnapshot &) = nullptr)
{
    Item item;
    item.key = key;
    item.label = label;
    item.kind = Kind::Number;
    item.read = read;
    item.enabled = enabled;
    item.minimum = minimum;
    item.maximum = maximum;
    item.step = step;
    item.unit = unit;
    return item;
}

inline const std::vector<Page> &Pages()
{
    static const std::vector<Page> pages = [] {
        Item snapAngle = Numeric("SnapTurnAngle", "Snap Turn Angle",
            [](const ConfigSnapshot &c) { return Number(c.snapTurnAngle); }, 15.0f, 90.0f, 0.0f, Unit::Degrees,
            [](const ConfigSnapshot &c) { return c.snapTurning; });
        snapAngle.presets = {15.0f, 30.0f, 45.0f, 60.0f, 90.0f};

        Item direction;
        direction.key = "MovementDirection";
        direction.label = "Movement Direction";
        direction.kind = Kind::Choice;
        direction.read = [](const ConfigSnapshot &c) -> std::string {
            switch (c.movementDirection) {
            case TrackingSpace::MovementDirection::LeftController: return "LeftController";
            case TrackingSpace::MovementDirection::RightController: return "RightController";
            default: return "HMD";
            }
        };
        direction.choices = {{"HMD", "Head"}, {"LeftController", "Left Controller"},
                             {"RightController", "Right Controller"}};

        Item advanced;
        advanced.label = "Advanced VR";
        advanced.kind = Kind::Link;
        advanced.link = 1;

        const auto hapticsOn = [](const ConfigSnapshot &c) { return c.haptics; };

        Page main{"VR", {
            Toggle("SnapTurning", "Snap Turning", [](const ConfigSnapshot &c) { return Bool(c.snapTurning); }),
            snapAngle,
            Numeric("TurnSpeed", "Smooth Turn Speed", [](const ConfigSnapshot &c) { return Number(c.turnSpeed); },
                    0.05f, 1.0f, 0.05f, Unit::Plain, [](const ConfigSnapshot &c) { return !c.snapTurning; }),
            direction,
            Toggle("PhysicalCrouch", "Physical Crouch", [](const ConfigSnapshot &c) { return Bool(c.physicalCrouch); },
                   [](const ConfigSnapshot &c) { return c.sixDof; }),
            Toggle("LeftHanded", "Left-Handed Mode", [](const ConfigSnapshot &c) { return Bool(c.leftHanded); }),
            Toggle("Haptics", "Controller Vibration", [](const ConfigSnapshot &c) { return Bool(c.haptics); }),
            Numeric("HapticsIntensity", "Vibration Strength",
                    [](const ConfigSnapshot &c) { return Number(c.hapticsIntensity); },
                    0.0f, 1.0f, 0.1f, Unit::Percent, hapticsOn),
            Toggle("ExperimentalWorldAimMarker", "Aim Line",
                   [](const ConfigSnapshot &c) { return Bool(c.experimentalWorldAimMarker); }),
            advanced,
        }, {{"Back", FooterAction::Back}, {"Use Defaults", FooterAction::UseDefaults}}};

        Item heightOffset = Numeric("HeightOffsetMeters", "Standing Height Offset",
            [](const ConfigSnapshot &c) { return Number(c.heightOffsetMeters); }, -0.5f, 0.5f, 0.05f, Unit::Meters,
            [](const ConfigSnapshot &c) { return c.trackingMode == TrackingSpace::TrackingMode::Standing && c.sixDof; });
        heightOffset.appliesOnRecenter = true;
        Item worldScale = Numeric("VRScale", "World Scale", [](const ConfigSnapshot &c) { return Number(c.vrScale); },
                                  20.0f, 80.0f, 0.5f, Unit::Plain);
        worldScale.appliesOnRecenter = true;

        Item mirror;
        mirror.key = "RenderWindow";
        mirror.label = "Desktop Mirror";
        mirror.kind = Kind::Choice;
        mirror.read = [](const ConfigSnapshot &c) { return std::string(c.renderWindow ? "1" : "0"); };
        mirror.choices = {{"0", "Disabled"}, {"1", "Enabled"}};

        Page advancedPage{"Advanced VR", {
            // ActiveExperimental roomscale is only valid with positional tracking.
            Toggle("6DOF", "Positional Tracking", [](const ConfigSnapshot &c) { return Bool(c.sixDof); },
                   [](const ConfigSnapshot &c) { return c.roomscaleMode != RoomscaleMotion::Mode::ActiveExperimental; }),
            Numeric("ControllerPitchDegrees", "Controller Pitch",
                    [](const ConfigSnapshot &c) { return Number(c.controllerPitchDegrees); },
                    -60.0f, 60.0f, 5.0f, Unit::Degrees),
            heightOffset,
            Numeric("PhysicalCrouchDropMeters", "Crouch Head Drop",
                    [](const ConfigSnapshot &c) { return Number(c.physicalCrouchDropMeters); },
                    0.15f, 0.8f, 0.05f, Unit::Meters,
                    [](const ConfigSnapshot &c) { return c.physicalCrouch && c.sixDof; }),
            worldScale,
            Numeric("IPDScale", "IPD Scale", [](const ConfigSnapshot &c) { return Number(c.ipdScale); },
                    0.5f, 1.5f, 0.05f, Unit::Multiplier),
            Toggle("HapticsNativeRumble", "Game Rumble",
                   [](const ConfigSnapshot &c) { return Bool(c.hapticsNativeRumble); }, hapticsOn),
            Toggle("HapticsHeldObject", "Held Object Vibration",
                   [](const ConfigSnapshot &c) { return Bool(c.hapticsHeldObject); }, hapticsOn),
            Toggle("HapticsButtons", "Button Vibration",
                   [](const ConfigSnapshot &c) { return Bool(c.hapticsButtons); }, hapticsOn),
            Toggle("ReticleDistanceScaling", "Reticle Distance Scaling",
                   [](const ConfigSnapshot &c) { return Bool(c.reticleDistanceScaling); }),
            Toggle("LaserCubeAimAssist", "Laser Cube Aim Assist",
                   [](const ConfigSnapshot &c) { return Bool(c.laserCubeAimAssist); }),
            Toggle("HeldObjectAnchor", "Attach Objects To Gun",
                   [](const ConfigSnapshot &c) { return Bool(c.heldObjectAnchor); }),
            mirror,
        }, {{"Back", FooterAction::Back}, {"Recenter", FooterAction::Recenter}}};

        return std::vector<Page>{main, advancedPage};
    }();
    return pages;
}

inline bool IsMenuKey(const std::string &key)
{
    for (const auto &page : Pages())
        for (const auto &item : page.items)
            if (item.key && key == item.key) return true;
    return false;
}

inline bool Enabled(const Item &item, const ConfigSnapshot &config)
{
    return item.kind == Kind::Link || !item.enabled || item.enabled(config);
}

inline std::string TrimmedDecimal(float value, int decimals)
{
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    std::string result = text;
    if (result.find('.') != std::string::npos) {
        while (!result.empty() && result.back() == '0') result.pop_back();
        if (!result.empty() && result.back() == '.') result.pop_back();
    }
    return result == "-0" ? "0" : result;
}

inline std::optional<float> ParseNumber(const std::string &text)
{
    std::istringstream input(text);
    float value = 0.0f;
    char extra = 0;
    if (!(input >> value) || (input >> extra) || !std::isfinite(value)) return std::nullopt;
    return value;
}

inline std::string DisplayValue(const Item &item, const ConfigSnapshot &config)
{
    if (item.kind == Kind::Link || !item.read) return {};
    const std::string value = item.read(config);
    if (item.kind == Kind::Toggle) return value == "true" ? "Enabled" : "Disabled";
    if (item.kind == Kind::Choice) {
        for (const auto &choice : item.choices)
            if (value == choice.value) return choice.label;
        return value;
    }
    const float number = ParseNumber(value).value_or(0.0f);
    switch (item.unit) {
    case Unit::Degrees: return TrimmedDecimal(number, 1) + "\xC2\xB0";
    case Unit::Percent: return TrimmedDecimal(std::round(number * 100.0f), 0) + "%";
    case Unit::Meters: {
        char text[32]{};
        std::snprintf(text, sizeof(text), std::fabs(number) < 0.005f ? "%.2f m" : "%+.2f m", number);
        return text;
    }
    case Unit::Multiplier: {
        char text[32]{};
        std::snprintf(text, sizeof(text), "%.2fx", number);
        return text;
    }
    default: return TrimmedDecimal(number, 2);
    }
}

// Next config value in the given direction, or nullopt at the end of a range.
// Toggles flip and choices wrap like Portal 2 list buttons.
inline std::optional<std::string> Step(const Item &item, const ConfigSnapshot &config, int direction)
{
    if (item.kind == Kind::Link || !item.read || !Enabled(item, config)) return std::nullopt;
    const std::string current = item.read(config);
    if (item.kind == Kind::Toggle) return current == "true" ? "false" : "true";
    if (item.kind == Kind::Choice) {
        const int count = static_cast<int>(item.choices.size());
        if (count == 0) return std::nullopt;
        int index = -1;
        for (int i = 0; i < count; ++i)
            if (current == item.choices[i].value) index = i;
        const int next = index < 0 ? (direction > 0 ? 0 : count - 1) :
            ((index + (direction > 0 ? 1 : -1)) % count + count) % count;
        return item.choices[next].value;
    }
    const auto value = ParseNumber(current);
    if (!value) return std::nullopt;
    constexpr float epsilon = 1e-4f;
    float next = *value;
    if (!item.presets.empty()) {
        std::optional<float> found;
        for (const float preset : item.presets) {
            if (direction > 0 && preset > *value + epsilon && (!found || preset < *found)) found = preset;
            if (direction < 0 && preset < *value - epsilon && (!found || preset > *found)) found = preset;
        }
        if (!found) return std::nullopt;
        next = *found;
    } else {
        if ((direction > 0 && *value >= item.maximum - epsilon) ||
            (direction < 0 && *value <= item.minimum + epsilon))
            return std::nullopt;
        const float steps = *value / item.step;
        next = (direction > 0 ? std::floor(steps + epsilon + 1.0f) : std::ceil(steps - epsilon - 1.0f)) * item.step;
        next = std::clamp(next, item.minimum, item.maximum);
        if (std::fabs(next - *value) < epsilon * item.step) return std::nullopt;
    }
    return Number(next);
}

// In-game overrides: Key=Value lines layered after config.txt, limited to menu keys.
using Overrides = std::map<std::string, std::string>;

inline Overrides ParseOverrides(std::istream &stream)
{
    Overrides overrides;
    std::string line;
    while (std::getline(stream, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = TrimConfigValue(line.substr(0, separator));
        const std::string value = TrimConfigValue(line.substr(separator + 1));
        if (IsMenuKey(key) && !value.empty()) overrides[key] = value;
    }
    return overrides;
}

inline std::string OverrideLines(const Overrides &overrides)
{
    std::string text;
    for (const auto &page : Pages())
        for (const auto &item : page.items) {
            if (!item.key) continue;
            const auto it = overrides.find(item.key);
            if (it != overrides.end()) text += it->first + "=" + it->second + "\n";
        }
    return text;
}

inline std::string SerializeOverrides(const Overrides &overrides)
{
    return "# Written by the Portal2VR in-game VR options (Options > VR).\n"
           "# These values override VR\\config.txt; delete this file or use\n"
           "# \"Use Defaults\" in the game to return to config.txt.\n" + OverrideLines(overrides);
}

inline std::string ComposeConfigText(const std::string &base, const Overrides &overrides)
{
    // ParseConfig keeps the last occurrence of a key, so overrides win.
    return base + "\n" + OverrideLines(overrides);
}

// Portal 2 dialog geometry, in pixels. One unit is a proportional 1/480 of the
// screen height; tiles are 50x50 units (basemodui_scheme Dialog.Tile*).
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool Contains(int px, int py) const { return w > 0 && h > 0 && px >= x && py >= y && px < x + w && py < y + h; }
};

namespace Units {
inline constexpr float Tile = 50.0f;
inline constexpr float FirstRow = 25.0f;      // Video.res ypos of the first row
inline constexpr float RowPitch = 25.0f;
inline constexpr float RowHeight = 20.0f;
inline constexpr float CursorHeight = 25.0f;  // HybridButton.CursorHeight
inline constexpr float ButtonInset = 1.0f;    // rows span the body minus one unit per side
inline constexpr float TextInset = 10.0f;     // HybridButton.TextInsetX
inline constexpr float TitleOffsetX = 10.0f;  // Dialog.TitleOffsetX
inline constexpr float TitleOffsetY = 9.0f;   // Dialog.TitleOffsetY
inline constexpr float FooterOffsetY = 12.0f; // FooterPanel.OffsetY
inline constexpr float FooterGapX = 12.0f;    // FooterPanel.ButtonGapX
inline constexpr float FooterPaddingX = 20.0f;
inline constexpr float BottomRowSlack = 5.0f;
inline constexpr int PageTiles = 7;           // Video.res wide
inline constexpr int MinTabTiles = 2;
}

struct PageLayout {
    float tile = 0.0f;
    int wideTiles = 0;
    int tallTiles = 0;
    int tabTiles = 0;
    Rect body;
    Rect tab;
    std::vector<Rect> rows;
    std::vector<Rect> footer;
};

inline int TallTiles(int rowCount)
{
    const float needed = Units::FirstRow + rowCount * Units::RowPitch - Units::BottomRowSlack;
    return (std::max)(3, static_cast<int>(std::ceil(needed / Units::Tile)));
}

inline int TabTiles(float unit, int titleWide)
{
    const float tile = Units::Tile * unit;
    const float needed = titleWide + 2.0f * Units::TitleOffsetX * unit;
    return (std::max)(Units::MinTabTiles, static_cast<int>(std::ceil(needed / tile)));
}

// bodyLeft/bodyBottom anchor the page where the native Options body sits, so the
// VR page replaces it in place like Video replaces Options.
inline PageLayout LayoutPage(float unit, int bodyLeft, int bodyBottom, int rowCount, int titleWide,
                             const std::vector<int> &footerTextWide, int footerTall)
{
    PageLayout layout;
    layout.tile = Units::Tile * unit;
    layout.wideTiles = Units::PageTiles;
    layout.tallTiles = TallTiles(rowCount);
    layout.tabTiles = TabTiles(unit, titleWide);
    const auto px = [&](float units) { return static_cast<int>(std::lround(units * unit)); };
    const int wide = static_cast<int>(std::lround(layout.wideTiles * layout.tile));
    const int tall = static_cast<int>(std::lround(layout.tallTiles * layout.tile));
    layout.body = {bodyLeft, bodyBottom - tall, wide, tall};
    layout.tab = {bodyLeft, layout.body.y - static_cast<int>(std::lround(layout.tile)),
                  static_cast<int>(std::lround(layout.tabTiles * layout.tile)),
                  static_cast<int>(std::lround(layout.tile))};
    for (int i = 0; i < rowCount; ++i)
        layout.rows.push_back({bodyLeft + px(Units::ButtonInset),
                               layout.body.y + px(Units::FirstRow + i * Units::RowPitch),
                               wide - 2 * px(Units::ButtonInset), px(Units::RowHeight)});
    int x = bodyLeft;
    const int y = bodyBottom + px(Units::FooterOffsetY);
    for (const int textWide : footerTextWide) {
        const int width = textWide + px(Units::FooterPaddingX);
        layout.footer.push_back({x, y, width, footerTall});
        x += width + px(Units::FooterGapX);
    }
    return layout;
}

inline Rect CursorBar(const Rect &row, float unit)
{
    const int tall = static_cast<int>(std::lround(Units::CursorHeight * unit));
    return {row.x, row.y - (tall - row.h) / 2, row.w, tall};
}

// The VR entry sits one native row pitch below CONTROLLER on the Options screen.
inline Rect EntryRect(const Rect &lastButton, int pitch)
{
    return {lastButton.x, lastButton.y + pitch, lastButton.w, lastButton.h};
}

enum class TargetType { None, Row, Decrease, Increase, Footer, Body };

struct Target {
    TargetType type = TargetType::None;
    int index = -1;
};

// Arrow hit areas are recorded by the painter for the focused row only.
struct HitAreas {
    PageLayout layout;
    float unit = 1.0f;
    int arrowRow = -1;
    Rect decrease;
    Rect increase;
};

inline Target HitTest(const HitAreas &areas, int x, int y)
{
    if (areas.arrowRow >= 0) {
        if (areas.decrease.Contains(x, y)) return {TargetType::Decrease, areas.arrowRow};
        if (areas.increase.Contains(x, y)) return {TargetType::Increase, areas.arrowRow};
    }
    for (int i = 0; i < static_cast<int>(areas.layout.rows.size()); ++i)
        if (CursorBar(areas.layout.rows[i], areas.unit).Contains(x, y)) return {TargetType::Row, i};
    for (int i = 0; i < static_cast<int>(areas.layout.footer.size()); ++i)
        if (areas.layout.footer[i].Contains(x, y)) return {TargetType::Footer, i};
    if (areas.layout.body.Contains(x, y) || areas.layout.tab.Contains(x, y)) return {TargetType::Body, -1};
    return {};
}

// Keyboard/controller focus: rows first, then the footer buttons.
struct Focus {
    bool footer = false;
    int index = 0;
};

inline Focus MoveFocus(Focus focus, int rows, int footers, int direction)
{
    if (rows <= 0) return {footers > 0, 0};
    if (direction > 0) {
        if (focus.footer) return {false, 0};
        if (focus.index + 1 < rows) return {false, focus.index + 1};
        return footers > 0 ? Focus{true, 0} : Focus{false, 0};
    }
    if (focus.footer) return {false, rows - 1};
    if (focus.index > 0) return {false, focus.index - 1};
    return footers > 0 ? Focus{true, 0} : Focus{false, rows - 1};
}

inline Focus MoveFooterFocus(Focus focus, int footers, int direction)
{
    if (!focus.footer || footers <= 0) return focus;
    return {true, std::clamp(focus.index + (direction > 0 ? 1 : -1), 0, footers - 1)};
}

} // namespace VrSettings
