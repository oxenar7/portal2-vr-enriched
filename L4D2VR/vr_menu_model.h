#pragma once

// The in-world pause menu: what each of its up to three panels shows and what a click does.
// Left, Portal 2's pause entries. Middle, the list that entry opens (saves, option groups).
// Right, the page for the row chosen there. No engine calls: clicks come back as commands.

#include "vr_menu_data.h"
#include "vr_menu_paint.h"
#include "vr_settings.h"

#include <string>
#include <utility>
#include <vector>

namespace VrMenu {

struct Command {
    enum class Kind { None, Resume, ExitToMainMenu, ClassicMenu, Recenter, Defaults, Save, Load, Set };
    Kind kind = Kind::None;
    std::string text;  // save name, or setting key
    std::string value; // setting value
};

// The sound an action deserves, as Portal 2's own menus give it: a tick when the pointer
// lands on something, a confirm when a click does something, the back sound when a panel
// closes, and the refusal when a value is already at its end.
enum class Cue { None, Focus, Accept, Back, Invalid };

// Portal 2's own words, in the game's language, with English fallbacks.
struct Texts {
    std::wstring resume = L"RETURN TO GAME", save = L"SAVE GAME", load = L"LOAD GAME", options = L"OPTIONS",
        exit = L"EXIT TO MAIN MENU", back = L"BACK", saveAction = L"SAVE", loadAction = L"LOAD";
    std::wstring chapterTitle[11], chapterSubtitle[11];

    void Load(const VrMenuData::Strings &strings)
    {
        resume = strings.Get("PORTAL2_InGameMainMenu_ReturnToGame", resume.c_str());
        save = strings.Get("PORTAL2_SaveGame", save.c_str());
        load = strings.Get("PORTAL2_LoadGame", load.c_str());
        options = strings.Get("PORTAL2_MainMenu_Options", options.c_str());
        exit = strings.Get("PORTAL2_InGameMainMenu_ExitToMainMenu", exit.c_str());
        back = strings.Get("L4D360UI_Back", back.c_str());
        saveAction = strings.Get("PORTAL2_ButtonAction_Save", saveAction.c_str());
        loadAction = strings.Get("PORTAL2_ButtonAction_Load", loadAction.c_str());
        for (int chapter = 1; chapter <= 10; ++chapter) {
            const std::string token = "portal2_Chapter" + std::to_string(chapter);
            chapterTitle[chapter] = strings.Get((token + "_Title").c_str(), (L"CHAPTER " + std::to_wstring(chapter)).c_str());
            chapterSubtitle[chapter] = strings.Get((token + "_Subtitle").c_str(), L"");
        }
    }
};

// One row of an options page: a setting of VrSettings under a short everyday name.
struct Setting {
    const char *key;
    const wchar_t *label;
    const wchar_t *on = L"ON";
    const wchar_t *off = L"OFF";
};

struct Group {
    const wchar_t *title;
    std::vector<Setting> settings;
    bool tools = false; // Recenter and Defaults in the footer
};

// Few groups, plain names, and only what changes how the game feels in the headset. Rows
// whose setting does not apply right now are left out, so a page shows only what works.
inline const std::vector<Group> &Groups()
{
    static const std::vector<Group> groups{
        {L"COMFORT", {{"SnapTurning", L"TURN", L"SNAP", L"SMOOTH"}, {"SnapTurnAngle", L"SNAP ANGLE"},
                      {"TurnSpeed", L"TURN SPEED"}, {"MovementDirection", L"MOVE TOWARD"},
                      {"PhysicalCrouch", L"CROUCH FOR REAL"}}},
        {L"CONTROLS", {{"LeftHanded", L"LEFT HANDED"}, {"SwapSticks", L"SWAP STICKS"},
                       {"ExperimentalWorldAimMarker", L"AIM LINE"},
                       {"ControllerAimedUse", L"POINT TO USE"}, {"Haptics", L"VIBRATION"},
                       {"HapticsIntensity", L"STRENGTH"}}},
        {L"ADVANCED", {{"HeightOffsetMeters", L"HEIGHT OFFSET"}, {"VRScale", L"WORLD SCALE"},
                       {"IPDScale", L"EYE DISTANCE"}, {"ControllerPitchDegrees", L"GUN ANGLE"},
                       {"PhysicalCrouchDropMeters", L"CROUCH DEPTH"}, {"LaserCubeAimAssist", L"LASER CUBE ASSIST"},
                       {"HeldObjectAnchor", L"OBJECTS ON THE GUN"}, {"RenderWindow", L"DESKTOP MIRROR"}},
         true}};
    return groups;
}

class Model {
public:
    enum Root { RootResume, RootSave, RootLoad, RootOptions, RootExit, RootCount };
    static constexpr int kSavesPerPage = 5;

