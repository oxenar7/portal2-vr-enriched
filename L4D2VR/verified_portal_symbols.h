#pragma once

#include <Windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "psapi.lib")

namespace VerifiedPortalSymbols {

enum class Symbol { EyePosition, WeaponShootPosition, ViewModelFov };

struct Result {
    std::uintptr_t address = 0;
    std::uintptr_t shootCaller = 0;
};

// C_BasePlayer field offsets; zero when the loaded client.dll is not a verified build.
struct PlayerFields {
    std::uint32_t viewOffsetZ = 0; // float m_vecViewOffset.z
    std::uint32_t flags = 0;       // int m_fFlags
};

// Addresses behind the held-object log and options; all zero unless server.dll and client.dll
// are both verified builds of the same game.
struct HeldObjectLayout {
    std::uintptr_t entityList = 0;           // global pointer to the entity slots
    std::uintptr_t laserAutoaimCvar = 0;     // server sv_laser_cube_autoaim
    std::uintptr_t holdColumnCvar = 0;       // server player_hold_object_in_column
    std::uintptr_t clientHoldColumnCvar = 0; // client copy; its grab code reads it as well
    std::uintptr_t holdShootCaller = 0;      // return address of UpdateObject's ray origin call
    std::uintptr_t preferredCarryAngles = 0; // Pickup_GetPreferredCarryAngles
};

namespace Detail {

struct Build {
    const wchar_t *module;
    std::string_view sha256;
    std::uint32_t timestamp;
    std::uint32_t size;
};

// Every RVA below was read from one exact build and is accepted only with that build's
// SHA-256 and with all of its witnesses. The games differ in addresses, not in layout:
// the struct offsets checked by the witnesses are the same in all of them.
struct ServerLayout {
    Build build;
    std::uint32_t eyePosition;        // CPortal_Player::EyePosition
    std::uint32_t eyeAngles;          // CBasePlayer::EyeAngles
    std::uint32_t shootWrapper;       // EarPosition / Weapon_ShootPosition wrapper
    std::uint32_t portalPlayerVtable; // CPortal_Player
    std::uint32_t basePlayerVtable;   // CBasePlayer
    std::uint32_t fireOwnerCall;      // CWeaponPortalgun::FirePortal: mov edi,ecx ... call GetOwner
    std::array<std::uint8_t, 12> fireOwnerBytes;
    std::uint32_t fireShootCall;      // FirePortal: virtual Weapon_ShootPosition on the owner
    std::uint32_t grabDistanceOffset; // CGrabController datamap: m_flDistanceOffset fieldOffset
    std::uint32_t laserName, columnName, modelName;
    std::uint32_t laserCtor, columnCtor; // word holding the name in each ConVar constructor
    std::uint32_t laserCvar, columnCvar;
    std::uint32_t laserReader;        // mov eax,[parent]; cmp dword [eax+30h],0 at +5
    std::uint32_t columnReader;       // mov edx,[parent]; cmp dword [edx+30h],0 at columnReaderCheck
    std::uint32_t columnReaderCheck;
    std::uint32_t entityListSite;     // UpdateObject resolving m_attachedEntity
    std::uint32_t entityList;
    std::uint32_t holdShootCall;      // UpdateObject: virtual Weapon_ShootPosition, call ends at +0x13
    std::array<std::uint8_t, 19> holdShootBytes;
    std::uint32_t carryAngles;        // Pickup_GetPreferredCarryAngles
    std::uint32_t carryAnglesBody;
    std::uint32_t carryAnglesCall;
    std::array<std::uint8_t, 5> carryAnglesCallBytes;
    std::uint32_t overheadReader1, overheadReader2; // the only readers of m_bAllowObjectOverhead
    std::uint32_t classnameCompare;   // cmp [esi+60h] (m_iClassname)
    std::uint32_t modelNameField;     // datamap word naming m_ModelName; its offset 0xAC at +10
    std::uint32_t radiusSite, absOriginSite, absAnglesSite;
};

struct ClientLayout {
    Build build;
    std::uint32_t fovFunction;        // ClientModePortalNormal::GetViewModelFOV
    std::uint32_t fovCvarParent;      // cl_viewmodelfov parent pointer the function reads
    std::uint32_t normalVtable, fullscreenVtable;
    std::uint32_t fovName, fovCtor, fovCvar;
    std::uint32_t viewOffsetName, viewOffsetPush; // RecvPropFloat("m_vecViewOffset[2]", 0x104)
    std::uint32_t flagsName, flagsPush;           // RecvPropInt("m_fFlags", 0xF8)
    std::uint32_t columnName, columnCtor, columnCvar;
    std::uint32_t columnReader;       // mov edx,[parent]; cmp dword [edx+30h],0 at +6
};

// Portal 2, Windows build of 2026 (server/client dated 2026-09-08).
inline constexpr ServerLayout portal2Server{
    {L"server.dll", "deb9fc303fdf4b0b0edcf58c4e1b17fbf20276f047728180f7ba7c8bf510f6b6",
        0x6AA0746Au, 0x978000u},
    0x369A70, 0x104590, 0x368270, 0x670B2C, 0x6166AC,
    0x40141E, {0x8B,0xF9,0x89,0x7D,0xEC,0xE8,0xD8,0x67,0xCD,0xFF,0x8B,0xF0},
    0x40160C, 0x7965CC,
    0x67F0A8, 0x689EC8, 0x5BB468,
    0x572EBB, 0x574000, 0x8562D0, 0x858ED8,
    0x3B1EEF, 0x3CAA34, 0x3CAA3D,
    0x3CA207, 0x7339F8,
    0x3CA5D4, {0x8B,0x16,0x8B,0x92,0x84,0x04,0,0,0x83,0xC4,0x10,0x8D,0x45,0xB4,0x50,0x8B,0xCE,0xFF,0xD2},
    0x3C4BA0, 0x3C4BD8, 0x3CB332, {0xE8,0x69,0x98,0xFF,0xFF},
    0x3CA57B, 0x3CB0FC,
    0x3B1EA2, 0xF038F,
    0x3CA91D, 0x3CA404, 0x311B5};

inline constexpr ClientLayout portal2Client{
    {L"client.dll", "1b22a5008c10c91215f5c66061ff24dd84e3afb626d60b9d74c600974536f66d",
        0x6AA07473u, 0xFF3000u},
    0x28AD20, 0xA08F84, 0x7C03D4, 0x7C0944,
    0x78C010, 0x6FE500, 0xA08F68,
    0x74C1A8, 0x8F8E9, 0x742218, 0x92CA4,
    0x81A3F0, 0x704EF0, 0xA228D0, 0x3B9B85};

// Portal: Alive & Kicking (Steam app 270470), build 25291273 (server/client dated 2024-03-12).
inline constexpr ServerLayout aakServer{
    {L"server.dll", "a9f7cbf8716b4568118184e8090794a0d2627ac70fee04f0680ca6daa3b17b15",
        0x65EFB6A1u, 0x971000u},
    0x362EB0, 0x100DE0, 0xF11D0, 0x669944, 0x60E984,
    0x3F95DE, {0x8B,0xF9,0x89,0x7D,0xEC,0xE8,0x18,0xAF,0xCD,0xFF,0x8B,0xF0},
    0x3F97CC, 0x78FE14,
    0x677ED8, 0x683090, 0x5B3980,
    0x56A4EB, 0x56B6E0, 0x84F920, 0x8526D0,
    0x3AA84F, 0x3C300C, 0x3C3012,
    0x3C27D7, 0x72CE18,
    0x3C2BA4, {0x8B,0x16,0x8B,0x92,0x84,0x04,0,0,0x83,0xC4,0x10,0x8D,0x45,0xC0,0x50,0x8B,0xCE,0xFF,0xD2},
    0x3BCF90, 0x3BCFC8, 0x3C38FF, {0xE8,0x8C,0x96,0xFF,0xFF},
    0x3C2B4B, 0x3C36D6,
    0x3AA802, 0xECB5F,
    0x3C2EED, 0x3C29D4, 0x2F155};

inline constexpr ClientLayout aakClient{
    {L"client.dll", "e238fc755a326b6599c73436e04212279b31da1af0dcb62e02a897cea255ee60",
        0x65EFAED9u, 0xFEF000u},
    0x283530, 0xA051FC, 0x7B62EC, 0x7B685C,
    0x781E40, 0x6F49B0, 0xA051E0,
    0x742200, 0x8C969, 0x7382E0, 0x8FB24,
    0x80FCE0, 0x6FB360, 0xA1EAC0, 0x3AFE35};

// Portal Stories: Mel (Steam app 317400), build 6522712 (server/client dated 2021-03-02).
inline constexpr ServerLayout melServer{
    {L"server.dll", "3ea16e788523edfb4d5122acf80f1bdd40e65785d18b923ceba95c99aa728876",
        0x603E2DEEu, 0x972000u},
    0x365530, 0x102C20, 0x363F80, 0x66AD24, 0x6106CC,
    0x3FB6DE, {0x8B,0xF9,0x89,0x7D,0xEC,0xE8,0xA8,0xAE,0xCD,0xFF,0x8B,0xF0},
    0x3FB8CC, 0x7915FC,
    0x6792A8, 0x684460, 0x5B5478,
    0x56C68B, 0x56D880, 0x851110, 0x853EC0,
    0x3ACABF, 0x3C53BC, 0x3C53C2,
    0x3C4B87, 0x72E9E0,
    0x3C4F54, {0x8B,0x16,0x8B,0x92,0x84,0x04,0,0,0x83,0xC4,0x10,0x8D,0x45,0xC0,0x50,0x8B,0xCE,0xFF,0xD2},
    0x3BF340, 0x3BF378, 0x3C5CAF, {0xE8,0x8C,0x96,0xFF,0xFF},
    0x3C4EFB, 0x3C5A86,
    0x3ACA72, 0xEE9AF,
    0x3C529D, 0x3C4D84, 0xF3355};

inline constexpr ClientLayout melClient{
    {L"client.dll", "f848c91621ec7a5dcf3ea4cabf0f0e98fe6ee1ad3c7c9e949854d5c8049fb4a6",
        0x603E2E11u, 0xFEF000u},
    0x287050, 0xA049B4, 0x7BA56C, 0x7BAADC,
    0x7860B0, 0x6F9000, 0xA04998,
    0x746198, 0x8F529, 0x73C1F8, 0x927F4,
    0x814848, 0x6FF9D0, 0xA1E2D8, 0x3B49C5};

inline constexpr const ServerLayout *serverLayouts[] = {&portal2Server, &aakServer, &melServer};
inline constexpr const ClientLayout *clientLayouts[] = {&portal2Client, &aakClient, &melClient};

// Kept for callers that name the Portal 2 builds directly.
inline constexpr const Build &server = portal2Server.build;
inline constexpr const Build &client = portal2Client.build;

inline const ServerLayout *ServerFor(std::string_view sha256)
{
    for (const auto *layout : serverLayouts)
        if (layout->build.sha256 == sha256) return layout;
    return nullptr;
}

inline const ClientLayout *ClientFor(std::string_view sha256)
{
    for (const auto *layout : clientLayouts)
        if (layout->build.sha256 == sha256) return layout;
    return nullptr;
}

inline bool Readable(const void *address, std::size_t length, bool executable)
{
    if (!address || !length) return false;
    auto at = reinterpret_cast<std::uintptr_t>(address);
    if (length > (std::numeric_limits<std::uintptr_t>::max)() - at) return false;
    const auto end = at + length;
    while (at < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void *>(at), &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD)) return false;
        const DWORD protection = region.Protect & 0xff;
        const bool read = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
            protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        const bool exec = protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (!read || (executable && !exec)) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - base) return false;
        const auto next = base + region.RegionSize;
        if (next <= at) return false;
        at = next < end ? next : end;
    }
    return true;
}

