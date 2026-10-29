// tests/checks_emit_moves.cpp - the moves of the .ngc (7.1).
//
// The cuts and the rapids, the feed words and the coordinates that may not
// repeat: a move block is checked against the modal state of the file, not
// against a guessed pair of lines.
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


// The emitter writes G1 with F on cuts and G0 without F on rapids, and does
// not repeat an unchanged coordinate.
void checkEmitMoves() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, planned);
    check(contains(gcode, " G1 "), "emit.cut_g1", "a cutting move must be G1");
    check(contains(gcode, "F860"), "emit.cut_feed", "the working feed 860 must appear in a cut block");
    check(contains(gcode, "F215"), "emit.plunge_feed", "the plunge feed 215 must appear in a plunge block");
    check(contains(gcode, "G0 Z5."), "emit.rapid_safe", "after the operation the tool must go to safe Z");
    check(contains(gcode, "G0 X"), "emit.rapid_exists", "rapids must exist");
    check(!contains(gcode, "G0 Z-"), "emit.rapid_above_top", "a rapid must not go below the stock top");
    // No coordinate repeats on consecutive move lines: a line with a word
    // that did not change is a wasted coordinate. Checked over every move
    // line of the program, not on one guessed pair.
    const std::vector<std::string> moveLines = moveBlocks(gcode);
    // A coordinate that did not change has to be absent from the line. The
    // block before a move is not always a move (the first move follows a
    // spindle or a coolant word), so the check is on the value of the axis:
    // the word is present in the line, the block stands at that value already.
    bool repeat = false;
    ModalState state;
    for (const std::string& one : moveLines) {
        for (const char axis : {'X', 'Y', 'Z'}) {
            if (!hasWord(one, axis)) { continue; }
            const int slot = axisSlot(axis);
            const double value = axisValue(one, axis);
            // A repeated value is a bug, except the parking Z of a tool
            // change: the printer starts the modal Z from safe Z and the
            // change block parks the tool before M6, so the same Z twice is
            // the contract of PLAN-QUICKPROG, 7.1, not a lost coordinate.
            const bool toolChange = one.find(" M6") != std::string::npos;
            if (state.has[slot] && near(value, state.value[slot]) && !(toolChange && axis == 'Z')) {
                repeat = true;
            }
            setAxis(state, slot, value);
        }
        // A line that names no X and no Y leaves both unknown: the printer
        // prints "X.. Y.." side by side from the first move of a block on, so
        // a line without them says nothing about the plane position.
        if (!hasWord(one, 'X') && !hasWord(one, 'Y')) {
            state.has[0] = false;
            state.has[1] = false;
        }
    }
    check(!moveLines.empty(), "emit.move_lines", "the file has no move lines");
    check(!repeat, "emit.no_repeat_xy", "a move block repeated an unchanged X, Y or Z");
    check(contains(gcode, "(UI_OP: Плоскость)"), "emit.ui_op", "the operation tag UI_OP is missing");
    check(contains(gcode, "(UI_TOOL: T=2 TYPE=FLAT D=10.)"), "emit.ui_tool",
          "the UI_TOOL tag must carry number, type and diameter");
    check(contains(gcode, " T2 M6\n"), "emit.tool_change", "the block must call the tool T2 M6");
    check(contains(gcode, "S4775. M3\n"), "emit.spindle_on", "the spindle must start at the table speed");
    check(contains(gcode, "M8"), "emit.coolant_on", "the coolant must start before the first block");
    check(contains(gcode, "M9"), "emit.coolant_off", "the coolant must stop after the operation");
    check(contains(gcode, "M5"), "emit.spindle_off", "the spindle must stop after the operation");
}

