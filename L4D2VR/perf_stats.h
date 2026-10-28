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
