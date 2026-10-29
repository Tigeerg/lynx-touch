// tests/checks_pocket_ops.h - the pocket document shared by the pocket files.
//
// The rectangle planner is checked for its layout (rows, passes, bounds, the
// entry) and for its rules (.ngc block, corner warning, .qprog). Both need the
// one operation of PLAN-QUICKPROG, 10, so the document lives here and not
// twice.
#pragma once

#include "core/ops.h"

namespace pocket_checks {

// The reference pocket of the plan: 60x40 at the origin of the stock, an 8 mm
// end mill, 0.2 mm allowance, 6 mm deep in 2.5 mm passes.
qp::Operation pocketOp();

// The demo stock with the reference pocket as its only operation.
qp::Program pocketProgram();

} // namespace pocket_checks
