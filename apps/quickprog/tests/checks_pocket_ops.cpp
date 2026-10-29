// tests/checks_pocket_ops.cpp - the reference pocket of PLAN-QUICKPROG, 10.
//
// The one operation the two pocket case files build on: a 60x40 pocket, an
// 8 mm end mill, 0.2 mm allowance, a 2 mm raster step, 6 mm deep in 2.5 mm
// passes. The numbers are computed by hand in checks_pocket_layout.cpp.
#include "tests/checks_pocket_ops.h"

#include "tests/harness.h"

namespace pocket_checks {

using qpcheck::demoProgram;

qp::Operation pocketOp() {
    qp::Operation op;
    op.id = "Карман";
    op.tool.number = 3;
    op.tool.diameterMm = 8.0;
    op.cutting.spindleRpm = 5968.0;
    op.cutting.feedMmMin = 859.0;
    op.cutting.plungeFeedMmMin = 215.0;
    op.topZ = 0.0;
    op.depthMm = 6.0;
    op.stepDownMm = 2.5;
    qp::RectPocketOp pocket;
    pocket.rect = qp::Rect{-30.0, -20.0, 60.0, 40.0};
    pocket.finishAllowanceMm = 0.2;
    op.geometry = pocket;
    return op;
}

qp::Program pocketProgram() {
    qp::Program program = demoProgram();
    program.name = "POCKET DEMO";
    program.operations.push_back(pocketOp());
    return program;
}

} // namespace pocket_checks
