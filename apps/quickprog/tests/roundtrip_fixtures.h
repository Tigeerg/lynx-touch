// tests/roundtrip_fixtures.h - the documents and the small helpers of the
// round-trip cases (checks_roundtrip.cpp).
//
// Header-only, like harness.h: the fixtures are not one more check file, they
// are the input of the viewer-core cases. The documents are the ones a human
// would type on the stand - one per operation kind, plus the three-operation
// mixed file that section 10 of PLAN-QUICKPROG asks for.
#pragma once

#include <string>

#include "core/emit.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"

#include "tests/harness.h"

namespace roundtrip_fixtures {

// The viewer decimates collinear G1 runs by 0.5 mm: the last point of a path is
// kept exactly, the intermediate ones are not. So the check compares the final
// point, the segment count and the stage labels, as section 10 asks - not the
// full point list.
constexpr double kPosEps = 1e-6;

inline qp::Operation toolOp(const std::string& id, int number, double diameter) {
    qp::Operation op;
    op.id = id;
    op.tool.number = number;
    op.tool.diameterMm = diameter;
    op.topZ = 0.0;
    return op;
}

inline qp::Operation millingOp(const std::string& id, int number, double diameter,
                               qp::OperationGeometry geometry, double depth) {
    qp::Operation op = toolOp(id, number, diameter);
    op.depthMm = depth;
    op.stepDownMm = depth;
    const qp::Feeds table = qp::feedsFor(qp::Material::Aluminium, diameter, qp::ToolKind::Flat);
    op.cutting.spindleRpm = table.rpm;
    op.cutting.feedMmMin = table.feedMmMin;
    op.cutting.plungeFeedMmMin = table.plungeFeedMmMin;
    op.geometry = geometry;
    return op;
}

inline qp::Operation drillOp(const std::string& id, int number, double diameter,
                             qp::OperationGeometry geometry, double depth) {
    qp::Operation op = toolOp(id, number, diameter);
    op.depthMm = depth;
    op.stepDownMm = depth;
    const qp::Feeds table = qp::feedsFor(qp::Material::Aluminium, diameter, qp::ToolKind::Drill);
    op.cutting.spindleRpm = table.rpm;
    op.cutting.feedMmMin = table.feedMmMin;
    op.cutting.plungeFeedMmMin = table.plungeFeedMmMin;
    op.geometry = geometry;
    return op;
}

inline qp::Program faceProgram() {
    qp::Program program = qpcheck::demoProgram();
    program.name = "RT FACE";
    qp::FaceOp face;
    face.z = -2.0;
    face.stepOverPct = 70.0;
    program.operations.push_back(millingOp("Торцевание", 1, 10.0, face, 2.0));
    return program;
}

inline qp::Program pocketProgram() {
    qp::Program program = qpcheck::demoProgram();
    program.name = "RT POCKET";
    qp::RectPocketOp pocket;
    pocket.rect = qp::Rect{-30.0, -20.0, 60.0, 40.0};
    pocket.finishAllowanceMm = 0.5;
    program.operations.push_back(millingOp("Карман", 2, 8.0, pocket, 5.0));
    return program;
}

inline qp::Program circleProgram() {
    qp::Program program = qpcheck::demoProgram();
    program.name = "RT CIRCLE";
    qp::CirclePocketOp circle;
    circle.circle = qp::Circle{0.0, 0.0, 15.0};
    circle.finishAllowanceMm = 0.5;
    program.operations.push_back(millingOp("Круглый карман", 3, 6.0, circle, 4.0));
    return program;
}

inline qp::Program drillProgram() {
    qp::Program program = qpcheck::demoProgram();
    program.name = "RT DRILL";
    qp::DrillGridOp grid;
    grid.x0 = -20.0;
    grid.y0 = -20.0;
    grid.cols = 3;
    grid.rows = 2;
    grid.dx = 20.0;
    grid.dy = 20.0;
    program.operations.push_back(drillOp("Сетка отверстий", 5, 6.0, grid, 12.0));
    return program;
}

// Face + pocket + drill grid in one document: the labels of the stages must be
// the UI_OP names in the operation order.
inline qp::Program mixedProgram() {
    qp::Program program = faceProgram();
    program.name = "RT MIXED";
    qp::RectPocketOp pocket;
    pocket.rect = qp::Rect{-25.0, -15.0, 30.0, 20.0};
    program.operations.push_back(millingOp("Карман 30x20", 2, 8.0, pocket, 5.0));
    qp::DrillGridOp grid;
    grid.x0 = 25.0;
    grid.y0 = -20.0;
    grid.cols = 2;
    grid.rows = 2;
    grid.dx = 15.0;
    grid.dy = 20.0;
    program.operations.push_back(drillOp("Сверление 2x2", 5, 6.0, grid, 10.0));
    return program;
}

// The bytes of the file: exactly what a viewer would load from the disk.
inline std::string ncgOf(const qp::Program& program) {
    return qp::emitProgram(program, qp::planPath(program));
}

} // namespace roundtrip_fixtures
