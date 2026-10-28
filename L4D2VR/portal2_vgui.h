#pragma once

// Verified Portal 2 VGUI ABI used by the in-menu VR options page.
//
// Slots were read from the installed 32-bit binaries (RTTI vtables of
// CMatSystemSurface in vguimatsurface.dll, VPanelWrapper, CSchemeManager and
// CScheme in vgui2.dll, disassembled with dumpbin). Each slot carries the
// prologue observed there; "?" bytes are relocated absolute addresses. Any
// mismatch leaves the whole page disabled instead of calling an unknown slot.

#include "verified_portal_symbols.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace Portal2Vgui {

using VPANEL = std::uintptr_t;
using HFont = unsigned long;
using HScheme = unsigned long;

inline int HexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Pattern of space-separated hex bytes; "?" matches any byte.
inline bool MatchesSignature(const std::uint8_t *code, const char *pattern)
{
    std::size_t offset = 0;
    for (const char *p = pattern; *p;) {
        if (*p == ' ') { ++p; continue; }
        if (*p == '?') {
            ++offset;
            ++p;
            if (*p == '?') ++p;
            continue;
        }
        const int high = HexNibble(p[0]);
        const int low = p[1] ? HexNibble(p[1]) : -1;
        if (high < 0 || low < 0) return false;
        if (code[offset] != static_cast<std::uint8_t>(high * 16 + low)) return false;
        ++offset;
        p += 2;
    }
    return offset > 0;
}

inline std::size_t SignatureLength(const char *pattern)
{
    std::size_t length = 0;
    for (const char *p = pattern; *p;) {
        if (*p == ' ') { ++p; continue; }
        ++length;
        p += (*p == '?' && p[1] == '?') || (*p != '?') ? 2 : 1;
    }
    return length;
}

struct Slot {
    int index;
    const char *signature;
    const char *name;
};

namespace SurfaceSlot {
inline constexpr Slot GetEmbeddedPanel{9, "8B 81 54 02 00 00 C3", "GetEmbeddedPanel"};
inline constexpr Slot DrawSetColor{14, "55 8B EC 8A 45 08 8A 55 0C", "DrawSetColor"};
inline constexpr Slot DrawFilledRect{15, "55 8B EC 83 EC 40 53 57 8B F9 8B 0D", "DrawFilledRect"};
inline constexpr Slot DrawSetTextFont{22, "55 8B EC 8B 45 08 89 81 B0 02 00 00 5D C2 04 00", "DrawSetTextFont"};
inline constexpr Slot DrawSetTextColor{24, "55 8B EC 66 0F 6E 45 14 53 56 8B F1", "DrawSetTextColor"};
inline constexpr Slot DrawSetTextPos{25, "55 8B EC 8B 45 08 8B 55 0C 89 41 34 89 51 38 5D C2 08 00", "DrawSetTextPos"};
inline constexpr Slot DrawPrintText{27, "55 8B EC 83 EC 4C 53 56 8B F1 8B 0D", "DrawPrintText"};
inline constexpr Slot DrawGetTextureId{33, "55 8B EC E8 ? ? ? ? 8B 10 8B C8 8B 52 34 5D FF E2", "DrawGetTextureId"};
inline constexpr Slot DrawSetTextureFile{35, "55 8B EC 56 57 8B F1 E8", "DrawSetTextureFile"};
inline constexpr Slot DrawSetTexture{37, "55 8B EC 56 8B 75 08 57 8B F9 3B B7 BC 02 00 00", "DrawSetTexture"};
inline constexpr Slot DrawTexturedRect{39, "55 8B EC 83 EC 50 56 8B F1 80 7E 2F 00 0F 84", "DrawTexturedRect"};
inline constexpr Slot CreateNewTextureID{41, "55 8B EC E8 ? ? ? ? 8B 10 8B C8 8B 12 5D FF E2", "CreateNewTextureID"};
inline constexpr Slot GetScreenSize{42, "55 8B EC 83 EC 08 80 B9 80 03 01 00 00", "GetScreenSize"};
inline constexpr Slot GetFontTall{72, "55 8B EC 8B 45 08 50 E8", "GetFontTall"};
inline constexpr Slot GetTextSize{77, "55 8B EC 8B 45 14 8B 4D 10 8B 55 0C 50 8B 45 08", "GetTextSize"};
inline constexpr Slot PlayUiSound{80, "55 8B EC 8B 81 EC 02 01 00 85 C0 74 09", "PlaySound"};
inline constexpr Slot GetPopupCount{81, "8B 81 6C 02 00 00 C3", "GetPopupCount"};
inline constexpr Slot GetPopup{82, "55 8B EC 8B 81 60 02 00 00 8B 4D 08 8B 04 88 5D C2", "GetPopup"};
inline constexpr Slot PaintTraverseEx{117, "55 8B EC 8B 55 08 83 EC 2C 56 8B F1 8B 0D", "PaintTraverseEx"};
inline constexpr const Slot *All[] = {&GetEmbeddedPanel, &DrawSetColor, &DrawFilledRect, &DrawSetTextFont,
    &DrawSetTextColor, &DrawSetTextPos, &DrawPrintText, &DrawGetTextureId, &DrawSetTextureFile,
    &DrawSetTexture, &DrawTexturedRect, &CreateNewTextureID, &GetScreenSize, &GetFontTall,
    &GetTextSize, &PlayUiSound, &GetPopupCount, &GetPopup, &PaintTraverseEx};
}