struct Image {
    const std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
    const IMAGE_SECTION_HEADER *sections = nullptr;
    unsigned count = 0;

    bool Open(const std::uint8_t *source, std::size_t imageSize, const Build &build)
    {
        if (!source || imageSize != build.size || imageSize < sizeof(IMAGE_DOS_HEADER) ||
            !Readable(source, sizeof(IMAGE_DOS_HEADER), false)) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(source);
        if (imageSize > (std::numeric_limits<std::uintptr_t>::max)() - base) return false;
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(source);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            static_cast<std::size_t>(dos->e_lfanew) > imageSize - sizeof(IMAGE_NT_HEADERS32)) return false;
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(source + dos->e_lfanew);
        if (!Readable(nt, sizeof(*nt), false) || nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->FileHeader.TimeDateStamp != build.timestamp ||
            nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage != imageSize ||
            // The loader rewrites ImageBase to the actual base when ASLR relocates the DLL;
            // every Portal 2 DLL prefers 0x10000000, so server/client are always relocated.
            (nt->OptionalHeader.ImageBase != 0x10000000u && nt->OptionalHeader.ImageBase != base) ||
            !nt->FileHeader.NumberOfSections || nt->OptionalHeader.SizeOfHeaders > imageSize)
            return false;
        const std::uint64_t tableOffset = static_cast<std::uint64_t>(dos->e_lfanew) +
            sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt->FileHeader.SizeOfOptionalHeader;
        const std::uint64_t tableSize = static_cast<std::uint64_t>(nt->FileHeader.NumberOfSections) *
            sizeof(IMAGE_SECTION_HEADER);
        if (tableOffset > nt->OptionalHeader.SizeOfHeaders ||
            tableSize > nt->OptionalHeader.SizeOfHeaders - tableOffset ||
            !Readable(source + tableOffset, static_cast<std::size_t>(tableSize), false)) return false;
        bytes = source;
        size = imageSize;
        sections = reinterpret_cast<const IMAGE_SECTION_HEADER *>(source + tableOffset);
        count = nt->FileHeader.NumberOfSections;
        return true;
    }

    bool Span(std::uint32_t rva, std::size_t length, bool executable = false) const
    {
        if (!bytes || !rva || !length || rva >= size || length > size - rva) return false;
        for (unsigned i = 0; i < count; ++i) {
            const auto &section = sections[i];
            const std::size_t start = section.VirtualAddress;
            const std::size_t span = section.Misc.VirtualSize ? section.Misc.VirtualSize :
                section.SizeOfRawData;
            if (start >= size || span > size - start || rva < start ||
                static_cast<std::size_t>(rva) - start > span ||
                length > span - (static_cast<std::size_t>(rva) - start)) continue;
            if (!(section.Characteristics & IMAGE_SCN_MEM_READ) ||
                (executable && !(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))) return false;
            return Readable(bytes + rva, length, executable);
        }
        return false;
    }

    std::uintptr_t At(std::uint32_t rva) const { return reinterpret_cast<std::uintptr_t>(bytes) + rva; }

    bool Match(std::uint32_t rva, const std::uint8_t *expected, std::size_t length,
               bool executable = true) const
    {
        return Span(rva, length, executable) && std::memcmp(bytes + rva, expected, length) == 0;
    }

    bool Match(std::uint32_t rva, std::initializer_list<std::uint8_t> expected,
               bool executable = true) const
    {
        return Match(rva, expected.begin(), expected.size(), executable);
    }

    template <std::size_t N>
    bool Match(std::uint32_t rva, const std::array<std::uint8_t, N> &expected,
               bool executable = true) const
    {
        return Match(rva, expected.data(), N, executable);
    }

    bool Word(std::uint32_t rva, std::uintptr_t expected, bool executable = false) const
    {
        if (expected > UINT32_MAX || !Span(rva, 4, executable)) return false;
        std::uint32_t actual = 0;
        std::memcpy(&actual, bytes + rva, 4);
        return actual == expected;
    }

    bool Text(std::uint32_t rva, const char *text, std::size_t length) const
    {
        return Span(rva, length) && std::memcmp(bytes + rva, text, length) == 0;
    }
};

