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

// C_BasePlayer field offsets; zero when the loaded client.dll is not the verified build.
struct PlayerFields {
    std::uint32_t viewOffsetZ = 0; // float m_vecViewOffset.z
    std::uint32_t flags = 0;       // int m_fFlags
};

namespace Detail {

struct Build {
    const wchar_t *module;
    std::string_view sha256;
    std::uint32_t timestamp;
    std::uint32_t size;
};

inline constexpr Build server{L"server.dll",
    "deb9fc303fdf4b0b0edcf58c4e1b17fbf20276f047728180f7ba7c8bf510f6b6",
    0x6AA0746Au, 0x978000u};
inline constexpr Build client{L"client.dll",
    "1b22a5008c10c91215f5c66061ff24dd84e3afb626d60b9d74c600974536f66d",
    0x6AA07473u, 0xFF3000u};

inline const Build &For(Symbol symbol) { return symbol == Symbol::ViewModelFov ? client : server; }

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

    bool Match(std::uint32_t rva, std::initializer_list<std::uint8_t> expected,
               bool executable = true) const
    {
        return Span(rva, expected.size(), executable) &&
            std::memcmp(bytes + rva, expected.begin(), expected.size()) == 0;
    }

    bool Word(std::uint32_t rva, std::uintptr_t expected, bool executable = false) const
    {
        if (expected > UINT32_MAX || !Span(rva, 4, executable)) return false;
        std::uint32_t actual = 0;
        std::memcpy(&actual, bytes + rva, 4);
        return actual == expected;
    }
};

inline bool EyeWitness(const Image &image)
{
    // CPortal_Player overrides the generic EyePosition implementation.
    return image.Match(0x369A70, {0x55,0x8B,0xEC,0x83,0xEC,0x18,0x56,0x8B,0xF1,
        0x8B,0x86,0x14,0x15,0,0,0x8B,0x16,0xF3,0x0F,0x7E,0x86,0x0C,0x15,0,0}) &&
        image.Word(0x670B2C + 128*4, image.At(0x369A70)) &&
        image.Word(0x670B2C + 129*4, image.At(0x104590)) &&
        image.Word(0x670B2C + 131*4, image.At(0x368270));
}

inline bool ShootWitness(const Image &image)
{
    // Both EarPosition and Weapon_ShootPosition slots use this wrapper.
    // Only the verified FirePortal caller is eligible for shoot override.
    return image.Match(0x368270, {0x55,0x8B,0xEC,0x8B,0x01,0x8B,0x90,0,0x02,0,0,
        0x56,0x8B,0x75,0x08,0x56,0xFF,0xD2,0x8B,0xC6,0x5E,0x5D,0xC2,0x04,0}) &&
        image.Word(0x670B2C + 131*4, image.At(0x368270)) &&
        image.Word(0x670B2C + 289*4, image.At(0x368270)) &&
        image.Word(0x6166AC + 131*4, image.At(0x368270)) &&
        image.Word(0x6166AC + 289*4, image.At(0x368270)) &&
        image.Match(0x40141E, {0x8B,0xF9,0x89,0x7D,0xEC,0xE8,0xD8,0x67,0xCD,0xFF,0x8B,0xF0}) &&
        image.Match(0x40160C, {0x8B,0x16,0x8B,0x92,0x84,0x04,0,0,0x8D,0x45,0xF0,
            0x50,0x8B,0xCE,0xFF,0xD2,0xF3,0x0F,0x10,0x08});
}

