// core/path_face.cpp - the face milling planner (PLAN-QUICKPROG, 7.2, 10).
//
// Strategy: strips along the long side of the stock, laid across the short
// side at `stepOverPct` % of the tool diameter. "Along the long side" means the
// stock dimension the strips run in: for a stock longer in X the strips sweep
// X and step in Y (the usual case), for a stock longer in Y the role swaps.
// The strip grid covers the stock from both edges: the first centre sits half a
// diameter inside one edge and the last half a diameter inside the opposite
// one, so the tool body reaches both edges with no uncut rim. The strips sweep
// `leadMm` (10 % of D, clamped to 1..D/2 mm) past both ends to clear the corner
// and add no runout across the strips: the face covers exactly the width.
//
// Every strip is an open slot cut down from the material top, so each strip
// starts from the material top: the tool rapids over the strip start at safe
// Z, rapids down to the top, cuts down into the plane at the plunge feed, cuts
// the strip at the working feed, cuts back up to the top, then rapids to the
// next strip. After the last strip of a pass the tool rapids to safe Z and the
// next pass repeats the strips one step deeper. The rapid therefore always
// ends at the material top, never below it.
//
// Finish allowance (`finishAllowanceMm` of the pocket kinds) is NOT handled as
// a separate finishing pass in the MVP: the face cuts the plane in the given
// passes in one go. A finishing pass for any kind is a later slice and is
// named in the report of this slice.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/ops.h"
#include "core/path.h"