inline bool EyeWitness(const Image &image, const ServerLayout &l)
{
    // CPortal_Player overrides the generic EyePosition implementation.
    return image.Match(l.eyePosition, {0x55,0x8B,0xEC,0x83,0xEC,0x18,0x56,0x8B,0xF1,
        0x8B,0x86,0x14,0x15,0,0,0x8B,0x16,0xF3,0x0F,0x7E,0x86,0x0C,0x15,0,0}) &&
        image.Word(l.portalPlayerVtable + 128*4, image.At(l.eyePosition)) &&
        image.Word(l.portalPlayerVtable + 129*4, image.At(l.eyeAngles)) &&
        image.Word(l.portalPlayerVtable + 131*4, image.At(l.shootWrapper));
}

inline bool ShootWitness(const Image &image, const ServerLayout &l)
{
    // Both EarPosition and Weapon_ShootPosition slots use this wrapper.
    // Only the verified FirePortal caller is eligible for shoot override.
    return image.Match(l.shootWrapper, {0x55,0x8B,0xEC,0x8B,0x01,0x8B,0x90,0,0x02,0,0,
        0x56,0x8B,0x75,0x08,0x56,0xFF,0xD2,0x8B,0xC6,0x5E,0x5D,0xC2,0x04,0}) &&
        image.Word(l.portalPlayerVtable + 131*4, image.At(l.shootWrapper)) &&
        image.Word(l.portalPlayerVtable + 289*4, image.At(l.shootWrapper)) &&
        image.Word(l.basePlayerVtable + 131*4, image.At(l.shootWrapper)) &&
        image.Word(l.basePlayerVtable + 289*4, image.At(l.shootWrapper)) &&
        image.Match(l.fireOwnerCall, l.fireOwnerBytes) &&
        image.Match(l.fireShootCall, {0x8B,0x16,0x8B,0x92,0x84,0x04,0,0,0x8D,0x45,0xF0,
            0x50,0x8B,0xCE,0xFF,0xD2,0xF3,0x0F,0x10,0x08});
}

