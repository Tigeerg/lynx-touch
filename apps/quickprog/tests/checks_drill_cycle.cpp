// tests/checks_drill_cycle.cpp - the drilling cycle and its R plane (10).
//
// G81 when the depth is at most three diameters and no peck is asked for;
// G83 with Q for a deeper hole or an explicit peck; G80 closes the cycle after
// the last hole, not only in the modal block of the header.
//
// The R plane is checked against the rule, not against itself: with no R in the
// operation the cycle stands 1 mm above the material top (core/path_drill.cpp,
// kRiseMm), so a change of that millimetre moves the case. The plane is read
// from the emitted R word and from the drilling stats, and both are compared
// with topZ + 1 - a comparison of `stats.rMm` with `stats.rMm` would pass for
// any rise.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/drill_options.h"
#include "core/emit.h"
#include "core/emit_internal.h"
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

namespace {

// The rule of the default R plane: one millimetre above the material top. The
// literal is repeated here on purpose - the case has to fail when the planner
// changes the rise, so it must not read the planner's own constant.
constexpr double kDefaultRiseMm = 1.0;

// The lines of the file, split once: the cycle cases look for the last cycle
// block and the G80 after it.
std::vector<std::string> linesOf(const std::string& gcode) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < gcode.size()) {
        const std::size_t end = gcode.find('\n', at);
        lines.push_back(
            gcode.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos) { break; }
        at = end + 1;
    }
    return lines;
}

// True when a line carries the cycle word as a word of its own: " G81 " inside
// the block, not the "G81" of a longer code.
bool hasCycleWord(const std::string& line, const std::string& cycle) {
    return line.find(" " + cycle + " ") != std::string::npos ||
           (line.size() >= cycle.size() + 1 &&
            line.compare(line.size() - cycle.size() - 1, cycle.size() + 1, " " + cycle) == 0);
}

// The verdict of the check: G80 is a block of its own after the last G81/G83
// and before the end of the operation (M9/M5). The header block
// "G21 G90 G94 G17 G40 G49 G80" does not count: it is not a block of its own.
bool cycleClosedAfterLast(const std::string& gcode, std::string& reason) {
    const std::vector<std::string> lines = linesOf(gcode);
    std::size_t lastCycle = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (hasCycleWord(lines[i], "G81") || hasCycleWord(lines[i], "G83")) { lastCycle = i; }
    }
    if (lastCycle == lines.size()) {
        reason = "no G81/G83 block in the file";
        return false;
    }
    for (std::size_t i = lastCycle + 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.find("M9") != std::string::npos || line.find("M5") != std::string::npos) {
            reason = "the operation ended at block '" + line + "' without a G80 after cycle block '" +
                     lines[lastCycle] + "'";
            return false;
        }
        if (line.find(" G80") != std::string::npos) {
            // A block of its own: "N210 G80", no other word on the line.
            const std::size_t space = line.find(' ');
            const std::size_t second = line.find(' ', space + 1);
            const bool ownBlock = second == std::string::npos && line.compare(space + 1, 3, "G80") == 0;
            if (!ownBlock) {
                reason = "G80 is not a block of its own: '" + line + "'";
                return false;
            }
            return true;
        }
    }
    reason = "no G80 after the last cycle block '" + lines[lastCycle] + "'";
    return false;
}

// The R word of the last cycle block: the number after " R".
bool lastRword(const std::string& gcode, double& value) {
    const std::vector<std::string> lines = linesOf(gcode);
    for (std::size_t i = lines.size(); i-- > 0;) {
        const std::size_t at = lines[i].find(" R");
        if (at != std::string::npos && (hasCycleWord(lines[i], "G81") || hasCycleWord(lines[i], "G83"))) {
            value = std::atof(lines[i].c_str() + at + 2);
            return true;
        }
    }
    return false;
}

} // namespace

