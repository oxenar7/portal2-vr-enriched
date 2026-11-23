#pragma once

// IMaterial slots used to hide a gun model's arms while the viewmodels draw. Read from
// CMaterial's vtable (found by RTTI) in materialsystem.dll of Portal 2, Alive & Kicking, Portal
// Stories: Mel, Thinking with Time Machine and Portal Reloaded (mel-re/material_slots.py): the
// order is the SDK's in all five. A mismatch leaves the arms drawn.
//
// FindMaterial hands the game thread a CMaterial_QueueFriendly, whose slots forward to the real
// CMaterial it keeps at +4. The slots below are the real material's.

#include <cstdint>
#include <cstring>
#include <string>

#include "portal2_vgui.h"

namespace MaterialAbi {

using Portal2Vgui::Slot;

// void SetMaterialVarFlag(MaterialVarFlags_t, bool): first asks the material system for the
// render thread's call queue (mov ecx,[IMaterialSystem *]; call [vtable+N]) and queues itself
// there when one runs; otherwise sets the flag and rebuilds the material's state snapshots.
inline constexpr Slot SetMaterialVarFlag{29,
    "55 8B EC 56 8B F1 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? FF D2 85 C0 74", "SetMaterialVarFlag"};
inline constexpr std::size_t kSystemPointerAt = 8;  // imm32 of mov ecx,[...]
inline constexpr std::size_t kCallQueueSlotAt = 16; // disp32 of mov edx,[eax+...]
// bool GetMaterialVarFlag(MaterialVarFlags_t): the integer of the $flags var, the first shader
// parameter (m_pShaderParams at +24h, the integer at +8).
inline constexpr Slot GetMaterialVarFlag{30,
    "55 8B EC 8B 41 24 85 C0 74 17 83 38 00 74 12 8B 00 8B 40 08 23 45 08", "GetMaterialVarFlag"};
// bool IsErrorMaterial(): this is the material FindMaterial returns for a missing file.
inline constexpr Slot IsErrorMaterial{42, "33 C0 39 0D ? ? ? ? 0F 94 C0 C3", "IsErrorMaterial"};
// void DrawMesh(...): skips the shader's draw when the $flags integer has MATERIAL_VAR_NO_DRAW,
// read at draw time, so writing the integer hides the material for the draws that follow.
inline constexpr Slot DrawMesh{70,
    "55 8B EC 56 8B F1 83 7E 14 00 74 ? 8B 46 24 85 C0 74 ? 83 38 00 74 ? 8B 00 8B 40 08 EB ? 33 C0 A8 04 75",
    "DrawMesh"};
inline constexpr const Slot *All[] = {&SetMaterialVarFlag, &GetMaterialVarFlag, &IsErrorMaterial, &DrawMesh};
inline constexpr std::size_t kShaderParams = 0x24;
inline constexpr std::size_t kVarInteger = 8;

// CMaterial_QueueFriendly::GetMaterialVarFlag: mov ecx,[ecx+4]; jmp [vtable+78h].
inline constexpr Slot QueueFriendlyGetMaterialVarFlag{30, "55 8B EC 8B 49 04 8B 01 8B 40 78 5D FF E0",
    "CMaterial_QueueFriendly::GetMaterialVarFlag"};
inline constexpr std::size_t kRealMaterial = 4;

// MATERIAL_VAR_NO_DRAW
inline constexpr int kNoDraw = 1 << 2;

inline constexpr char kModelTextures[] = "Model textures"; // TEXTURE_GROUP_MODEL

inline const void *Table(const void *object) { return *static_cast<const void *const *>(object); }

inline void *Field(void *object, std::size_t offset)
{
    void *value = nullptr;
    std::memcpy(&value, static_cast<std::uint8_t *>(object) + offset, sizeof(value));
    return value;
}

// Checks the first material found and keeps its vtables, which later lookups compare against:
// CMaterial_QueueFriendly's (null when FindMaterial returned the CMaterial itself) and CMaterial's.
inline bool Verify(void *found, const void *&wrapperTable, const void *&materialTable, std::string &failure)
{
    void *material = found;
    wrapperTable = materialTable = nullptr;
    if (Portal2Vgui::VerifyCode(Portal2Vgui::SlotCode(found, QueueFriendlyGetMaterialVarFlag.index),
            QueueFriendlyGetMaterialVarFlag.signature)) {
        if (!VerifiedPortalSymbols::Detail::Readable(found, kRealMaterial + sizeof(void *), false)) {
            failure = "CMaterial_QueueFriendly";
            return false;
        }
        wrapperTable = Table(found);
        material = Field(found, kRealMaterial);
    }
    for (const Slot *slot : All)
        if (!Portal2Vgui::VerifySlot(material, *slot, failure))
            return false;
    materialTable = Table(material);
    return true;
}

// The CMaterial behind what FindMaterial returned, or null when its vtables are not the verified ones.
inline void *Real(void *found, const void *wrapperTable, const void *materialTable)
{
    if (!found || !materialTable)
        return nullptr;
    void *material = wrapperTable && Table(found) == wrapperTable ? Field(found, kRealMaterial) : found;
    return material && Table(material) == materialTable ? material : nullptr;
}

// True when a render thread draws later (mat_queue_mode): SetMaterialVarFlag's own test.
inline bool RenderThreadQueues(void *material)
{
    const std::uint8_t *code = Portal2Vgui::SlotCode(material, SetMaterialVarFlag.index);
    void **systemPointer = nullptr;
    std::uint32_t queueSlot = 0;
    std::memcpy(&systemPointer, code + kSystemPointerAt, sizeof(systemPointer));
    std::memcpy(&queueSlot, code + kCallQueueSlotAt, sizeof(queueSlot));
    void *system = *systemPointer;
    if (!system)
        return false;
    using CallQueue = void *(__thiscall *)(void *);
    const auto *table = static_cast<const std::uint8_t *>(Table(system));
    return reinterpret_cast<CallQueue>(Field(const_cast<std::uint8_t *>(table), queueSlot))(system) != nullptr;
}

// Hides a CMaterial for one scope and puts its flag back as it was. Without a render thread the
// $flags integer is written in place, which DrawMesh reads, so the state snapshots are not
// rebuilt twice per draw. With one, SetMaterialVarFlag queues both changes around the draws.
class HideScope
{
public:
    explicit HideScope(void *material) : m_Material(material)
    {
        if (!m_Material)
            return;
        if (RenderThreadQueues(m_Material)) {
            m_Queued = !Portal2Vgui::CallSlot<bool>(m_Material, GetMaterialVarFlag.index, kNoDraw);
            if (m_Queued)
                Portal2Vgui::CallSlot<void>(m_Material, SetMaterialVarFlag.index, kNoDraw, true);
            return;
        }
        void *params = Field(m_Material, kShaderParams);
        void *flagsVar = params ? Field(params, 0) : nullptr;
        m_Flags = flagsVar ? reinterpret_cast<int *>(static_cast<std::uint8_t *>(flagsVar) + kVarInteger) : nullptr;
        if (m_Flags && (*m_Flags & kNoDraw))
            m_Flags = nullptr;
        if (m_Flags)
            *m_Flags |= kNoDraw;
    }
    ~HideScope()
    {
        if (m_Queued)
            Portal2Vgui::CallSlot<void>(m_Material, SetMaterialVarFlag.index, kNoDraw, false);
        else if (m_Flags)
            *m_Flags &= ~kNoDraw;
    }
    HideScope(const HideScope &) = delete;
    HideScope &operator=(const HideScope &) = delete;

private:
    void *m_Material;
    bool m_Queued = false;
    int *m_Flags = nullptr;
};

} // namespace MaterialAbi