// CMatSystemSurface::PaintTraverseEx brackets painting with the non-virtual
// StartDrawing/FinishDrawing pair, guarded by a module-global "in drawing" flag.
namespace PaintTraverseExLayout {
inline constexpr std::size_t FlagCompare = 0x4B;        // cmp byte ptr ds:[flag],0
inline constexpr const char *FlagCompareSignature = "80 3D ? ? ? ? 00";
inline constexpr std::size_t StartCall = 0x79;          // mov ecx,esi; mov [ebp-1],al; call StartDrawing
inline constexpr const char *StartCallSignature = "8B CE 88 45 FF E8";
inline constexpr std::size_t FinishCall = 0x258;        // mov ecx,esi; call FinishDrawing
inline constexpr const char *FinishCallSignature = "8B CE E8";
inline constexpr const char *StartDrawingSignature = "55 8B EC 83 EC 10 80 3D";
inline constexpr std::size_t StartDrawingFlagStore = 0x82; // mov byte ptr ds:[flag],1
inline constexpr const char *StartDrawingFlagStoreSignature = "C6 05 ? ? ? ? 01";
inline constexpr const char *FinishDrawingSignature = "56 6A 00 E8";
}

namespace PanelSlot {
inline constexpr Slot GetSize{5, "55 8B EC 8B 55 10 8B 4D 08 8B 01 8B 40 34", "GetSize"};
inline constexpr Slot GetAbsPos{10, "55 8B EC 8B 55 10 8B 4D 08 8B 01 8B 40 48", "GetAbsPos"};
inline constexpr Slot SetVisible{14, "55 8B EC 8B 4D 08 8B 01 8B 55 0C 8B 40 5C 52 FF D0 5D C2 08 00", "SetVisible"};
inline constexpr Slot IsVisible{15, "55 8B EC 8B 4D 08 8B 01 8B 50 64 FF D2 5D C2 04 00", "IsVisible"};
inline constexpr Slot GetChildCount{17, "55 8B EC 8B 4D 08 8B 01 8B 50 70 FF D2 5D C2 04 00", "GetChildCount"};
inline constexpr Slot GetChild{18, "55 8B EC 8B 4D 08 8B 01 8B 55 0C 8B 40 74", "GetChild"};
inline constexpr Slot IsFullyVisible{26, "55 8B EC 8B 4D 08 8B 01 8B 50 24 FF D2 5D C2 04 00", "IsFullyVisible"};
inline constexpr Slot GetName{36, "55 8B EC 8B 4D 08 8B 01 8B 90 8C 00 00 00 FF D2", "GetName"};
// Both forward through Client(panel) (slot 65) to IClientPanel::RequestFocus (+0x30)
// and IClientPanel::GetCurrentKeyFocus (+0x40).
inline constexpr Slot RequestFocus{48, "55 8B EC 8B 01 8B 55 08 8B 80 04 01 00 00 52 FF D0 8B 4D 0C 8B 10 8B 52 30", "RequestFocus"};
inline constexpr Slot GetCurrentKeyFocus{51, "55 8B EC 8B 01 8B 55 08 8B 80 04 01 00 00 52 FF D0 8B 10 8B C8 8B 42 40", "GetCurrentKeyFocus"};
inline constexpr const Slot *All[] = {&GetSize, &GetAbsPos, &SetVisible, &IsVisible, &GetChildCount, &GetChild,
    &IsFullyVisible, &GetName, &RequestFocus, &GetCurrentKeyFocus};
}

