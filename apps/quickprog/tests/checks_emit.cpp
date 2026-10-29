// tests/checks_emit.cpp - the words of the .ngc: header, footer, moves (7.1).
//
// Every case reads the text the emitter produces for a small document and
// checks a word of the contract: the modal header, the work offset, the tags
// the viewer reads, the tool change, the feeds and the parking moves.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"

#include "tests/harness.h"

namespace emit_checks {

using namespace qpcheck;

using qpcheck::axisSlot;
using qpcheck::axisValue;
using qpcheck::check;
using qpcheck::contains;
using qpcheck::demoProgram;
using qpcheck::faceOp;
using qpcheck::faceProgram;
using qpcheck::hasWord;
using qpcheck::ModalState;
using qpcheck::moveBlocks;
using qpcheck::near;
using qpcheck::setAxis;

// Header parity with PLAN-QUICKPROG, section 7.1: the same words in the same
// order keep the generated file readable for the existing viewer.
void checkHeader() {
    const qp::Program program = demoProgram();
    const qp::Path path = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, path);

    check(contains(gcode, "N10 G21 G90 G94 G17 G40 G49 G80"),
          "emit.header_modals",
          "the modal block G21 G90 G94 G17 G40 G49 G80 is missing");
    // A bare "G54" would also match the "WCS INVALID ... G54 USED" comment and
    // the "N15G54" spelling a broken separator gives; the block has to be a
    // word of its own line.
    check(contains(gcode, "N15 G54\n"), "emit.header_wcs", "the work offset block N15 G54 is missing");
    check(contains(gcode, "(UI_STOCK:"), "emit.ui_stock", "the UI_STOCK tag is missing");
    check(contains(gcode, "XMIN=-50."), "emit.ui_stock_bounds", "UI_STOCK has no XMIN from the stock");
    // The footer M30 is the last numbered block; the number depends on the
    // operations before it, so the case matches the block, not the number.
    check(contains(gcode, " M30\n"), "emit.footer", "the footer M30 block is missing");
    check(!contains(gcode, "G54 USED"), "emit.header_wcs_valid", "a valid WCS still gets the invalid warning");
    check(path.moves.empty(), "path.empty", "the planner must return a path without moves");
}

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

// The strip layout of path_face.cpp covers the stock from both edges, not a
// centred grid. `usable = width - D` is the span the centres have to cross;
// with a nominal step `D * stepOver` the count is `ceil(usable / nominal) + 1`
// and the real step is `usable / (count - 1)`, so the edges land exactly at
// `D/2` and no strip is farther apart than the nominal step. 100x80, D10, 70 %:
// usable 70, nominal 7, ceil(70 / 7) + 1 = 12, real step 70 / 11 = 6.3636 mm.
void checkFaceStripFormula() {
    const qp::Program program = faceProgram();
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    // 70 mm of the span over a 7 mm nominal step is exactly 10 intervals, and
    // ceil(10) + 1 is 11 strips — the same count the former floor formula gave
    // here. The difference shows on a span that is not a multiple of the step:
    // there the ceiling keeps the edge strips at half a tool inside the edges
    // instead of centring the grid and leaving a wider margin at both sides.
    check(stats.strips == 11, "face.formula_strips",
          "100x80, D10, 70 % must give ceil(70 / 7) + 1 = 11 strips, got " +
              std::to_string(stats.strips));
    check(near(stats.stripStepMm, 70.0 / 10.0), "face.formula_step",
          "the real step must be 70 / 10 = 7 mm, got " + std::to_string(stats.stripStepMm));
    check(stats.stripStepMm <= 7.0 + 1e-9 && stats.stripStepMm <= 10.0,
          "face.formula_step_bounded",
          "the real step must not exceed the nominal 7 mm nor the 10 mm diameter");

    {
        qp::Program d6 = faceProgram();
        d6.operations[0].tool.diameterMm = 6.0;
        const qp::FaceStats s6 = qp::faceStats(d6, d6.operations.front());
        // usable 74, nominal 4.2, ceil(74 / 4.2) = ceil(17.62) = 18, + 1 = 19.
        check(s6.strips == 19, "face.formula_d6",
              "100x80, D6, 70 % must give ceil(74 / 4.2) + 1 = 19 strips, got " +
                  std::to_string(s6.strips));
        // The stall count (74 / 4.2) is real outside the 1e-9 clamp, so the
        // count has to be the ceil and not a floor with a tail dropped.
        check(near(s6.stripStepMm, 74.0 / 18.0), "face.formula_d6_step",
              "the real step of D6 must be 74 / 18 = 4.1111 mm, got " +
                  std::to_string(s6.stripStepMm));
    }
    {
        // A 100 % step: usable 70, nominal 10, ceil(70 / 10) + 1 = 8 strips,
        // the edges still at D/2 and the real step exactly the diameter.
        qp::Program coarse = faceProgram();
        std::get<qp::FaceOp>(coarse.operations[0].geometry).stepOverPct = 100.0;
        const qp::FaceStats cs = qp::faceStats(coarse, coarse.operations.front());
        check(cs.strips == 8, "face.formula_full_step",
              "100 % step on 80 mm with D10 must give ceil(70 / 10) + 1 = 8 strips, got " +
                  std::to_string(cs.strips));
        check(near(cs.stripStepMm, 10.0), "face.formula_full_step_step",
              "100 % step must give a real step of 10 mm, got " + std::to_string(cs.stripStepMm));
    }
}