inline bool FovWitness(const Image &image, const ClientLayout &l)
{
    constexpr char name[] = "cl_viewmodelfov";
    return image.Match(l.fovFunction, {0xA1}) &&
        image.Word(l.fovFunction + 1, image.At(l.fovCvarParent), true) &&
        image.Match(l.fovFunction + 5, {0xD9,0x40,0x2C,0xC3}) &&
        l.fovCvarParent == l.fovCvar + 0x1C &&
        image.Word(l.normalVtable + 35*4, image.At(l.fovFunction)) &&
        image.Word(l.fullscreenVtable + 35*4, image.At(l.fovFunction)) &&
        image.Text(l.fovName, name, sizeof(name)) &&
        image.Match(l.fovCtor, {0x6A,0x02,0x68}) &&
        image.Match(l.fovCtor + 7, {0x68}) &&
        image.Word(l.fovCtor + 8, image.At(l.fovName), true) &&
        image.Match(l.fovCtor + 12, {0xB9}) &&
        image.Word(l.fovCtor + 13, image.At(l.fovCvar), true) &&
        image.Match(l.fovCtor + 17, {0xE8});
}

inline bool PlayerFieldWitness(const Image &image, const ClientLayout &l)
{
    // RecvPropFloat("m_vecViewOffset[2]", 0x104, ...) in DT_LocalPlayerExclusive and
    // RecvPropInt("m_fFlags", 0xF8, ...): push offset immediately before push name.
    constexpr char viewOffsetName[] = "m_vecViewOffset[2]";
    constexpr char flagsName[] = "m_fFlags";
    return image.Text(l.viewOffsetName, viewOffsetName, sizeof(viewOffsetName)) &&
        image.Match(l.viewOffsetPush, {0x68,0x04,0x01,0,0,0x68}) &&
        image.Word(l.viewOffsetPush + 6, image.At(l.viewOffsetName), true) &&
        image.Text(l.flagsName, flagsName, sizeof(flagsName)) &&
        image.Match(l.flagsPush, {0x68,0xF8,0,0,0,0x68}) &&
        image.Word(l.flagsPush + 6, image.At(l.flagsName), true);
}