    void Open()
    {
        m_Root = m_Second = -1;
        m_ListPage = 0;
        m_HoverColumn = m_HoverRow = m_HoverButton = -1;
    }

    void SetTexts(Texts texts) { m_Texts = std::move(texts); }

    void SetSaves(std::vector<VrMenuData::Save> saves)
    {
        m_Saves = std::move(saves);
        if (m_ListPage > LastPage()) m_ListPage = LastPage();
        if ((m_Root == RootSave || m_Root == RootLoad) && m_Second >= 0) m_Second = -1;
    }

    int Columns() const { return m_Root < 0 ? 1 : m_Second < 0 ? 2 : 3; }

    VrMenuPaint::Panel Panel(int column, const ConfigSnapshot &config) const
    {
        Built built = Build(column, config);
        if (column == m_HoverColumn) {
            const bool live = m_HoverRow >= 0 && m_HoverRow < static_cast<int>(built.rows.size()) &&
                built.rows[m_HoverRow].kind != Act::None;
            built.panel.hovered = live ? m_HoverRow : -1;
            built.panel.hoveredButton = m_HoverButton;
        }
        return built.panel;
    }

    // The pointer moved; row and button are -1 off any target. True when the picture changes.
    bool Hover(int column, int row, int button)
    {
        if (column == m_HoverColumn && row == m_HoverRow && button == m_HoverButton) return false;
        m_HoverColumn = column;
        m_HoverRow = row;
        m_HoverButton = button;
        return true;
    }

    // The pointer is on something that answers a click, not on an information row.
    bool HoverLive(const ConfigSnapshot &config) const
    {
        if (m_HoverColumn < 0) return false;
        const Built built = Build(m_HoverColumn, config);
        if (m_HoverButton >= 0) return m_HoverButton < static_cast<int>(built.buttons.size());
        return m_HoverRow >= 0 && m_HoverRow < static_cast<int>(built.rows.size()) &&
            built.rows[m_HoverRow].kind != Act::None;
    }

    // The sound for what the last click or back press did; read once.
    Cue TakeCue()
    {
        const Cue cue = m_Cue;
        m_Cue = Cue::None;
        return cue;
    }

    // direction: -1 and +1 for a row's steppers, 0 for the row itself.
    Command Click(int column, int row, int direction, int button, const ConfigSnapshot &config)
    {
        const Built built = Build(column, config);
        if (button >= 0 && button < static_cast<int>(built.buttons.size())) return Run(column, built.buttons[button], 0, config);
        if (row >= 0 && row < static_cast<int>(built.rows.size())) {
            Act act = built.rows[row];
            act.row = row;
            return Run(column, act, direction, config);
        }
        return {};
    }

    // The back button: close the rightmost panel, or resume from the first one.
    Command Back()
    {
        m_Cue = Cue::Back;
        if (m_Second >= 0) m_Second = -1;
        else if (m_Root >= 0) m_Root = -1;
        else return {Command::Kind::Resume};
        return {};
    }

private:
    struct Act {
        enum Kind { None, OpenRoot, OpenSecond, Back, Page, Do, Step } kind = None;
        int index = 0;
        Command command;
        std::string key;
        int row = -1;
    };

    struct Built {
        VrMenuPaint::Panel panel;
        std::vector<Act> rows, buttons;
    };

    using RowKind = VrMenuPaint::RowKind;

    static Act Do(Command::Kind kind, std::string text = {})
    {
        Act act;
        act.kind = Act::Do;
        act.command.kind = kind;
        act.command.text = std::move(text);
        return act;
    }

    static Act Go(Act::Kind kind, int index = 0)
    {
        Act act;
        act.kind = kind;
        act.index = index;
        return act;
    }

    // Saves the list shows: every save to load from, only the player's own to save over.
    std::vector<int> Listed() const
    {
        std::vector<int> listed;
        for (int i = 0; i < static_cast<int>(m_Saves.size()); ++i)
            if (m_Root == RootLoad || !m_Saves[i].autosave) listed.push_back(i);
        return listed;
    }

