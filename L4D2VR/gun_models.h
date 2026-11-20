#pragma once

// Portal gun viewmodels whose grip the mod knows. Their idle animations hold the gun at
// different places relative to the model origin, so each needs its own shift of the grip
// offset. They are told apart by the checksum the model compiler stores in studiohdr_t, which
// the game reads from whichever models/weapons/v_portalgun.mdl it mounted (a texture/model
// pack in a later VPK replaces the game's own).

#include <cstdint>

namespace GunModels {

struct Model {
    std::int32_t checksum; // studiohdr_t::checksum
    const char *name;      // for the log
    // Added to the grip offset, in the units and axes of ViewmodelPosCustomOffset (x forward,
    // y right, z up; the model moves against them), relative to Portal 2's own model.
    float shift[3];
};

// Portal 2's own model (portal2/pak01_dir.vpk).
inline constexpr Model kPortal2{-1707162297, "Portal 2", {0.0f, 0.0f, 0.0f}};
// Portal: Alive & Kicking's (AAK/pak01_dir.vpk): Portal 2's skeleton, but every sequence puts the
// root bone, and so the hand and muzzle, 6 units further forward and 1 further left (frame 0 of
// each sequence decoded from both models).
inline constexpr Model kAliveAndKicking{435277794, "Portal: Alive & Kicking", {6.0f, -1.0f, 0.0f}};
// Portal Stories: Mel's (portal_stories/pak01_dir.vpk): its own 1970s gun on a new rig with both
// of Mel's arms. The shift puts the same place on its body (by the gun's box in frame 0 of idle,
// skinned from both models) where Portal 2's grip sits on Portal 2's; not checked in the headset.
inline constexpr Model kPortalStoriesMel{1942580893, "Portal Stories: Mel", {2.4f, -0.4f, 1.7f}};
// "Portal VR Gun Model" from GameBanana, part of the Portal 2 VR HD Collection (pak25 there): its
// idle holds the gun axis-aligned at the model origin, so with Portal 2's grip it shows about
// 26 cm too high. The shift was matched to the controller in the headset on 2026-11-10.
inline constexpr Model kVrGunModel{-1964506577, "Portal VR Gun Model", {-7.3f, -8.2f, 11.35f}};

inline constexpr const Model *kKnown[] = {&kPortal2, &kAliveAndKicking, &kPortalStoriesMel, &kVrGunModel};

inline const Model *Find(std::int32_t checksum)
{
    for (const Model *model : kKnown)
        if (model->checksum == checksum) return model;
    return nullptr;
}

// The model whose grip applies: the loaded one when it was read and is known, otherwise the
// game's own.
inline const Model &Choose(bool read, std::int32_t checksum, const Model &gameDefault)
{
    const Model *known = read ? Find(checksum) : nullptr;
    return known ? *known : gameDefault;
}

} // namespace GunModels