// No strip pair and no edge leaves a gap: the tool body of one strip
// `[y - D/2, y + D/2]` overlaps its neighbour, the first strip reaches the low
// edge and the last reaches the high edge. Checked over the real move list, so
// a planner that drifts from the formula fails here.
void checkFaceCoverage() {
    auto stripYs = [](const qp::Program& program) {
        const qp::Path planned = qp::planPath(program);
        // The deepest plane is the last pass; its long cuts are the strips.
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
            if (m.kind != qp::MoveKind::Cut || !near(m.to.z, minZ) || m.hasArc) { continue; }
            const qp::Move& prev = planned.moves[i - 1];
            if (!near(m.to.z, prev.to.z)) { continue; }
            if (std::fabs(m.to.x - prev.to.x) < 1.0) { continue; }
            if (ys.empty() || !near(ys.back(), m.to.y)) { ys.push_back(m.to.y); }
        }
        return ys;
    };

    const qp::Program program = faceProgram();
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    const std::vector<double> ys = stripYs(program);
    const double r = stats.toolRadiusMm;
    int gaps = 0;
    for (std::size_t i = 0; i + 1 < ys.size(); ++i) {
        if (ys[i + 1] - r > ys[i] + r + 1e-9) { ++gaps; }
    }
    const bool edgeLow = !ys.empty() && ys.front() - r <= program.stock.min.y + 1e-9;
    const bool edgeHigh = !ys.empty() && ys.back() + r >= program.stock.max.y - 1e-9;
    check(gaps == 0 && edgeLow && edgeHigh, "face.coverage",
          "the strips must cover Y -40..+40 without gaps: gaps=" + std::to_string(gaps) +
              " low=" + std::to_string(static_cast<int>(edgeLow)) +
              " high=" + std::to_string(static_cast<int>(edgeHigh)));

    // A 100 % step has zero overlap: the bodies only touch. The coverage is
    // still complete, and the narrow overlap is called out as a warning.
    qp::Program full = faceProgram();
    std::get<qp::FaceOp>(full.operations[0].geometry).stepOverPct = 100.0;
    const qp::FaceStats fs = qp::faceStats(full, full.operations.front());
    const std::vector<double> fys = stripYs(full);
    int fgaps = 0;
    for (std::size_t i = 0; i + 1 < fys.size(); ++i) {
        if (fys[i + 1] - fs.toolRadiusMm > fys[i] + fs.toolRadiusMm + 1e-9) { ++fgaps; }
    }
    const bool fEdgeLow = !fys.empty() && fys.front() - fs.toolRadiusMm <= full.stock.min.y + 1e-9;
    const bool fEdgeHigh = !fys.empty() && fys.back() + fs.toolRadiusMm >= full.stock.max.y - 1e-9;
    check(fgaps == 0 && fEdgeLow && fEdgeHigh, "face.coverage_full_step",
          "a 100 % step must still cover the stock: gaps=" + std::to_string(fgaps));
    bool warned = false;
    for (const std::string& text : qp::faceWarnings(full, full.operations.front())) {
        if (text.find("Перекрытие") != std::string::npos) { warned = true; }
    }
    check(warned, "face.coverage_full_step_warning",
          "a 100 % step leaves no overlap and must warn about it");

    // A stock exactly one diameter wide: one strip through the centre, no gap
    // on either edge, no overlap warning.
    qp::Program one = faceProgram();
    one.stock.min.y = -5.0;
    one.stock.max.y = 5.0;
    const std::vector<double> oys = stripYs(one);
    check(oys.size() == 1 && near(oys.front(), 0.0), "face.coverage_one_strip",
          "an 80 mm stock replaced by a 10 mm one must give a single centred strip, got " +
              std::to_string(oys.size()) + " strips");
}