    int LastPage() const
    {
        const int count = static_cast<int>(Listed().size());
        return count ? (count - 1) / kSavesPerPage : 0;
    }

    std::wstring SaveTitle(const VrMenuData::Save &save) const
    {
        const int chapter = VrMenuData::ChapterOfMap(save.map);
        return chapter ? m_Texts.chapterTitle[chapter] : VrMenuData::Upper(VrMenuData::Widen(save.map));
    }

    Built Build(int column, const ConfigSnapshot &config) const
    {
        Built built;
        VrMenuPaint::Panel &panel = built.panel;
        const auto row = [&](std::wstring label, RowKind kind, Act act, std::wstring value = {}) {
            VrMenuPaint::Row r;
            r.label = std::move(label);
            r.value = std::move(value);
            r.kind = kind;
            panel.rows.push_back(std::move(r));
            built.rows.push_back(std::move(act));
        };
        const auto button = [&](const std::wstring &label, Act act) {
            panel.buttons.push_back(label);
            built.buttons.push_back(std::move(act));
        };

        if (column == 0) {
            row(m_Texts.resume, RowKind::Action, Do(Command::Kind::Resume));
            row(m_Texts.save, RowKind::Submenu, Go(Act::OpenRoot, RootSave));
            row(m_Texts.load, RowKind::Submenu, Go(Act::OpenRoot, RootLoad));
            row(m_Texts.options, RowKind::Submenu, Go(Act::OpenRoot, RootOptions));
            row(m_Texts.exit, RowKind::Submenu, Go(Act::OpenRoot, RootExit));
            panel.selected = m_Root;
            return built;
        }

        if (column == 1) {
            panel.selected = m_Second;
            if (m_Root == RootSave || m_Root == RootLoad) {
                panel.title = m_Root == RootSave ? m_Texts.save : m_Texts.load;
                if (m_Root == RootSave) row(L"NEW SAVE", RowKind::Action, Do(Command::Kind::Save, VrMenuData::NewSaveName()));
                const std::vector<int> listed = Listed();
                for (int i = m_ListPage * kSavesPerPage; i < static_cast<int>(listed.size()) && i < (m_ListPage + 1) * kSavesPerPage; ++i) {
                    const VrMenuData::Save &save = m_Saves[listed[i]];
                    row(SaveTitle(save), RowKind::Submenu, Go(Act::OpenSecond, static_cast<int>(panel.rows.size())), save.when);
                }
                if (listed.empty() && m_Root == RootLoad) row(L"NO SAVED GAMES", RowKind::Info, {});
                button(m_Texts.back, Go(Act::Back));
                if (m_ListPage < LastPage()) button(L"OLDER", Go(Act::Page, +1));
                if (m_ListPage > 0) button(L"NEWER", Go(Act::Page, -1));
            } else if (m_Root == RootOptions) {
                panel.title = m_Texts.options;
                for (const Group &group : Groups())
                    row(group.title, RowKind::Submenu, Go(Act::OpenSecond, static_cast<int>(panel.rows.size())));
                row(L"PORTAL 2 OPTIONS", RowKind::Action, Do(Command::Kind::ClassicMenu));
                button(m_Texts.back, Go(Act::Back));
            } else if (m_Root == RootExit) {
                panel.title = m_Texts.exit;
                row(L"PROGRESS SINCE YOUR LAST SAVE IS LOST", RowKind::Info, {});
                button(m_Texts.back, Go(Act::Back));
                button(L"EXIT", Do(Command::Kind::ExitToMainMenu));
            }
            return built;
        }

        // Third panel: the page of the row chosen in the second.
        if (m_Root == RootSave || m_Root == RootLoad) {
            const std::vector<int> listed = Listed();
            const int index = m_ListPage * kSavesPerPage + m_Second - (m_Root == RootSave ? 1 : 0);
            if (index < 0 || index >= static_cast<int>(listed.size())) return built;
            const VrMenuData::Save &save = m_Saves[listed[index]];
            const int chapter = VrMenuData::ChapterOfMap(save.map);
            panel.title = m_Root == RootSave ? L"SAVE OVER THIS GAME" : m_Texts.load;
            row(SaveTitle(save), RowKind::Info, {});
            if (chapter && !m_Texts.chapterSubtitle[chapter].empty()) row(m_Texts.chapterSubtitle[chapter], RowKind::Info, {});
            row(save.autosave ? L"AUTOSAVE" : L"SAVED", RowKind::Info, {}, save.when);
            if (!save.played.empty()) row(L"TIME PLAYED", RowKind::Info, {}, save.played);
            button(m_Texts.back, Go(Act::Back));
            button(m_Root == RootSave ? m_Texts.saveAction : m_Texts.loadAction,
                   Do(m_Root == RootSave ? Command::Kind::Save : Command::Kind::Load, save.name));
        } else if (m_Root == RootOptions && m_Second >= 0 && m_Second < static_cast<int>(Groups().size())) {
            const Group &group = Groups()[m_Second];
            panel.title = group.title;
            for (const Setting &setting : group.settings) {
                const VrSettings::Item *item = VrSettings::FindItem(setting.key);
                if (!item || !item->read || !VrSettings::Enabled(*item, config)) continue;
                const std::string raw = item->read(config);
                VrMenuPaint::Row r;
                r.label = setting.label;
                const bool binary = item->kind == VrSettings::Kind::Choice && item->choices.size() == 2 &&
                    std::string(item->choices[0].value) == "0" && std::string(item->choices[1].value) == "1";
                if (item->kind == VrSettings::Kind::Toggle || binary) {
                    r.on = raw == "true" || raw == "1";
                    r.value = r.on ? setting.on : setting.off;
                    r.kind = std::wstring(setting.on) == L"ON" ? RowKind::Toggle : RowKind::Choice;
                } else if (item->kind == VrSettings::Kind::Choice) {
                    r.kind = RowKind::Choice;
                    r.value = VrMenuData::Upper(VrMenuData::Widen(VrSettings::DisplayValue(*item, config)));
                } else {
                    r.kind = RowKind::Slider;
                    r.value = VrMenuData::Upper(VrMenuData::Widen(VrSettings::DisplayValue(*item, config)));
                    const float number = VrSettings::ParseNumber(raw).value_or(item->minimum);
                    const float low = item->presets.empty() ? item->minimum : item->presets.front();
                    const float high = item->presets.empty() ? item->maximum : item->presets.back();
                    r.fraction = high > low ? (number - low) / (high - low) : 0.0f;
                }
                panel.rows.push_back(std::move(r));
                Act act;
                act.kind = Act::Step;
                act.key = setting.key;
                built.rows.push_back(std::move(act));
            }
            button(m_Texts.back, Go(Act::Back));
            if (group.tools) {
                button(L"RECENTER", Do(Command::Kind::Recenter));
                button(L"DEFAULTS", Do(Command::Kind::Defaults));
            }
        }
        return built;
    }