namespace SchemeManagerSlot {
inline constexpr Slot GetScheme{5, "55 8B EC 53 56 57 8B F9 BE 01 00 00 00 39 77 10", "GetScheme"};
inline constexpr Slot GetIScheme{8, "55 8B EC 8B 45 08 3B 41 10 72", "GetIScheme"};
inline constexpr Slot GetProportionalScaledValueEx{13, "55 8B EC 56 8B F1 8B 4D 08 8B 06 8B", "GetProportionalScaledValueEx"};
inline constexpr const Slot *All[] = {&GetScheme, &GetIScheme, &GetProportionalScaledValueEx};
}

namespace SchemeSlot {
inline constexpr Slot GetFont{3, "55 8B EC 56 8B F1 8D 4E 18 E8", "GetFont"};
}

template <typename Return, typename... Args>
Return CallSlot(void *object, int index, Args... args)
{
    using Function = Return(__thiscall *)(void *, Args...);
    return reinterpret_cast<Function>((*reinterpret_cast<void ***>(object))[index])(object, args...);
}

inline const std::uint8_t *SlotCode(void *object, int index)
{
    if (!object || !VerifiedPortalSymbols::Detail::Readable(object, sizeof(void *), false))
        return nullptr;
    void **table = *reinterpret_cast<void ***>(object);
    if (!VerifiedPortalSymbols::Detail::Readable(table + index, sizeof(void *), false))
        return nullptr;
    return reinterpret_cast<const std::uint8_t *>(table[index]);
}

inline bool VerifyCode(const std::uint8_t *code, const char *signature)
{
    return code && VerifiedPortalSymbols::Detail::Readable(code, SignatureLength(signature), true) &&
        MatchesSignature(code, signature);
}

inline bool VerifySlot(void *object, const Slot &slot, std::string &failure)
{
    if (VerifyCode(SlotCode(object, slot.index), slot.signature))
        return true;
    failure = slot.name;
    return false;
}

