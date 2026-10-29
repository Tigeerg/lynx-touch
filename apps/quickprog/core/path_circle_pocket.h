// core/path_circle_pocket.h - the circular pocket planner (PLAN-QUICKPROG, 7.2, 10).
//
// Strategy, in short: a spiral from the centre outwards, one turn per radius.
// The centre line is the pocket circle shrunk by `inset = D/2 + finish
// allowance`: the tool body of the outer turn touches the wall minus the
// allowance and never crosses it. The turns are spread evenly over the centre
// line radius, so the real step between two neighbouring turns never exceeds
// the nominal one `D * kPocketStepOverPct / 100` - the same percentage the
// rectangular pocket uses (the document has no field of its own yet):
//
//   turns = ceil(radius / nominal - eps)
//   step  = radius / turns
//
// A turn is written as two half arcs G3 with I/J and an end point: a full
// circle without an end point would be read as a zero-length chord by the
// viewer (it drops an arc without an end coordinate) and a real control reads
// a half turn differently from a full one. Two half arcs are a full circle for
// both: the viewer tessellates each half and the control needs no special case.
//
// Every Z pass is a spiral from the centre to the outer turn, then a cut up.
// The tool enters the material with a vertical cut in the centre at the plunge
// feed: the centre of the spiral is the only place no wall is in the way.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"
#include "core/path.h"

namespace qp {

// Numbers of the circular pocket layout for the console summary and the
// checks: the planner's own, not recomputed by the caller. A layout that
// cannot be planned (`inset >= radius`) has `fits` false and no turns; the
// check `validate` blocks it before planning.
struct CircleStats {
    bool fits = false;
    int turns = 0;                    // turns per Z pass, the outermost is the wall
    int passes = 0;                   // Z passes
    std::vector<double> stepDepthsMm; // bottom Z of each pass, top down
    double insetMm = 0.0;             // toolRadius + finish allowance, mm
    double outerRadiusMm = 0.0;       // radius of the outermost turn, mm
    double stepMm = 0.0;              // radial step between neighbouring turns, mm
    double toolRadiusMm = 0.0;
    int moves = 0;
    double lengthMm = 0.0;            // cutting length, mm
};

// Pocket layout numbers for one operation. The arithmetic is the planner's,
// so the console summary, the warnings and the moves read one source.
CircleStats circleStats(const Program& program, const Operation& operation);

// Russian warnings of the circular pocket that do not block planning: a
// radial step at 100 % of the diameter and a negative allowance.
std::vector<std::string> circleWarnings(const Program& program, const Operation& operation);

// Appends the moves of the pocket to `out`. The document is assumed valid
// (`validate` ran first); a pocket that cannot be inset appends nothing.
void planCirclePocket(const Program& program, const Operation& operation, PathBuilder& out);

} // namespace qp
