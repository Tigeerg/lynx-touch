// tests/checks_pocket_layout.cpp - the rectangular pocket planner and its numbers.
//
// The reference case of PLAN-QUICKPROG, 10: a 60x40 pocket, an 8 mm end mill,
// 0.2 mm allowance, a 2 mm raster step, 6 mm deep in 2.5 mm passes. The numbers
// below are computed by hand and fixed here, so a planner that drifts from the
// arithmetic fails on a number, not on a mood:
//
//   inset     D/2 + allowance            = 4.2 mm
//   inner     width 60 - 8.4, height 40 - 8.4 = 51.6 x 31.6 mm
//   nominal   D * 70 / 100               = 5.6 mm
//   rows      ceil(31.6 / 5.6) + 1 = ceil(5.64) + 1 = 7
//   step      31.6 / 6                   = 5.2666... mm
//   first row y = -20 + 4.2 = -15.8, last row y = 15.8: the centre of the tool
//   body is exactly the inset from the wall, so the body touches the wall minus
//   the allowance.
//
// 6 mm in 2.5 mm steps: 2.5, 5, then 6 (the last pass is exactly the bottom).
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_pocket.h"
#include "core/validate.h"

#include "tests/checks.h"
#include "tests/checks_pocket_ops.h"
#include "tests/harness.h"

namespace pocket_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::demoProgram;
using qpcheck::near;

// The Y values of the raster rows at the deepest pass, in the order they are
// cut. A row is a long cut at a constant Z along X; the link between two rows
// is a shorter cut along Y. Only the rows of the deepest pass count.
std::vector<double> deepestRows(const qp::Path& planned) {
    bool havePlane = false;
    double minZ = 0.0;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Cut && (!havePlane || m.to.z < minZ)) {
            minZ = m.to.z;
            havePlane = true;
        }
    }
    std::vector<double> ys;
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        const qp::Move& prev = planned.moves[i - 1];
        if (m.kind != qp::MoveKind::Cut || !near(m.to.z, minZ) || !near(prev.to.z, minZ)) { continue; }
        // A row changes X by the inner width; a link changes Y by one step.
        if (std::fabs(m.to.x - prev.to.x) < 10.0) { continue; }
        if (ys.empty() || !near(ys.back(), m.to.y)) { ys.push_back(m.to.y); }
    }
    return ys;
}

// The rows of the deepest pass: the first and the last sit on the inner
// boundary, the neighbours are closer than the tool diameter, so no band is
// left uncut.
void checkPocketRows() {
    const qp::Program program = pocketProgram();
    const qp::Path planned = qp::planPath(program);
    const std::vector<double> ys = deepestRows(planned);
    check(static_cast<int>(ys.size()) == 7, "pocket.last_pass_rows",
          "the deepest pass must sweep 7 rows, got " + std::to_string(ys.size()));
    check(!ys.empty() && near(ys.front(), -15.8), "pocket.first_row_wall",
          "the first row must sit 4.2 mm inside the low wall Y=-20, got " +
              (ys.empty() ? std::string("no row") : std::to_string(ys.front())));
    check(!ys.empty() && near(ys.back(), 15.8), "pocket.last_row_wall",
          "the last row must sit 4.2 mm inside the high wall Y=+20, got " +
              (ys.empty() ? std::string("no row") : std::to_string(ys.back())));

    const double diameter = program.operations.front().tool.diameterMm;
    int wideGap = 0;
    for (std::size_t i = 0; i + 1 < ys.size(); ++i) {
        if (ys[i + 1] - ys[i] > diameter + 1e-9) { ++wideGap; }
    }
    check(wideGap == 0, "pocket.no_wide_strip",
          "a band between two rows is wider than the tool: " + std::to_string(wideGap) + " gap(s)");

    // The rows alternate direction: the raster is a zigzag, not a one-way pass
    // with a return leg outside the material.
    bool alternates = true;
    double previousEndX = std::nan("");
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        const qp::Move& prev = planned.moves[i - 1];
        if (m.kind != qp::MoveKind::Cut || !near(m.to.z, -6.0) || !near(prev.to.z, -6.0)) { continue; }
        if (std::fabs(m.to.x - prev.to.x) < 10.0) { continue; }
        if (!std::isnan(previousEndX) && near(m.to.x, previousEndX)) { alternates = false; }
        previousEndX = m.to.x;
    }
    check(alternates, "pocket.zigzag", "two neighbouring rows must run in opposite directions");
}

