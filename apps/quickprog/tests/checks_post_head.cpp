// tests/checks_post_head.cpp - G64, the pocket step and their rules (7.3).
//
// The path tolerance of the header after the work offset, an explicit pair of
// numbers and the two rules of a tolerance: positive and not over a millimetre.
// The pocket step of the document changes the number of raster rows; its own
// rule holds the step between 10 and 100 % of the diameter.
#include <cstddef>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_circle_pocket.h"
#include "core/path_pocket.h"
#include "core/validate.h"

#include "tests/checks.h"
#include "tests/checks_pocket_ops.h"
#include "tests/harness.h"

namespace post_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::faceProgram;
using qpcheck::near;

namespace {

bool hasField(const std::vector<qp::Check>& checks, const std::string& field) {
    for (const qp::Check& one : checks) {
        if (one.field == field) { return true; }
    }
    return false;
}

} // namespace

// G64 P0.01 Q0.01 after the work offset, before the first motion.
void checkG64Header() {
    const qp::Program program = faceProgram();
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    check(contains(gcode, " G64 P0.010 Q0.010\n"), "post.g64_header",
          "the header must carry G64 P0.010 Q0.010");
    const std::size_t wcs = gcode.find(" G54\n");
    const std::size_t g64 = gcode.find(" G64 ");
    const std::size_t firstPark = gcode.find("G53 G0 Z0\n");
    check(wcs != std::string::npos && g64 != std::string::npos && g64 > wcs, "post.g64_after_wcs",
          "G64 must stand after the work offset");
    check(g64 < firstPark, "post.g64_before_park",
          "G64 must stand before the first motion of the operation");

    qp::Program typed = program;
    typed.g64P = 0.05;
    typed.g64Q = 0.02;
    const std::string text = qp::emitProgram(typed, qp::planPath(typed));
    check(contains(text, " G64 P0.050 Q0.020\n"), "post.g64_values",
          "the document values must be written, got no P0.050 Q0.020");
}

// The G64 rule: zero is "every corner", over a millimetre is "the tool leaves
// the contour"; both are refused with the field of the word.
void checkG64Rule() {
    qp::Program zero = faceProgram();
    zero.g64P = 0.0;
    const std::vector<qp::Check> badP = qp::validate(zero);
    check(hasField(badP, "g64P"), "post.g64_zero_p",
          "P = 0 must fail on g64P, got " + std::to_string(badP.size()) + " checks");

    qp::Program wide = faceProgram();
    wide.g64Q = 1.5;
    const std::vector<qp::Check> badQ = qp::validate(wide);
    check(hasField(badQ, "g64Q"), "post.g64_over_q", "Q over 1 mm must fail on g64Q");

    qp::Program edge = faceProgram();
    edge.g64P = 1.0;
    check(qp::validate(edge).empty(), "post.g64_one_mm", "1 mm is still inside the rule");
}

// The step of the pocket comes from the operation: 50 % gives more rows than
// 70 % on the same rectangle.
void checkPocketStep() {
    const qp::Program wide = pocket_checks::pocketProgram();
    const qp::PocketStats at70 = qp::pocketStats(wide, wide.operations.front());
    qp::Program fine = pocket_checks::pocketProgram();
    std::get<qp::RectPocketOp>(fine.operations.front().geometry).stepOverPct = 50.0;
    const qp::PocketStats at50 = qp::pocketStats(fine, fine.operations.front());
    check(at70.rows == 7 && at50.rows == 9, "post.step_rows",
          "a 60x40 pocket with D8 needs 7 rows at 70 % and 9 at 50 %, got " +
              std::to_string(at70.rows) + " and " + std::to_string(at50.rows));
    check(at50.stepMm < at70.stepMm, "post.step_finer",
          "a smaller step must give a finer raster");

    // The spiral of the round pocket reads the same field.
    const qp::Program circle = faceProgram();
    qp::Program ring = circle;
    qp::Operation op = circle.operations.front();
    op.tool.diameterMm = 6.0;
    op.tool.number = 3;
    qp::CirclePocketOp pocket;
    pocket.circle = qp::Circle{0.0, 0.0, 15.0};
    op.geometry = pocket;
    ring.operations = {op};
    qp::Program spiral = ring;
    std::get<qp::CirclePocketOp>(spiral.operations.front().geometry).stepOverPct = 50.0;
    check(qp::circleStats(spiral, spiral.operations.front()).turns >
              qp::circleStats(ring, ring.operations.front()).turns,
          "post.step_circle", "the spiral must take a smaller step from the same field");
}

// The step rule: under 10 % the tool rubs, over 100 % there are holes.
void checkPocketStepRule() {
    qp::Program fine = pocket_checks::pocketProgram();
    std::get<qp::RectPocketOp>(fine.operations.front().geometry).stepOverPct = 5.0;
    check(hasField(qp::validate(fine), "stepOver"), "post.step_small",
          "a 5 % step must fail on stepOver");

    qp::Program wide = pocket_checks::pocketProgram();
    std::get<qp::RectPocketOp>(wide.operations.front().geometry).stepOverPct = 120.0;
    check(hasField(qp::validate(wide), "stepOver"), "post.step_wide",
          "a 120 % step must fail on stepOver");

    qp::Program ring = faceProgram();
    qp::Operation op = ring.operations.front();
    qp::CirclePocketOp pocket;
    pocket.circle = qp::Circle{0.0, 0.0, 15.0};
    pocket.stepOverPct = 5.0;
    op.geometry = pocket;
    ring.operations = {op};
    check(hasField(qp::validate(ring), "stepOver"), "post.step_circle_small",
          "the same rule must guard the round pocket");
}

// The H row: negative is refused, an explicit row of the same tool is fine.
void checkHRule() {
    qp::Program bad = faceProgram();
    bad.operations.front().tool.hNumber = -1;
    check(hasField(qp::validate(bad), "tool.hNumber"), "post.h_negative",
          "H = -1 must fail on tool.hNumber");
    qp::Program ok = faceProgram();
    ok.operations.front().tool.hNumber = 7;
    check(qp::validate(ok).empty(), "post.h_same_tool_ok",
          "a row of the same tool is a valid document");
    check(ok.operations.front().tool.effectiveH() == 7 &&
              faceProgram().operations.front().tool.effectiveH() == 2,
          "post.h_effective", "the effective row is the explicit one, else the tool number");
}

} // namespace post_checks

// Runs the G64, the pocket step and the field rules.
void runPostHeadChecks() {
    post_checks::checkG64Header();
    post_checks::checkG64Rule();
    post_checks::checkPocketStep();
    post_checks::checkPocketStepRule();
    post_checks::checkHRule();
}
