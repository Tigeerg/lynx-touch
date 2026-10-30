// tests/checks_post_parking.cpp - the machine park G53 G0 Z0 (7.3).
//
// The park stands before every tool change and at the start of the program,
// and the footer parks before M30. A single operation with one tool changes
// nothing after the first block: no second park, no M6. The cases count the
// blocks and check their order, because a park written after M6 is the same as
// no park at all.
#include <cstddef>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace post_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::faceOp;
using qpcheck::faceProgram;

namespace {

int parkCount(const std::string& gcode) { return count(gcode, "G53 G0 Z0\n"); }

} // namespace

// One operation, one tool: the park of the program start, the park of the
// footer, no M6 after the first change.
void checkSingleOp() {
    const qp::Program program = faceProgram();
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    check(parkCount(gcode) == 2, "park.start_and_footer",
          "one operation needs the start park and the footer park, got " +
              std::to_string(parkCount(gcode)));
    check(count(gcode, " M6") == 1, "park.single_change",
          "one tool is one tool change, got " + std::to_string(count(gcode, " M6")));
    const std::size_t park = gcode.find("G53 G0 Z0\n");
    const std::size_t m6 = gcode.find(" M6");
    check(park != std::string::npos && m6 != std::string::npos && park < m6, "park.before_start_m6",
          "the start park must stand before the first T.. M6");
    // The footer park is the block right before M30: the next newline after
    // the park is the newline of the M30 line itself.
    const std::size_t lastPark = gcode.rfind("G53 G0 Z0\n");
    const std::size_t m30 = gcode.find(" M30\n");
    check(lastPark != std::string::npos && m30 != std::string::npos &&
              gcode.find("\n", lastPark + std::string("G53 G0 Z0\n").size()) ==
                  gcode.find("\n", m30),
          "park.before_m30", "the footer block must be the line right before M30");
}

// Two tools: the park before each change, so the first block of the change is
// the machine home, not the safe Z of the part.
void checkTwoTools() {
    qp::Program program = faceProgram();
    qp::Operation second = faceOp();
    second.id = "Второй проход";
    second.tool.number = 3;
    second.tool.diameterMm = 8.0;
    second.depthMm = 1.0;
    second.stepDownMm = 1.0;
    program.operations.push_back(second);
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    check(parkCount(gcode) == 3, "park.two_tools",
          "two tools need the start park, the change park and the footer park, got " +
              std::to_string(parkCount(gcode)));
    check(count(gcode, " M6") == 2, "park.two_changes",
          "two tools need two changes, got " + std::to_string(count(gcode, " M6")));
    const std::size_t park2 = gcode.find("G53 G0 Z0\n", gcode.find("G53 G0 Z0\n") + 1);
    const std::size_t m6second = gcode.find("T3 M6");
    check(park2 != std::string::npos && m6second != std::string::npos && park2 < m6second,
          "park.before_change", "the second park must stand before T3 M6");
    // The park is a line of its own, not a word glued to the change: a control
    // reads the machine word before the change or not at all.
    check(contains(gcode, "\nN") && !contains(gcode, "G53 G0 Z0 T"), "park.own_block",
          "the park must be a block of its own");

    // The same tool twice: no park for the second operation, no M6.
    qp::Program same = faceProgram();
    same.operations.push_back(faceOp());
    same.operations[1].id = "Второй проход";
    const std::string once = qp::emitProgram(same, qp::planPath(same));
    check(parkCount(once) == 2, "park.same_tool",
          "the same tool must not park again, got " + std::to_string(parkCount(once)));
    check(count(once, " M6") == 1, "park.same_tool_no_m6",
          "the same tool must not repeat M6");
}

} // namespace post_checks

// Runs the park cases.
void runPostParkingChecks() {
    post_checks::checkSingleOp();
    post_checks::checkTwoTools();
}
