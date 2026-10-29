// tests/checks_circle_ops.h - the circular pocket document shared by two files.
//
// The planner is checked for its arcs and for its rules; both need the one
// reference operation of PLAN-QUICKPROG, 10: a F20 pocket, centre at the
// origin, an 8 mm end mill, no allowance.
#pragma once

#include "core/ops.h"

namespace circle_checks {

// F20 pocket in the centre of the stock, an 8 mm end mill, no allowance.
qp::Operation circleOp();

// The demo stock with the reference circular pocket as its only operation.
qp::Program circleProgram();

} // namespace circle_checks
