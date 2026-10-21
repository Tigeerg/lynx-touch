// core/path_prep.h - path preparation for drawing: vertex streams, stages,
// bounds.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "gv_types.h"

namespace gv {

// Bounds in scene coordinates: minimum and maximum along the three axes, mm.
struct Aabb {
    std::array<double, 3> lo{0.0, 0.0, 0.0};
    std::array<double, 3> hi{0.0, 0.0, 0.0};
};

// Stage reduced to path fractions: the range [start, end] along the path length.
struct StageRange {
    double start = 0.0;
    double end = 1.0;
    std::string label;
    ToolInfo info;
};

// Buffer of one line stream: float32 (position 3 + path parameter), as on the
// GPU.
struct PackedStream {
    std::vector<unsigned char> bytes;
    int nv = 0;                 // vertex count (2 per segment)
};

// Prepared path (mirrors PathData from core/gv_types.h): packed buffers of both
// streams, the animation polyline in scene coordinates and its metrics.
struct PathData {
    int seg_count = 0;
    std::array<double, 3> last{0.0, 0.0, 0.0};
    std::vector<double> pts;              // scene (X, Z, -Y), N*3
    std::vector<double> cum;              // accumulated length, N
    std::vector<unsigned char> rapid;     // OUTGOING segment flag, N (1/0)
    std::vector<double> speeds;           // OUTGOING segment speed, N
    double total = 0.0;
    PackedStream rapid_stream;
    PackedStream feed_stream;
    bool has_bounds = false;
    Aabb bounds;
    std::array<double, 3> center{0.0, 0.0, 0.0};
    double radius = 50.0;
    double min_y = 0.0;
    std::vector<StageRange> stages;
};

// Machine bounds -> scene bounds (X, Z, -Y).
Aabb sceneBoundsLoHi(const std::array<double, 3>& lo, const std::array<double, 3>& hi);

// Packing of one line stream: mask by the outgoing segment flag; coordinates are
// converted to the scene when machine=true.
PackedStream packStream(const std::vector<double>& pts, const std::vector<unsigned char>& flags,
                        const std::vector<double>& cum, double total, bool wantRapid,
                        bool machine = true);

// Stages (index of the first point, label) -> ranges [start, end] along the path
// length.
std::vector<StageRange> stagesNorm(const std::vector<Stage>& stages,
                                   const std::vector<double>& cum, double total);

// Split the path into buffers and metrics. The heavy part of loading: it can run
// on a worker thread and creates no Qt objects.
PathData prepareOrdered(const std::vector<PathPoint>& ordered,
                        const std::vector<Stage>& stages);

} // namespace gv
