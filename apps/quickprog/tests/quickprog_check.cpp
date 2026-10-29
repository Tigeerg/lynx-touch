// tests/quickprog_check.cpp - self-check of the quickprog core.
//
// No test framework: the demo must build on any machine with a C++ compiler
// and nothing else. Each case prints "PASS <id>" or "FAIL <id>: <reason>" and
// the exit code is 0 when every case passed, 1 otherwise (PLAN-QUICKPROG,
// section 7.2).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/cli.h"
#include "core/emit.h"
#include "core/face_options.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/validate.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string& id, const std::string& reason) {
    if (ok) {
        std::printf("PASS %s\n", id.c_str());
        return;
    }
    std::printf("FAIL %s: %s\n", id.c_str(), reason.c_str());
    ++g_failed;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

// Lines of the file that carry a motion word. Comments and the header block
// (modals, work offset, parking) are not moves: a "G0" there is a mode, not a
// trajectory.
std::vector<std::string> moveBlocks(const std::string& gcode) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < gcode.size()) {
        const std::size_t end = gcode.find('\n', at);
        const std::string one = gcode.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (one.find(" G0 ") != std::string::npos || one.find(" G1 ") != std::string::npos) {
            out.push_back(one);
        }
        if (end == std::string::npos) { break; }
        at = end + 1;
    }
    return out;
}

// The modal state of the file while the check reads it: a word is present in a
// line and the previous value is known.
struct ModalState {
    bool has[3] = {false, false, false};  // X, Y, Z in this order
    double value[3] = {0.0, 0.0, 0.0};
};

void setAxis(ModalState& state, int axis, double value) {
    state.has[axis] = true;
    state.value[axis] = value;
}

// True when the block carries an axis word, "X12.000" after a space.
bool hasWord(const std::string& block, char axis) {
    return block.find(std::string(" ") + axis) != std::string::npos;
}

// Value of the axis word of a block, NaN when the word is absent.
double axisValue(const std::string& block, char axis) {
    const std::size_t at = block.find(std::string(" ") + axis);
    if (at == std::string::npos) { return std::nan(""); }
    return std::atof(block.c_str() + at + 2);
}

int axisSlot(char axis) {
    return axis == 'X' ? 0 : (axis == 'Y' ? 1 : 2);
}

// A minimal program: the stock block only, without operations. It is what
// `--demo` writes.
qp::Program demoProgram() {
    qp::Program program;
    program.name = "QUICKPROG DEMO";
    program.wcs = "G54";
    program.stock.min = {-50.0, -40.0, -20.0};
    program.stock.max = {50.0, 40.0, 0.0};
    program.safeZ = 5.0;
    return program;
}

// Face milling of a 100x80x20 stock top, 10 mm end mill, 5 mm down in 2 mm
// steps. The stock is the demo one, so the top is z = 0.
qp::Operation faceOp() {
    qp::Operation op;
    op.id = "Плоскость";
    op.tool.number = 2;
    op.tool.diameterMm = 10.0;
    op.cutting.spindleRpm = 4775.0;
    op.cutting.feedMmMin = 860.0;
    op.cutting.plungeFeedMmMin = 215.0;
    op.topZ = 0.0;
    op.depthMm = 5.0;
    op.stepDownMm = 2.0;
    qp::FaceOp face;
    face.z = -5.0;
    face.stepOverPct = 70.0;
    op.geometry = face;
    return op;
}

qp::Program faceProgram() {
    qp::Program program = demoProgram();
    program.name = "FACE DEMO";
    program.operations.push_back(faceOp());
    return program;
}

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

