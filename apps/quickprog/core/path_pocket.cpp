// core/path_pocket.cpp - the rectangular pocket by a raster (PLAN-QUICKPROG, 7.2, 10).
//
// The raster frame: `rows` run along the long side of the pocket and step
// across the short side. In the pocket frame the long side is `along` and the
// short side is `across`; the frame is mapped back to X/Y at every move, so the
// planner below has no coordinate branches.
//
// The inner boundary is the pocket rectangle shrunk by `inset = D/2 + finish
// allowance` on every side. That inset is what keeps the tool body inside the
// pocket: the centre of the last row is `D/2` from the wall, and the remaining
// `finishAllowance` is the stock the operator asked to leave on the wall. The
// floor carries no allowance: the last Z pass is the final depth. The raster
// covers the inner rectangle exactly:
//
//   rows   = ceil(across / nominal) + 1,  nominal = min(D * stepOverPct / 100, across)
//   step   = across / (rows - 1)
//
// so the real step never exceeds the nominal one, the first and the last row sit
// on the boundary and no band between two rows is wider than the tool body. A
// `rows - 1` of zero (a single row, `across` zero) is one straight cut along the
// long side.
//
// Every Z pass is a set of rows from the top down; the last pass is exactly the
// bottom. A pass starts with a rapid at safe Z over the start of the first row,
// a rapid down to the material top (never below it), a vertical cut at the
// plunge feed down to the pass Z, the raster itself, and a cut up to the top at
// the end of the last row. The rapid between two rows stays at the pass Z over
// the already cut material: it crosses the pocket, not the wall.
#include "core/path_pocket.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace qp {
namespace {

constexpr double kEps = 1e-6;

std::string fmt(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    return std::string(buf);
}

// One digit after the point for the radii of the corner warning: "4.0 мм", the
// same spelling the technologist used in the report.
std::string oneDecimal(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", value);
    return std::string(buf);
}

// One layout, read by the planner, the summary and the warnings.
struct Layout {
    bool fits = false;
    bool alongX = true;
    double inset = 0.0;
    double alongMin = 0.0;  // low edge of the inner rectangle, long side
    double alongMax = 0.0;
    double acrossMin = 0.0; // low edge across, short side
    double acrossMax = 0.0;
    int rows = 0;
    double step = 0.0;
    std::vector<double> passes;
};

Layout makeLayout(const Program&, const Operation& operation) {
    const RectPocketOp& pocket = std::get<RectPocketOp>(operation.geometry);
    const Rect& rect = pocket.rect;
    Layout out;
    out.inset = operation.tool.diameterMm / 2.0 + pocket.finishAllowanceMm;
    out.alongX = rect.w >= rect.h;
    const double along = out.alongX ? rect.w : rect.h;
    const double across = out.alongX ? rect.h : rect.w;
    const double acrossLow = out.alongX ? rect.y : rect.x;
    const double alongLow = out.alongX ? rect.x : rect.y;
    out.alongMin = alongLow + out.inset;
    out.alongMax = alongLow + along - out.inset;
    out.acrossMin = acrossLow + out.inset;
    out.acrossMax = acrossLow + across - out.inset;

    const double innerAlong = along - 2.0 * out.inset;
    const double innerAcross = across - 2.0 * out.inset;

    // The tool does not fit: the inner rectangle is not wider than a point, so
    // there is nowhere to cut. `validate` names the field and the numbers; the
    // planner only refuses to invent a path.
    if (innerAlong < -kEps || innerAcross < -kEps) { return out; }
    out.fits = true;
    // Rows `ceil(innerAcross / nominal) + 1`, so the real step never exceeds
    // the nominal one and the first and the last row stand on the boundary.
    const double nominal = std::fmin(operation.tool.diameterMm * kPocketStepOverPct / 100.0,
                                     innerAcross);
    out.rows = nominal <= kEps
                   ? 1
                   : static_cast<int>(std::ceil(innerAcross / nominal - kEps)) + 1;
    out.step = out.rows > 1 ? innerAcross / static_cast<double>(out.rows - 1) : 0.0;

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

} // namespace

PocketStats pocketStats(const Program& program, const Operation& operation) {
    const Layout layout = makeLayout(program, operation);
    PocketStats stats;
    stats.fits = layout.fits;
    stats.rows = layout.rows;
    stats.passes = static_cast<int>(layout.passes.size());
    stats.stepDepthsMm = layout.passes;
    stats.toolRadiusMm = operation.tool.diameterMm / 2.0;
    stats.insetMm = layout.inset;
    if (!layout.fits) { return stats; }
    stats.innerXMm = layout.alongX ? layout.alongMin : layout.acrossMin;
    stats.innerYMm = layout.alongX ? layout.acrossMin : layout.alongMin;
    stats.innerWidthMm = layout.alongX ? layout.alongMax - layout.alongMin
                                       : layout.acrossMax - layout.acrossMin;
    stats.innerHeightMm = layout.alongX ? layout.acrossMax - layout.acrossMin
                                        : layout.alongMax - layout.alongMin;
    stats.stepMm = layout.step;
    const int passes = stats.passes;
    const int rows = stats.rows;
    // Per pass: rapid over the start at safe Z, rapid down to the top, plunge,
    // `rows` row cuts, `rows - 1` link cuts across the step, cut up to the top,
    // rapid to safe Z. The raster is one continuous cut, so the pass has no
    // rapid inside it: the links go at the pass Z over the removed material.
    stats.moves = passes * (4 + rows + (rows - 1) + 1);
    const double along = layout.alongMax - layout.alongMin;
    const double across = layout.acrossMax - layout.acrossMin;
    // The cutting length of one pass: a cut per row plus the link cuts across
    // the step (each link is the real step long), the plunge and the exit.
    // The vertical moves are named by the moves as well; the number here is
    // comparable with the path's own cutting length.
    stats.lengthMm = static_cast<double>(passes) * (rows * along + (rows - 1) * across) +
                     2.0 * static_cast<double>(passes) * operation.depthMm;
    return stats;
}

std::vector<std::string> pocketWarnings(const Program&, const Operation& operation) {
    if (!std::holds_alternative<RectPocketOp>(operation.geometry)) { return {}; }
    std::vector<std::string> out;
    const RectPocketOp& pocket = std::get<RectPocketOp>(operation.geometry);
    // The raster has no corner rounding: the tool centre stops at the inner
    // rectangle, so every corner keeps the tool radius. The warning names both
    // numbers - the radius the tool leaves and the radius the operator asked
    // for - so the operator sees how far the cut is from the drawing.
    const double toolRadius = operation.tool.diameterMm / 2.0;
    if (pocket.cornerRadiusMm > kEps) {
        out.push_back("скругление угла не выполняется: в углах останется радиус фрезы " +
                      oneDecimal(toolRadius) + " мм (задан радиус " +
                      oneDecimal(pocket.cornerRadiusMm) + " мм).");
    } else if (operation.tool.diameterMm > kEps) {
        // No radius asked for, but a round tool still rounds every corner.
        out.push_back("скругление угла не выполняется: в углах останется радиус фрезы " +
                      oneDecimal(toolRadius) + " мм.");
    }
    if (pocket.finishAllowanceMm < -kEps) {
        out.push_back("Припуск " + fmt(pocket.finishAllowanceMm) +
                      " мм отрицательный: траектория выйдет за контур кармана.");
    }
    if (kPocketStepOverPct >= 100.0) {
        out.push_back("Шаг растра равен диаметру: соседние проходы только касаются.");
    }
    // A warning only: `validate` owns the blocking "tool does not fit" rule,
    // and the console prints it once.
    return out;
}

void planRectPocket(const Program& program, const Operation& operation, PathBuilder& out) {
    const Layout layout = makeLayout(program, operation);
    if (!layout.fits) { return; }
    const double safeZ = program.safeZ;
    const double top = operation.topZ;


    // A point of the pocket frame in machine coordinates.
    const bool alongX = layout.alongX;
    auto point = [alongX](double along, double across, double z) {
        return alongX ? Point3{along, across, z} : Point3{across, along, z};
    };

    for (const double planeZ : layout.passes) {
        // Row 0 starts at the low corner of the inner rectangle. The raster
        // alternates direction, so the start of row i is the low or the high
        // end of the long side.
        const double firstAcross = layout.acrossMin;
        out.add(MoveKind::Rapid, point(layout.alongMin, firstAcross, safeZ));
        out.add(MoveKind::Rapid, point(layout.alongMin, firstAcross, top));
        out.add(MoveKind::Cut, point(layout.alongMin, firstAcross, planeZ),
                operation.cutting.plungeFeedMmMin);
        double along = layout.alongMin;
        double across = firstAcross;
        for (int i = 0; i < layout.rows; ++i) {
            across = layout.acrossMin + layout.step * static_cast<double>(i);
            // The row: a cut along the long side at the pass Z.
            const double target = (i % 2 == 0) ? layout.alongMax : layout.alongMin;
            out.add(MoveKind::Cut, point(target, across, planeZ), operation.cutting.feedMmMin);
            along = target;
            if (i + 1 < layout.rows) {
                const double nextAcross =
                    layout.acrossMin + layout.step * static_cast<double>(i + 1);
                // The link: a short cut across the step at the same Z while it
                // is a step, a rapid over the already cut material for a long
                // one. The demo keeps the pass Z: the link crosses the pocket.
                out.add(MoveKind::Cut, point(along, nextAcross, planeZ),
                        operation.cutting.feedMmMin);
            }
        }
        // Out of the material with a cut up at the end of the last row.
        out.add(MoveKind::Cut, point(along, across, top), operation.cutting.plungeFeedMmMin);
    }
}

} // namespace qp
