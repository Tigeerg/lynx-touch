// cli/operation_program.cpp - the documents of the four `--op` kinds (7.2).
//
// Every builder follows the same skeleton: name and work offset, the stock frame
// around the given top, the safe Z, the operation header with the tool, the top,
// the depth and the step, then the geometry of the kind and the cutting data. A
// typed feed or speed wins over the table; a zero means "take the table value",
// which is how the help text phrases the defaults.
#include "cli/operation_program.h"

#include "core/feeds.h"

namespace qpcli {
namespace {

// The stock grows around its own centre so the frame stays symmetric, the top
// of the stock is the given Z.
void setStock(qp::Program& program, double sizeX, double sizeY, double sizeZ, double topZ) {
    program.stock.min = {-sizeX / 2.0, -sizeY / 2.0, topZ - sizeZ};
    program.stock.max = {sizeX / 2.0, sizeY / 2.0, topZ};
}

} // namespace

qp::Program faceProgram(const qp::FaceArgs& args) {
    qp::Program program;
    program.name = args.name;
    program.wcs = "G54";
    setStock(program, args.stockX, args.stockY, args.stockZ, args.topZ);
    program.safeZ = args.safeGiven ? args.safeZ : args.topZ + 5.0;

    qp::Operation op;
    op.id = "Плоскость";
    op.tool.number = args.tool;
    op.tool.diameterMm = args.diameter;
    op.topZ = args.topZ;
    op.depthMm = args.depth;
    op.stepDownMm = args.stepDown;
    qp::FaceOp face;
    face.z = args.planeGiven ? args.planeZ : args.topZ - args.depth;
    face.stepOverPct = args.stepOverPct;
    op.geometry = face;

    const qp::Feeds table = qp::feedsFor(args.material, args.diameter, qp::ToolKind::Flat);
    op.cutting.spindleRpm = args.rpm > 0.0 ? args.rpm : table.rpm;
    op.cutting.feedMmMin = args.feed > 0.0 ? args.feed : table.feedMmMin;
    op.cutting.plungeFeedMmMin = args.plunge > 0.0 ? args.plunge : table.plungeFeedMmMin;
    program.operations.push_back(op);
    return program;
}

qp::Program pocketProgram(const qp::PocketArgs& args) {
    qp::Program program;
    program.name = args.name;
    program.wcs = "G54";
    setStock(program, args.stockX, args.stockY, args.stockZ, args.topZ);
    program.safeZ = args.safeGiven ? args.safeZ : args.topZ + 5.0;

    qp::Operation op;
    op.id = "Карман";
    op.tool.number = args.tool;
    op.tool.diameterMm = args.diameter;
    op.topZ = args.topZ;
    op.depthMm = args.depth;
    op.stepDownMm = args.stepDown;
    qp::RectPocketOp pocket;
    pocket.rect = qp::Rect{args.x, args.y, args.width, args.height};
    pocket.cornerRadiusMm = args.cornerRadius;
    pocket.finishAllowanceMm = args.finishAllowance;
    op.geometry = pocket;

    const qp::Feeds table = qp::feedsFor(args.material, args.diameter, qp::ToolKind::Flat);
    op.cutting.spindleRpm = args.rpm > 0.0 ? args.rpm : table.rpm;
    op.cutting.feedMmMin = args.feed > 0.0 ? args.feed : table.feedMmMin;
    op.cutting.plungeFeedMmMin = args.plunge > 0.0 ? args.plunge : table.plungeFeedMmMin;
    program.operations.push_back(op);
    return program;
}

qp::Program circlePocketProgram(const qp::CirclePocketArgs& args) {
    qp::Program program;
    program.name = args.name;
    program.wcs = "G54";
    setStock(program, args.stockX, args.stockY, args.stockZ, args.topZ);
    program.safeZ = args.safeGiven ? args.safeZ : args.topZ + 5.0;

    qp::Operation op;
    op.id = "Круглый карман";
    op.tool.number = args.tool;
    op.tool.diameterMm = args.diameter;
    op.topZ = args.topZ;
    op.depthMm = args.depth;
    op.stepDownMm = args.stepDown;
    qp::CirclePocketOp pocket;
    pocket.circle = qp::Circle{args.x, args.y, args.diameterPocket / 2.0};
    pocket.finishAllowanceMm = args.finishAllowance;
    op.geometry = pocket;

    const qp::Feeds table = qp::feedsFor(args.material, args.diameter, qp::ToolKind::Flat);
    op.cutting.spindleRpm = args.rpm > 0.0 ? args.rpm : table.rpm;
    op.cutting.feedMmMin = args.feed > 0.0 ? args.feed : table.feedMmMin;
    op.cutting.plungeFeedMmMin = args.plunge > 0.0 ? args.plunge : table.plungeFeedMmMin;
    program.operations.push_back(op);
    return program;
}

qp::Program drillProgram(const qp::DrillArgs& args) {
    qp::Program program;
    program.name = args.name;
    program.wcs = "G54";
    setStock(program, args.stockX, args.stockY, args.stockZ, args.topZ);
    program.safeZ = args.safeGiven ? args.safeZ : args.topZ + 5.0;

    qp::Operation op;
    op.id = "Отверстия";
    op.tool.number = args.tool;
    op.tool.diameterMm = args.diameter;
    op.topZ = args.topZ;
    op.depthMm = args.depth;
    // A drilling cycle has no axial pass to split: the step stays zero and the
    // check treats a zero step of a drill as "not used".
    op.stepDownMm = 0.0;
    const qp::DrillParams params{args.peck, args.r, args.rGiven};
    if (args.pattern == qp::DrillPattern::Grid) {
        qp::DrillGridOp grid;
        grid.x0 = args.x;
        grid.y0 = args.y;
        grid.cols = args.cols;
        grid.rows = args.rows;
        grid.dx = args.dx;
        grid.dy = args.dy;
        grid.params = params;
        op.geometry = grid;
    } else if (args.pattern == qp::DrillPattern::Circle) {
        qp::DrillCircleOp ring;
        ring.circle = qp::Circle{args.x, args.y, args.radius};
        ring.count = args.count;
        ring.startDeg = args.startDeg;
        ring.arcDeg = args.arcDeg;
        ring.fullTurn = args.fullTurn;
        ring.params = params;
        op.geometry = ring;
    } else {
        qp::DrillHoleOp hole;
        hole.hole = qp::Circle{args.x, args.y, args.diameter / 2.0};
        hole.params = params;
        op.geometry = hole;
    }

    const qp::Feeds table = qp::feedsFor(args.material, args.diameter, qp::ToolKind::Drill);
    op.cutting.spindleRpm = args.rpm > 0.0 ? args.rpm : table.rpm;
    op.cutting.feedMmMin = args.feed > 0.0 ? args.feed : table.feedMmMin;
    // A drill has one feed: the plunge is the working feed of the cycle, and
    // the model keeps the two equal instead of a hidden quarter.
    op.cutting.plungeFeedMmMin = op.cutting.feedMmMin;
    program.operations.push_back(op);
    return program;
}

} // namespace qpcli