// The cutting data comes from calc.ts through feeds: Vc 150 m/min on a 10 mm
// tool is 4775 rpm, fz 0.06 on three flutes is 860 mm/min (calc.test.ts, lines
// 7 and 26). The number of flutes is fixed at three, as in the React defaults.
void checkFeeds() {
    const qp::Feeds alu = qp::feedsFor(qp::Material::Aluminium, 10.0, qp::ToolKind::Flat);
    check(alu.rpm == 4775.0, "feeds.rpm_alu_d10", "Vc 150 on D10 must be 4775 rpm, got " +
                                                     std::to_string(alu.rpm));
    check(alu.feedMmMin == 860.0, "feeds.feed_alu_d10", "fz 0.06 x 3 x 4775 must be 860 mm/min, got " +
                                                            std::to_string(alu.feedMmMin));
    check(alu.plungeFeedMmMin == 215.0, "feeds.plunge_alu_d10",
          "the plunge is a quarter of the feed, expected 215 mm/min");
    check(alu.source == "таблица: алюминий, ф10", "feeds.source_alu",
          "the source must name the material and the diameter, got '" + alu.source + "'");

    const qp::Feeds steel = qp::feedsFor(qp::Material::Steel, 10.0, qp::ToolKind::Flat);
    check(steel.rpm == 2865.0, "feeds.rpm_steel_d10", "Vc 90 on D10 must be 2865 rpm");
    check(steel.feedMmMin == 258.0, "feeds.feed_steel_d10", "fz 0.03 x 3 x 2865 must be 258 mm/min");

    // A drill takes fz 0.03 per flute from the table row, two flutes
    // (feeds.cpp, 2 flutes): 0.03 * 2 * 4775 = 286.5 -> 287 mm/min. The number
    // is checked as the product of the table values, not as a claim about a
    // handbook drill.
    const qp::Feeds drill = qp::feedsFor(qp::Material::Aluminium, 10.0, qp::ToolKind::Drill);
    check(drill.feedMmMin == 287.0, "feeds.drill_alu_d10",
          "a drill takes fz 0.03 on two flutes at 4775 rpm, 287 mm/min");

    const qp::Feeds none = qp::feedsFor(qp::Material::Aluminium, 0.0, qp::ToolKind::Flat);
    check(none.rpm == 0.0 && none.feedMmMin == 0.0, "feeds.zero_diameter", "a zero diameter gives zeros");
}

// The stock is 100x80: the strips run along X, so the cross direction is Y.
// The tool centre must span the whole usable width `80 - 10 = 70 mm` from one
// `D/2` line to the other, so a 70 % step of 7 mm needs
// ceil(70 / 7) + 1 = 12 strips (11 intervals of 6.3636 mm), not 11 centred ones.
void checkFaceStrips() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    check(stats.strips == 11, "face.strips",
          "100x80 stock, D10, 70 % covers with 11 strips, got " + std::to_string(stats.strips));
    check(stats.passes == 3, "face.passes", "5 mm in 2 mm steps must be 3 passes, got " +
                                               std::to_string(stats.passes));
    check(!planned.moves.empty(), "face.moves", "the face planner returned no moves");
}

// Strips run along X: the Y coordinate of every cutting strip is fixed and the
// X coordinate sweeps the whole strip. The first and the last strip sit half a
// tool inside the stock edge.
void checkFaceStripsAlongX() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    // A strip is a cut at a constant Z; the plunge and the retract are cuts at
    // a constant X/Y. Collect the Z of the deepest cutting plane and the
    // distinct Y values of the long cuts at that plane.
    bool havePlane = false;
    double minZ = 0.0;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Cut && (!havePlane || m.to.z < minZ)) {
            minZ = m.to.z;
            havePlane = true;
        }
    }
    std::vector<double> stripY;
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        if (m.kind != qp::MoveKind::Cut || !near(m.to.z, minZ) || m.hasArc) { continue; }
        const qp::Move& prev = planned.moves[i - 1];
        // A strip moves along X at one Z; the plunge and the ramp change Z.
        if (!near(m.to.z, prev.to.z)) { continue; }
        if (std::fabs(m.to.x - prev.to.x) < 1.0) { continue; }
        if (stripY.empty() || !near(stripY.back(), m.to.y)) { stripY.push_back(m.to.y); }
    }
    // One pass is a plunge plus one long cut per strip, so the last pass at the
    // deepest Z is exactly as many strip cuts as there are strips.
    check(static_cast<int>(stripY.size()) == 11, "face.strips_along_x",
          "the last pass must sweep 11 strips along X, got " + std::to_string(stripY.size()) +
              " cuts at the deepest Z");
    // First and last strip sit exactly half a tool inside each edge: the tool
    // body reaches the edge with its centre, not its far flank.
    check(!stripY.empty() && near(stripY.front(), -35.0), "face.first_strip_edge",
          "the first strip must sit 5 mm inside Y=-40, got " +
              (stripY.empty() ? std::string("no strip") : std::to_string(stripY.front())));
    check(!stripY.empty() && near(stripY.back(), 35.0), "face.last_strip_edge",
          "the last strip must sit 5 mm inside Y=+40, got " +
              (stripY.empty() ? std::string("no strip") : std::to_string(stripY.back())));
    // The tool centre goes 10 % of D past both ends, clamped to 1..D/2 mm: on
    // D10 that is 1 mm per side, so the centre span is 100 + 2 * 1 = 102 mm.
    check(near(planned.bbox.xMax - planned.bbox.xMin, 102.0), "face.bbox_x",
          "the tool centre must span 102 mm along X, got " +
              std::to_string(planned.bbox.xMax - planned.bbox.xMin));
    check(planned.bbox.valid && near(planned.bbox.zMax, 5.0), "face.bbox_z",
          "the highest point of the path must be safe Z");
}

