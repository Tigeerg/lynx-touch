// cli/demo_program.cpp - the document behind `--demo` (7.2).
//
// The face and the pocket of the example share the stock frame of the console:
// 100x80 stock, the top at Z 0, safe Z 5. The cutting data repeats the table
// rows for aluminium, so the summary shows a table source, not a typed one.
#include "cli/demo_program.h"

#include "core/geom.h"
#include "core/ops.h"

namespace qpcli {
namespace {

constexpr qp::Point3 kStockMin{-50.0, -40.0, -20.0};
constexpr qp::Point3 kStockMax{50.0, 40.0, 0.0};

} // namespace

qp::Program demoProgram() {
    qp::Program program;
    program.name = "QUICKPROG DEMO";
    program.wcs = "G54";
    program.stock.min = kStockMin;
    program.stock.max = kStockMax;
    program.safeZ = 5.0;

    qp::Operation face;
    face.id = "Плоскость";
    face.tool.number = 2;
    face.tool.diameterMm = 10.0;
    face.cutting.spindleRpm = 4775.0;
    face.cutting.feedMmMin = 860.0;
    face.cutting.plungeFeedMmMin = 215.0;
    face.topZ = 0.0;
    face.depthMm = 2.0;
    face.stepDownMm = 2.0;
    qp::FaceOp faceGeometry;
    faceGeometry.z = -2.0;
    faceGeometry.stepOverPct = 70.0;
    face.geometry = faceGeometry;
    program.operations.push_back(face);

    qp::Operation pocket;
    pocket.id = "Карман 60x40";
    pocket.tool.number = 3;
    pocket.tool.diameterMm = 8.0;
    pocket.cutting.spindleRpm = 5968.0;
    pocket.cutting.feedMmMin = 859.0;
    pocket.cutting.plungeFeedMmMin = 215.0;
    pocket.topZ = 0.0;
    pocket.depthMm = 6.0;
    pocket.stepDownMm = 2.5;
    qp::RectPocketOp pocketGeometry;
    pocketGeometry.rect = qp::Rect{-30.0, -20.0, 60.0, 40.0};
    pocketGeometry.finishAllowanceMm = 0.2;
    // The demo shows the rounded-corner warning, not a rounded corner.
    pocketGeometry.cornerRadiusMm = 3.0;
    pocket.geometry = pocketGeometry;
    program.operations.push_back(pocket);
    return program;
}

} // namespace qpcli
