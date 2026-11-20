#include "../L4D2VR/verified_portal_symbols.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

using namespace VerifiedPortalSymbols;

namespace {

int failures = 0;

void Expect(bool ok, const char *name)
{
    if (!ok) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}

struct Image {
    std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
    std::uint32_t dataStart = 0;

    Image(std::size_t imageSize, std::uint32_t timestamp, std::uint32_t textEnd,
          std::uint32_t rdataEnd) : size(imageSize), dataStart(rdataEnd)
    {
        bytes = static_cast<std::uint8_t *>(VirtualAlloc(nullptr, size,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!bytes) return;
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(bytes);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x80;
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(bytes + 0x80);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        nt->FileHeader.NumberOfSections = 3;
        nt->FileHeader.TimeDateStamp = timestamp;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt->OptionalHeader.ImageBase = 0x10000000;
        nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(size);
        nt->OptionalHeader.SizeOfHeaders = 0x1000;
        auto *section = IMAGE_FIRST_SECTION(nt);
        section[0].VirtualAddress = 0x1000;
        section[0].Misc.VirtualSize = textEnd - 0x1000;
        section[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
        section[1].VirtualAddress = textEnd;
        section[1].Misc.VirtualSize = rdataEnd - textEnd;
        section[1].Characteristics = IMAGE_SCN_MEM_READ;
        section[2].VirtualAddress = rdataEnd;
        section[2].Misc.VirtualSize = static_cast<DWORD>(size - rdataEnd);
        section[2].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    }

    ~Image() { if (bytes) VirtualFree(bytes, 0, MEM_RELEASE); }
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    std::uintptr_t At(std::size_t rva) const { return reinterpret_cast<std::uintptr_t>(bytes) + rva; }
    void Put(std::size_t rva, std::initializer_list<std::uint8_t> value)
    { std::memcpy(bytes + rva, value.begin(), value.size()); }
    template <std::size_t N>
    void Put(std::size_t rva, const std::array<std::uint8_t, N> &value)
    { std::memcpy(bytes + rva, value.data(), N); }
    void U32(std::size_t rva, std::uintptr_t value)
    { const std::uint32_t word = static_cast<std::uint32_t>(value); std::memcpy(bytes + rva, &word, 4); }
    void Text(std::size_t rva, const char *value) { std::memcpy(bytes + rva, value, std::strlen(value) + 1); }
    void Seal(std::uint32_t textEnd)
    {
        DWORD old = 0;
        VirtualProtect(bytes + 0x1000, textEnd - 0x1000, PAGE_EXECUTE_READ, &old);
        VirtualProtect(bytes + textEnd, dataStart - textEnd, PAGE_READONLY, &old);
    }
    void Unseal(std::uint32_t textEnd)
    {
        DWORD old = 0;
        VirtualProtect(bytes + 0x1000, textEnd - 0x1000, PAGE_READWRITE, &old);
        VirtualProtect(bytes + textEnd, dataStart - textEnd, PAGE_READWRITE, &old);
    }
};

using Detail::ServerLayout;
using Detail::ClientLayout;

void PopulateServer(Image &image, const ServerLayout &l)
{
    // Independently observed body starts and Portal-player vtable slots.
    image.Put(l.eyePosition, {0x55,0x8B,0xEC,0x83,0xEC,0x18,0x56,0x8B,0xF1,
        0x8B,0x86,0x14,0x15,0,0,0x8B,0x16,0xF3,0x0F,0x7E,0x86,0x0C,0x15,0,0});
    image.Put(l.shootWrapper, {0x55,0x8B,0xEC,0x8B,0x01,0x8B,0x90,0,0x02,0,0,
        0x56,0x8B,0x75,0x08,0x56,0xFF,0xD2,0x8B,0xC6,0x5E,0x5D,0xC2,0x04,0});
    image.U32(l.portalPlayerVtable + 128*4, image.At(l.eyePosition));
    image.U32(l.portalPlayerVtable + 129*4, image.At(l.eyeAngles));
    image.U32(l.portalPlayerVtable + 131*4, image.At(l.shootWrapper));
    image.U32(l.portalPlayerVtable + 289*4, image.At(l.shootWrapper));
    image.U32(l.basePlayerVtable + 131*4, image.At(l.shootWrapper));
    image.U32(l.basePlayerVtable + 289*4, image.At(l.shootWrapper));
    image.Put(l.fireOwnerCall, l.fireOwnerBytes);
    image.Put(l.fireShootCall, {0x8B,0x16,0x8B,0x92,0x84,0x04,0,0,0x8D,0x45,0xF0,
        0x50,0x8B,0xCE,0xFF,0xD2,0xF3,0x0F,0x10,0x08});
    // Held-object cvars, their readers, the entity list and the entity field references.
    image.Text(l.laserName, "sv_laser_cube_autoaim");
    image.Text(l.columnName, "player_hold_object_in_column");
    image.Text(l.modelName, "m_ModelName");
    image.U32(l.laserCtor, image.At(l.laserName));
    image.Put(l.laserCtor + 4, {0xB9});
    image.U32(l.laserCtor + 5, image.At(l.laserCvar));
    image.U32(l.columnCtor, image.At(l.columnName));
    image.Put(l.columnCtor + 4, {0xB9});
    image.U32(l.columnCtor + 5, image.At(l.columnCvar));
    image.Put(l.laserReader, {0xA1});
    image.U32(l.laserReader + 1, image.At(l.laserCvar + 0x1C));
    image.Put(l.laserReader + 5, {0x83,0x78,0x30,0});
    image.Put(l.columnReader, {0x8B,0x15});
    image.U32(l.columnReader + 2, image.At(l.columnCvar + 0x1C));
    image.Put(l.columnReaderCheck, {0x83,0x7A,0x30,0});
    image.Put(l.entityListSite, {0x8B,0x15});
    image.U32(l.entityListSite + 2, image.At(l.entityList));
    image.Put(l.entityListSite + 6, {0x8B,0xC8,0x81,0xE1,0xFF,0xFF,0,0,0x8D,0x0C,0x49,
        0x8D,0x4C,0xCA,0x04,0xC1,0xE8,0x10,0x39,0x41,0x04});
    image.Put(l.classnameCompare, {0x81,0x7E,0x60});
    image.U32(l.modelNameField, image.At(l.modelName));
    image.U32(l.modelNameField + 10, 0xAC);
    image.Put(l.absAnglesSite, {0x8D,0x86,0xD8,1,0,0});
    image.Put(l.carryAngles, {0x55,0x8B,0xEC,0x8B,0x45,0x08,0x83,0xEC,0x18,0x56,0x6A,0x00});
    image.Put(l.carryAnglesBody, {0x8B,0x45,0x10,0x8B,0x16,0x8B,0x52,0x14,0x50});
    image.Put(l.carryAnglesCall, l.carryAnglesCallBytes);
    image.Put(l.absOriginSite, {0x8D,0x86,0xCC,1,0,0});
    image.Put(l.radiusSite, {0x8B,0x45,0xDC,0xD9,0x80,0x0C,0x01,0,0});
    image.Put(l.overheadReader1, {0x80,0xBF,0x88,0x20,0,0,0});
    image.Put(l.overheadReader2, {0x80,0xBF,0x88,0x20,0,0,0});
    image.Put(l.holdShootCall, l.holdShootBytes);
    const struct { int index; std::uint32_t offset; } grabFields[] = {
        {-14, 0x4}, {-11, 0x203C}, {-7, 0x204C}, {-5, 0x2049}, {-4, 0x2054}, {-3, 0x2044},
        {-2, 0x2058}, {-1, 0x2064}, {0, 0x2068}, {1, 0x206C}, {3, 0x2088}};
    for (const auto &field : grabFields)
        image.U32(l.grabDistanceOffset + field.index * 0x3C, field.offset);
}

void PopulateClient(Image &image, const ClientLayout &l)
{
    image.Text(l.columnName, "player_hold_object_in_column");
    image.U32(l.columnCtor, image.At(l.columnName));
    image.Put(l.columnCtor + 4, {0xB9});
    image.U32(l.columnCtor + 5, image.At(l.columnCvar));
    image.Put(l.columnReader, {0x8B,0x15});
    image.U32(l.columnReader + 2, image.At(l.columnCvar + 0x1C));
    image.Put(l.columnReader + 6, {0x83,0x7A,0x30,0});
    image.Put(l.fovFunction, {0xA1,0,0,0,0,0xD9,0x40,0x2C,0xC3});
    image.U32(l.fovFunction + 1, image.At(l.fovCvarParent));
    image.U32(l.normalVtable + 35*4, image.At(l.fovFunction));
    image.U32(l.fullscreenVtable + 35*4, image.At(l.fovFunction));
    image.Text(l.fovName, "cl_viewmodelfov");
    image.Put(l.fovCtor, {0x6A,0x02,0x68,0x74,0x19,0x7A,0x10,0x68,0,0,0,0,
        0xB9,0,0,0,0,0xE8});
    image.U32(l.fovCtor + 8, image.At(l.fovName));
    image.U32(l.fovCtor + 13, image.At(l.fovCvar));
    // RecvProp initializers for m_vecViewOffset[2] (0x104) and m_fFlags (0xF8).
    image.Text(l.viewOffsetName, "m_vecViewOffset[2]");
    image.Put(l.viewOffsetPush, {0x68,0x04,0x01,0,0,0x68});
    image.U32(l.viewOffsetPush + 6, image.At(l.viewOffsetName));
    image.Text(l.flagsName, "m_fFlags");
    image.Put(l.flagsPush, {0x68,0xF8,0,0,0,0x68});
    image.U32(l.flagsPush + 6, image.At(l.flagsName));
}

void TestHash()
{
    wchar_t temp[MAX_PATH]{};
    wchar_t file[MAX_PATH]{};
    char digest[65]{};
    Expect(GetTempPathW(MAX_PATH, temp) && GetTempFileNameW(temp, L"vps", 0, file),
        "temporary hash fixture path");
    HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY, nullptr);
    Expect(handle != INVALID_HANDLE_VALUE, "temporary hash fixture open");
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        Expect(WriteFile(handle, "abc", 3, &written, nullptr) && written == 3,
            "temporary hash fixture write");
        CloseHandle(handle);
        Expect(HashFileSha256(file, digest) &&
            std::string(digest) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 of file bytes");
    }
    DeleteFileW(file);
    Expect(!HashFileSha256(file, digest), "missing file does not hash");
}

void RunSuite(const ServerLayout &sl, const ClientLayout &cl, const char *game)
{
    const std::string prefix = std::string(game) + ": ";
    const auto check = [&](bool ok, const char *name) { Expect(ok, (prefix + name).c_str()); };
    const std::string_view serverHash = sl.build.sha256;
    const std::string_view clientHash = cl.build.sha256;
    // The text section ends past every code site; data starts past every table and string.
    constexpr std::uint32_t serverText = 0x580000, clientText = 0x750000;
    Image server(sl.build.size, sl.build.timestamp, serverText, 0x700000);
    Image client(cl.build.size, cl.build.timestamp, clientText, 0x900000);
    check(server.bytes && client.bytes, "synthetic PE allocation");
    if (!server.bytes || !client.bytes) return;
    PopulateServer(server, sl);
    PopulateClient(client, cl);
    server.Seal(serverText);
    client.Seal(clientText);

    const auto eye = ResolveImage(server.bytes, server.size, serverHash, Symbol::EyePosition);
    check(eye.address == server.At(sl.eyePosition) && !eye.shootCaller,
        "Portal-player EyePosition resolves at loaded base plus RVA");
    const auto shoot = ResolveImage(server.bytes, server.size, serverHash, Symbol::WeaponShootPosition);
    check(shoot.address == server.At(sl.shootWrapper) && shoot.shootCaller == server.At(sl.fireShootCall + 0x10),
        "shoot target and verified FirePortal return address resolve together");
    const auto fov = ResolveImage(client.bytes, client.size, clientHash, Symbol::ViewModelFov);
    check(fov.address == client.At(cl.fovFunction) && !fov.shootCaller,
        "Portal ClientMode FOV resolves through both vtables and cvar reference");
    auto *serverNt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(server.bytes + 0x80);
    serverNt->OptionalHeader.ImageBase = static_cast<DWORD>(server.At(0));
    check(ResolveImage(server.bytes, server.size, serverHash, Symbol::EyePosition).address ==
        server.At(sl.eyePosition), "ASLR-relocated header ImageBase resolves");
    serverNt->OptionalHeader.ImageBase = 0x20000000;
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::EyePosition).address,
        "unrelated header ImageBase rejects fallback");
    serverNt->OptionalHeader.ImageBase = 0x10000000;
    check(!ResolveImage(server.bytes, server.size, clientHash, Symbol::EyePosition).address,
        "wrong DLL hash rejects fallback");
    check(!ResolveImage(client.bytes, client.size, serverHash, Symbol::ViewModelFov).address,
        "client fallback rejects server hash");
    for (const auto *other : Detail::serverLayouts)
        if (other != &sl)
            check(!ResolveImage(server.bytes, server.size, other->build.sha256, Symbol::EyePosition).address,
                "the other game's server hash rejects this image");
    for (const auto *other : Detail::clientLayouts)
        if (other != &cl)
            check(!ResolveImage(client.bytes, client.size, other->build.sha256, Symbol::ViewModelFov).address,
                "the other game's client hash rejects this image");