// Every cut stays inside the stock grown by the required edge overrun; no
// rapid goes below safe Z.
void checkFaceBounds() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    // The overrun along X is 10 % of the diameter = 1 mm per side, and the
    // strip centre stays half a tool inside Y: the tool body touches the edge.
    const double overrun = stats.edgeOverrunMm;
    const double xMin = program.stock.min.x - overrun;
    const double xMax = program.stock.max.x + overrun;
    const double yMin = program.stock.min.y + stats.toolRadiusMm;
    const double yMax = program.stock.max.y - stats.toolRadiusMm;

    bool inBounds = true;
    bool rapidSafe = true;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Cut) {
            if (m.to.x < xMin - 1e-6 || m.to.x > xMax + 1e-6 || m.to.y < yMin - 1e-6 ||
                m.to.y > yMax + 1e-6) {
                inBounds = false;
            }
        } else if (m.to.z < program.operations.front().topZ - 1e-6 &&
                   m.to.z < program.stock.max.z - 1e-6) {
            // A rapid may leave safe Z only to land on the material top: below
            // the top inside the stock is a crash, the descent there is a cut.
            rapidSafe = false;
        }
    }
    check(inBounds, "face.cuts_in_bounds", "a cutting move left the stock plus the edge overrun");
    check(rapidSafe, "face.rapid_not_below_top", "a rapid went below the material top");
}