inline bool HeldObjectWitness(const Image &image, const ServerLayout &l)
{
    constexpr char laserName[] = "sv_laser_cube_autoaim";
    constexpr char columnName[] = "player_hold_object_in_column";
    constexpr char modelName[] = "m_ModelName";
    // CGrabController datamap: field index relative to m_flDistanceOffset, then its offset.
    constexpr struct { int index; std::uint32_t offset; } grabFields[] = {
        {-14, 0x4}, {-11, 0x203C}, {-7, 0x204C}, {-5, 0x2049}, {-4, 0x2054}, {-3, 0x2044},
        {-2, 0x2058}, {-1, 0x2064}, {0, 0x2068}, {1, 0x206C}, {3, 0x2088}};
    for (const auto &field : grabFields)
        if (!image.Word(static_cast<std::uint32_t>(l.grabDistanceOffset + field.index * 0x3C), field.offset))
            return false;
    return image.Text(l.laserName, laserName, sizeof(laserName)) &&
        image.Text(l.columnName, columnName, sizeof(columnName)) &&
        image.Text(l.modelName, modelName, sizeof(modelName)) &&
        // ConVar constructors tie each name to its object.
        image.Word(l.laserCtor, image.At(l.laserName), true) &&
        image.Match(l.laserCtor + 4, {0xB9}) &&
        image.Word(l.laserCtor + 5, image.At(l.laserCvar), true) &&
        image.Word(l.columnCtor, image.At(l.columnName), true) &&
        image.Match(l.columnCtor + 4, {0xB9}) &&
        image.Word(l.columnCtor + 5, image.At(l.columnCvar), true) &&
        // Game readers: parent pointer at +0x1C, int value at +0x30.
        image.Match(l.laserReader, {0xA1}) &&
        image.Word(l.laserReader + 1, image.At(l.laserCvar + 0x1C), true) &&
        image.Match(l.laserReader + 5, {0x83,0x78,0x30,0}) &&
        image.Match(l.columnReader, {0x8B,0x15}) &&
        image.Word(l.columnReader + 2, image.At(l.columnCvar + 0x1C), true) &&
        image.Match(l.columnReaderCheck, {0x83,0x7A,0x30,0}) &&
        // UpdateObject resolving m_attachedEntity through the entity list.
        image.Match(l.entityListSite, {0x8B,0x15}) &&
        image.Word(l.entityListSite + 2, image.At(l.entityList), true) &&
        image.Match(l.entityListSite + 6, {0x8B,0xC8,0x81,0xE1,0xFF,0xFF,0,0,0x8D,0x0C,0x49,
            0x8D,0x4C,0xCA,0x04,0xC1,0xE8,0x10,0x39,0x41,0x04}) &&
        // UpdateObject's ray origin: virtual Weapon_ShootPosition on the player.
        image.Match(l.holdShootCall, l.holdShootBytes) &&
        // Pickup_GetPreferredCarryAngles, UpdateObject's call and its PreferredCarryAngles slot.
        image.Match(l.carryAngles, {0x55,0x8B,0xEC,0x8B,0x45,0x08,0x83,0xEC,0x18,0x56,0x6A,0x00}) &&
        image.Match(l.carryAnglesBody, {0x8B,0x45,0x10,0x8B,0x16,0x8B,0x52,0x14,0x50}) &&
        image.Match(l.carryAnglesCall, l.carryAnglesCallBytes) &&
        // The only two readers of m_bAllowObjectOverhead: pitch clamp and player box clamp.
        image.Match(l.overheadReader1, {0x80,0xBF,0x88,0x20,0,0,0}) &&
        image.Match(l.overheadReader2, {0x80,0xBF,0x88,0x20,0,0,0}) &&
        // CBaseEntity: m_iClassname, m_ModelName, collision radius, abs origin and angles.
        image.Match(l.classnameCompare, {0x81,0x7E,0x60}) &&
        image.Word(l.modelNameField, image.At(l.modelName), true) &&
        image.Word(l.modelNameField + 10, 0xAC, true) &&
        image.Match(l.radiusSite, {0x8B,0x45,0xDC,0xD9,0x80,0x0C,0x01,0,0}) &&
        image.Match(l.absOriginSite, {0x8D,0x86,0xCC,1,0,0}) &&
        image.Match(l.absAnglesSite, {0x8D,0x86,0xD8,1,0,0});
}

