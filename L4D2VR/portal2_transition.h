#pragma once

// Portal 2 BaseModUI's tile-flip transition (CBaseModTransitionPanel, client.dll).
//
// Every frame each dialog marks the screen tiles it draws with its window type:
// CBaseModFrame::DrawDialogBackground calls MarkTileAt once per tile. The
// transition panel paints after all menus; a tile whose owner changed flips
// from the previous frame's capture to the current one, and a tile nobody
// marked flips back to the background. Found by disassembling the installed
// client.dll with dumpbin. The call site and every target are checked against
// the bytes observed there; any mismatch leaves the transition off.

#include "portal2_vgui.h"
#include "sigscanner.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace Portal2Transition {

// WINDOW_TYPE values from CBaseModPanel::OpenWindow's switch.
inline constexpr int kWindowOptions = 23;
inline constexpr int kWindowVideo = 38;
inline constexpr int kWindowAdvancedVideo = 56;

// DrawDialogBackground: push 0; push window type; push y; push x;
// call CBaseModPanel::GetSingleton; mov ecx,eax; call GetTransitionEffectPanel;
// mov ecx,eax; call CBaseModTransitionPanel::MarkTileAt
inline constexpr int kCallSiteRva = 0x2F82F0;
inline constexpr const char *kCallSiteSignature =
    "6A 00 51 8B 4D EC 50 8D 14 0B 52 E8 ? ? ? ? 8B C8 E8 ? ? ? ? 8B C8 E8 ? ? ? ? 80 BE 00 07 00 00 00";
inline constexpr std::size_t kSingletonCall = 11;
inline constexpr std::size_t kPanelCall = 18;
inline constexpr std::size_t kMarkCall = 25;
// mov eax,ds:[singleton]; ret
inline constexpr const char *kGetSingletonSignature = "A1 ? ? ? ? C3";
// add ecx,460h; jmp vgui::PHandle::Get
inline constexpr const char *kGetPanelSignature = "81 C1 60 04 00 00 E9";
inline constexpr const char *kHandleGetSignature = "8B 01 56 83 F8 FF 74";
// MarkTileAt(x, y, windowType, bool): effect-enabled check, then the grid tile under (x, y).
inline constexpr const char *kMarkTileAtSignature =
    "55 8B EC 56 8B F1 E8 ? ? ? ? 84 C0 74 48 8B 45 0C 2B 86 C0 01 00 00 99 F7 BE B0 01 00 00";
// SetExpectedDirection(bool forward, int windowType): the whole function.
inline constexpr int kSetDirectionRva = 0x306580;
inline constexpr const char *kSetDirectionSignature =
    "55 8B EC 8A 45 08 88 81 C5 01 00 00 8B 81 C8 01 00 00 39 81 CC 01 00 00 74 13 "
    "8B 55 0C 89 81 CC 01 00 00 89 91 C8 01 00 00 5D C2 08 00";

class Api
{
public:
    bool Resolve(std::string &failure)
    {
        m_Ready = false;
        const auto *module = reinterpret_cast<const std::uint8_t *>(GetModuleHandleA("client.dll"));
        if (!module) {
            failure = "client.dll not loaded";
            return false;
        }
        const auto locate = [module](int rva, const char *signature) -> const std::uint8_t * {
            const int moved = SigScanner::VerifyOffset("client.dll", rva, signature);
            return moved < 0 ? nullptr : module + (moved > 0 ? moved : rva);
        };
        const auto relativeTarget = [](const std::uint8_t *call) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, call + 1, sizeof(displacement));
            return call + 5 + displacement;
        };
        const auto *site = locate(kCallSiteRva, kCallSiteSignature);
        if (!site) {
            failure = "DrawDialogBackground call site";
            return false;
        }
        const auto *singleton = relativeTarget(site + kSingletonCall);
        const auto *panel = relativeTarget(site + kPanelCall);
        const auto *mark = relativeTarget(site + kMarkCall);
        if (!Portal2Vgui::VerifyCode(singleton, kGetSingletonSignature) ||
            !Portal2Vgui::VerifyCode(panel, kGetPanelSignature) ||
            !Portal2Vgui::VerifyCode(relativeTarget(panel + 6), kHandleGetSignature)) {
            failure = "CBaseModPanel transition accessor";
            return false;
        }
        if (!Portal2Vgui::VerifyCode(mark, kMarkTileAtSignature)) {
            failure = "MarkTileAt";
            return false;
        }
        const auto *direction = locate(kSetDirectionRva, kSetDirectionSignature);
        if (!direction) {
            failure = "SetExpectedDirection";
            return false;
        }
        m_GetSingleton = reinterpret_cast<GetSingletonFn>(const_cast<std::uint8_t *>(singleton));
        m_GetPanel = reinterpret_cast<GetPanelFn>(const_cast<std::uint8_t *>(panel));
        m_MarkTileAt = reinterpret_cast<MarkTileAtFn>(const_cast<std::uint8_t *>(mark));
        m_SetDirection = reinterpret_cast<SetDirectionFn>(const_cast<std::uint8_t *>(direction));
        m_Ready = true;
        return true;
    }

    bool Ready() const { return m_Ready; }

    // Null until BaseModUI has created its transition panel.
    void *Panel() const
    {
        if (!m_Ready) return nullptr;
        void *base = m_GetSingleton();
        return base ? m_GetPanel(base) : nullptr;
    }
    void MarkTileAt(void *panel, int x, int y, int windowType) const { m_MarkTileAt(panel, x, y, windowType, false); }
    void SetExpectedDirection(void *panel, bool forward, int windowType) const { m_SetDirection(panel, forward, windowType); }

private:
    using GetSingletonFn = void *(__cdecl *)();
    using GetPanelFn = void *(__thiscall *)(void *);
    using MarkTileAtFn = void(__thiscall *)(void *, int, int, int, bool);
    using SetDirectionFn = void(__thiscall *)(void *, bool, int);

    bool m_Ready = false;
    GetSingletonFn m_GetSingleton = nullptr;
    GetPanelFn m_GetPanel = nullptr;
    MarkTileAtFn m_MarkTileAt = nullptr;
    SetDirectionFn m_SetDirection = nullptr;
};

} // namespace Portal2Transition
