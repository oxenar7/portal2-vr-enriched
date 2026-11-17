#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class StartupSymbolRole { Required, WarningOnly };

inline StartupSymbolRole RoleForStartupSymbol(std::string_view name)
{
    if (name == "EyePosition" || name == "Weapon_ShootPosition" ||
        name == "GetViewModelFOV")
        return StartupSymbolRole::WarningOnly;
    return StartupSymbolRole::Required;
}

struct StartupSymbolStatus {
    bool requiredAvailable = true;
    std::vector<std::string> warnings;
};

inline void InspectStartupSymbol(StartupSymbolStatus &status, const char *name,
                                 std::uintptr_t address)
{
    if (address) return;
    switch (RoleForStartupSymbol(name)) {
    case StartupSymbolRole::Required:
        status.requiredAvailable = false;
        break;
    case StartupSymbolRole::WarningOnly:
        status.warnings.emplace_back(name);
        break;
    }
}