inline bool ClientHeldColumnWitness(const Image &image, const ClientLayout &l)
{
    constexpr char name[] = "player_hold_object_in_column";
    return image.Text(l.columnName, name, sizeof(name)) &&
        image.Word(l.columnCtor, image.At(l.columnName), true) &&
        image.Match(l.columnCtor + 4, {0xB9}) &&
        image.Word(l.columnCtor + 5, image.At(l.columnCvar), true) &&
        image.Match(l.columnReader, {0x8B,0x15}) &&
        image.Word(l.columnReader + 2, image.At(l.columnCvar + 0x1C), true) &&
        image.Match(l.columnReader + 6, {0x83,0x7A,0x30,0});
}

} // namespace Detail

inline bool HashFileSha256(const wchar_t *path, char (&hex)[65])
{
    hex[0] = '\0';
    if (!path || !*path) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<std::uint8_t, 32> digest{};
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
        nullptr, 0));
    if (ok) ok = BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0));
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (ok) {
        DWORD count = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
            ok = false;
            break;
        }
        if (!count) break;
        ok = BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), count, 0));
    }
    if (ok) ok = BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(),
        static_cast<ULONG>(digest.size()), 0));
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) return false;
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < digest.size(); ++i) {
        hex[2*i] = digits[digest[i] >> 4];
        hex[2*i + 1] = digits[digest[i] & 15];
    }
    hex[64] = '\0';
    return true;
}

