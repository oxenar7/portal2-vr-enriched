#pragma once

// Which game the mod runs in. Portal: Alive & Kicking (Steam app 270470), Portal Stories: Mel
// (Steam app 317400) and Portal Reloaded (Steam app 1255980) are standalone installs of Portal 2's
// engine, each with its own game folder ("AAK", "portal_stories", "portalreloaded"), localisation
// files, save folder and campaign. The code paths are the same; only these facts differ.

#include "gun_models.h"

#include <Windows.h>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <string>

namespace GameProfile {

struct ButtonCommand {
    const char *press;
    const char *release; // null: nothing on release
};

struct Profile {
    const char *name;         // for the log
    const char *folder;       // game folder under the install root (the -game folder)
    const char *appId;        // Steam app id: appmanifest_<appId>.acf names the language
    const char *gameStrings;  // "<gameStrings><language>.txt" next to basemodui_ in <folder>\resource
    // Chapter title tokens, printf-style with the chapter number. Without a subtitle token the
    // title token holds "title\nsubtitle". Without a title token chapterLabel names the chapter.
    const char *chapterTitleToken;
    const char *chapterSubtitleToken;
    int chapterCount;
    const char *const *chapterMaps; // per chapter, its maps separated by spaces
    // CLocatorTarget fields CLocatorPanel::DrawTarget leaves behind (hint log only):
    // icon centre x, y and size from locatorCentre, effect flags at locatorFlags.
    std::uint32_t locatorCentre;
    std::uint32_t locatorFlags;
    // The game's own v_portalgun.mdl. Its grip applies until the mod reads which model the
    // game loaded, and when that one is not in gun_models.h.
    const GunModels::Model *gunModel;
    // A chapter's name when its title token is missing, printf-style with the chapter number.
    const wchar_t *chapterLabel = L"CHAPTER %d";
    // What the gun trigger, the gun grip and the off-hand trigger send: Portal 2's own
    // +commands, or a game's aliases that run through its map logic and release themselves
    // (release null). Portal 2 has no use for +reload.
    ButtonCommand primary{"+attack", "-attack"};
    ButtonCommand secondary{"+attack2", "-attack2"};
    ButtonCommand offHand{"+reload", "-reload"};
};

inline constexpr const char *kPortal2Chapters[] = {
    "sp_a1_intro1 sp_a1_intro2 sp_a1_intro3 sp_a1_intro4 sp_a1_intro5 sp_a1_intro6 sp_a1_intro7 sp_a1_wakeup",
    "sp_a2_intro sp_a2_laser_intro sp_a2_laser_stairs sp_a2_dual_lasers sp_a2_laser_over_goo sp_a2_catapult_intro "
    "sp_a2_trust_fling sp_a2_pit_flings sp_a2_fizzler_intro",
    "sp_a2_sphere_peek sp_a2_ricochet sp_a2_bridge_intro sp_a2_bridge_the_gap sp_a2_turret_intro "
    "sp_a2_laser_relays sp_a2_turret_blocker sp_a2_laser_vs_turret sp_a2_pull_the_rug",
    "sp_a2_column_blocker sp_a2_laser_chaining sp_a2_triple_laser sp_a2_bts1 sp_a2_bts2",
    "sp_a2_bts3 sp_a2_bts4 sp_a2_bts5 sp_a2_bts6 sp_a2_core",
    "sp_a3_00 sp_a3_01 sp_a3_03 sp_a3_jump_intro sp_a3_bomb_flings sp_a3_crazy_box sp_a3_transition01",
    "sp_a3_speed_ramp sp_a3_speed_flings sp_a3_portal_intro sp_a3_end",
    "sp_a4_intro sp_a4_tb_intro sp_a4_tb_trust_drop sp_a4_tb_wall_button sp_a4_tb_polarity sp_a4_tb_catch "
    "sp_a4_stop_the_box sp_a4_laser_catapult sp_a4_laser_platform sp_a4_speed_tb_catch sp_a4_jump_polarity",
    "sp_a4_finale1 sp_a4_finale2 sp_a4_finale3 sp_a4_finale4",
    "sp_a5_credits"};

// Alive & Kicking's chapters start at test chambers 00, 04, 08, 10, 13 and then one per chamber,
// as its chapter tokens say; a map named chambNN opens chamber NN. The escape and the credits
// follow chamber 19. The advanced chambers (sp_a56_adv*) belong to no chapter.
inline constexpr const char *kAliveAndKickingChapters[] = {
    "sp_a50_chamb00 sp_a50_chamb01 sp_a51_chamb02",
    "sp_a51_chamb04 sp_a51_chamb06 sp_a51_chamb07",
    "sp_a51_chamb08 sp_a51_chamb09",
    "sp_a51_chamb10 sp_a51_chamb11 sp_a52_chamb12",
    "sp_a52_chamb13",
    "sp_a52_chamb14",
    "sp_a52_chamb15",
    "sp_a52_chamb16",
    "sp_a53_chamb17",
    "sp_a53_chamb18",
    "sp_a53_chamb19 sp_a54_escape00 sp_a54_escape01 sp_a54_escape02 sp_a54_escape03 sp_a55_credits"};

// Portal Stories: Mel's chapters as its own chapter select lists them (scripts/extras.txt): story
// mode plays the st_ maps, advanced mode the sp_ maps of the same names. Chapter 5 starts at the
// core access; the credits close it.
inline constexpr const char *kPortalStoriesMelChapters[] = {
    "sp_a1_tramride sp_a1_mel_intro sp_a1_lift sp_a1_garden "
    "st_a1_tramride st_a1_mel_intro st_a1_lift st_a1_garden",
    "sp_a2_garden_de sp_a2_underbounce sp_a2_once_upon sp_a2_past_power sp_a2_ramp sp_a2_firestorm "
    "st_a2_garden_de st_a2_underbounce st_a2_once_upon st_a2_past_power st_a2_ramp st_a2_firestorm",
    "sp_a3_junkyard sp_a3_concepts sp_a3_paint_fling sp_a3_faith_plate sp_a3_transition "
    "st_a3_junkyard st_a3_concepts st_a3_paint_fling st_a3_faith_plate st_a3_transition",
    "sp_a4_overgrown sp_a4_tb_over_goo sp_a4_two_of_a_kind sp_a4_destroyed sp_a4_factory "
    "st_a4_overgrown st_a4_tb_over_goo st_a4_two_of_a_kind st_a4_destroyed st_a4_factory",
    "sp_a4_core_access sp_a4_finale st_a4_core_access st_a4_finale sp_a5_credits"};

// Portal Reloaded's campaign is twelve maps that its own map selection (scripts/extras.txt) lists
// one by one, numbered 01-12, with no chapter tokens; each map is a chapter here, named "MAP NN".
inline constexpr const char *kPortalReloadedChapters[] = {
    "sp_a1_pr_map_001", "sp_a1_pr_map_002", "sp_a1_pr_map_003", "sp_a1_pr_map_004",
    "sp_a1_pr_map_005", "sp_a1_pr_map_006", "sp_a1_pr_map_007", "sp_a1_pr_map_008",
    "sp_a1_pr_map_009", "sp_a1_pr_map_010", "sp_a1_pr_map_011", "sp_a1_pr_map_012"};

inline constexpr int kMaxChapters = 12;

inline constexpr Profile kPortal2{"Portal 2", "portal2", "620", "portal2_",
    "portal2_Chapter%d_Title", "portal2_Chapter%d_Subtitle",
    static_cast<int>(std::size(kPortal2Chapters)), kPortal2Chapters, 0xCC, 0x13C, &GunModels::kPortal2};

// The locator offsets were read from the same instructions of DrawTarget and Paint in both
// client.dll builds (Portal 2 +0xCC/+0x13C, Alive & Kicking +0x8C/+0xF8).
inline constexpr Profile kAliveAndKicking{"Portal: Alive & Kicking", "AAK", "270470", "AAK_",
    "SP_PRESENCE_TEXT_CH%d", nullptr,
    static_cast<int>(std::size(kAliveAndKickingChapters)), kAliveAndKickingChapters, 0x8C, 0xF8,
    &GunModels::kAliveAndKicking};

// Mel keeps Portal 2's strings and adds its own in valve_<language>.txt: the chapter titles are
// Portal 2's, the names its own (chapter 1, "1952", has no token and shows the title alone).
// Its client.dll is a Portal 2 build, so the locator offsets are Portal 2's.
inline constexpr Profile kPortalStoriesMel{"Portal Stories: Mel", "portal_stories", "317400", "valve_",
    "portal2_Chapter%d_Title", "PS_CHAPTER%d",
    static_cast<int>(std::size(kPortalStoriesMelChapters)), kPortalStoriesMelChapters, 0xCC, 0x13C,
    &GunModels::kPortalStoriesMel};

// Reloaded runs Mel's 2021 client.dll code and Portal 2's own v_portalgun.mdl (same checksum).
// Its own bindings (cfg/autoexec.cfg) fire portals through aliases, "ent_fire @att1|@att2|@att3
// Trigger", that every map but the first answers with a relay. In single player @att1 and @att2
// tap +attack and +attack2; @att3, the time portal, switches the gun to linkage id 2, taps
// +attack and switches back, so every shot takes the controller's aim. In co-op @att1 and @att2
// check who pressed: the player who places time portals gets them from the same two buttons,
// which plain +attack cannot do. Use stays +use: the first map has a cube and no @using.
inline constexpr Profile kPortalReloaded{"Portal Reloaded", "portalreloaded", "1255980", "Portal Reloaded_",
    nullptr, nullptr,
    static_cast<int>(std::size(kPortalReloadedChapters)), kPortalReloadedChapters, 0xCC, 0x13C,
    &GunModels::kPortal2, L"MAP %02d", {"att1", nullptr}, {"att2", nullptr}, {"att3", nullptr}};

static_assert(std::size(kPortal2Chapters) <= kMaxChapters && std::size(kAliveAndKickingChapters) <= kMaxChapters &&
              std::size(kPortalStoriesMelChapters) <= kMaxChapters &&
              std::size(kPortalReloadedChapters) <= kMaxChapters);

// The profile whose game folder holds the given client.dll path (...\<folder>\bin\client.dll).
inline const Profile &ForClientPath(const std::wstring &clientPath)
{
    const std::size_t file = clientPath.find_last_of(L"\\/");
    const std::size_t bin = file == std::wstring::npos || file == 0 ? std::wstring::npos :
        clientPath.find_last_of(L"\\/", file - 1);
    if (bin == std::wstring::npos || bin == 0) return kPortal2;
    const std::size_t folder = clientPath.find_last_of(L"\\/", bin - 1);
    std::wstring name = clientPath.substr(folder == std::wstring::npos ? 0 : folder + 1,
                                          bin - (folder == std::wstring::npos ? 0 : folder + 1));
    for (wchar_t &c : name) c = static_cast<wchar_t>(std::towlower(c));
    if (name == L"aak") return kAliveAndKicking;
    if (name == L"portal_stories") return kPortalStoriesMel;
    if (name == L"portalreloaded") return kPortalReloaded;
    return kPortal2;
}

// Decided from where the game loaded client.dll, and kept once that is known; Portal 2
// while client.dll is not loaded or its path cannot be read.
inline const Profile &Current()
{
    static const Profile *decided = nullptr;
    if (decided) return *decided;
    HMODULE client = GetModuleHandleW(L"client.dll");
    wchar_t path[MAX_PATH * 2]{};
    const DWORD length = client ? GetModuleFileNameW(client, path, static_cast<DWORD>(std::size(path))) : 0;
    if (!length || length >= std::size(path)) return kPortal2;
    decided = &ForClientPath(path);
    return *decided;
}

} // namespace GameProfile
