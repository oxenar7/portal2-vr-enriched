#pragma once

// IMaterial slots used to hide a gun model's arms while the viewmodels draw. Read from
// CMaterial's vtable (found by RTTI) in materialsystem.dll of Portal 2, Alive & Kicking, Portal
// Stories: Mel, Thinking with Time Machine and Portal Reloaded (mel-re/material_slots.py): the
// order is the SDK's in all five. A mismatch leaves the arms drawn.

#include "portal2_vgui.h"

namespace MaterialAbi {

using Portal2Vgui::Slot;

// void SetMaterialVarFlag(MaterialVarFlags_t, bool): queued behind the draws when a render thread
// runs (mat_queue_mode), so setting and clearing around a draw keeps their order.
inline constexpr Slot SetMaterialVarFlag{29, "55 8B EC 56 8B F1 8B 0D ? ? ? ? 8B 01 8B 90",
    "SetMaterialVarFlag"};
// bool GetMaterialVarFlag(MaterialVarFlags_t): the $flags parameter's value and the flag.
inline constexpr Slot GetMaterialVarFlag{30, "55 8B EC 8B 41 24 85 C0 74 17 83 38 00 74 12 8B",
    "GetMaterialVarFlag"};
// bool IsErrorMaterial(): this is the material FindMaterial returns for a missing file.
inline constexpr Slot IsErrorMaterial{42, "33 C0 39 0D ? ? ? ? 0F 94 C0 C3", "IsErrorMaterial"};
inline constexpr const Slot *All[] = {&SetMaterialVarFlag, &GetMaterialVarFlag, &IsErrorMaterial};

// MATERIAL_VAR_NO_DRAW: CMaterial::DrawMesh skips the material's meshes.
inline constexpr int kNoDraw = 1 << 2;

inline constexpr char kModelTextures[] = "Model textures"; // TEXTURE_GROUP_MODEL

// Hides a material for one scope and puts its flag back as it was.
class HideScope
{
public:
    explicit HideScope(void *material) : m_Material(material)
    {
        if (!m_Material) return;
        m_WasHidden = Portal2Vgui::CallSlot<bool>(m_Material, GetMaterialVarFlag.index, kNoDraw);
        if (!m_WasHidden)
            Portal2Vgui::CallSlot<void>(m_Material, SetMaterialVarFlag.index, kNoDraw, true);
    }
    ~HideScope()
    {
        if (m_Material && !m_WasHidden)
            Portal2Vgui::CallSlot<void>(m_Material, SetMaterialVarFlag.index, kNoDraw, false);
    }
    HideScope(const HideScope &) = delete;
    HideScope &operator=(const HideScope &) = delete;

private:
    void *m_Material;
    bool m_WasHidden = false;
};

} // namespace MaterialAbi