inline Result ResolveImage(const std::uint8_t *source, std::size_t imageSize,
                           std::string_view sha256, Symbol symbol)
{
    if (symbol == Symbol::ViewModelFov) {
        const auto *layout = Detail::ClientFor(sha256);
        Detail::Image image;
        if (!layout || !image.Open(source, imageSize, layout->build)) return {};
        return Detail::FovWitness(image, *layout) ? Result{image.At(layout->fovFunction), 0} : Result{};
    }
    if (symbol != Symbol::EyePosition && symbol != Symbol::WeaponShootPosition) return {};
    const auto *layout = Detail::ServerFor(sha256);
    Detail::Image image;
    if (!layout || !image.Open(source, imageSize, layout->build)) return {};
    if (symbol == Symbol::EyePosition)
        return Detail::EyeWitness(image, *layout) ? Result{image.At(layout->eyePosition), 0} : Result{};
    return Detail::ShootWitness(image, *layout) ?
        Result{image.At(layout->shootWrapper), image.At(layout->fireShootCall + 0x10)} : Result{};
}

inline PlayerFields ResolvePlayerFieldsImage(const std::uint8_t *source, std::size_t imageSize,
                                             std::string_view sha256)
{
    const auto *layout = Detail::ClientFor(sha256);
    Detail::Image image;
    if (!layout || !image.Open(source, imageSize, layout->build) ||
        !Detail::PlayerFieldWitness(image, *layout))
        return {};
    return {0x104, 0xF8};
}

