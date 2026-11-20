#pragma once

// IVModelInfoClient ("VModelInfoClient004", engine.dll) slots used to read which portal gun
// model the game loaded. Read from CModelInfoClient's vtable in Portal 2's and Portal: Alive &
// Kicking's engine.dll (aak-re/modelinfo_slots3.py); the three functions match byte for byte
// apart from relocated addresses. A mismatch leaves the game's own gun grip in place.

#include "portal2_vgui.h"

#include <cstdint>

namespace ModelInfoAbi {

using Portal2Vgui::Slot;

// model_t *GetModel(int index) and int GetModelIndex(const char *name) forward to the local
// client state: the index to its model precache table, the name to its string table (-1 when
// the model is not precached). Same shape, so the slot order follows the SDK.
inline constexpr Slot GetModel{1, "55 8B EC 8B 45 08 50 E8 ? ? ? ? 8B C8 E8 ? ? ? ? 5D C2 04 00", "GetModel"};
inline constexpr Slot GetModelIndex{2, "55 8B EC 8B 45 08 50 E8 ? ? ? ? 8B C8 E8 ? ? ? ? 5D C2 04 00",
    "GetModelIndex"};
// studiohdr_t *GetStudiomodel(const model_t *): null unless the model type is mod_studio (3),
// otherwise IMDLCache::GetStudioHdr of its handle.
inline constexpr Slot GetStudiomodel{30,
    "55 8B EC 8B 45 08 83 B8 10 01 00 00 03 75 19 8B 0D ? ? ? ? 0F B7 80 38 01 00 00 "
    "8B 11 8B 52 34 50 FF D2 5D C2 04 00 33 C0 5D C2 04 00", "GetStudiomodel"};
inline constexpr const Slot *All[] = {&GetModel, &GetModelIndex, &GetStudiomodel};

inline constexpr char kPortalGunModel[] = "models/weapons/v_portalgun.mdl";
inline constexpr std::int32_t kStudioId = 0x54534449; // "IDST"

// studiohdr_t begins with id, version and checksum.
inline bool ReadChecksum(const void *header, std::int32_t &checksum)
{
    if (!header || !VerifiedPortalSymbols::Detail::Readable(header, 3 * sizeof(std::int32_t), false))
        return false;
    const auto *fields = static_cast<const std::int32_t *>(header);
    if (fields[0] != kStudioId) return false;
    checksum = fields[2];
    return true;
}

} // namespace ModelInfoAbi
