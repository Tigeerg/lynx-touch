// tests/checks_post_emit.cpp - G43 H and the M1 stop of the emitted file (7.3).
//
// The length compensation after a tool change, its default row, an explicit
// `--h` and the pause between two operations. The cases read the bytes of the
// file, not the document: the order of the blocks is the contract of the
// reference post, and a block written in the wrong place is a crash on the
// stand.
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

// Two operations with two different tools: the second one changes, so the file
// carries one more park, one more G43 and one M1.
qp::Program twoTools() {
    qp::Program program = faceProgram();
    qp::Operation second = faceOp();
    second.id = "Второй проход";
    second.tool.number = 3;
    second.tool.diameterMm = 8.0;
    second.depthMm = 1.0;
    second.stepDownMm = 1.0;
    program.operations.push_back(second);
    return program;
}

// The type of the operation the case edited: a copy, so the fixtures stay the
// same for the next case.
qp::Program withH(int h) {
    qp::Program program = faceProgram();
    program.operations.front().tool.hNumber = h;
    return program;
}

} // namespace

// G43 stands after the tool change of every operation that changed the tool:
// the default row is the tool number (N55 G43 Z15. H2 of the reference file).
void checkG43Default() {
    const qp::Program program = faceProgram();
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    check(contains(gcode, " G43 Z5.000 H2\n"), "post.g43_after_m6",
          "the tool change of T2 must be followed by G43 Z<safe> H2");
    const std::size_t at6 = gcode.find(" T2 M6\n");
    const std::size_t at43 = gcode.find(" G43 Z");
    check(at6 != std::string::npos && at43 > at6, "post.g43_order",
          "G43 must come after T.. M6");
    check(count(gcode, " G43 ") == 1, "post.g43_count",
          "one tool change is one G43, got " + std::to_string(count(gcode, " G43 ")));

    // Two tools: one G43 per change, each with its own row.
    const std::string two = qp::emitProgram(twoTools(), qp::planPath(twoTools()));
    check(contains(two, " G43 Z5.000 H3\n"), "post.g43_second_tool",
          "the second tool must get G43 with its own H");
    check(count(two, " G43 ") == 2, "post.g43_two_tools",
          "two tool changes need two G43 blocks");
}

// An explicit H row wins over the tool number, and the same tool with another
// row prints G43 again: another row is another length offset.
void checkG43Explicit() {
    qp::Program program = withH(7);
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    check(contains(gcode, " H7\n"), "post.h_explicit",
          "H7 of the document must be written as H7");
    check(!contains(gcode, " H2\n"), "post.h_no_tool_number",
          "an explicit H7 must not also print the tool number");

    qp::Program same = faceProgram();
    same.operations.push_back(faceOp());
    same.operations[1].tool.hNumber = 4;
    same.operations[1].id = "Другая коррекция";
    const std::string twice = qp::emitProgram(same, qp::planPath(same));
    check(count(twice, " M6") == 1, "post.h_same_tool_no_m6",
          "a row change of the same tool must not repeat T/M6");
    check(count(twice, " G43 ") == 2, "post.h_row_change_g43",
          "a change of the H row of the same tool must print G43 again");
    check(contains(twice, " H4\n"), "post.h_row_value",
          "the second G43 must carry the new row");
}

// M1 before every operation after the first, and never before the first one.
void checkStopBetweenOps() {
    const qp::Program program = twoTools();
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    check(count(gcode, "M1\n") == 1, "post.m1_one_stop",
          "two operations need exactly one M1, got " + std::to_string(count(gcode, "M1\n")));
    const std::size_t m1 = gcode.find("M1\n");
    const std::size_t firstOp = gcode.find("(UI_OP:");
    const std::size_t secondOp = gcode.find("(UI_OP: Второй проход)");
    check(m1 != std::string::npos && m1 > firstOp && m1 < secondOp, "post.m1_before_second_op",
          "M1 must stand between the two UI_OP tags");
    check(gcode.find("M1\n") > gcode.find(" M5\n"), "post.m1_after_m5",
          "the stop comes after M5 of the previous operation, not inside it");

    qp::Program off = program;
    off.stopAfterOp = false;
    const std::string quiet = qp::emitProgram(off, qp::planPath(off));
    check(count(quiet, "M1\n") == 0, "post.m1_off", "stopAfterOp off must remove every M1");
}

} // namespace post_checks

// Runs the G43 and the M1 cases.
void runPostEmitChecks() {
    post_checks::checkG43Default();
    post_checks::checkG43Explicit();
    post_checks::checkStopBetweenOps();
}