inline bool FovWitness(const Image &image)
{
    constexpr char name[] = "cl_viewmodelfov";
    return image.Match(0x28AD20, {0xA1}) &&
        image.Word(0x28AD21, image.At(0xA08F84), true) &&
        image.Match(0x28AD25, {0xD9,0x40,0x2C,0xC3}) &&
        image.Word(0x7C03D4 + 35*4, image.At(0x28AD20)) &&
        image.Word(0x7C0944 + 35*4, image.At(0x28AD20)) &&
        image.Span(0x78C010, sizeof(name)) &&
        std::memcmp(image.bytes + 0x78C010, name, sizeof(name)) == 0 &&
        image.Match(0x6FE500, {0x6A,0x02,0x68}) &&
        image.Match(0x6FE507, {0x68}) &&
        image.Word(0x6FE508, image.At(0x78C010), true) &&
        image.Match(0x6FE50C, {0xB9}) &&
        image.Word(0x6FE50D, image.At(0xA08F68), true) &&
        image.Match(0x6FE511, {0xE8});
}

inline bool PlayerFieldWitness(const Image &image)
{
    // RecvPropFloat("m_vecViewOffset[2]", 0x104, ...) in DT_LocalPlayerExclusive and
    // RecvPropInt("m_fFlags", 0xF8, ...): push offset immediately before push name.
    constexpr char viewOffsetName[] = "m_vecViewOffset[2]";
    constexpr char flagsName[] = "m_fFlags";
    return image.Span(0x74C1A8, sizeof(viewOffsetName)) &&
        std::memcmp(image.bytes + 0x74C1A8, viewOffsetName, sizeof(viewOffsetName)) == 0 &&
        image.Match(0x8F8E9, {0x68,0x04,0x01,0,0,0x68}) &&
        image.Word(0x8F8EF, image.At(0x74C1A8), true) &&
        image.Span(0x742218, sizeof(flagsName)) &&
        std::memcmp(image.bytes + 0x742218, flagsName, sizeof(flagsName)) == 0 &&
        image.Match(0x92CA4, {0x68,0xF8,0,0,0,0x68}) &&
        image.Word(0x92CAA, image.At(0x742218), true);
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
    if (symbol != Symbol::EyePosition && symbol != Symbol::WeaponShootPosition &&
        symbol != Symbol::ViewModelFov) return {};
    const auto &build = Detail::For(symbol);
    if (sha256 != build.sha256) return {};
    Detail::Image image;
    if (!image.Open(source, imageSize, build)) return {};
    switch (symbol) {
    case Symbol::EyePosition:
        return Detail::EyeWitness(image) ? Result{image.At(0x369A70), 0} : Result{};
    case Symbol::WeaponShootPosition:
        return Detail::ShootWitness(image) ?
            Result{image.At(0x368270), image.At(0x40161C)} : Result{};
    case Symbol::ViewModelFov:
        return Detail::FovWitness(image) ? Result{image.At(0x28AD20), 0} : Result{};
    }
    return {};
}

inline PlayerFields ResolvePlayerFieldsImage(const std::uint8_t *source, std::size_t imageSize,
                                             std::string_view sha256)
{
    if (sha256 != Detail::client.sha256) return {};
    Detail::Image image;
    if (!image.Open(source, imageSize, Detail::client) || !Detail::PlayerFieldWitness(image))
        return {};
    return {0x104, 0xF8};
}

namespace Detail {

// The loaded module's image and the SHA-256 of the file it was loaded from.
inline bool LoadedImage(const Build &build, const std::uint8_t *&bytes, std::size_t &size,
                        char (&digest)[65])
{
    HMODULE module = GetModuleHandleW(build.module);
    MODULEINFO info{};
    if (!module || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) ||
        info.lpBaseOfDll != module || info.SizeOfImage != build.size) return false;
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
    if (!Detail::LoadedImage(Detail::For(symbol), bytes, size, digest)) return {};
    return ResolveImage(bytes, size, digest, symbol);
}

inline PlayerFields ResolveLoadedPlayerFields()
{
    const std::uint8_t *bytes = nullptr;
    std::size_t size = 0;
    char digest[65]{};
    if (!Detail::LoadedImage(Detail::client, bytes, size, digest)) return {};
    return ResolvePlayerFieldsImage(bytes, size, digest);
}

} // namespace VerifiedPortalSymbols