namespace qp {
namespace {

constexpr double kOverrunShare = 0.10;  // edge lead, share of D
constexpr double kMinLeadMm = 1.0;      // the lead is never shorter than this
constexpr double kThinPassShare = 0.10; // a last pass below 10 % of the step
constexpr double kEps = 1e-6;

// Everything the planner and the summary read, computed once: strips across
// the stock, their coordinates, the axial passes from the top down. Sides
// (edge, side) say which stock dimension the overrun is counted along: the
// strip direction and the step direction are known only after the layout picks
// the long side.
struct Layout {
    bool stripsAlongX = true;   // strips sweep X and step in Y
    double edgeOverrunMm = 0.0; // along the strip, per side
    double sideOverrunMm = 0.0; // across the strips, per side: face adds none
    double stripStepMm = 0.0;   // real step, usable / (strips - 1)
    int strips = 0;
    double stripStart = 0.0;    // across coordinate of the first strip
    std::vector<double> passes; // memory Z of each pass, top down
};

// `share` of the diameter, clamped to [min, D/2]: a fraction below the floor
// still clears the corner, a share above the radius would hand the centre over
// the stock edge.
double clampLead(double diameterMm, double share, double minMm) {
    const double half = diameterMm / 2.0;
    return std::fmin(std::fmax(diameterMm * share, std::fmin(minMm, half)), half);
}

Layout makeLayout(const Program& program, const Operation& operation) {
    const FaceOp& face = std::get<FaceOp>(operation.geometry);
    const double d = operation.tool.diameterMm;
    const double half = d / 2.0;
    const double sizeX = program.stock.max.x - program.stock.min.x;
    const double sizeY = program.stock.max.y - program.stock.min.y;

    Layout out;
    out.stripsAlongX = sizeX >= sizeY;
    out.edgeOverrunMm = clampLead(d, kOverrunShare, kMinLeadMm);
    out.sideOverrunMm = 0.0;

    const double across = out.stripsAlongX ? sizeY : sizeX;
    const double acrossMin = out.stripsAlongX ? program.stock.min.y : program.stock.min.x;
    // The centres span `usable`: from half a tool inside one edge to half a
    // tool inside the other. That span is what the strips have to cover; a
    // nominal `D * stepOver` is the coarsest step allowed, so the count is the
    // ceiling of the span over it plus the two edge strips.
    const double usable = across - d;
    // A 100 % step leaves no overlap; the count is still the ceiling, so the
    // strips meet exactly at their bodies and no gap opens.
    const double nominal = std::fmin(std::fmax(d * (face.stepOverPct / 100.0), kEps), d);
    if (usable <= kEps) {
        // The tool is at least as wide as the stock: one strip through the
        // centre covers the width (it is wider than the stock).
        out.strips = 1;
        out.stripStart = acrossMin + across / 2.0;
        out.stripStepMm = 0.0;
    } else {
        out.strips = static_cast<int>(std::ceil(usable / nominal - kEps)) + 1;
        // The real step never exceeds the nominal one and spreads the span so
        // the last strip lands exactly half a tool inside the far edge.
        out.stripStepMm = usable / static_cast<double>(out.strips - 1);
        out.stripStart = acrossMin + half;
    }

    // The passes follow the plane the operator set (`depth` below `topZ`), not
    // the stock top: a light pass above the top is allowed and the check says
    // so. The last pass is exactly the bottom, never a rounding tail below it.
    const double bottom = operation.topZ - operation.depthMm;
    const double stepDown = operation.stepDownMm;
    double z = operation.topZ;
    if (stepDown > 0.0) {
        while (z - stepDown > bottom + kEps) {
            z -= stepDown;
            out.passes.push_back(z);
        }
    }
    out.passes.push_back(bottom);
    return out;
}

std::string fmt(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", value);
    return std::string(buf);
}

std::vector<std::string> layoutWarnings(const Program& program, const Operation& operation,
                                        const Layout& layout) {
    std::vector<std::string> out;
    const FaceOp& face = std::get<FaceOp>(operation.geometry);
    // A zero overlap is a legal layout - the strip bodies only touch - but it
    // leaves a ridge on any real stock, so the operator hears about it.
    if (face.stepOverPct >= 100.0) {
        out.push_back("Перекрытие полос нулевое при шаге " + fmt(face.stepOverPct) +
                      " %: соседние полосы только касаются, между ними останется гребешок.");
    }
    // The tool is at least as wide as the stock: one strip through the centre
    // cuts the whole width, but the operator asked for a tool wider than the
    // part. The blocking check covers the Y width; this line names the case
    // where the width across the strips is not covered by the count.
    const double across = layout.stripsAlongX ? program.stock.max.y - program.stock.min.y
                                              : program.stock.max.x - program.stock.min.x;
    if (operation.tool.diameterMm >= across - kEps) {
        out.push_back("Фреза " + fmt(operation.tool.diameterMm) + " мм шире заготовки " + fmt(across) +
                      " мм: одна полоса по центру режет всю ширину.");
    }
    if (layout.passes.size() >= 2) {
        const std::size_t n = layout.passes.size();
        const double last = layout.passes[n - 2] - layout.passes[n - 1];
        if (last < operation.stepDownMm * kThinPassShare) {
            out.push_back("Тонкий последний проход: " + fmt(last) + " мм меньше 10 % шага " +
                          fmt(operation.stepDownMm) + " мм.");
        }
    }
    const double top = std::fmax(program.stock.min.z, program.stock.max.z);
    if (operation.topZ > top + kEps) {
        out.push_back("Плоскость " + fmt(operation.topZ) + " выше верха заготовки " + fmt(top) +
                      " мм: часть проходов не режет.");
    }
    return out;
}

} // namespace

FaceStats faceStats(const Program& program, const Operation& operation) {
    const Layout layout = makeLayout(program, operation);
    FaceStats stats;
    stats.strips = layout.strips;
    stats.passes = static_cast<int>(layout.passes.size());
    stats.stepDepthsMm = layout.passes;
    stats.toolRadiusMm = operation.tool.diameterMm / 2.0;
    stats.stripStepMm = layout.stripStepMm;
    stats.edgeOverrunMm = layout.edgeOverrunMm;
    stats.sideOverrunMm = layout.sideOverrunMm;

    const double alongLimit =
        (layout.stripsAlongX ? program.stock.max.x - program.stock.min.x
                             : program.stock.max.y - program.stock.min.y) + 2.0 * layout.edgeOverrunMm;
    // How far below the material top the cutting descends, mm: the deepest
    // pass, which is the axial step or the whole depth when it is shorter.
    stats.leadMm = std::fmin(operation.depthMm, operation.stepDownMm);
    // Moves per strip: rapid to safe Z over the start, rapid down to the top,
    // plunge, strip cut, ramp up. Plus a rapid to safe Z for the pass.
    stats.moves = layout.strips * 5 * static_cast<int>(layout.passes.size()) +
                  static_cast<int>(layout.passes.size());
    // Each strip: the plunge and the ramp over `lead`, the strip itself.
    stats.lengthMm = static_cast<double>(layout.strips * static_cast<int>(layout.passes.size())) *
                     (alongLimit + 2.0 * stats.leadMm);
    return stats;
}

std::vector<std::string> faceWarnings(const Program& program, const Operation& operation) {
    if (!std::holds_alternative<FaceOp>(operation.geometry)) { return {}; }
    return layoutWarnings(program, operation, makeLayout(program, operation));
}

void planFace(const Program& program, const Operation& operation, PathBuilder& out) {
    const Layout layout = makeLayout(program, operation);
    const bool alongX = layout.stripsAlongX;
    const double alongMin = alongX ? program.stock.min.x : program.stock.min.y;
    const double alongMax = alongX ? program.stock.max.x : program.stock.max.y;
    const double start = alongMin - layout.edgeOverrunMm;
    const double end = alongMax + layout.edgeOverrunMm;
    const double safeZ = program.safeZ;

    // A point in the strip frame: `along` sweeps the strip, `across` steps
    // between strips. The rotation keeps the planner free of coordinate
    // branches below.
    auto point = [alongX](double along, double across, double z) {
        return alongX ? Point3{along, across, z} : Point3{across, along, z};
    };

    double lastAcross = layout.stripStart;
    for (const double planeZ : layout.passes) {
        for (int i = 0; i < layout.strips; ++i) {
            lastAcross = layout.stripStart + i * layout.stripStepMm;
            // Rapid over the strip start and down to the material top: the
            // rapid never goes below the top, the descent into the material is
            // a cut at the plunge feed.
            out.add(MoveKind::Rapid, point(start, lastAcross, safeZ));
            out.add(MoveKind::Rapid, point(start, lastAcross, operation.topZ));
            out.add(MoveKind::Cut, point(start, lastAcross, planeZ),
                    operation.cutting.plungeFeedMmMin);
            out.add(MoveKind::Cut, point(end, lastAcross, planeZ), operation.cutting.feedMmMin);
            // Ramp back to the top at the plunge feed; the next strip starts
            // with a rapid from here.
            out.add(MoveKind::Cut, point(end, lastAcross, operation.topZ),
                    operation.cutting.plungeFeedMmMin);
        }
        // After the last strip of the pass the tool leaves the material.
        out.add(MoveKind::Rapid, point(end, lastAcross, safeZ));
    }
}

} // namespace qp