// Every cut stays inside the pocket shrunk by the inset: the tool body never
// crosses the wall minus the allowance. No rapid goes below the material top.
void checkPocketBounds() {
    const qp::Program program = pocketProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::PocketStats stats = qp::pocketStats(program, program.operations.front());
    const qp::Rect& rect = std::get<qp::RectPocketOp>(program.operations.front().geometry).rect;
    const double xMin = rect.x + stats.insetMm - 1e-6;
    const double xMax = rect.x + rect.w - stats.insetMm + 1e-6;
    const double yMin = rect.y + stats.insetMm - 1e-6;
    const double yMax = rect.y + rect.h - stats.insetMm + 1e-6;
    bool inside = true;
    bool rapidSafe = true;
    double deepestZ = 0.0;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Cut) {
            if (m.to.x < xMin || m.to.x > xMax || m.to.y < yMin || m.to.y > yMax) { inside = false; }
            deepestZ = std::fmin(deepestZ, m.to.z);
        } else if (m.to.z < program.operations.front().topZ - 1e-6) {
            rapidSafe = false;
        }
    }
    check(inside, "pocket.cuts_inside",
          "a cutting move left the pocket shrunk by D/2 + allowance");
    check(rapidSafe, "pocket.rapid_above_top", "a rapid went below the material top");
    check(near(deepestZ, -6.0), "pocket.deepest_cut", "the deepest cut must be the bottom -6");
}

// The entry into the material is a vertical cut at the plunge feed on the edge
// of the pocket; the exit is a cut up. No rapid meets the material: a rapid
// ends at the top, the descent below it is a cut.
void checkPocketEntry() {
    const qp::Program program = pocketProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::Operation& op = program.operations.front();
    int plungedPasses = 0;
    bool plungeFeed = true;
    bool entryInZone = true;
    const qp::PocketStats stats = qp::pocketStats(program, op);
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        const qp::Move& prev = planned.moves[i - 1];
        if (m.kind != qp::MoveKind::Cut) { continue; }
        // A plunge: a vertical cut downwards from the material top.
        if (near(m.to.x, prev.to.x) && near(m.to.y, prev.to.y) && m.to.z < prev.to.z &&
            near(prev.to.z, op.topZ)) {
            ++plungedPasses;
            if (!near(m.feedMmMin, op.cutting.plungeFeedMmMin)) { plungeFeed = false; }
            if (m.to.x < -30.0 + stats.insetMm - 1e-6 || m.to.x > 30.0 - stats.insetMm + 1e-6 ||
                m.to.y < -20.0 + stats.insetMm - 1e-6 || m.to.y > 20.0 - stats.insetMm + 1e-6) {
                entryInZone = false;
            }
        }
        // A rapid that lands below the top is a rapid into the material.
        if (m.kind == qp::MoveKind::Rapid && m.to.z < op.topZ - 1e-6) { plungeFeed = false; }
    }
    check(plungedPasses == stats.passes, "pocket.one_plunge_per_pass",
          "every Z pass must start with one vertical plunge, got " + std::to_string(plungedPasses) +
              " of " + std::to_string(stats.passes));
    check(plungeFeed, "pocket.plunge_feed",
          "the plunge must use the plunge feed and no rapid may enter the material");
    check(entryInZone, "pocket.plunge_in_zone",
          "the plunge must stand on the edge of the pocket inside the cutting zone");

    // Between two passes the tool goes up to safe Z with a rapid: after the
    // exit cut it stands at the top, then a rapid takes it to safe Z.
    bool safeBetweenPasses = false;
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        if (m.kind == qp::MoveKind::Rapid && near(m.to.z, program.safeZ)) { safeBetweenPasses = true; }
    }
    check(safeBetweenPasses, "pocket.rapid_to_safe", "the tool must go to safe Z between the passes");
}

} // namespace pocket_checks

// Runs the raster cases of the reference pocket: the rows of the deepest pass,
// the bounds of every cut and the entry into the material.
void runPocketRasterChecks() {
    pocket_checks::checkPocketRows();
    pocket_checks::checkPocketBounds();
    pocket_checks::checkPocketEntry();
}
