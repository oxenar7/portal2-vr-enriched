#pragma once

// Eye mask: the parts of an eye render target the headset never shows.
//
// The mod renders both eyes with one symmetric frustum and submits only the
// part inside each eye's texture bounds, so the strips outside the bounds are
// rendered for nothing (about a third of the texture on Quest 3). Inside the
// bounds, the lens hidden area mesh from OpenVR covers the corners the lenses
// cut away. DXVK stamps these rects to the nearest depth after each full depth
// clear of the eye, so the GPU rejects those pixels before the pixel shader.
//
// OpenVR does not document whether the hidden area mesh has v pointing up or
// down, so only the cells hidden under both readings are masked.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace EyeMask {

struct Rect {
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
};

struct Point {
    float u = 0.0f, v = 0.0f;
};

struct Bounds {
    float uMin = 0.0f, vMin = 0.0f, uMax = 1.0f, vMax = 1.0f;
};

struct Result {
    std::vector<Rect> rects;
    Rect visible;                 // submitted bounds plus margin; empty if invalid
    double outsideFraction = 0.0; // of the whole texture
    double lensFraction = 0.0;    // of the whole texture
};

// Pixels kept unmasked around the submitted bounds and the lens edge.
constexpr int32_t kBoundsMargin = 8;
constexpr int32_t kCellSize = 16;

inline bool InsideTriangle(const Point &a, const Point &b, const Point &c, float u, float v)
{
    const float d1 = (b.u - a.u) * (v - a.v) - (b.v - a.v) * (u - a.u);
    const float d2 = (c.u - b.u) * (v - b.v) - (c.v - b.v) * (u - b.u);
    const float d3 = (a.u - c.u) * (v - c.v) - (a.v - c.v) * (u - c.u);
    const bool hasNeg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
    const bool hasPos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
    return !(hasNeg && hasPos);
}

inline bool InsideMesh(const std::vector<Point> &triangles, float u, float v)
{
    for (size_t i = 0; i + 2 < triangles.size(); i += 3)
        if (InsideTriangle(triangles[i], triangles[i + 1], triangles[i + 2], u, v))
            return true;
    return false;
}

// width/height: eye render target size. bounds: the texture bounds the eye is
// submitted with (v down). hiddenMesh: OpenVR hidden area triangles in the
// submitted image's 0..1 space (may be empty).
inline Result Build(uint32_t width, uint32_t height, const Bounds &bounds, const std::vector<Point> &hiddenMesh)
{
    Result result;
    if (!width || !height || !(bounds.uMax > bounds.uMin) || !(bounds.vMax > bounds.vMin))
        return result;

    const int32_t w = static_cast<int32_t>(width), h = static_cast<int32_t>(height);
    const int32_t x0 = std::clamp(static_cast<int32_t>(std::floor(bounds.uMin * w)) - kBoundsMargin, 0, w);
    const int32_t x1 = std::clamp(static_cast<int32_t>(std::ceil(bounds.uMax * w)) + kBoundsMargin, 0, w);
    const int32_t y0 = std::clamp(static_cast<int32_t>(std::floor(bounds.vMin * h)) - kBoundsMargin, 0, h);
    const int32_t y1 = std::clamp(static_cast<int32_t>(std::ceil(bounds.vMax * h)) + kBoundsMargin, 0, h);
    if (x1 <= x0 || y1 <= y0)
        return result;
    result.visible = {x0, y0, static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)};

    auto add = [&](int32_t x, int32_t y, int32_t rw, int32_t rh, double &fraction) {
        if (rw <= 0 || rh <= 0)
            return;
        result.rects.push_back({x, y, static_cast<uint32_t>(rw), static_cast<uint32_t>(rh)});
        fraction += static_cast<double>(rw) * rh / (static_cast<double>(w) * h);
    };
    add(0, 0, w, y0, result.outsideFraction);
    add(0, y1, w, h - y1, result.outsideFraction);
    add(0, y0, x0, y1 - y0, result.outsideFraction);
    add(x1, y0, w - x1, y1 - y0, result.outsideFraction);

    if (hiddenMesh.size() < 3)
        return result;

    // Lens part: cells inside the bounds whose centre is hidden under both v
    // readings, eroded by one cell so the mask stays clear of the lens edge.
    const int32_t cols = (x1 - x0) / kCellSize, rows = (y1 - y0) / kCellSize;
    if (cols < 3 || rows < 3)
        return result;
    const float uSpan = bounds.uMax - bounds.uMin, vSpan = bounds.vMax - bounds.vMin;
    std::vector<uint8_t> hidden(static_cast<size_t>(cols) * rows, 0);
    for (int32_t r = 0; r < rows; ++r) {
        for (int32_t c = 0; c < cols; ++c) {
            const float px = x0 + (c + 0.5f) * kCellSize, py = y0 + (r + 0.5f) * kCellSize;
            const float u = (px / w - bounds.uMin) / uSpan;
            const float v = (py / h - bounds.vMin) / vSpan;
            hidden[static_cast<size_t>(r) * cols + c] =
                InsideMesh(hiddenMesh, u, v) && InsideMesh(hiddenMesh, u, 1.0f - v);
        }
    }
    auto at = [&](int32_t r, int32_t c) {
        // Cells beyond the grid border belong to the masked outside strips.
        if (r < 0 || c < 0 || r >= rows || c >= cols)
            return true;
        return hidden[static_cast<size_t>(r) * cols + c] != 0;
    };
    std::vector<uint8_t> masked(hidden.size(), 0);
    for (int32_t r = 0; r < rows; ++r)
        for (int32_t c = 0; c < cols; ++c) {
            bool all = true;
            for (int32_t dr = -1; dr <= 1 && all; ++dr)
                for (int32_t dc = -1; dc <= 1 && all; ++dc)
                    all = at(r + dr, c + dc);
            masked[static_cast<size_t>(r) * cols + c] = all;
        }

    // Horizontal runs per cell row, merged downwards while the run repeats.
    struct Run { int32_t c0, c1, r0, r1; };
    std::vector<Run> open, done;
    for (int32_t r = 0; r < rows; ++r) {
        std::vector<Run> next;
        for (int32_t c = 0; c < cols;) {
            if (!masked[static_cast<size_t>(r) * cols + c]) { ++c; continue; }
            int32_t e = c;
            while (e < cols && masked[static_cast<size_t>(r) * cols + e])
                ++e;
            auto it = std::find_if(open.begin(), open.end(), [&](const Run &o) { return o.c0 == c && o.c1 == e; });
            if (it != open.end()) {
                Run grown = *it;
                grown.r1 = r + 1;
                open.erase(it);
                next.push_back(grown);
            } else {
                next.push_back({c, e, r, r + 1});
            }
            c = e;
        }
        done.insert(done.end(), open.begin(), open.end());
        open.swap(next);
    }
    done.insert(done.end(), open.begin(), open.end());
    for (const Run &run : done)
        add(x0 + run.c0 * kCellSize, y0 + run.r0 * kCellSize,
            (run.c1 - run.c0) * kCellSize, (run.r1 - run.r0) * kCellSize, result.lensFraction);
    return result;
}

} // namespace EyeMask
