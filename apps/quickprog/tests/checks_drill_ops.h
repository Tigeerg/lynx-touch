// tests/checks_drill_ops.h - the drill cases shared by two files (10).
//
// A grid of 3 x 4 from (10, 10) and a ring of six holes on a radius of 25:
// the layout cases check the points, the cycle cases check the emitted blocks,
// so the document lives here and not twice.
#pragma once

#include "core/ops.h"

namespace drill_checks {

// One operation of the given shape with the table feeds of the drill.
qp::Operation drillOp(qp::OperationGeometry geometry, double diameter, double depth);

// A 3 x 4 grid from (10, 10), dx = dy = 15.
qp::DrillGridOp grid();

// The grid document: the demo stock with one drilling operation of 12 mm.
qp::Program gridProgram();

// The ring document: six holes on a radius of 25 from 0 degrees, 12 mm deep.
qp::Program circleDrillProgram();

} // namespace drill_checks
