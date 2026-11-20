#pragma once

// Which game the mod runs in. Portal: Alive & Kicking (Steam app 270470) is a standalone
// install of Portal 2's engine with its own game folder ("AAK"), localisation files, save
// folder and campaign. The code paths are the same; only these facts differ.

#include <Windows.h>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <string>

namespace GameProfile {

struct Profile {
    const char *name;         // for the log
    const char *folder;       // game folder under the install root (the -game folder)
    const char *appId;        // Steam app id: appmanifest_<appId>.acf names the language
    const char *gameStrings;  // "<gameStrings><language>.txt" next to basemodui_ in <folder>\resource
    // Chapter title tokens, printf-style with the chapter number. Without a subtitle token the
    // title token holds "title\nsubtitle".
    const char *chapterTitleToken;
    const char *chapterSubtitleToken;
    int chapterCount;
    const char *const *chapterMaps; // per chapter, its maps separated by spaces
    // CLocatorTarget fields CLocatorPanel::DrawTarget leaves behind (hint log only):
    // icon centre x, y and size from locatorCentre, effect flags at locatorFlags.
    std::uint32_t locatorCentre;
    std::uint32_t locatorFlags;
    // How far the game's v_portalgun.mdl animates its whole rig from Portal 2's, in model space
    // (x forward, y left, z up). The controller grip offset moves by the same amount.
    float viewmodelRigShift[3];
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

inline constexpr int kMaxChapters = 11;

inline constexpr Profile kPortal2{"Portal 2", "portal2", "620", "portal2_",
    "portal2_Chapter%d_Title", "portal2_Chapter%d_Subtitle",
    static_cast<int>(std::size(kPortal2Chapters)), kPortal2Chapters, 0xCC, 0x13C, {0.0f, 0.0f, 0.0f}};

// The locator offsets were read from the same instructions of DrawTarget and Paint in both
// client.dll builds (Portal 2 +0xCC/+0x13C, Alive & Kicking +0x8C/+0xF8).
// Its v_portalgun.mdl has Portal 2's skeleton, but every sequence puts the root bone, and so
// the hand and muzzle, 6 units further forward and 1 further left (frame 0 of each sequence
// decoded from both models).
inline constexpr Profile kAliveAndKicking{"Portal: Alive & Kicking", "AAK", "270470", "AAK_",
    "SP_PRESENCE_TEXT_CH%d", nullptr,
    static_cast<int>(std::size(kAliveAndKickingChapters)), kAliveAndKickingChapters, 0x8C, 0xF8,
    {6.0f, 1.0f, 0.0f}};

static_assert(std::size(kPortal2Chapters) <= kMaxChapters && std::size(kAliveAndKickingChapters) <= kMaxChapters);

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
    return name == L"aak" ? kAliveAndKicking : kPortal2;
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
