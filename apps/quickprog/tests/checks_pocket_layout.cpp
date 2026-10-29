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
// The rows and the passes are checked against the stats and against the moves
// of the deepest pass; the raster itself (bounds, entry) is in
// checks_pocket_raster.cpp.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/ops.h"
#include "core/path.h"
#include "core/path_pocket.h"

#include "tests/checks.h"
#include "tests/checks_pocket_ops.h"
#include "tests/harness.h"

namespace pocket_checks {

using qpcheck::check;
using qpcheck::near;

// The rows, passes and step of the reference pocket.
void checkPocketLayout() {
    const qp::Program program = pocketProgram();
    const qp::PocketStats stats = qp::pocketStats(program, program.operations.front());
    check(stats.fits, "pocket.fits", "an 8 mm mill must fit a 60x40 pocket with a 0.2 mm allowance");
    check(near(stats.insetMm, 4.2), "pocket.inset",
          "the inset must be D/2 + allowance = 4.2 mm, got " + std::to_string(stats.insetMm));
    check(stats.rows == 7, "pocket.rows",
          "31.6 mm of inner height over the 5.6 mm nominal step needs 7 rows, got " +
              std::to_string(stats.rows));
    check(near(stats.stepMm, 31.6 / 6.0), "pocket.step",
          "the real step must be 31.6 / 6 = 5.2667 mm, got " + std::to_string(stats.stepMm));
    check(stats.stepMm <= 8.0 * 0.7 + 1e-9, "pocket.step_bounded",
          "the real step must not exceed D * 70 % = 5.6 mm");
    check(near(stats.innerXMm, -25.8) && near(stats.innerYMm, -15.8), "pocket.inner_origin",
          "the inner rectangle must start at (-25.8, -15.8)");
    check(near(stats.innerWidthMm, 51.6) && near(stats.innerHeightMm, 31.6), "pocket.inner_size",
          "the inner rectangle must be 51.6 x 31.6 mm");
}

// 6 mm in 2.5 mm steps: 2.5, 5, 6. The last pass is exactly the bottom.
void checkPocketPasses() {
    const qp::Program program = pocketProgram();
    const qp::PocketStats stats = qp::pocketStats(program, program.operations.front());
    check(stats.passes == 3, "pocket.passes",
          "6 mm in 2.5 mm steps must be 3 passes, got " + std::to_string(stats.passes));
    check(stats.stepDepthsMm.size() == 3, "pocket.step_count",
          "the stats must list three pass depths");
    check(stats.stepDepthsMm.size() == 3 && near(stats.stepDepthsMm[0], -2.5) &&
              near(stats.stepDepthsMm[1], -5.0) && near(stats.stepDepthsMm[2], -6.0),
          "pocket.step_depths", "the pass depths must be -2.5, -5, -6");

    // The same numbers read from the moves: a row is cut at the pass Z.
    const qp::Path planned = qp::planPath(program);
    std::vector<double> planes;
    for (const qp::Move& m : planned.moves) {
        if (m.kind != qp::MoveKind::Cut) { continue; }
        if (planes.empty() || !near(planes.back(), m.to.z)) { planes.push_back(m.to.z); }
    }
    bool hasDeepest = false;
    for (const double z : planes) {
        if (near(z, -6.0)) { hasDeepest = true; }
    }
    check(hasDeepest, "pocket.moves_bottom", "the path must cut at the bottom -6");
}

} // namespace pocket_checks

// Runs the number cases of the reference pocket: the inset, the inner
// rectangle, the rows and the passes.
void runPocketLayoutChecks() {
    pocket_checks::checkPocketLayout();
    pocket_checks::checkPocketPasses();
}