// 5 mm depth in 2 mm steps: 2 + 2 + 1. The last pass is exactly the bottom,
// every earlier one is a multiple of the step.
void checkFacePasses() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    // The Z planes of the cutting passes, ordered from the top down: a strip
    // cut moves in XY only at the plane, so it marks the pass bottom.
    std::vector<double> planes;
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        if (m.kind != qp::MoveKind::Cut) { continue; }
        const qp::Move& prev = planned.moves[i - 1];
        if (!near(m.to.z, prev.to.z)) { continue; }
        // A plunge and a ramp change Z; only a long cut in XY at one Z is a
        // strip. The length separates them without knowing the planner.
        const double dx = m.to.x - prev.to.x;
        const double dy = m.to.y - prev.to.y;
        if (std::sqrt(dx * dx + dy * dy) < 1.0) { continue; }
        if (planes.empty() || !near(planes.back(), m.to.z)) { planes.push_back(m.to.z); }
    }
    // Even a single-plane pass shows up once; the pass count comes from the
    // stats, the planes from the moves.
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    check(stats.passes == 3, "face.passes_5mm", "5 mm / 2 mm must be 3 passes");
    check(!planes.empty() && near(planes.back(), -5.0), "face.last_pass_bottom",
          "the last pass must reach the bottom -5, got " +
              (planes.empty() ? std::string("no strip cut at a plane") : std::to_string(planes.back())));
    check(stats.stepDepthsMm.size() == 3, "face.step_count",
          "the stats must list three pass depths, got " + std::to_string(stats.stepDepthsMm.size()));
    check(near(stats.stepDepthsMm[0], -2.0) && near(stats.stepDepthsMm[1], -4.0) &&
              near(stats.stepDepthsMm[2], -5.0),
          "face.step_depths", "the pass depths must be -2, -4, -5");
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
            // The first block of a motion after a non-move line (a spindle or a
            // coolant word) keeps the Z the header parked the tool at: the
            // printer starts the modal Z from safe Z, and a parking block that
            // writes the same Z is part of the header contract, not a move.
            const bool firstBlock = one.find("G0 Z") == 0;
            if (state.has[slot] && near(value, state.value[slot]) && !(firstBlock && axis == 'Z')) {
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

// The checks of the request, each on its own field: a depth below the stock
// bottom, a zero step, a safe Z below the top and a zero diameter.
void checkValidateFields() {
    {
        qp::Program program = faceProgram();
        // The stock is 20 mm thick and the plane is the top: 21 mm is below
        // the bottom by one millimetre.
        program.operations.front().depthMm = 21.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "depthMm", "validate.bottom_field",
              "a depth below the stock bottom must come back on depthMm");
    }
    {
        qp::Program program = faceProgram();
        program.operations.front().stepDownMm = 0.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "stepDownMm", "validate.step_field",
              "a zero step must come back on stepDownMm");
    }
    {
        qp::Program program = faceProgram();
        program.safeZ = -1.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "safeZ", "validate.safez_field",
              "a safe Z below the top must come back on safeZ");
    }
    {
        qp::Program program = faceProgram();
        program.operations.front().tool.diameterMm = 0.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "tool.diameterMm", "validate.diameter_field",
              "a zero diameter must come back on tool.diameterMm");
    }
}

// A thin last pass: 5 mm in 2.5 mm steps gives 2.5 + 2.5, the remainder is
// zero; 5 in 2.4 gives 2.4 + 2.4 + 0.2, and 0.2 is below 10 % of 2.4. The
// planner still plans the remainder and the warning names it.
void checkThinPassWarning() {
    qp::Program program = faceProgram();
    qp::Operation& op = program.operations.front();
    op.stepDownMm = 2.4;
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    check(stats.passes == 3, "face.thin_passes", "5 mm in 2.4 mm steps must be 3 passes");
    check(near(stats.stepDepthsMm[2], -5.0), "face.thin_bottom", "the last pass must end at -5 mm");
    const std::vector<std::string> warnings = qp::faceWarnings(program, program.operations.front());
    bool thin = false;
    for (const std::string& text : warnings) {
        if (text.find("Тонкий последний проход") != std::string::npos) { thin = true; }
    }
    check(thin, "face.thin_warning", "a 0.2 mm last pass below 10 % of 2.4 mm must warn");
    // The deep step (5 mm) leaves no remainder and no warning.
    qp::Program even = faceProgram();
    even.operations.front().stepDownMm = 5.0;
    check(qp::faceWarnings(even, even.operations.front()).empty(), "face.no_thin_warning",
          "a single 5 mm pass must not warn about a thin remainder");
}


