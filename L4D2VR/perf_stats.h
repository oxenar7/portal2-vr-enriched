#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Frame timing statistics for the automatic performance log. Samples are
// collected per window (a few seconds) and summarised as average, 95th
// percentile and maximum, so a session log can be read without any input
// from the player.
namespace PerfStats {

enum class Metric : std::size_t
{
    Frame,        // VR::Update start to the next one: the game's frame interval
    MainWork,     // Frame minus the GPU drain and WaitGetPoses waits
    RenderStereo, // whole stereo dRenderView, both eyes and the optional window
    RenderLeft,   // original RenderView for the left eye, portals included
    RenderRight,
    RenderWindow, // third RenderView when RenderWindow=1
    Present,      // desktop swapchain Present inside PresentEx
    Drain,        // WaitDeviceIdle after Present: CPU waits for the GPU
    Submit,       // SubmitVRTextures
    WaitPoses,    // WaitGetPoses: CPU waits for the compositor
    GpuApp,       // compositor timing: application GPU time of the frame
    GpuTotal,     // compositor timing: application plus compositor GPU time
    CompositorIdle, // compositor timing: time the application left unused
    Count
};

constexpr std::size_t kMetricCount = static_cast<std::size_t>(Metric::Count);

inline const char *MetricName(Metric metric)
{
    static constexpr std::array<const char *, kMetricCount> names{
        "frame", "main", "pair", "left", "right", "window", "present", "drain",
        "submit", "poses", "gpu", "gpuTotal", "idle"};
    return names[static_cast<std::size_t>(metric)];
}

struct Summary
{
    std::size_t count = 0;
    float average = 0.0f;
    float p95 = 0.0f;
    float maximum = 0.0f;
};

inline Summary Summarize(std::vector<float> samples)
{
    Summary summary;
    summary.count = samples.size();
    if (samples.empty()) return summary;
    double total = 0.0;
    for (float sample : samples) total += sample;
    summary.average = static_cast<float>(total / samples.size());
    summary.maximum = *std::max_element(samples.begin(), samples.end());
    const std::size_t index = (std::min)(samples.size() - 1,
        static_cast<std::size_t>(std::ceil(0.95 * samples.size())) - 1);
    std::nth_element(samples.begin(), samples.begin() + index, samples.end());
    summary.p95 = samples[index];
    return summary;
}

inline double ElapsedMs(std::chrono::steady_clock::time_point start,
                        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now())
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// The game thread's own work: what remains of the frame after the two
// blocking waits. Clamped because the waits are timed separately.
inline double MainWorkMs(double frameMs, double drainMs, double waitPosesMs)
{
    return (std::max)(0.0, frameMs - drainMs - waitPosesMs);
}

class Window
{
public:
    void Add(Metric metric, double milliseconds)
    {
        if (!std::isfinite(milliseconds) || milliseconds < 0.0) return;
        m_Samples[static_cast<std::size_t>(metric)].push_back(static_cast<float>(milliseconds));
    }

    Summary Get(Metric metric) const
    {
        return Summarize(m_Samples[static_cast<std::size_t>(metric)]);
    }

    void Clear()
    {
        for (auto &samples : m_Samples) samples.clear();
    }

private:
    std::array<std::vector<float>, kMetricCount> m_Samples;
};

// One frame interval split into its timed parts. The log keeps the slowest
// frame of each window so a hitch can be attributed to its cause.
struct FrameBreakdown
{
    double frame = 0.0;
    double main = 0.0;
    double pair = 0.0;
    double drain = 0.0;
    double poses = 0.0;
    double submit = 0.0;
};

// " worst=frame:main:pair:drain:poses:submit" in milliseconds.
inline std::string FormatBreakdown(const FrameBreakdown &frame)
{
    if (frame.frame <= 0.0) return {};
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), " worst=%.1f:%.1f:%.1f:%.1f:%.1f:%.1f", frame.frame,
                  frame.main, frame.pair, frame.drain, frame.poses, frame.submit);
    return buffer;
}

// A frame that took more than one and a half refresh intervals: the
// compositor showed at least one frame without new content.
inline bool IsLongFrame(double frameMs, float displayHz)
{
    return displayHz > 0.0f && frameMs > 1.5 * 1000.0 / displayHz;
}

// Compositor counters are cumulative for the process; the log reports the
// difference over each window.
struct CompositorCounts
{
    std::uint32_t presents = 0;
    std::uint32_t dropped = 0;
    std::uint32_t reprojected = 0;
    std::uint32_t timedOut = 0;
};

