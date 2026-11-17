#include "../L4D2VR/sigscanner.h"
#include "../L4D2VR/config.h"
#include "../L4D2VR/required_hooks.h"
#include "../L4D2VR/optional_hooks.h"
#include "../L4D2VR/startup_symbol_policy.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct ImageFixture {
    std::uint8_t *bytes = static_cast<std::uint8_t *>(
        VirtualAlloc(nullptr, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

    ImageFixture()
    {
        if (!bytes) return;
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(bytes);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x80;
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(bytes + 0x80);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.NumberOfSections = 2;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt->OptionalHeader.SizeOfImage = 0x3000;
        nt->OptionalHeader.SizeOfHeaders = 0x1000;
        auto *section = IMAGE_FIRST_SECTION(nt);
        section[0].VirtualAddress = 0x1000;
        section[0].Misc.VirtualSize = 0x1000;
        section[0].Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
        section[1].VirtualAddress = 0x2000;
        section[1].Misc.VirtualSize = 0x1000;
        section[1].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    }

    ~ImageFixture() { if (bytes) VirtualFree(bytes, 0, MEM_RELEASE); }
    void Write(std::size_t offset, std::initializer_list<std::uint8_t> pattern)
    {
        std::memcpy(bytes + offset, pattern.begin(), pattern.size());
    }
    void ProtectText(DWORD protection)
    {
        DWORD oldProtection = 0;
        VirtualProtect(bytes + 0x1000, 0x1000, protection, &oldProtection);
    }
};

static int failures = 0;

static void Expect(bool condition, const char *name)
{
    if (!condition) {
        std::cerr << name << " failed\n";
        ++failures;
    }
}

int main()
{
    const std::uint8_t repeated[] = {0xAB, 0xCD, 0x00, 0xAB, 0xCD};
    Expect(SigScanner::FindPattern(repeated, sizeof(repeated), {0xAB, 0xCD}) == -1,
           "duplicate signature must be ambiguous");
    Expect(SigScanner::FindPattern(repeated, sizeof(repeated), {0xCD, 0x00}) == 1,
           "unique signature still resolves");

    ImageFixture image;
    Expect(image.bytes != nullptr, "fixture allocation");
    if (!image.bytes) return 1;
    image.Write(0x1100, {0xAB, 0xCD, 0xEF});
    image.Write(0x2100, {0xAB, 0xCD, 0xEF});
    image.ProtectText(PAGE_EXECUTE_READ);
    Expect(SigScanner::VerifyImageOffset(image.bytes, 0x3000, 0x1100, "AB CD EF") == 0,
           "matching executable RVA survives duplicate data bytes");
    Expect(SigScanner::VerifyImageOffset(image.bytes, 0x3000, 0x1200, "AB CD EF") == 0x1100,
           "unique executable match relocates an old RVA");
    Expect(SigScanner::VerifyImageOffset(image.bytes, 0x3000, 0x2100, "AB CD EF", 0x1000) == -1,
           "resolved RVA cannot leave the executable section");
    Expect(SigScanner::VerifyImageOffset(image.bytes, 0x3000, 0x1100, "AB CD ZZ") == -1,
           "invalid hex token rejects signature");
    image.ProtectText(PAGE_NOACCESS);
    Expect(SigScanner::VerifyImageOffset(image.bytes, 0x3000, 0x1100, "AB CD EF") == -1,
           "inaccessible executable section is never dereferenced");

    ImageFixture duplicateImage;
    duplicateImage.Write(0x1100, {0xAB, 0xCD, 0xEF});
    duplicateImage.Write(0x1200, {0xAB, 0xCD, 0xEF});
    duplicateImage.ProtectText(PAGE_EXECUTE_READ);
    Expect(SigScanner::VerifyImageOffset(duplicateImage.bytes, 0x3000, 0x1100, "AB CD EF") == -1,
           "anchored duplicate executable signature is rejected");
    Expect(SigScanner::VerifyImageOffset(duplicateImage.bytes, 0x3000, 0x1100, "AB CD EF", INT_MAX) == -1,
           "signature offset cannot overflow the image");

    const char *warningOnly[] = {"EyePosition", "Weapon_ShootPosition", "GetViewModelFOV"};
    for (const char *name : warningOnly) {
        StartupSymbolStatus missing;
        InspectStartupSymbol(missing, name, 0);
        Expect(missing.requiredAvailable &&
               missing.warnings == std::vector<std::string>{name},
               "each unresolved compatibility hook warns without blocking startup");
        StartupSymbolStatus present;
        InspectStartupSymbol(present, name, 1);
        Expect(present.requiredAvailable && present.warnings.empty(),
               "resolved compatibility hook emits no warning");
    }
    StartupSymbolStatus allMissing;
    for (const char *name : warningOnly) InspectStartupSymbol(allMissing, name, 0);
    Expect(allMissing.requiredAvailable &&
           allMissing.warnings == std::vector<std::string>{
               "EyePosition", "Weapon_ShootPosition", "GetViewModelFOV"},
           "all unresolved compatibility hooks leave required startup available");
    StartupSymbolStatus requiredMissing;
    InspectStartupSymbol(requiredMissing, "RenderView", 0);
    Expect(!requiredMissing.requiredAvailable &&
           requiredMissing.warnings.empty(), "unresolved required hook still blocks startup");

    ConfigSnapshot prior;
    prior.turnSpeed = 0.25f;
    std::istringstream malformedReload("TurnSpeed=0.5\nIPDScale=nan\n");
    const auto malformed = ApplyRuntimeConfig(prior, ParseConfig(malformedReload, prior), true);
    Expect(malformed.value.turnSpeed == 0.25f && !malformed.errors.empty(),
           "malformed reload keeps entire active snapshot");

    std::istringstream invalidRoomscale("RoomscaleMode=ActiveExperimental\n6DOF=false\n");
    const auto roomscale = ApplyRuntimeConfig(prior, ParseConfig(invalidRoomscale, prior), false);
    Expect(roomscale.value.roomscaleMode == RoomscaleMotion::Mode::Off && !roomscale.errors.empty(),
           "native startup rejects active roomscale without 6DOF");

    std::istringstream invalidMuzzle("AimFromViewmodelMuzzle=true\n");
    const auto muzzle = ApplyRuntimeConfig(prior, ParseConfig(invalidMuzzle, prior), false);
    Expect(!muzzle.value.aimFromViewmodelMuzzle && !muzzle.errors.empty(),
           "native startup rejects muzzle aim without alignment");

    ConfigSnapshot active = prior;
    active.roomscaleMode = RoomscaleMotion::Mode::ActiveExperimental;
    std::istringstream disableSixDof("6DOF=false\n");
    const auto suspended = ApplyRuntimeConfig(active, ParseConfig(disableSixDof, active), true);
    Expect(suspended.value.sixDof && suspended.value.roomscaleMode == RoomscaleMotion::Mode::ActiveExperimental,
           "reload cannot silently suspend active roomscale by disabling 6DOF");

    std::istringstream enterRoomscale("RoomscaleMode=ActiveExperimental\n");
    const auto restartOnly = ApplyRuntimeConfig(prior, ParseConfig(enterRoomscale, prior), true);
    Expect(restartOnly.value.roomscaleMode == RoomscaleMotion::Mode::Off,
           "reload cannot enter active roomscale before restart");

    std::vector<std::string> calls;
    RequiredHooks creationFailure;
    creationFailure.Add("first", [&] { calls.push_back("create-first"); return 0; },
                        [&] { calls.push_back("enable-first"); return 0; });
    creationFailure.Add("second", [&] { calls.push_back("create-second"); return 1; },
                        [&] { calls.push_back("enable-second"); return 0; });
    std::string failedHook;
    Expect(!creationFailure.CreateAll(failedHook) && failedHook == "second" &&
           calls == std::vector<std::string>{"create-first", "create-second"},
           "failed required creation prevents any enable");

    calls.clear();
    RequiredHooks enableFailure;
    enableFailure.Add("first", [&] { calls.push_back("create-first"); return 0; },
                      [&] { calls.push_back("enable-first"); return 0; });
    enableFailure.Add("second", [&] { calls.push_back("create-second"); return 0; },
                      [&] { calls.push_back("enable-second"); return 1; });
    Expect(enableFailure.CreateAll(failedHook) && !enableFailure.EnableAll(failedHook) &&
           failedHook == "second" &&
           calls == std::vector<std::string>{"create-first", "create-second", "enable-first", "enable-second"},
           "required enable failure is reported after complete creation stage");

    calls.clear();
    OptionalHooks unresolvedHooks;
    for (const char *name : warningOnly) {
        unresolvedHooks.AddIfResolved(name, nullptr,
            [&] { calls.push_back("create-unresolved"); return 1; },
            [&] { calls.push_back("enable-unresolved"); return 1; });
    }
    std::vector<std::string> warnings;
    unresolvedHooks.Install([&](const std::string &warning) { warnings.push_back(warning); });
    Expect(calls.empty() && warnings.empty(),
           "unresolved compatibility hooks are never created or enabled");

    calls.clear();
    OptionalHooks resolvedHooks;
    for (const char *name : warningOnly) {
        resolvedHooks.AddIfResolved(name, reinterpret_cast<void *>(1),
            [&, name] { calls.push_back(std::string("create-") + name); return 0; },
            [&, name] { calls.push_back(std::string("enable-") + name); return 0; });
    }
    resolvedHooks.Install([&](const std::string &warning) { warnings.push_back(warning); });
    Expect(warnings.empty() && calls == std::vector<std::string>{
               "create-EyePosition", "create-Weapon_ShootPosition", "create-GetViewModelFOV",
               "enable-EyePosition", "enable-Weapon_ShootPosition", "enable-GetViewModelFOV"},
           "resolved compatibility hooks create before enabling without warnings");

    calls.clear();
    OptionalHooks partialHooks;
    partialHooks.AddIfResolved("created", reinterpret_cast<void *>(1),
        [&] { calls.push_back("create-created"); return 0; },
        [&] { calls.push_back("enable-created"); return 0; });
    partialHooks.AddIfResolved("createFailed", reinterpret_cast<void *>(2),
        [&] { calls.push_back("create-createFailed"); return 1; },
        [&] { calls.push_back("enable-createFailed"); return 0; });
    partialHooks.AddIfResolved("enableFailed", reinterpret_cast<void *>(3),
        [&] { calls.push_back("create-enableFailed"); return 0; },
        [&] { calls.push_back("enable-enableFailed"); return 1; });
    partialHooks.AddIfResolved("later", reinterpret_cast<void *>(4),
        [&] { calls.push_back("create-later"); return 0; },
        [&] { calls.push_back("enable-later"); return 0; });
    warnings.clear();
    partialHooks.Install([&](const std::string &warning) { warnings.push_back(warning); });
    Expect(calls == std::vector<std::string>{
               "create-created", "create-createFailed", "create-enableFailed", "create-later",
               "enable-created", "enable-enableFailed", "enable-later"},
           "optional hook failures do not prevent later creates and enables");
    Expect(warnings.size() == 2 &&
           warnings[0].find("createFailed") != std::string::npos &&
           warnings[0].find("create") != std::string::npos &&
           warnings[1].find("enableFailed") != std::string::npos &&
           warnings[1].find("enable") != std::string::npos,
           "optional hook failures identify hook and failed stage");
    return failures ? 1 : 0;
}