inline HeldObjectLayout ResolveHeldObjectLayoutImage(
    const std::uint8_t *server, std::size_t serverSize, std::string_view serverSha256,
    const std::uint8_t *client, std::size_t clientSize, std::string_view clientSha256)
{
    const auto *serverLayout = Detail::ServerFor(serverSha256);
    const auto *clientLayout = Detail::ClientFor(clientSha256);
    // Both halves must come from the same game: the index pairs the tables.
    if (!serverLayout || !clientLayout) return {};
    std::size_t serverIndex = 0, clientIndex = 0;
    while (Detail::serverLayouts[serverIndex] != serverLayout) ++serverIndex;
    while (Detail::clientLayouts[clientIndex] != clientLayout) ++clientIndex;
    if (serverIndex != clientIndex) return {};
    Detail::Image serverImage, clientImage;
    if (!serverImage.Open(server, serverSize, serverLayout->build) ||
        !clientImage.Open(client, clientSize, clientLayout->build) ||
        !Detail::HeldObjectWitness(serverImage, *serverLayout) ||
        !Detail::ClientHeldColumnWitness(clientImage, *clientLayout))
        return {};
    return {serverImage.At(serverLayout->entityList), serverImage.At(serverLayout->laserCvar),
        serverImage.At(serverLayout->columnCvar), clientImage.At(clientLayout->columnCvar),
        serverImage.At(serverLayout->holdShootCall + 0x13), serverImage.At(serverLayout->carryAngles)};
}

namespace Detail {

// The loaded module's image and the SHA-256 of the file it was loaded from.
inline bool LoadedImage(const wchar_t *moduleName, const std::uint8_t *&bytes, std::size_t &size,
                        char (&digest)[65])
{
    HMODULE module = GetModuleHandleW(moduleName);
    MODULEINFO info{};
    if (!module || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) ||
        info.lpBaseOfDll != module) return false;
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const DWORD count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!count) return false;
        if (count < path.size() - 1) break;
        if (path.size() >= 32768) return false;
        path.resize(path.size() * 2);
    }
    if (!HashFileSha256(path.data(), digest)) return false;
    bytes = static_cast<const std::uint8_t *>(info.lpBaseOfDll);
    size = info.SizeOfImage;
    return true;
}

} // namespace Detail

inline Result ResolveLoaded(Symbol symbol)
{
    if (symbol != Symbol::EyePosition && symbol != Symbol::WeaponShootPosition &&
        symbol != Symbol::ViewModelFov) return {};
    const std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
    char digest[65]{};
    if (!Detail::LoadedImage(symbol == Symbol::ViewModelFov ? L"client.dll" : L"server.dll",
            bytes, size, digest)) return {};
    return ResolveImage(bytes, size, digest, symbol);
}

inline PlayerFields ResolveLoadedPlayerFields()
{
    const std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
    char digest[65]{};
    if (!Detail::LoadedImage(L"client.dll", bytes, size, digest)) return {};
    return ResolvePlayerFieldsImage(bytes, size, digest);
}

inline HeldObjectLayout ResolveLoadedHeldObjectLayout()
{
    const std::uint8_t *server = nullptr, *client = nullptr;
    std::size_t serverSize = 0, clientSize = 0;
    char serverDigest[65]{}, clientDigest[65]{};
    if (!Detail::LoadedImage(L"server.dll", server, serverSize, serverDigest) ||
        !Detail::LoadedImage(L"client.dll", client, clientSize, clientDigest)) return {};
    return ResolveHeldObjectLayoutImage(server, serverSize, serverDigest,
                                        client, clientSize, clientDigest);
}

} // namespace VerifiedPortalSymbols
