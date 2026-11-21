#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace Portal2MaterialAbi {

// Verified against the Windows materialsystem.dll of Portal 2 (2026-07-01), of
// Portal: Alive & Kicking (2024-02-20), of Portal Stories: Mel (2021-03-02) and of Thinking with
// Time Machine (2014-07-09). Other builds must retain the native HUD
// draw rather than call unknown slots.
inline constexpr std::size_t kViewportSlot = 38;
inline constexpr std::size_t kGetViewportSlot = 39;
inline constexpr std::size_t kDrawScreenSpaceRectangleSlot = 105;

enum class Kind { Unsupported, Queued, Immediate };

struct ContextLayout
{
    std::uint32_t vtable, viewport, getViewport, drawScreenSpaceRectangle;
};

struct Build
{
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    ContextLayout queued;    // CMatQueuedRenderContext
    ContextLayout immediate; // CMatRenderContext
};

inline constexpr Build kBuilds[] = {
    {0x6A4466CAu, 0x14D000u, {0x9BEF4u, 0x27710u, 0x25190u, 0x27C40u},
        {0x9ED4Cu, 0x2D8B0u, 0x2CAF0u, 0x2A510u}},  // Portal 2
    {0x65D40926u, 0x14C000u, {0x99E2Cu, 0x274F0u, 0x24F70u, 0x27A20u},
        {0x9CC84u, 0x2D560u, 0x2C7A0u, 0x2A1E0u}},  // Portal: Alive & Kicking
    {0x603E2D60u, 0x14C000u, {0x9AE84u, 0x27730u, 0x251B0u, 0x27C60u},
        {0x9DCDCu, 0x2D840u, 0x2CA80u, 0x2A430u}},  // Portal Stories: Mel
    {0x53BDC87Fu, 0x14C000u, {0x9AE3Cu, 0x27620u, 0x250A0u, 0x27B50u},
        {0x9DC94u, 0x2D7D0u, 0x2CA10u, 0x2A320u}},  // Thinking with Time Machine
};

inline const Build *FindBuild(std::uint32_t timestamp, std::uint32_t imageSize)
{
    for (const auto &build : kBuilds)
        if (build.timestamp == timestamp && build.imageSize == imageSize) return &build;
    return nullptr;
}

// True when vtable is one of the build's two render-context tables, before any slot is read.
inline bool KnownVtable(const Build &build, std::uintptr_t moduleBase, std::uintptr_t vtable)
{
    return vtable == moduleBase + build.queued.vtable || vtable == moduleBase + build.immediate.vtable;
}

struct Probe
{
    std::uintptr_t moduleBase;
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    std::uintptr_t vtable;
    std::uintptr_t viewport;
    std::uintptr_t getViewport;
    std::uintptr_t drawScreenSpaceRectangle;
};

inline Kind Classify(const Probe &probe)
{
    const Build *build = FindBuild(probe.timestamp, probe.imageSize);
    if (probe.moduleBase == 0 || !build ||
        probe.moduleBase > (std::numeric_limits<std::uintptr_t>::max)() - probe.imageSize)
        return Kind::Unsupported;
    const auto matches = [&](const ContextLayout &layout) {
        const auto at = [&](std::uintptr_t rva) { return probe.moduleBase + rva; };
        return probe.vtable == at(layout.vtable) && probe.viewport == at(layout.viewport) &&
            probe.getViewport == at(layout.getViewport) &&
            probe.drawScreenSpaceRectangle == at(layout.drawScreenSpaceRectangle);
    };
    if (matches(build->queued)) return Kind::Queued;
    if (matches(build->immediate)) return Kind::Immediate;
    return Kind::Unsupported;
}

} // namespace Portal2MaterialAbi
