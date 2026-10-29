// core/path_drill.h - the drilling planner: one hole, a grid, a circle (7.2, 10).
//
// Drilling is one canned cycle per hole: the planner computes the hole
// centres, the cycle (G81 or G83) and the depth and R planes. The plan carries
// one move per hole (the centre at the R plane) so the preview shows the same
// hole centres the cycle will drill; the emitter replaces those moves by the
// cycle words G81/G83 and closes the cycle with G80. The cycle expands on the
// control, not in the plan: the pecking is a property of the machine.
//
// Cycle choice: G81 when the depth is at most three diameters and no peck is
// asked for, G83 with Q = D otherwise (a fixed `peckMm` of the operation wins
// over the rule). The cycle is closed with G80 after the last hole.
//
// The R plane: 1 mm above the material top, but never above safe Z and never
// below the top. A rapid of the cycle therefore never meets the material.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"
#include "core/path.h"

namespace qp {

enum class DrillKind { Simple, Peck };

// The planned drilling of one operation: hole centres in cutting order, the
// cycle and its parameters.
struct DrillStats {
    std::vector<Point3> points;  // hole centres, XY; z is the top
    DrillKind cycle = DrillKind::Simple;
    double peckMm = 0.0;         // Q of G83, zero for G81
    double rMm = 0.0;            // R plane, mm
    double depthMm = 0.0;        // bottom Z, mm
    std::vector<std::string> errors;  // grid/circle shape problems, Russian
};

// Hole centres and cycle of one operation. A document is assumed valid; a
// shape that cannot be laid out (zero count) gives no points and an error.
DrillStats drillStats(const Program& program, const Operation& operation);

// Appends the moves of a drilling operation to `out`: a rapid to safe Z over
// each hole centre. The cycle is not a move: it expands on the control, so the
// emitter writes it from the same DrillStats and the plan carries only the
// real travel between the holes.
void planDrill(const Program& program, const Operation& operation, PathBuilder& out);

// True for the three drilling kinds. The emitter asks this to write the cycle
// block instead of G0/G1 moves: a cycle is a machine feature, not a path.
bool isDrill(const OperationGeometry& geometry);

// The hole centres of a drilling operation in cutting order, row by row in X
// for a grid and clockwise for a ring (see path_drill.cpp). The planner, the
// summary and the cycle block read this one list, so the printed X/Y of a hole
// is the point the preview draws. An empty list means a shape that cannot be
// laid out (`DrillStats::errors` says why).
std::vector<Point3> drillHoles(const Operation& operation);

} // namespace qp
