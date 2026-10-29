// tests/checks_drill_ops.cpp - the drill operation the drill cases build on (10).
//
// One hole, a grid and a ring with the table feeds of the drill: the layout
// cases check the points, the cycle cases the emitted blocks, so the document
// lives here and not twice.
#include <string>
#include <vector>

#include "core/feeds.h"
#include "core/ops.h"

#include "tests/checks_drill_ops.h"
#include "tests/harness.h"

namespace drill_checks {

using qpcheck::demoProgram;

qp::Operation drillOp(qp::OperationGeometry geometry, double diameter, double depth) {
    qp::Operation op;
    op.id = "Отверстия";
    op.tool.number = 5;
    op.tool.diameterMm = diameter;
    op.topZ = 0.0;
    op.depthMm = depth;
    op.stepDownMm = depth;
    const qp::Feeds table = qp::feedsFor(qp::Material::Aluminium, diameter, qp::ToolKind::Drill);
    op.cutting.spindleRpm = table.rpm;
    op.cutting.feedMmMin = table.feedMmMin;
    op.cutting.plungeFeedMmMin = table.plungeFeedMmMin;
    op.geometry = geometry;
    return op;
}

qp::DrillGridOp grid() {
    qp::DrillGridOp g;
    g.x0 = 10.0;
    g.y0 = 10.0;
    g.cols = 3;
    g.rows = 4;
    g.dx = 15.0;
    g.dy = 15.0;
    return g;
}

qp::Program gridProgram() {
    qp::Program program = demoProgram();
    program.name = "DRILL GRID";
    program.operations.push_back(drillOp(grid(), 6.0, 12.0));
    return program;
}

qp::Program circleDrillProgram() {
    qp::Program program = demoProgram();
    program.name = "DRILL CIRCLE";
    qp::DrillCircleOp ring;
    ring.circle = qp::Circle{0.0, 0.0, 25.0};
    ring.count = 6;
    ring.startDeg = 0.0;
    program.operations.push_back(drillOp(ring, 6.0, 12.0));
    return program;
}

// 3 x 4 grid: twelve points, row by row, in the expected coordinates.

} // namespace drill_checks
