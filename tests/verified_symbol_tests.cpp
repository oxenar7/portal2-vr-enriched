#include "../L4D2VR/verified_portal_symbols.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

using namespace VerifiedPortalSymbols;

namespace {

constexpr char kServerHash[] = "deb9fc303fdf4b0b0edcf58c4e1b17fbf20276f047728180f7ba7c8bf510f6b6";
constexpr char kClientHash[] = "1b22a5008c10c91215f5c66061ff24dd84e3afb626d60b9d74c600974536f66d";
constexpr std::size_t kServerSize = 0x978000;
constexpr std::size_t kClientSize = 0xFF3000;
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
    void U32(std::size_t rva, std::uintptr_t value)
    { const std::uint32_t word = static_cast<std::uint32_t>(value); std::memcpy(bytes + rva, &word, 4); }
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

void PopulateServer(Image &image)
{
    // Independently observed body starts and Portal-player vtable slots.
    image.Put(0x369A70, {0x55,0x8B,0xEC,0x83,0xEC,0x18,0x56,0x8B,0xF1,
        0x8B,0x86,0x14,0x15,0,0,0x8B,0x16,0xF3,0x0F,0x7E,0x86,0x0C,0x15,0,0});
    image.Put(0x368270, {0x55,0x8B,0xEC,0x8B,0x01,0x8B,0x90,0,0x02,0,0,
        0x56,0x8B,0x75,0x08,0x56,0xFF,0xD2,0x8B,0xC6,0x5E,0x5D,0xC2,0x04,0});
    image.U32(0x670B2C + 128*4, image.At(0x369A70));
    image.U32(0x670B2C + 129*4, image.At(0x104590));
    image.U32(0x670B2C + 131*4, image.At(0x368270));
    image.U32(0x670B2C + 289*4, image.At(0x368270));
    image.U32(0x6166AC + 131*4, image.At(0x368270));
    image.U32(0x6166AC + 289*4, image.At(0x368270));
    image.Put(0x40141E, {0x8B,0xF9,0x89,0x7D,0xEC,0xE8,0xD8,0x67,0xCD,0xFF,
        0x8B,0xF0});
    image.Put(0x40160C, {0x8B,0x16,0x8B,0x92,0x84,0x04,0,0,0x8D,0x45,0xF0,
        0x50,0x8B,0xCE,0xFF,0xD2,0xF3,0x0F,0x10,0x08});
}

void PopulateClient(Image &image)
{
    image.Put(0x28AD20, {0xA1,0,0,0,0,0xD9,0x40,0x2C,0xC3});
    image.U32(0x28AD21, image.At(0xA08F84));
    image.U32(0x7C03D4 + 35*4, image.At(0x28AD20));
    image.U32(0x7C0944 + 35*4, image.At(0x28AD20));
    image.Put(0x78C010, {'c','l','_','v','i','e','w','m','o','d','e','l','f','o','v',0});
    image.Put(0x6FE500, {0x6A,0x02,0x68,0x74,0x19,0x7A,0x10,0x68,0,0,0,0,
        0xB9,0,0,0,0,0xE8});
    image.U32(0x6FE508, image.At(0x78C010));
    image.U32(0x6FE50D, image.At(0xA08F68));
    // RecvProp initializers for m_vecViewOffset[2] (0x104) and m_fFlags (0xF8).
    image.Put(0x74C1A8, {'m','_','v','e','c','V','i','e','w','O','f','f','s','e','t','[','2',']',0});
    image.Put(0x8F8E9, {0x68,0x04,0x01,0,0,0x68});
    image.U32(0x8F8EF, image.At(0x74C1A8));
    image.Put(0x742218, {'m','_','f','F','l','a','g','s',0});
    image.Put(0x92CA4, {0x68,0xF8,0,0,0,0x68});
    image.U32(0x92CAA, image.At(0x742218));
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

} // namespace

int main()
{
    TestHash();
    Image server(kServerSize, 0x6AA0746A, 0x500000, 0x700000);
    Image client(kClientSize, 0x6AA07473, 0x750000, 0x900000);
    Expect(server.bytes && client.bytes, "synthetic PE allocation");
    if (!server.bytes || !client.bytes) return 1;
    PopulateServer(server);
    PopulateClient(client);
    server.Seal(0x500000);
    client.Seal(0x750000);

    const auto eye = ResolveImage(server.bytes, server.size, kServerHash, Symbol::EyePosition);
    Expect(eye.address == server.At(0x369A70) && !eye.shootCaller,
        "Portal-player EyePosition resolves at loaded base plus RVA");
    const auto shoot = ResolveImage(server.bytes, server.size, kServerHash, Symbol::WeaponShootPosition);
    Expect(shoot.address == server.At(0x368270) && shoot.shootCaller == server.At(0x40161C),
        "shoot target and verified FirePortal return address resolve together");
    const auto fov = ResolveImage(client.bytes, client.size, kClientHash, Symbol::ViewModelFov);
    Expect(fov.address == client.At(0x28AD20) && !fov.shootCaller,
        "Portal ClientMode FOV resolves through both vtables and cvar reference");
    auto *serverNt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(server.bytes + 0x80);
    serverNt->OptionalHeader.ImageBase = static_cast<DWORD>(server.At(0));
    Expect(ResolveImage(server.bytes, server.size, kServerHash, Symbol::EyePosition).address ==
        server.At(0x369A70), "ASLR-relocated header ImageBase resolves");
    serverNt->OptionalHeader.ImageBase = 0x20000000;
    Expect(!ResolveImage(server.bytes, server.size, kServerHash, Symbol::EyePosition).address,
        "unrelated header ImageBase rejects fallback");
    serverNt->OptionalHeader.ImageBase = 0x10000000;
    Expect(!ResolveImage(server.bytes, server.size, kClientHash, Symbol::EyePosition).address,
        "wrong DLL hash rejects fallback");
    Expect(!ResolveImage(client.bytes, client.size, kServerHash, Symbol::ViewModelFov).address,
        "client fallback rejects server hash");

    server.Unseal(0x500000);
    server.bytes[0x369A70] ^= 1;
    server.Seal(0x500000);
    Expect(!ResolveImage(server.bytes, server.size, kServerHash, Symbol::EyePosition).address,
        "changed EyePosition body rejects fallback");
    server.Unseal(0x500000);
    server.bytes[0x369A70] ^= 1;
    server.U32(0x670B2C + 128*4, server.At(0xF4CC0));
    server.Seal(0x500000);
    Expect(!ResolveImage(server.bytes, server.size, kServerHash, Symbol::EyePosition).address,
        "generic EyePosition vtable substitution rejects fallback");
    server.Unseal(0x500000);
    server.U32(0x670B2C + 128*4, server.At(0x369A70));
    server.bytes[0x40160C + 4] ^= 1;
    server.Seal(0x500000);
    Expect(!ResolveImage(server.bytes, server.size, kServerHash, Symbol::WeaponShootPosition).address,
        "changed FirePortal virtual call rejects shoot fallback");
    server.Unseal(0x500000);
    server.bytes[0x40160C + 4] ^= 1;
    server.U32(0x670B2C + 289*4, server.At(0x39480));
    server.Seal(0x500000);
    Expect(!ResolveImage(server.bytes, server.size, kServerHash, Symbol::WeaponShootPosition).address,
        "changed weapon slot rejects shoot fallback");

    client.Unseal(0x750000);
    client.U32(0x7C0944 + 35*4, client.At(0xE7D60));
    client.Seal(0x750000);
    Expect(!ResolveImage(client.bytes, client.size, kClientHash, Symbol::ViewModelFov).address,
        "shared ClientMode FOV substitution rejects fallback");
    client.Unseal(0x750000);
    client.U32(0x7C0944 + 35*4, client.At(0x28AD20));
    client.bytes[0x78C010] = 'x';
    client.Seal(0x750000);
    Expect(!ResolveImage(client.bytes, client.size, kClientHash, Symbol::ViewModelFov).address,
        "changed cvar identity rejects FOV fallback");
    client.Unseal(0x750000);
    client.bytes[0x78C010] = 'c';
    client.U32(0x6FE508, client.At(0x78C011));
    client.Seal(0x750000);
    Expect(!ResolveImage(client.bytes, client.size, kClientHash, Symbol::ViewModelFov).address,
        "changed cvar constructor reference rejects FOV fallback");

    const auto fields = ResolvePlayerFieldsImage(client.bytes, client.size, kClientHash);
    Expect(fields.viewOffsetZ == 0x104 && fields.flags == 0xF8,
        "player view-offset and flags fields resolve from RecvProp initializers");
    Expect(!ResolvePlayerFieldsImage(client.bytes, client.size, kServerHash).viewOffsetZ,
        "player fields reject wrong DLL hash");
    client.Unseal(0x750000);
    client.bytes[0x8F8EA] = 0x08;
    client.Seal(0x750000);
    Expect(!ResolvePlayerFieldsImage(client.bytes, client.size, kClientHash).viewOffsetZ,
        "changed view-offset RecvProp offset rejects player fields");
    client.Unseal(0x750000);
    client.bytes[0x8F8EA] = 0x04;
    client.bytes[0x742218] = 'x';
    client.Seal(0x750000);
    Expect(!ResolvePlayerFieldsImage(client.bytes, client.size, kClientHash).flags,
        "changed flags name rejects player fields");

    DWORD old = 0;
    VirtualProtect(server.bytes + 0x1000, 0x4FF000, PAGE_READONLY, &old);
    Expect(!ResolveImage(server.bytes, server.size, kServerHash, Symbol::EyePosition).address,
        "nonexecutable target rejects fallback");
    VirtualProtect(server.bytes + 0x1000, 0x4FF000, PAGE_NOACCESS, &old);
    Expect(!ResolveImage(server.bytes, server.size, kServerHash, Symbol::EyePosition).address,
        "inaccessible executable section rejects fallback without dereference");
    VirtualProtect(server.bytes + 0x1000, 0x4FF000, PAGE_READWRITE, &old);
    server.bytes[0x80] = 0;
    server.Seal(0x500000);
    Expect(!ResolveImage(server.bytes, server.size, kServerHash, Symbol::EyePosition).address,
        "malformed PE signature rejects fallback");
    Expect(!ResolveImage(client.bytes, 0x1000, kClientHash, Symbol::ViewModelFov).address,
        "truncated image size rejects fallback");

    if (!failures) std::cout << "verified symbol tests passed\n";
    return failures ? 1 : 0;
}