// The cycle: G81 for a shallow hole, G83 with Q for a deep one, G80 closes the
// cycle after the last hole.
void checkCycles() {
    // 12 mm through a 6 mm drill: two diameters, G81.
    const qp::Program shallow = gridProgram();
    const std::string shallowG = qp::emitProgram(shallow, qp::planPath(shallow));
    check(contains(shallowG, " G81 "), "drill.g81", "a 12 mm hole in a 6 mm drill must use G81");
    check(!contains(shallowG, "G83"), "drill.no_g83_for_shallow",
          "a shallow hole must not be pecked");
    check(contains(shallowG, "Z-12.000"), "drill.depth", "the hole must be 12 mm deep");
    std::string shallowWhy;
    check(cycleClosedAfterLast(shallowG, shallowWhy), "drill.g80",
          "the cycle must be closed with G80 after the last hole: " + shallowWhy);

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
    std::string deepWhy;
    check(cycleClosedAfterLast(deepG, deepWhy), "drill.g83_g80",
          "the deep cycle must be closed with G80 after the last hole: " + deepWhy);

    // A shallow hole with an explicit peck: G83 with the asked Q.
    qp::Program pecked = gridProgram();
    std::get<qp::DrillGridOp>(pecked.operations.front().geometry).params.peckMm = 2.5;
    const std::string peckG = qp::emitProgram(pecked, qp::planPath(pecked));
    check(contains(peckG, "G83") && contains(peckG, "Q2.500"), "drill.explicit_peck",
          "an explicit peck must select G83 with its own Q");
}

// The R plane: the rule is 1 mm above the material top when the operation does
// not set it. Both the G81 and the G83 cycle are checked, and the value is read
// from the emitted R word and from the drilling stats.
void checkRPlane() {
    const qp::Program program = gridProgram();
    const qp::Operation& op = program.operations.front();
    const double expected = op.topZ + kDefaultRiseMm;
    const qp::DrillStats stats = qp::drillStats(program, op);
    check(near(stats.rMm, expected), "drill.r_rule",
          "a drill without R must stand " + std::to_string(kDefaultRiseMm) +
              " mm above the top: expected R" + std::to_string(expected) + ", got R" +
              std::to_string(stats.rMm));
    check(stats.rMm < program.safeZ, "drill.r_below_safe",
          "the R plane must be below the safe Z");
    const std::string shallowG = qp::emitProgram(program, qp::planPath(program));
    double shallowR = 0.0;
    check(lastRword(shallowG, shallowR) && near(shallowR, expected), "drill.r_word_g81",
          "the G81 block must carry R equal to top + " + std::to_string(kDefaultRiseMm) + " mm");
    check(contains(shallowG, "R" + qp::coord(stats.rMm)), "drill.rapid_to_r",
          "the cycle must name the R plane of the rule");

    // The same rule for the deep hole: G83 without a typed R.
    qp::Program deep = gridProgram();
    deep.operations.front().depthMm = 24.0;
    deep.operations.front().stepDownMm = 24.0;
    const std::string deepG = qp::emitProgram(deep, qp::planPath(deep));
    double deepR = 0.0;
    check(lastRword(deepG, deepR) && near(deepR, expected), "drill.r_word_g83",
          "the G83 block must carry R equal to top + " + std::to_string(kDefaultRiseMm) + " mm");

    // No rapid of the cycle descends below R.
    const qp::Path planned = qp::planPath(program);
    bool rapidBelowR = false;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Rapid && m.to.z < stats.rMm - 1e-6) { rapidBelowR = true; }
    }
    check(!rapidBelowR, "drill.rapid_not_below_r", "a rapid of the cycle descended below R");

    // An R typed by the operation wins over the rule.
    qp::Program typed = gridProgram();
    qp::DrillParams& params = std::get<qp::DrillGridOp>(typed.operations.front().geometry).params;
    params.hasR = true;
    params.rMm = 3.0;
    check(near(qp::drillStats(typed, typed.operations.front()).rMm, 3.0), "drill.r_typed",
          "a typed R above the top must be used as typed");
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
    check(contains(gcode, " G81 "), "drill.emit_g81", "the single hole must use G81");
}

} // namespace drill_checks

// Runs the cycle cases: G81/G83, the R plane and the single hole.
void runDrillCycleChecks() {
    drill_checks::checkCycles();
    drill_checks::checkRPlane();
    drill_checks::checkSingleHole();
}