class Api
{
public:
    // Null interfaces or a single mismatching prologue keep the API unavailable.
    bool Resolve(void *surface, void *panel, void *schemeManager, std::string &failure)
    {
        m_Ready = false;
        if (!surface || !panel || !schemeManager) {
            failure = "VGUI interface missing";
            return false;
        }
        for (const Slot *slot : SurfaceSlot::All)
            if (!VerifySlot(surface, *slot, failure)) return false;
        for (const Slot *slot : PanelSlot::All)
            if (!VerifySlot(panel, *slot, failure)) return false;
        for (const Slot *slot : SchemeManagerSlot::All)
            if (!VerifySlot(schemeManager, *slot, failure)) return false;

        using namespace PaintTraverseExLayout;
        const auto *paint = SlotCode(surface, SurfaceSlot::PaintTraverseEx.index);
        if (!VerifyCode(paint + FlagCompare, FlagCompareSignature) ||
            !VerifyCode(paint + StartCall, StartCallSignature) ||
            !VerifyCode(paint + FinishCall, FinishCallSignature)) {
            failure = "PaintTraverseEx drawing bracket";
            return false;
        }
        const auto relativeTarget = [](const std::uint8_t *call) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, call + 1, sizeof(displacement));
            return call + 5 + displacement;
        };
        const auto *start = relativeTarget(paint + StartCall + 5);
        const auto *finish = relativeTarget(paint + FinishCall + 2);
        std::uintptr_t flag = 0, storedFlag = 0;
        std::memcpy(&flag, paint + FlagCompare + 2, sizeof(flag));
        if (!VerifyCode(start, StartDrawingSignature) || !VerifyCode(finish, FinishDrawingSignature) ||
            !VerifyCode(start + StartDrawingFlagStore, StartDrawingFlagStoreSignature)) {
            failure = "StartDrawing/FinishDrawing";
            return false;
        }
        std::memcpy(&storedFlag, start + StartDrawingFlagStore + 2, sizeof(storedFlag));
        if (flag != storedFlag ||
            !VerifiedPortalSymbols::Detail::Readable(reinterpret_cast<const void *>(flag), 1, false)) {
            failure = "drawing flag";
            return false;
        }
        m_Surface = surface;
        m_Panel = panel;
        m_SchemeManager = schemeManager;
        m_PaintTraverseEx = paint;
        m_StartDrawing = start;
        m_FinishDrawing = finish;
        m_InDrawing = reinterpret_cast<const volatile std::uint8_t *>(flag);
        m_Ready = true;
        return true;
    }

    bool Ready() const { return m_Ready; }
    void *PaintTraverseExAddress() const { return const_cast<std::uint8_t *>(m_PaintTraverseEx); }

    // Surface
    VPANEL GetEmbeddedPanel() const { return CallSlot<VPANEL>(m_Surface, SurfaceSlot::GetEmbeddedPanel.index); }
    void DrawSetColor(int r, int g, int b, int a) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawSetColor.index, r, g, b, a); }
    void DrawFilledRect(int x0, int y0, int x1, int y1) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawFilledRect.index, x0, y0, x1, y1); }
    void DrawSetTextFont(HFont font) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawSetTextFont.index, font); }
    void DrawSetTextColor(int r, int g, int b, int a) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawSetTextColor.index, r, g, b, a); }
    void DrawSetTextPos(int x, int y) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawSetTextPos.index, x, y); }
    void DrawPrintText(const wchar_t *text, int length) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawPrintText.index, text, length, 0); }
    int DrawGetTextureId(const char *file) const { return CallSlot<int>(m_Surface, SurfaceSlot::DrawGetTextureId.index, file); }
    void DrawSetTextureFile(int id, const char *file) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawSetTextureFile.index, id, file, 1, false); }
    void DrawSetTexture(int id) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawSetTexture.index, id); }
    void DrawTexturedRect(int x0, int y0, int x1, int y1) const { CallSlot<void>(m_Surface, SurfaceSlot::DrawTexturedRect.index, x0, y0, x1, y1); }
    int CreateNewTextureID() const { return CallSlot<int>(m_Surface, SurfaceSlot::CreateNewTextureID.index, false); }
    void GetScreenSize(int &wide, int &tall) const { CallSlot<void, int &, int &>(m_Surface, SurfaceSlot::GetScreenSize.index, wide, tall); }
    int GetFontTall(HFont font) const { return CallSlot<int>(m_Surface, SurfaceSlot::GetFontTall.index, font); }
    void GetTextSize(HFont font, const wchar_t *text, int &wide, int &tall) const { CallSlot<void, HFont, const wchar_t *, int &, int &>(m_Surface, SurfaceSlot::GetTextSize.index, font, text, wide, tall); }
    void PlayUiSound(const char *file) const { CallSlot<void>(m_Surface, SurfaceSlot::PlayUiSound.index, file); }
    int GetPopupCount() const { return CallSlot<int>(m_Surface, SurfaceSlot::GetPopupCount.index); }
    VPANEL GetPopup(int index) const { return CallSlot<VPANEL>(m_Surface, SurfaceSlot::GetPopup.index, index); }
    bool InDrawing() const { return *m_InDrawing != 0; }
    void StartDrawing() const { reinterpret_cast<void(__thiscall *)(void *)>(const_cast<std::uint8_t *>(m_StartDrawing))(m_Surface); }
    void FinishDrawing() const { reinterpret_cast<void(__thiscall *)(void *)>(const_cast<std::uint8_t *>(m_FinishDrawing))(m_Surface); }

    // Panels
    void GetSize(VPANEL panel, int &wide, int &tall) const { CallSlot<void, VPANEL, int &, int &>(m_Panel, PanelSlot::GetSize.index, panel, wide, tall); }
    void GetAbsPos(VPANEL panel, int &x, int &y) const { CallSlot<void, VPANEL, int &, int &>(m_Panel, PanelSlot::GetAbsPos.index, panel, x, y); }
    bool IsVisible(VPANEL panel) const { return CallSlot<bool>(m_Panel, PanelSlot::IsVisible.index, panel); }
    bool IsFullyVisible(VPANEL panel) const { return CallSlot<bool>(m_Panel, PanelSlot::IsFullyVisible.index, panel); }
    int GetChildCount(VPANEL panel) const { return CallSlot<int>(m_Panel, PanelSlot::GetChildCount.index, panel); }
    VPANEL GetChild(VPANEL panel, int index) const { return CallSlot<VPANEL>(m_Panel, PanelSlot::GetChild.index, panel, index); }
    const char *GetName(VPANEL panel) const { return CallSlot<const char *>(m_Panel, PanelSlot::GetName.index, panel); }
    void SetVisible(VPANEL panel, bool visible) const { CallSlot<void, VPANEL, bool>(m_Panel, PanelSlot::SetVisible.index, panel, visible); }
    void RequestFocus(VPANEL panel) const { CallSlot<void, VPANEL, int>(m_Panel, PanelSlot::RequestFocus.index, panel, 0); }
    VPANEL GetCurrentKeyFocus(VPANEL panel) const { return CallSlot<VPANEL>(m_Panel, PanelSlot::GetCurrentKeyFocus.index, panel); }

    // Schemes. The IScheme object is verified the first time it is used.
    HScheme GetScheme(const char *tag) const { return CallSlot<HScheme>(m_SchemeManager, SchemeManagerSlot::GetScheme.index, tag); }
    int ScaledValue(HScheme scheme, int value) const { return CallSlot<int>(m_SchemeManager, SchemeManagerSlot::GetProportionalScaledValueEx.index, scheme, value); }
    void *VerifiedScheme(HScheme scheme, std::string &failure) const
    {
        void *object = CallSlot<void *>(m_SchemeManager, SchemeManagerSlot::GetIScheme.index, scheme);
        return object && VerifySlot(object, SchemeSlot::GetFont, failure) ? object : nullptr;
    }
    HFont GetFont(void *scheme, const char *name, bool proportional) const { return CallSlot<HFont>(scheme, SchemeSlot::GetFont.index, name, proportional); }

private:
    bool m_Ready = false;
    void *m_Surface = nullptr;
    void *m_Panel = nullptr;
    void *m_SchemeManager = nullptr;
    const std::uint8_t *m_PaintTraverseEx = nullptr;
    const std::uint8_t *m_StartDrawing = nullptr;
    const std::uint8_t *m_FinishDrawing = nullptr;
    const volatile std::uint8_t *m_InDrawing = nullptr;
};

} // namespace Portal2Vgui
