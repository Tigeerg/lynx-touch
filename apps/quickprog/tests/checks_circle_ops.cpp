// tests/checks_circle_ops.cpp - the circular pocket document (10).
//
// A F20 pocket, centre at the origin, an 8 mm end mill, no allowance, 1 mm
// deep in one pass: the shape the arc cases and the rule cases build on.
#include "tests/checks_circle_ops.h"

#include "tests/harness.h"

namespace circle_checks {

using qpcheck::demoProgram;

// F20 pocket in the centre of the stock, an 8 mm end mill, no allowance.
qp::Operation circleOp() {
    qp::Operation op;
    op.id = "Круглый карман";
    op.tool.number = 4;
    op.tool.diameterMm = 8.0;
    op.cutting.spindleRpm = 5968.0;
    op.cutting.feedMmMin = 859.0;
    op.cutting.plungeFeedMmMin = 215.0;
    op.topZ = 0.0;
    op.depthMm = 1.0;
    op.stepDownMm = 1.0;
    qp::CirclePocketOp pocket;
    pocket.circle = qp::Circle{0.0, 0.0, 10.0};
    pocket.finishAllowanceMm = 0.0;
    op.geometry = pocket;
    return op;
}

qp::Program circleProgram() {
    qp::Program program = demoProgram();
    program.name = "CIRCLE DEMO";
    program.operations.push_back(circleOp());
    return program;
}

} // namespace circle_checks
