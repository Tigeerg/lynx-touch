// core/collision.h - holder collisions with the stock and tool check against the
// machine.
//
// Stage 5.4c of the port. The scan runs along a path sampling with step
// `collisionStep` (mm): at each step the holder sections (the collision code
// takes them from core/tooling) are checked for intersection with the stock
// bounds. A depth threshold of 0.05 mm filters out a face touch: without it,
// cutting along the top plane of the stock would produce false hits.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "core/path_prep.h"     // gv::StageRange
#include "gv_types.h"

namespace gv {

// Sampling step along the path during the collision scan, mm
// (CNC_COLLISION_STEP).
double collisionStep();

// True when the circle (cx, cy, r) overlaps the XY rectangle. A touch counts as
// a hit (`<=`), and the boundary matters: it decides whether a collision is
// found at the stock wall itself.
bool circleRectHit(double cx, double cy, double r,
                   double xmin, double xmax, double ymin, double ymax);

// A found collision range: path length to the representative point, penetration
// depth (mm) and the point itself in scene coordinates; `scene_hb` is the holder
// bottom at the same point (the scene marker sits on it).
struct CollisionRange {
    double d = 0.0;
    double depth = 0.0;
    std::array<double, 3> scene{0.0, 0.0, 0.0};
    std::array<double, 3> scene_hb{0.0, 0.0, 0.0};
};

// Scan of holder collisions with the stock along the path.
//
// `pts`/`cum` - path sampling in scene coordinates (X*3 per point) and the
// accumulated length; `stage_starts` - start of each stage along the path
// length; `stage_len[i]` - tool gauge length of stage i; `stock` - (xmin, xmax,
// ymin, ymax, zmin, zmax) in machine coordinates. Consecutive ranges are merged
// into one.
std::vector<CollisionRange> scanCollisions(const std::vector<double>& pts,
                                           const std::vector<double>& cum,
                                           double total,
                                           const std::vector<double>& stage_starts,
                                           const std::vector<double>& stage_len,
                                           const std::array<double, 6>& stock,
                                           double step);

// Measured tool from the machine table: gauge length (correction length, mm),
// name and the "enabled" flag. The key is the tool number.
struct MachineTool {
    double length = 0.0;
    std::string name;
    bool enabled = true;
};

// Machine axis limits: `present` - the map was non-empty (for the reference an
// empty dict is told from a non-empty one precisely by truthiness).
struct MachineLimits {
    bool present = false;
    double z_max = 0.0;
};

// A tool check row: tool number, label, NC length and the minimum machining Z,
// the measured machine length and their difference (the `has_*` fields cover the
// reference's None), status and a message for the operator.
struct ToolCheckRow {
    int number = 0;
    std::string name;
    double nc_length = 0.0;
    double depth = 0.0;
    bool has_machine_length = false;
    double machine_length = 0.0;
    bool has_delta = false;
    double delta = 0.0;
    std::string status;                  // ok / missing / short / limit / unknown
    std::string message;
};

// Check every NC tool against the measured machine table: whether the number is
// in the table and whether the machine gauge length is not shorter than the
// program assumes. An empty table means the machine is not connected and there
// is nothing to check.
std::vector<ToolCheckRow> toolCheckReport(const std::vector<ToolInfo>& stage_tools,
                                          const std::vector<StageRange>& stages,
                                          const std::vector<double>& pts,
                                          const std::vector<double>& cum,
                                          double total,
                                          const std::map<int, MachineTool>& machine_tools,
                                          const MachineLimits& limits);

} // namespace gv
