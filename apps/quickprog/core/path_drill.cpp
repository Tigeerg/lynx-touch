// core/path_drill.cpp - hole centres, the cycle and its planes (7.2, 10).
//
// The hole centres are the whole geometry of drilling: the cycle expands on the
// control, so the planner lays out the centres, picks the cycle (G81 or G83)
// and the R and depth planes, and the emitter writes the cycle words.
//
// Grid: `cols` points along X and `rows` along Y from (x0, y0), row by row in
// X. Circle: `count` points on `circle.r` around the centre, the first at
// `startDeg` from +X, then clockwise. With `fullTurn` false the points cover
// `arcDeg` degrees, the last point included.
//
// The R plane: `kRiseMm` above the material top, clamped to safe Z from above
// and to the top from below - a cycle never rapid-descends into the material.
#include "core/path_drill.h"

#include <cmath>
#include <variant>
#include <cstdio>

namespace qp {
namespace {

constexpr double kRiseMm = 1.0;  // R plane above the material top, mm
constexpr double kEps = 1e-6;
constexpr double kPeckToDiameter = 3.0;  // depth over this many diameters pecks
constexpr double kPi = 3.14159265358979323846;

std::string fmt(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    return std::string(buf);
}

std::vector<Point3> gridPoints(const DrillGridOp& grid, std::vector<std::string>& errors) {
    std::vector<Point3> out;
    if (grid.cols <= 0 || grid.rows <= 0) {
        errors.push_back("Сетка отверстий: число по X и по Y должно быть больше нуля, а задано " +
                         std::to_string(grid.cols) + " и " + std::to_string(grid.rows) + ".");
        return out;
    }
    for (int row = 0; row < grid.rows; ++row) {
        for (int col = 0; col < grid.cols; ++col) {
            out.push_back(Point3{grid.x0 + grid.dx * static_cast<double>(col),
                                 grid.y0 + grid.dy * static_cast<double>(row), 0.0});
        }
    }
    return out;
}

std::vector<Point3> ringPoints(const DrillCircleOp& ring, std::vector<std::string>& errors) {
    std::vector<Point3> out;
    if (ring.count <= 0) {
        errors.push_back("Круг отверстий: число отверстий должно быть больше нуля, а задано " +
                         std::to_string(ring.count) + ".");
        return out;
    }
    if (ring.circle.r <= 0.0) {
        errors.push_back("Круг отверстий: радиус окружности " + fmt(ring.circle.r) +
                         " мм должен быть больше нуля.");
        return out;
    }
    const double start = ring.startDeg * kPi / 180.0;
    // A full turn spreads `count` holes on the closed circle: the last step
    // brings the ring back to the first hole, so the step is 360 / count. An
    // arc with `count` holes has `count - 1` intervals inside it.
    const double step = ring.fullTurn ? 2.0 * kPi / static_cast<double>(ring.count)
                                      : (ring.arcDeg * kPi / 180.0) /
                                            static_cast<double>(ring.count - 1 > 0 ? ring.count - 1 : 1);
    for (int i = 0; i < ring.count; ++i) {
        const double angle = start - step * static_cast<double>(i);  // clockwise
        // A point on the axis: the sine of a multiple of pi is a rounded value
        // (the sine of pi is 1e-16, not zero), so a point that belongs on an
        // axis is put on it exactly and the file has no "-0.000".
        double cx = std::cos(angle);
        double sy = std::sin(angle);
        constexpr double kAxisEps = 1e-12;
        if (std::fabs(cx) < kAxisEps) { cx = 0.0; }
        if (std::fabs(sy) < kAxisEps) { sy = 0.0; }
        out.push_back(Point3{ring.circle.x + ring.circle.r * cx,
                             ring.circle.y + ring.circle.r * sy, 0.0});
    }
    return out;
}

} // namespace

namespace {

// The drilling parameters of whichever of the three drill kinds the operation
// holds: the material of the drill body lives there (ops.h). A non-drill has
// none, and its caller never asks for it.
const DrillParams* drillParams(const Operation& operation) {
    if (const auto* hole = std::get_if<DrillHoleOp>(&operation.geometry)) { return &hole->params; }
    if (const auto* grid = std::get_if<DrillGridOp>(&operation.geometry)) { return &grid->params; }
    if (const auto* ring = std::get_if<DrillCircleOp>(&operation.geometry)) { return &ring->params; }
    return nullptr;
}

} // namespace

Feeds drillFeeds(const Operation& operation) {
    // The stock material and the drill body material are both in the
    // operation: the row of the table follows the document, not a constant.
    const DrillParams* params = drillParams(operation);
    const DrillBody body = params == nullptr ? DrillBody::Hss : params->kind;
    return feedsForDrill(operation.material, operation.tool.diameterMm, body);
}

Feeds tableRowFor(const Operation& operation) {
    if (isDrill(operation.geometry)) { return drillFeeds(operation); }
    return feedsFor(operation.material, operation.tool.diameterMm, ToolKind::Flat);
}

double drillFeed(const Operation& operation) {
    // A typed feed of the form is the drill feed too: a drill has no separate
    // plunge, so the one number is both. A zero falls back to the table.
    return operation.cutting.feedMmMin > 0.0 ? operation.cutting.feedMmMin
                                             : drillFeeds(operation).feedMmMin;
}

bool isDrill(const OperationGeometry& geometry) {
    return std::holds_alternative<DrillHoleOp>(geometry) ||
           std::holds_alternative<DrillGridOp>(geometry) ||
           std::holds_alternative<DrillCircleOp>(geometry);
}

std::vector<Point3> drillHoles(const Operation& operation) {
    std::vector<std::string> ignored;
    if (const auto* hole = std::get_if<DrillHoleOp>(&operation.geometry)) {
        return {Point3{hole->hole.x, hole->hole.y, operation.topZ}};
    }
    if (const auto* grid = std::get_if<DrillGridOp>(&operation.geometry)) {
        return gridPoints(*grid, ignored);
    }
    if (const auto* ring = std::get_if<DrillCircleOp>(&operation.geometry)) {
        return ringPoints(*ring, ignored);
    }
    return {};
}

DrillStats drillStats(const Program& program, const Operation& operation) {
    DrillStats stats;
    double askedR = 0.0;
    bool hasAskedR = false;
    if (const auto* hole = std::get_if<DrillHoleOp>(&operation.geometry)) {
        stats.peckMm = hole->params.peckMm;
        askedR = hole->params.rMm;
        hasAskedR = hole->params.hasR;
    } else if (const auto* grid = std::get_if<DrillGridOp>(&operation.geometry)) {
        stats.peckMm = grid->params.peckMm;
        askedR = grid->params.rMm;
        hasAskedR = grid->params.hasR;
        gridPoints(*grid, stats.errors);
    } else if (const auto* ring = std::get_if<DrillCircleOp>(&operation.geometry)) {
        stats.peckMm = ring->params.peckMm;
        askedR = ring->params.rMm;
        hasAskedR = ring->params.hasR;
        ringPoints(*ring, stats.errors);
    }
    stats.points = drillHoles(operation);
    // The cycle: an asked peck wins; otherwise a hole deeper than three
    // diameters pecks, a shallower one is drilled in one go.
    const bool deep = operation.depthMm > kPeckToDiameter * operation.tool.diameterMm + 1e-9;
    const bool peck = stats.peckMm > 0.0 || deep;
    stats.cycle = peck ? DrillKind::Peck : DrillKind::Simple;
    if (peck) {
        // The default peck is one diameter: the usual "clear the chips every
        // diameter" rule of a twist drill, kept as an explicit number so the
        // form can show it and the operator can change it.
        if (stats.peckMm <= 0.0) { stats.peckMm = operation.tool.diameterMm; }
    } else {
        stats.peckMm = 0.0;
    }
    // The R plane: a value of the document at or above the top is used as
    // typed (clamped to safe Z from above); zero or a value below the top means
    // "the default" - one millimetre above the material top, below safe Z. A
    // cycle that rapid-descends below the top would meet the material.
    const double defaultR = std::fmax(std::fmin(operation.topZ + kRiseMm, program.safeZ),
                                      operation.topZ);
    if (hasAskedR && askedR >= operation.topZ) {
        stats.rMm = std::fmin(askedR, std::fmax(program.safeZ, operation.topZ));
    } else {
        stats.rMm = defaultR;
    }
    stats.depthMm = operation.topZ - operation.depthMm;
    return stats;
}

void planDrill(const Program& program, const Operation& operation, PathBuilder& out) {
    const DrillStats stats = drillStats(program, operation);
    if (stats.points.empty()) { return; }
    // The moves are the real travel of the tool between the holes: the cycle
    // itself is written by the emitter from the same DrillStats, so the plan
    // does not carry a second copy of the pecking. A move to the hole at safe Z
    // is the real rapid; the drill-to-depth is a cycle, not a move, so it has
    // no place in the list and the cutting length of the operation is the feed
    // drilling of the holes, counted by the summary, not by the move chords.
    for (const Point3& hole : stats.points) {
        out.add(MoveKind::Rapid, Point3{hole.x, hole.y, program.safeZ});
    }
}

} // namespace qp
