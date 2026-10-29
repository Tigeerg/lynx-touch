// core/path_circle_pocket.cpp - the circular pocket by a spiral (7.2, 10).
//
// The centre line: the pocket circle shrunk by `inset = D/2 + finish
// allowance`. The turns are spread evenly over its radius, `turns = ceil(radius
// / nominal)` with the nominal step of the rectangular pocket, so the real
// radial step never exceeds `D * kPocketStepOverPct / 100` and the outermost
// turn puts the tool body exactly the allowance away from the wall.
//
// The spiral leaves the centre along +X. The first arc climbs from the centre
// to half the radial step; every next arc carries the radius to the next turn.
// The climbs are half arcs at the mean radius - a spiral piece of constant
// radius, which is what a G2/G3 can express. The last turn is the full circle
// at the outer radius.
//
// Every arc is written on its own and never without an end point: a G2/G3
// without X/Y is not a full circle for the viewer. Its line reader takes the
// target of a line with no axis word of the plane as the current point and
// borrows U/V words from a neighbouring line (a known trap), so a full turn is
// written as two or four half arcs; a real control runs a half arc without a
// special case as well.
#include "core/path_circle_pocket.h"

#include <cmath>
#include <cstdio>

#include "core/path_pocket.h"

namespace qp {
namespace {

constexpr double kEps = 1e-6;
constexpr double kPi = 3.14159265358979323846;

std::string fmt(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    return std::string(buf);
}

// One arc of the spiral: a piece of a circle of `radius` around the pocket
// centre from `startAngle`, `sweep` radians long (positive: counter-clockwise).
// Every arc is a half turn and ends on the +X/-X axis: the first one starts at
// the centre, the next ones start at the +X point of the previous turn, so the
// spiral is a chain of half circles of growing radius and the outer full turn
// closes on the axis.
struct Arc {
    double radius = 0.0;
    double startAngle = 0.0;
    double sweep = 0.0;
};

// One layout, read by the planner, the summary and the warnings.
struct Layout {
    bool fits = false;
    double cx = 0.0;
    double cy = 0.0;
    double inset = 0.0;
    double radius = 0.0;  // centre line radius, mm
    int turns = 0;
    double step = 0.0;    // radial step between two neighbouring turns, mm
    std::vector<double> passes;
    std::vector<Arc> spiral;  // one Z pass, the first arc leaves the centre
    double spiralMm = 0.0;    // arc length of one pass, mm
};

Layout makeLayout(const Program&, const Operation& operation) {
    const CirclePocketOp& pocket = std::get<CirclePocketOp>(operation.geometry);
    Layout out;
    out.cx = pocket.circle.x;
    out.cy = pocket.circle.y;
    out.inset = operation.tool.diameterMm / 2.0 + pocket.finishAllowanceMm;
    out.radius = pocket.circle.r - out.inset;
    // The tool does not fit: the centre line is a point or behind the wall.
    // `validate` names the field and the numbers; the planner only refuses to
    // invent a path.
    if (out.radius <= kEps) { return out; }
    out.fits = true;
    const double nominal =
        std::fmin(operation.tool.diameterMm * kPocketStepOverPct / 100.0, out.radius);
    out.turns = static_cast<int>(std::ceil(out.radius / nominal - kEps));
    out.step = out.radius / static_cast<double>(out.turns);

    // The spiral of one pass, from the centre along +X. The first arc is a half
    // turn at half the first step and ends at the +X point of the first turn;
    // every following arc is a half turn at the mean radius between two turns.
    // The last climb reaches the outer turn, where the full circle of two half
    // arcs closes the pass.
    auto addArc = [&out](double radius, double startAngle) {
        out.spiral.push_back(Arc{radius, startAngle, kPi});
    };
    addArc(out.step / 2.0, 0.0);
    for (int i = 1; i < out.turns; ++i) {
        const double inner = out.step * static_cast<double>(i);
        const double outer = out.step * static_cast<double>(i + 1);
        addArc((inner + outer) / 2.0, 0.0);
    }
    // The full turn at the outer radius, in two half arcs.
    addArc(out.radius, 0.0);
    addArc(out.radius, kPi);
    // The length of the pass is the length of its arcs: one sum, after the
    // whole spiral is known, so a change of the spiral cannot leave the length
    // behind.
    out.spiralMm = 0.0;
    for (const Arc& arc : out.spiral) { out.spiralMm += arc.radius * arc.sweep; }

    const double bottom = operation.topZ - operation.depthMm;
    double z = operation.topZ;
    if (operation.stepDownMm > 0.0) {
        while (z - operation.stepDownMm > bottom + kEps) {
            z -= operation.stepDownMm;
            out.passes.push_back(z);
        }
    }
    out.passes.push_back(bottom);
    return out;
}

// A point of the circle around the pocket centre at `angle`, radians. The sine
// and the cosine of a multiple of pi are rounded values (the sine of pi is
// 1e-16, not zero), so a point that belongs on the axis is put on it exactly:
// a half arc of the toolpath must end at (x, 0), not at (x, 4.6e-16), or the
// I/J of the next arc is measured from a point off the axis.
Point3 pointAt(const Layout& layout, double radius, double angle, double z) {
    double x = std::cos(angle);
    double y = std::sin(angle);
    constexpr double kAxisEps = 1e-12;
    if (std::fabs(x) < kAxisEps) { x = 0.0; }
    if (std::fabs(y) < kAxisEps) { y = 0.0; }
    return Point3{layout.cx + radius * x, layout.cy + radius * y, z};
}

} // namespace

CircleStats circleStats(const Program& program, const Operation& operation) {
    const Layout layout = makeLayout(program, operation);
    CircleStats stats;
    stats.fits = layout.fits;
    stats.turns = layout.turns;
    stats.passes = static_cast<int>(layout.passes.size());
    stats.stepDepthsMm = layout.passes;
    stats.toolRadiusMm = operation.tool.diameterMm / 2.0;
    stats.insetMm = layout.inset;
    if (!layout.fits) { return stats; }
    stats.outerRadiusMm = layout.radius;
    stats.stepMm = layout.step;
    const int arcs = static_cast<int>(layout.spiral.size());
    // Per pass: rapid over the centre at safe Z, rapid down to the top, plunge
    // to the pass Z, one arc per spiral piece and a cut up.
    stats.moves = stats.passes * (4 + arcs);
    // The cutting length of the whole operation, not of one pass. Every pass
    // starts its plunge at the material top: the planner takes the tool up to
    // the top after each pass (a cut up from the centre), so the next pass
    // plunges from the top again, down to its own pass Z. The climb of a pass
    // is the operation depth of the first pass only; later passes descend the
    // whole distance from the top.
    double verticalMm = 0.0;
    for (const double passZ : layout.passes) {
        verticalMm += operation.topZ - passZ;  // the plunge to this pass
        verticalMm += operation.topZ - passZ;  // the cut up to the top
    }
    stats.lengthMm = verticalMm + static_cast<double>(stats.passes) *
                                     (layout.spiralMm + layout.radius);
    return stats;
}

std::vector<std::string> circleWarnings(const Program& program, const Operation& operation) {
    if (!std::holds_alternative<CirclePocketOp>(operation.geometry)) { return {}; }
    std::vector<std::string> out;
    const CirclePocketOp& pocket = std::get<CirclePocketOp>(operation.geometry);
    if (pocket.finishAllowanceMm < -kEps) {
        out.push_back("Припуск " + fmt(pocket.finishAllowanceMm) +
                      " мм отрицательный: траектория выйдет за контур кармана.");
    }
    if (kPocketStepOverPct >= 100.0) {
        out.push_back("Шаг спирали равен диаметру: соседние витки только касаются.");
    }
    // Not blocking: a pocket of one diameter still has a centre line of a
    // point, the check stays in `validate`; this line says why the path is one
    // short arc.
    const Layout layout = makeLayout(program, operation);
    if (layout.fits && layout.turns == 1) {
        out.push_back("Карман только на два диаметра: спираль вырождается в одну дугу.");
    }
    return out;
}

void planCirclePocket(const Program& program, const Operation& operation, PathBuilder& out) {
    const Layout layout = makeLayout(program, operation);
    if (!layout.fits) { return; }
    const double top = operation.topZ;
    const double safeZ = program.safeZ;

    for (const double planeZ : layout.passes) {
        out.add(MoveKind::Rapid, Point3{layout.cx, layout.cy, safeZ});
        out.add(MoveKind::Rapid, Point3{layout.cx, layout.cy, top});
        // The entry in the centre: the only point of the pocket with no wall
        // in the way of a vertical plunge.
        out.add(MoveKind::Cut, Point3{layout.cx, layout.cy, planeZ},
                operation.cutting.plungeFeedMmMin);
        for (const Arc& arc : layout.spiral) {
            const Point3 start = pointAt(layout, arc.radius, arc.startAngle, planeZ);
            const Point3 to = pointAt(layout, arc.radius, arc.startAngle + arc.sweep, planeZ);
            Move move;
            move.kind = MoveKind::Cut;
            move.to = to;
            move.feedMmMin = operation.cutting.feedMmMin;
            move.hasFeed = true;
            move.hasArc = true;
            move.arc.cw = false;
            // I/J are the offset of the arc centre from the start of the arc,
            // as G2/G3 defines them: the pocket centre minus the start point.
            move.arc.i = layout.cx - start.x;
            move.arc.j = layout.cy - start.y;
            // The length of the arc is known here: the radius times the sweep.
            move.arc.lengthMm = arc.radius * arc.sweep;
            out.addArc(move);
        }
        // Out of the material: a cut along the +X axis back to the centre at the
        // pass Z, then a cut up from the centre. The last half arc ends on the
        // axis, so the return line does not cross the wall and the climb out is
        // a vertical cut from the middle of the pocket, not from the wall.
        out.add(MoveKind::Cut, Point3{layout.cx, layout.cy, planeZ},
                operation.cutting.feedMmMin);
        out.add(MoveKind::Cut, Point3{layout.cx, layout.cy, top},
                operation.cutting.plungeFeedMmMin);
    }
}

} // namespace qp