// The console parser: the defaults of the help text, a full set of typed keys
// and the usage errors. The check does not run the binary, so a broken parser
// still fails here instead of in a manual run.
void checkFaceArgs() {
    {
        qp::FaceArgs args;
        const std::vector<std::string> words = {"--op", "face", "out.ngc"};
        check(qp::parseFaceArgs(words, 2, args) && args.output == "out.ngc", "args.output",
              "the bare word must be the output file");
        check(args.tool == 1 && near(args.diameter, 10.0) && near(args.stepOverPct, 70.0),
              "args.defaults", "the defaults must be T1, D10, 70 %");
        check(near(args.stockX, 100.0) && near(args.stockZ, 20.0) && !args.safeGiven,
              "args.stock_default", "the stock must default to 100x80x20");
    }
    {
        qp::FaceArgs args;
        const std::vector<std::string> words = {"--op",   "face",   "--tool",      "2",
                                                "--diameter", "8", "--stock",    "60x40x10",
                                                "--top",  "2",      "--depth",     "1.5",
                                                "--step-down", "0.5", "--step-over", "50",
                                                "--feed", "700",    "--rpm",       "6000",
                                                "--material", "steel", "--safe",  "9",
                                                "--name", "Плоскость", "a.ngc"};
        const bool ok = qp::parseFaceArgs(words, 2, args);
        check(ok && args.tool == 2 && near(args.diameter, 8.0) && near(args.stockY, 40.0) &&
                  near(args.topZ, 2.0) && near(args.depth, 1.5) && near(args.stepDown, 0.5) &&
                  near(args.stepOverPct, 50.0) && near(args.feed, 700.0) && near(args.rpm, 6000.0) &&
                  args.material == qp::Material::Steel && args.safeGiven && near(args.safeZ, 9.0) &&
                  args.name == "Плоскость" && args.output == "a.ngc",
              "args.all_keys", "every key must land in its field");
    }
    {
        qp::FaceArgs args;
        const std::vector<std::string> missing = {"--op", "face", "--tool"};
        check(!qp::parseFaceArgs(missing, 2, args), "args.missing_value",
              "a key without a value must fail");
        qp::FaceArgs second;
        const std::vector<std::string> twoOut = {"--op", "face", "a.ngc", "b.ngc"};
        check(!qp::parseFaceArgs(twoOut, 2, second), "args.two_outputs",
              "a second output file must fail");
        qp::FaceArgs unknown;
        const std::vector<std::string> badKey = {"--op", "face", "--nope", "1", "a.ngc"};
        check(!qp::parseFaceArgs(badKey, 2, unknown), "args.unknown_key",
              "an unknown key must fail");
        qp::FaceArgs number;
        const std::vector<std::string> badNumber = {"--op", "face", "--depth", "abc", "a.ngc"};
        check(!qp::parseFaceArgs(badNumber, 2, number), "args.bad_number",
              "a non-number must fail");
        qp::FaceArgs noOut;
        const std::vector<std::string> onlyKeys = {"--op", "face", "--depth", "2"};
        check(!qp::parseFaceArgs(onlyKeys, 2, noOut), "args.no_output",
              "no output file must fail");
    }
}

// Each rule names its field and explains the problem in the operator's words.
void checkValidate() {
    {
        qp::Program program = faceProgram();
        program.operations.front().depthMm = 25.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "depthMm", "validate.depth_below_stock",
              "a depth below the stock bottom must fail on depthMm");
        check(!bad.empty() && !bad.front().reason.empty(), "validate.reason_text",
              "every check carries a reason");
    }
    {
        qp::Program program = faceProgram();
        program.operations.front().stepDownMm = 0.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "stepDownMm", "validate.step_zero",
              "a zero step down must fail on stepDownMm");
    }
    {
        qp::Program program = faceProgram();
        program.safeZ = -5.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "safeZ", "validate.safe_z",
              "safe Z below the stock top must fail on safeZ");
    }
    {
        qp::Program program = faceProgram();
        program.operations.front().tool.diameterMm = 0.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "tool.diameterMm", "validate.zero_diameter",
              "a zero tool diameter must fail on tool.diameterMm");
    }
    {
        qp::Program program = faceProgram();
        const std::vector<qp::Check> bad = qp::validate(program);
        check(bad.empty(), "validate.face_ok", "the face program must be valid");
    }
}

} // namespace

int main() {
    checkHeader();
    checkFeeds();
    checkFaceStrips();
    checkFaceStripsAlongX();
    checkFaceBounds();
    checkFacePasses();
    checkEmitMoves();
    checkValidate();
    checkFaceStripFormula();
    checkFaceCoverage();
    checkFaceBoundsAndRapids();
    checkEmitFeeds();
    checkTwoOperations();
    checkValidateFields();
    checkThinPassWarning();
    checkFaceArgs();
    if (g_failed != 0) {
        std::printf("%d case(s) failed\n", g_failed);
        return 1;
    }
    std::printf("all cases passed\n");
    return 0;
}