    Command Run(int column, const Act &act, int direction, const ConfigSnapshot &config)
    {
        switch (act.kind) {
        case Act::OpenRoot:
            m_Cue = m_Root == act.index ? Cue::Back : Cue::Accept;
            m_Root = m_Root == act.index ? -1 : act.index;
            m_Second = -1;
            m_ListPage = 0;
            break;
        case Act::OpenSecond:
            m_Cue = m_Second == act.index ? Cue::Back : Cue::Accept;
            m_Second = m_Second == act.index ? -1 : act.index;
            break;
        case Act::Back:
            m_Cue = Cue::Back;
            if (column == 2) m_Second = -1;
            else m_Root = m_Second = -1;
            break;
        case Act::Page:
            m_Cue = Cue::Accept;
            m_ListPage = (std::max)(0, (std::min)(LastPage(), m_ListPage + act.index));
            m_Second = -1;
            break;
        case Act::Do:
            m_Cue = Cue::Accept;
            if (act.command.kind == Command::Kind::Save || act.command.kind == Command::Kind::Load)
                m_Second = -1;
            return act.command;
        case Act::Step: {
            const VrSettings::Item *item = VrSettings::FindItem(act.key);
            if (!item) break;
            m_Cue = Cue::Invalid;
            if (const auto next = VrSettings::Step(*item, config, direction < 0 ? -1 : 1)) {
                m_Cue = Cue::Accept;
                return {Command::Kind::Set, act.key, *next};
            }
            break;
        }
        case Act::None:
            break;
        }
        return {};
    }

    Texts m_Texts;
    std::vector<VrMenuData::Save> m_Saves;
    int m_Root = -1, m_Second = -1, m_ListPage = 0;
    int m_HoverColumn = -1, m_HoverRow = -1, m_HoverButton = -1;
    Cue m_Cue = Cue::None;
};

} // namespace VrMenu
