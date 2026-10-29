// tests/checks_drill_cycle.cpp - the drilling cycle and its R plane (10).
//
// G81 when the depth is at most three diameters and no peck is asked for;
// G83 with Q for a deeper hole or an explicit peck; G80 closes the cycle.
// The R plane stays above the top and no rapid of the cycle descends below it.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/drill_options.h"
#include "core/emit.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_drill.h"
#include "core/serialize.h"
#include "core/validate.h"

#include "tests/checks.h"
#include "tests/checks_drill_ops.h"
#include "tests/harness.h"

namespace drill_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::demoProgram;
using qpcheck::near;


// The cycle: G81 for a shallow hole, G83 with Q for a deep one, G80 closes the
// cycle after the last hole.
void checkCycles() {
    // 12 mm through a 6 mm drill: two diameters, G81.
    const qp::Program shallow = gridProgram();
    const std::string shallowG = qp::emitProgram(shallow, qp::planPath(shallow));
    check(contains(shallowG, "G81"), "drill.g81", "a 12 mm hole in a 6 mm drill must use G81");
    check(!contains(shallowG, "G83"), "drill.no_g83_for_shallow",
          "a shallow hole must not be pecked");
    check(contains(shallowG, "G80"), "drill.g80", "the cycle must be closed with G80");
    check(contains(shallowG, "Z-12.000"), "drill.depth", "the hole must be 12 mm deep");
    check(contains(shallowG, "R1.000"), "drill.r_plane",
          "the R plane must be 1 mm above the top");

    // 24 mm is four diameters: G83 with the default peck Q = D = 6.
    qp::Program deep = gridProgram();
    deep.operations.front().depthMm = 24.0;
    deep.operations.front().stepDownMm = 24.0;
    const qp::DrillStats deepStats = qp::drillStats(deep, deep.operations.front());
    check(deepStats.cycle == qp::DrillKind::Peck && near(deepStats.peckMm, 6.0), "drill.g83_auto",
          "a hole deeper than three diameters must be pecked with Q = D");
    const std::string deepG = qp::emitProgram(deep, qp::planPath(deep));
    check(contains(deepG, "G83"), "drill.g83", "a deep hole must use G83");
    check(contains(deepG, "Q6.000"), "drill.q_word", "G83 must carry Q6.000, the tool diameter");
    check(contains(deepG, "G80"), "drill.g83_g80", "the deep cycle must be closed with G80");

    // A shallow hole with an explicit peck: G83 with the asked Q.
    qp::Program pecked = gridProgram();
    std::get<qp::DrillGridOp>(pecked.operations.front().geometry).params.peckMm = 2.5;
    const std::string peckG = qp::emitProgram(pecked, qp::planPath(pecked));
    check(contains(peckG, "G83") && contains(peckG, "Q2.500"), "drill.explicit_peck",
          "an explicit peck must select G83 with its own Q");
}

// The R plane and rapids: R is above the top and never below it; a rapid of a
// cycle does not descend below R.
void checkRPlane() {
    const qp::Program program = gridProgram();
    const qp::DrillStats stats = qp::drillStats(program, program.operations.front());
    check(stats.rMm > program.operations.front().topZ + 1e-9, "drill.r_above_top",
          "the R plane must be above the material top");
    check(stats.rMm < program.safeZ, "drill.r_below_safe",
          "the R plane must be below the safe Z");
    const qp::Path planned = qp::planPath(program);
    bool rapidBelowR = false;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Rapid && m.to.z < stats.rMm - 1e-6) { rapidBelowR = true; }
    }
    check(!rapidBelowR, "drill.rapid_not_below_r", "a rapid of the cycle descended below R");
}

// The single hole: one cycle, one point, the tool change before it.
void checkSingleHole() {
    qp::Program program = demoProgram();
    qp::DrillHoleOp hole;
    hole.hole = qp::Circle{-20.0, 15.0, 3.0};
    program.operations.push_back(drillOp(hole, 6.0, 10.0));
    const qp::DrillStats stats = qp::drillStats(program, program.operations.front());
    check(stats.points.size() == 1 && near(stats.points.front().x, -20.0) &&
              near(stats.points.front().y, 15.0),
          "drill.single_point", "one hole must give one point at its centre");
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    check(contains(gcode, " T5 M6\n"), "drill.emit_tool", "the drill must call T5 M6");
    check(contains(gcode, "(UI_TOOL: T=5 TYPE=DRILL D=6.)"), "drill.emit_type",
          "the tool tag must say DRILL");
    check(contains(gcode, "G81"), "drill.emit_g81", "the single hole must use G81");
}

} // namespace drill_checks

// Runs the cycle cases: G81/G83, the R plane and the single hole.
void runDrillCycleChecks() {
    drill_checks::checkCycles();
    drill_checks::checkRPlane();
    drill_checks::checkSingleHole();
}