// Every cut stays inside the stock grown by the lead on each side along X and
// exactly inside the stock across Y (no Y lead runout on face); a rapid never
// descends below topZ inside the material. The second check is about the
// descent, not about safe Z: the lead of the plunge is a real Z of the planner
// and must be named by the rule.
void checkFaceBoundsAndRapids() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    // The X lead is 10 % of D clamped to 1..D/2: 1 mm on D10.
    const double lead = stats.edgeOverrunMm;
    check(near(lead, 1.0), "face.lead_length", "the X lead on D10 must be 1 mm, got " +
                                                  std::to_string(lead));
    check(near(stats.sideOverrunMm, 0.0), "face.side_overrun",
          "face must not add a Y runout: got " + std::to_string(stats.sideOverrunMm));
    const double xMin = program.stock.min.x - lead - 1e-6;
    const double xMax = program.stock.max.x + lead + 1e-6;
    // Across the strips the tool centre stays half a diameter inside each edge:
    // the whole stock is covered and the centre never passes the edge.
    const double yMin = program.stock.min.y + stats.toolRadiusMm - 1e-6;
    const double yMax = program.stock.max.y - stats.toolRadiusMm + 1e-6;

    bool inBounds = true;
    bool descentOk = true;
    for (const qp::Move& m : planned.moves) {
        if (m.to.x < xMin || m.to.x > xMax || m.to.y < yMin || m.to.y > yMax) { inBounds = false; }
        if (m.kind == qp::MoveKind::Rapid && m.to.z < program.operations.front().topZ - 1e-6 &&
            m.to.z < program.stock.max.z - 1e-6) {
            // A rapid below the stock top is only allowed when the move stays
            // outside the stock rectangle: the planner never does that, so any
            // such rapid is a bug.
            descentOk = false;
        }
    }
    check(inBounds, "face.cuts_in_stock_plus_lead",
          "a move left the stock plus the X lead or crossed the Y centre window");
    check(descentOk, "face.rapid_not_into_material",
          "a rapid descended below the stock top inside the material");
    // The plunge of one strip hides the first axial step below the top; the
    // ramp back is a cut, so a rapid never descends into the material.
    check(near(stats.leadMm, 2.0), "face.lead",
          "the deepest descent below the top of one strip must be the 2 mm step");
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
    // The park comes before the change, in the same block, so M6 never finds
    // the tool inside the material.
    check(contains(gcode2, " G0 Z5.000 T7 M6\n"), "toolchange.park_before_change",
          "the change block must park at safe Z before M6");
}

} // namespace emit_checks

// Runs the cases of this file: the entry point calls the four runners in the
// order of the slices.
void runEmitChecks() {
    emit_checks::checkHeader();
    emit_checks::checkEmitMoves();
    emit_checks::checkFaceStripFormula();
    emit_checks::checkFaceCoverage();
    emit_checks::checkFaceBoundsAndRapids();
    emit_checks::checkEmitFeeds();
    emit_checks::checkTwoOperations();
    emit_checks::checkToolChangeRule();
}
