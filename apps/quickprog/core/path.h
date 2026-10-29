// core/path.h - the planner result: moves, not G-code lines (PLAN-QUICKPROG, 6).
//
// The preview and the emitter read the same Path: one geometry, two
// representations. The planner returns moves in machine coordinates; the
// emitter turns them into G-code and never recomputes a coordinate.
#pragma once

#include <string>
#include <vector>

#include "core/geom.h"
#include "core/ops.h"

namespace qp {

// Arc centre offset from the start point of the arc, mm, in the XY plane, as
// the I/J words of G2/G3. The end point is Move::to.
struct ArcOffset {
    bool cw = false;
    double i = 0.0;
    double j = 0.0;
};

enum class MoveKind { Rapid, Cut };

// One move. `feedMmMin` is set for cuts and empty for rapids: a rapid has no
// programmed feed, and writing F on it would change the modal state by accident.
struct Move {
    MoveKind kind = MoveKind::Rapid;
    Point3 to;
    double feedMmMin = 0.0;
    bool hasFeed = false;
    bool hasArc = false;
    ArcOffset arc;
};

// Bounds of the tool centre path in XY and its highest Z, mm. `valid` is false
// for an empty path: zeros mean "no path", not "a point at the origin".
struct PathBounds {
    bool valid = false;
    double xMin = 0.0;
    double xMax = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;
    double zMax = 0.0;
};

// Planned trajectory of a program. `uuid` identifies the planning result so a
// stale preview can be told from the fresh one after a debounce.
struct Path {
    std::vector<Move> moves;
    PathBounds bbox;
    double lengthMm = 0.0;
    std::string uuid;
};

// Operation -> moves. A stub in the first slice: it returns an empty Path, so
// the pipeline runs end to end without a trajectory (PLAN-QUICKPROG, 7.2).
Path planPath(const Program& program);

} // namespace qp