// The .ngc carries G1 with F on cuts and G0 without F on rapids, and the feed
// is written only where it changes: after F860 the next strip cut does not
// repeat it, only the plunge feed F215 does.
void checkEmitFeeds() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, planned);
    // Count the feed words: one plunge, one working feed, one ramp per strip is
    // the first strip only; the modal state keeps the rest quiet.
    const std::size_t plunge = gcode.find("F215");
    const std::size_t work = gcode.find("F860");
    check(plunge != std::string::npos && work != std::string::npos && plunge < work,
          "emit.feed_order", "the plunge feed must come before the working feed");
    // The plunge feed and the working feed alternate between strips, so both
    // appear again on the next strip - but never twice in a row: a repeated F
    // would be a wasted word and the sign of a broken modal state.
    bool feedRepeat = false;
    std::string previousFeed;
    std::size_t at = gcode.find(" F");
    while (at != std::string::npos) {
        const std::size_t end = gcode.find('\n', at);
        const std::string one = gcode.substr(at + 1, (end == std::string::npos ? gcode.size() : end) - at - 1);
        const std::string word = one.substr(0, one.find(' '));
        if (word == previousFeed) { feedRepeat = true; }
        previousFeed = word;
        at = gcode.find(" F", end == std::string::npos ? gcode.size() : end);
    }
    check(!feedRepeat, "emit.feed_no_repeat", "two blocks in a row carried the same feed word");
    check(contains(gcode, " G1 Z-2.000 F215"), "emit.plunge_block",
          "the plunge of the first pass must be G1 with the plunge feed");
    check(contains(gcode, " X51.000 F860"), "emit.strip_block",
          "the first strip must be G1 X51.000 with the working feed");
    // The second strip starts at the same X and the same safe Z as the first,
    // so the printer changes only Y there. The rule is checked on the moves
    // themselves: a block is bad when both its X and its Y equal the values
    // of the previous move that wrote them (the feed table below holds the
    // position of the first strip).
    const std::vector<std::string> blocks = moveBlocks(gcode);
    bool bothSame = false;
    ModalState seen;
    for (const std::string& one : blocks) {
        const bool hasX = hasWord(one, 'X');
        const bool hasY = hasWord(one, 'Y');
        if (hasX && hasY && seen.has[0] && seen.has[1] && near(axisValue(one, 'X'), seen.value[0]) &&
            near(axisValue(one, 'Y'), seen.value[1])) {
            bothSame = true;
        }
        if (hasX) { setAxis(seen, 0, axisValue(one, 'X')); }
        if (hasY) { setAxis(seen, 1, axisValue(one, 'Y')); }
    }
    check(!bothSame, "emit.rapid_y_only",
          "a move block repeated both X and Y of the previous move");
    // Rapids never carry F: a feed on a G0 would change the modal feed.
    bool rapidFeed = false;
    std::size_t rapidAt = gcode.find(" G0 ");
    while (rapidAt != std::string::npos) {
        const std::size_t end = gcode.find('\n', rapidAt);
        const std::string one = gcode.substr(rapidAt, end - rapidAt);
        if (one.find('F') != std::string::npos) { rapidFeed = true; }
        rapidAt = gcode.find(" G0 ", end);
    }
    check(!rapidFeed, "emit.rapid_no_feed", "a rapid block carried a feed word");
    // Sequence numbers grow by the step of the reference file and never repeat.
    bool increasing = true;
    int last = 0;
    std::size_t pos = 0;
    while ((pos = gcode.find("N", pos)) != std::string::npos) {
        if (pos != 0 && gcode[pos - 1] != '\n') { ++pos; continue; }
        int number = 0;
        if (std::sscanf(gcode.c_str() + pos + 1, "%d", &number) == 1) {
            if (number <= last) { increasing = false; }
            last = number;
        }
        ++pos;
    }
    check(increasing && last > 10, "emit.sequence_numbers",
          "block numbers must grow across the whole file");
}

} // namespace emit_checks

// Runs the move cases: the words of a cut, the feeds and the block numbers.
void runEmitMovesChecks() {
    emit_checks::checkEmitMoves();
    emit_checks::checkEmitFeeds();
}