inline CompositorCounts CountsSince(const CompositorCounts &start, const CompositorCounts &now)
{
    // Unsigned subtraction tolerates a counter that wrapped; a compositor
    // restart (counters going backwards) reports zero instead of garbage.
    const auto delta = [](std::uint32_t from, std::uint32_t to) {
        return to >= from ? to - from : 0u;
    };
    return {delta(start.presents, now.presents), delta(start.dropped, now.dropped),
            delta(start.reprojected, now.reprojected), delta(start.timedOut, now.timedOut)};
}

// DXVK pipeline compiler counters, cumulative for the device.
struct CompilerCounts
{
    std::uint64_t tasksDone = 0;  // background compiles finished by the workers
    std::uint64_t tasksTotal = 0; // background compiles requested
    std::uint64_t pipelines = 0;  // pipeline instances created
    std::uint64_t libraries = 0;  // GPL shader libraries compiled
};

// Compiler activity over one log window. A frame is busy when compiles were
// queued at its Present or finished since the previous one: in those frames
// the workers competed with the game, SteamVR and the streamer for the CPU.
class CompilerActivity
{
public:
    // Records the counters read at a Present; returns whether the frame was busy.
    bool Note(const CompilerCounts &now)
    {
        const std::uint64_t queued = Queued(now);
        const bool busy = queued > 0 || (m_HaveLast && now.tasksDone != m_Last.tasksDone);
        if (!m_HaveStart) {
            m_Start = now;
            m_HaveStart = true;
        }
        m_Last = now;
        m_HaveLast = true;
        m_MaxQueued = (std::max)(m_MaxQueued, queued);
        if (busy) ++m_BusyFrames;
        return busy;
    }

    // " compiled=N queued=N busy=N longBusy=N pipes=N libs=N" for the window,
    // or nothing when the device never reported its counters.
    std::string Format(std::uint32_t longBusyFrames) const
    {
        if (!m_HaveStart) return {};
        const auto delta = [](std::uint64_t from, std::uint64_t to) { return to >= from ? to - from : 0ull; };
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), " compiled=%llu queued=%llu busy=%u longBusy=%u pipes=%llu libs=%llu",
                      delta(m_Start.tasksDone, m_Last.tasksDone), m_MaxQueued, m_BusyFrames, longBusyFrames,
                      delta(m_Start.pipelines, m_Last.pipelines), delta(m_Start.libraries, m_Last.libraries));
        return buffer;
    }

    void StartWindow()
    {
        m_Start = m_Last;
        m_HaveStart = m_HaveLast;
        m_MaxQueued = 0;
        m_BusyFrames = 0;
    }

    void Clear() { *this = {}; }

private:
    // The two counters are read separately, so a just-finished task can make
    // the done count overtake the requested count.
    static std::uint64_t Queued(const CompilerCounts &counts)
    {
        return counts.tasksTotal > counts.tasksDone ? counts.tasksTotal - counts.tasksDone : 0;
    }

    CompilerCounts m_Start{};
    CompilerCounts m_Last{};
    bool m_HaveStart = false;
    bool m_HaveLast = false;
    std::uint64_t m_MaxQueued = 0;
    std::uint32_t m_BusyFrames = 0;
};

// Swaps between two settings every few log windows, so a setting can be
// compared with itself in the same scenes without any input from the player.
class AbSchedule
{
public:
    // Called once per log window; returns true when the side changes.
    bool Next(int windowsPerSide)
    {
        if (++m_Windows < windowsPerSide) return false;
        m_Windows = 0;
        m_SideB = !m_SideB;
        return true;
    }

    bool SideB() const { return m_SideB; }

private:
    int m_Windows = 0;
    bool m_SideB = false;
};

// " vm=used vmMax=total" in MiB: the 32-bit game's virtual address space.
inline std::string FormatAddressSpace(std::uint64_t totalBytes, std::uint64_t availableBytes)
{
    if (!totalBytes || availableBytes > totalBytes) return {};
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), " vm=%llu vmMax=%llu", (totalBytes - availableBytes) >> 20,
                  totalBytes >> 20);
    return buffer;
}

// "name=avg/p95/max" in milliseconds, or nothing when the metric had no samples.
inline std::string FormatMetric(Metric metric, const Summary &summary)
{
    if (!summary.count) return {};
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), " %s=%.2f/%.2f/%.2f", MetricName(metric),
                  summary.average, summary.p95, summary.maximum);
    return buffer;
}

inline std::string FormatMetrics(const Window &window)
{
    std::string text;
    for (std::size_t i = 0; i < kMetricCount; ++i) {
        const auto metric = static_cast<Metric>(i);
        text += FormatMetric(metric, window.Get(metric));
    }
    return text;
}

} // namespace PerfStats
