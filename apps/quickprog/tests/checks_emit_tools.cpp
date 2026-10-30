// tests/checks_emit_tools.cpp - the tool change and the multi-operation file (7.1).
//
// The rule of the preamble: a new tool parks and repeats T/M6, the same tool
// keeps its T and starts a new spindle speed; two operations keep the block
// numbers growing and carry their own stage identifiers.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace emit_checks {

using namespace qpcheck;

using qpcheck::axisSlot;
using qpcheck::axisValue;
using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::demoProgram;
using qpcheck::faceOp;
using qpcheck::faceProgram;
using qpcheck::hasWord;
using qpcheck::ModalState;
using qpcheck::moveBlocks;
using qpcheck::near;
using qpcheck::setAxis;


// Two operations in one document: the numbers continue, the tool block of the
// second carries its own tool and the first operation is closed before it.
void checkTwoOperations() {
    qp::Program program = faceProgram();
    qp::Operation second = faceOp();
    second.id = "Второй проход";
    second.tool.number = 3;
    second.tool.diameterMm = 8.0;
    second.cutting.spindleRpm = 5968.0;
    second.cutting.feedMmMin = 859.0;
    second.cutting.plungeFeedMmMin = 215.0;
    second.depthMm = 1.0;
    second.stepDownMm = 1.0;
    program.operations.push_back(second);
    const qp::Path planned = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, planned);
    check(gcode.find("(UI_OP: Второй проход)") > gcode.find("(UI_OP: Плоскость)"),
          "emit.two_ops_order", "the second operation must follow the first");
    check(contains(gcode, "(UI_TOOL: T=3 TYPE=FLAT D=8.)"), "emit.two_ops_tool",
          "the second operation must carry its own tool tag");
    check(contains(gcode, " T3 M6"), "emit.two_ops_change", "the second operation must change to T3");
    check(contains(gcode, "path-") && contains(gcode, "/op2"), "emit.two_ops_span",
          "the spans of two operations must be distinct");
}

// The tool-change rule of PLAN-QUICKPROG, 7.1: a new tool repeats T/M6 and
// parks first; the same tool keeps its T, but a new spindle speed starts a new
// S/M3. The rule lives in one place of the emitter, so the cases read the file.
void checkToolChangeRule() {
    const qp::Program program = faceProgram();
    qp::Operation same = faceOp();
    same.id = "Второй проход";
    same.cutting.spindleRpm = 6000.0;
    qp::Program second = program;
    second.operations.push_back(same);
    const std::string gcode = qp::emitProgram(second, qp::planPath(second));
    check(count(gcode, " M6") == 1, "toolchange.same_tool_no_m6",
          "the same tool must not repeat T/M6, got " + std::to_string(count(gcode, " M6")));
    check(count(gcode, " M3\n") == 2, "toolchange.same_tool_spindle",
          "a new spindle speed must start with S/M3 again");
    check(count(gcode, "M8\n") == 2, "toolchange.same_tool_coolant",
          "M9 ends the first operation, the second asks for the coolant again");

    qp::Program changed = program;
    qp::Operation other = faceOp();
    other.id = "Другая фреза";
    other.tool.number = 7;
    other.tool.diameterMm = 8.0;
    changed.operations.push_back(other);
    const std::string gcode2 = qp::emitProgram(changed, qp::planPath(changed));
    check(count(gcode2, " M6") == 2, "toolchange.new_tool_m6",
          "two tools in one file need two tool changes");
    check(count(gcode2, " M3\n") == 2, "toolchange.new_tool_spindle",
          "two operations need two spindle starts");
    // The park comes before the change (PLAN-QUICKPROG, 7.3): a block of its
    // own at the machine home, so M6 never finds the tool inside the material.
    const std::size_t park = gcode2.rfind("G53 G0 Z0\n", gcode2.find("T7 M6"));
    check(park != std::string::npos && park < gcode2.find("T7 M6"), "toolchange.park_before_change",
          "the change must be parked at the machine home before M6");
}

} // namespace emit_checks

// Runs the tool-change cases of two operations in one file.
void runEmitToolsChecks() {
    emit_checks::checkTwoOperations();
    emit_checks::checkToolChangeRule();
}