    server.Unseal(serverText);
    server.bytes[sl.eyePosition] ^= 1;
    server.Seal(serverText);
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::EyePosition).address,
        "changed EyePosition body rejects fallback");
    server.Unseal(serverText);
    server.bytes[sl.eyePosition] ^= 1;
    server.U32(sl.portalPlayerVtable + 128*4, server.At(0xF4CC0));
    server.Seal(serverText);
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::EyePosition).address,
        "generic EyePosition vtable substitution rejects fallback");
    server.Unseal(serverText);
    server.U32(sl.portalPlayerVtable + 128*4, server.At(sl.eyePosition));
    server.bytes[sl.fireShootCall + 4] ^= 1;
    server.Seal(serverText);
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::WeaponShootPosition).address,
        "changed FirePortal virtual call rejects shoot fallback");
    server.Unseal(serverText);
    server.bytes[sl.fireShootCall + 4] ^= 1;
    server.bytes[sl.fireOwnerCall + 6] ^= 1;
    server.Seal(serverText);
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::WeaponShootPosition).address,
        "changed FirePortal owner call rejects shoot fallback");
    server.Unseal(serverText);
    server.bytes[sl.fireOwnerCall + 6] ^= 1;
    server.U32(sl.portalPlayerVtable + 289*4, server.At(0x39480));
    server.Seal(serverText);
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::WeaponShootPosition).address,
        "changed weapon slot rejects shoot fallback");
    server.Unseal(serverText);
    server.U32(sl.portalPlayerVtable + 289*4, server.At(sl.shootWrapper));
    server.Seal(serverText);
    check(ResolveImage(server.bytes, server.size, serverHash, Symbol::WeaponShootPosition).address ==
        server.At(sl.shootWrapper), "shoot fallback resolves again once restored");

    client.Unseal(clientText);
    client.U32(cl.fullscreenVtable + 35*4, client.At(0xE7D60));
    client.Seal(clientText);
    check(!ResolveImage(client.bytes, client.size, clientHash, Symbol::ViewModelFov).address,
        "shared ClientMode FOV substitution rejects fallback");
    client.Unseal(clientText);
    client.U32(cl.fullscreenVtable + 35*4, client.At(cl.fovFunction));
    client.bytes[cl.fovName] = 'x';
    client.Seal(clientText);
    check(!ResolveImage(client.bytes, client.size, clientHash, Symbol::ViewModelFov).address,
        "changed cvar identity rejects FOV fallback");
    client.Unseal(clientText);
    client.bytes[cl.fovName] = 'c';
    client.U32(cl.fovCtor + 8, client.At(cl.fovName + 1));
    client.Seal(clientText);
    check(!ResolveImage(client.bytes, client.size, clientHash, Symbol::ViewModelFov).address,
        "changed cvar constructor reference rejects FOV fallback");
    client.Unseal(clientText);
    client.U32(cl.fovCtor + 8, client.At(cl.fovName));
    client.Seal(clientText);
    check(ResolveImage(client.bytes, client.size, clientHash, Symbol::ViewModelFov).address ==
        client.At(cl.fovFunction), "FOV fallback resolves again once restored");

    const auto fields = ResolvePlayerFieldsImage(client.bytes, client.size, clientHash);
    check(fields.viewOffsetZ == 0x104 && fields.flags == 0xF8,
        "player view-offset and flags fields resolve from RecvProp initializers");
    check(!ResolvePlayerFieldsImage(client.bytes, client.size, serverHash).viewOffsetZ,
        "player fields reject wrong DLL hash");
    client.Unseal(clientText);
    client.bytes[cl.viewOffsetPush + 1] = 0x08;
    client.Seal(clientText);
    check(!ResolvePlayerFieldsImage(client.bytes, client.size, clientHash).viewOffsetZ,
        "changed view-offset RecvProp offset rejects player fields");
    client.Unseal(clientText);
    client.bytes[cl.viewOffsetPush + 1] = 0x04;
    client.bytes[cl.flagsName] = 'x';
    client.Seal(clientText);
    check(!ResolvePlayerFieldsImage(client.bytes, client.size, clientHash).flags,
        "changed flags name rejects player fields");
    client.Unseal(clientText);
    client.bytes[cl.flagsName] = 'm';
    client.Seal(clientText);

    const auto resolveHeld = [&](std::string_view sHash, std::string_view cHash) {
        return ResolveHeldObjectLayoutImage(server.bytes, server.size, sHash,
                                            client.bytes, client.size, cHash);
    };
    const auto held = resolveHeld(serverHash, clientHash);
    check(held.entityList == server.At(sl.entityList) && held.laserAutoaimCvar == server.At(sl.laserCvar) &&
        held.holdColumnCvar == server.At(sl.columnCvar) && held.clientHoldColumnCvar == client.At(cl.columnCvar) &&
        held.holdShootCaller == server.At(sl.holdShootCall + 0x13) &&
        held.preferredCarryAngles == server.At(sl.carryAngles),
        "held object layout resolves from both DLLs");
    check(!resolveHeld(clientHash, clientHash).entityList, "held layout rejects wrong server hash");
    check(!resolveHeld(serverHash, serverHash).entityList, "held layout rejects wrong client hash");
    for (const auto *other : Detail::clientLayouts)
        if (other != &cl)
            check(!resolveHeld(serverHash, other->build.sha256).entityList,
                "held layout rejects a client from the other game");
    const auto heldRejects = [&](Image &image, std::uint32_t textEnd, std::uint32_t rva, const char *name) {
        image.Unseal(textEnd);
        image.bytes[rva] ^= 1;
        image.Seal(textEnd);
        check(!resolveHeld(serverHash, clientHash).entityList, name);
        image.Unseal(textEnd);
        image.bytes[rva] ^= 1;
        image.Seal(textEnd);
    };
    heldRejects(server, serverText, sl.grabDistanceOffset, "changed grab controller datamap rejects held layout");
    heldRejects(server, serverText, sl.laserName, "changed laser cvar name rejects held layout");
    heldRejects(server, serverText, sl.columnCtor + 5, "changed column cvar object rejects held layout");
    heldRejects(server, serverText, sl.columnReaderCheck, "changed column reader rejects held layout");
    heldRejects(server, serverText, sl.entityListSite + 6, "changed handle walk rejects held layout");
    heldRejects(server, serverText, sl.modelNameField + 10, "changed model name offset rejects held layout");
    heldRejects(server, serverText, sl.holdShootCall + 4, "changed ray origin call rejects held layout");
    heldRejects(server, serverText, sl.carryAnglesBody + 7, "changed carry angles slot rejects held layout");
    heldRejects(server, serverText, sl.carryAnglesCall + 1, "changed carry angles call rejects held layout");
    heldRejects(server, serverText, sl.overheadReader2 + 2, "changed overhead reader rejects held layout");
    heldRejects(server, serverText, sl.radiusSite + 5, "changed radius offset rejects held layout");
    heldRejects(server, serverText, sl.absOriginSite + 2, "changed abs origin offset rejects held layout");
    heldRejects(client, clientText, cl.columnName, "changed client cvar name rejects held layout");
    heldRejects(client, clientText, cl.columnReader + 6, "changed client reader rejects held layout");
    check(resolveHeld(serverHash, clientHash).entityList == server.At(sl.entityList),
        "held layout resolves again once restored");

    DWORD old = 0;
    VirtualProtect(server.bytes + 0x1000, serverText - 0x1000, PAGE_READONLY, &old);
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::EyePosition).address,
        "nonexecutable target rejects fallback");
    VirtualProtect(server.bytes + 0x1000, serverText - 0x1000, PAGE_NOACCESS, &old);
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::EyePosition).address,
        "inaccessible executable section rejects fallback without dereference");
    VirtualProtect(server.bytes + 0x1000, serverText - 0x1000, PAGE_READWRITE, &old);
    server.bytes[0x80] = 0;
    server.Seal(serverText);
    check(!ResolveImage(server.bytes, server.size, serverHash, Symbol::EyePosition).address,
        "malformed PE signature rejects fallback");
    check(!ResolveImage(client.bytes, 0x1000, clientHash, Symbol::ViewModelFov).address,
        "truncated image size rejects fallback");
}

} // namespace

int main()
{
    TestHash();
    RunSuite(Detail::portal2Server, Detail::portal2Client, "Portal 2");
    RunSuite(Detail::aakServer, Detail::aakClient, "Alive & Kicking");
    RunSuite(Detail::melServer, Detail::melClient, "Portal Stories: Mel");
    if (!failures) std::cout << "verified symbol tests passed\n";
    return failures ? 1 : 0;
}
