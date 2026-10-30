// core/emit.cpp - the G-code emitter (PLAN-QUICKPROG, 7.1).
//
// The emitter reads Program and Path and returns the whole file as text. It
// never recomputes a coordinate: a number in the file is a number of a move.
// The tags and the block order repeat data/cad-testdata/linuxcnc_ui.cps, so the
// viewer and the operator read the same file the way they read a CAM file:
//
//   (UI_OP: <name>)                        operation marker
//   (UI_STAGE: <path uuid>/op<n>)          stable block identifier
//   M1                                     stop before a new operation, when on
//   (UI_OP: <name>)                        operation marker
//   (UI_STAGE: <path uuid>/op<n>)          stable block identifier
//   (UI_TOOL: T=.. TYPE=.. D=..)           tool geometry, before the change
//   G53 G0 Z0                              machine park, before a change and
//                                          at the start of the program
//   T.. M6                                 change, only when the tool differs
//   G43 Z<safe> H<n>                       length compensation of the tool
//   S.. M3 / M8                            spindle and coolant, when they differ
//   G0 / G1 X.. Y.. Z.. F..                moves of the operation
//   M9 / M5 / G0 Z<safe>                   end of the operation (M9 when on)
//
// One rule decides the preamble of every operation - the park, the tool change,
// G43 and the spindle - and it lives in `emitOperationStart` alone
// (PLAN-QUICKPROG, 7.1, 7.3): the first operation and every new tool park at
// the machine home with G53 G0 Z0, then T/M6, then G43; the same tool keeps its
// tool and H, starts a new spindle speed when it differs, and still gets G43
// when only the H row changed. M8 is written only when the coolant was off; M9
// turns it off at the end of an operation that asked for it, so no coolant word
// is printed for an operation with the coolant switched off. M1 marks the start
// of every operation after the first, before its UI_OP tag.
//
// Blocks are numbered N10, N15, N20, ... across the whole file: the step of
// the reference file, so a block keeps a readable number in the editor.
#include "core/emit.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

#include "core/emit_frame.h"
#include "core/emit_internal.h"
#include "core/path_drill.h"
#include "core/version.h"

namespace qp {
namespace {

constexpr double kEps = 1e-6;

// One line of the file; the block builders return their text without it.
void line(std::string& out, const std::string& text) {
    out += text;
    out += '\n';
}

// Comment for the viewer: the same "(TAG: ...)" form as the reference files.
void tag(std::string& out, const std::string& body) {
    line(out, "(" + body + ")");
}

// The preamble of one operation, the single place of the tool-change rule:
//
//   * the program start or the tool differs from the one the machine holds ->
//     park at the machine home (G53 G0 Z0), then T.. M6;
//   * the tool is the same -> no park and no T/M6: the machine holds it;
//   * the H row or the tool changed -> G43 Z<safe> H<n>; G43 is always printed
//     after a change and never printed twice in a row for one H;
//   * the spindle was off or its speed differs -> S.. M3;
//   * the coolant is on and was off -> M8.
//
// The order is the one of the reference post: M1, UI_OP, UI_TOOL, park, change,
// G43, spindle, coolant. The park before the change is mandatory: M6 releases
// the tool, and a tool inside the material is a crash whatever the machine's
// own retract does. The park is a machine move (G53), not a move to the part's
// safe Z: the stand leaves the working zone whatever offset is active.
void emitOperationStart(std::string& out, const Program& program, const Operation& operation,
                        int& sequence, const std::string& uuid, EmitState& state) {
    // M1 stands before the UI_OP of every operation after the first: the
    // operator sees the pause on the border of two operations, not inside one
    // (PLAN-QUICKPROG, 7.3). The disabled operations have no span and no block.
    if (state.operationsStarted > 0 && program.stopAfterOp) {
        line(out, numberedBlock(sequence, "M1"));
        sequence += kSequenceStep;
    }
    ++state.operationsStarted;
    tag(out, "UI_OP: " + commentSafe(operation.id.empty() ? std::string("Операция") : operation.id));
    // The viewer takes the label after "UI_OP:" and splits stages there; the
    // stable identifier travels in its own tag, so a stale preview can be told
    // from the fresh one without breaking the label.
    tag(out, "UI_STAGE: " + uuid);
    tag(out, "UI_TOOL: T=" + std::to_string(operation.tool.number) + " TYPE=" +
                 toolType(operation.geometry) + " D=" + num(operation.tool.diameterMm));
    const int h = operation.tool.effectiveH();
    const bool toolChanged = state.tool != operation.tool.number;
    if (state.tool == 0 || toolChanged) {
        // The machine home first (N20 G53 G0 Z0. of the reference file): the
        // tool leaves the working zone before M6 releases it.
        line(out, numberedBlock(sequence, "G53 G0 Z0"));
        sequence += kSequenceStep;
        line(out, numberedBlock(sequence, "T" + std::to_string(operation.tool.number) + " M6"));
        sequence += kSequenceStep;
        state.tool = operation.tool.number;
    }
    // G43 after every change and after a change of the H row alone: another
    // row is another length offset, even for the same tool. The tool body
    // starts out at safe Z above the stock; the length table of the stand
    // turns the word into the real offset.
    if (toolChanged || state.hNumber != h) {
        line(out, numberedBlock(sequence, "G43 Z" + coord(program.safeZ) + " H" +
                                               std::to_string(h)));
        sequence += kSequenceStep;
        state.hNumber = h;
    }
    // M5 of the previous operation stopped the spindle: a speed of zero in the
    // state means "has to start" even when the number is the same.
    if (state.spindleRpm <= 0.0 || std::fabs(state.spindleRpm - operation.cutting.spindleRpm) > kEps) {
        line(out, numberedBlock(sequence, "S" + num(operation.cutting.spindleRpm) + " M3"));
        sequence += kSequenceStep;
        state.spindleRpm = operation.cutting.spindleRpm;
    }
    // Coolant before any move; the header already parked the tool at safe Z.
    if (operation.coolant != Coolant::Off && !state.coolantOn) {
        line(out, numberedBlock(sequence, "M8"));
        sequence += kSequenceStep;
    }
    // M9 at the end of the previous operation switched the coolant off, so the
    // state of the machine at this block is "off" unless the header turned it
    // on - it does not.
    state.coolantOn = operation.coolant != Coolant::Off;
}

// The operation leaves at safe Z already when its planner ends there; a tag
// without moves leaves the tool wherever the header parked it. The trailer
// writes M5, writes M9 only for an operation that switched the coolant on, and
// parks the tool: the next tool change must not find the tool inside the
// material.
void emitOperationEnd(std::string& out, const Program& program, const Operation& operation,
                      int& sequence, const Point3& parked, EmitState& state) {
    // M9 only turns off a coolant that was on: an operation with the coolant
    // off has nothing to stop, and the block would read as a coolant command.
    if (operation.coolant != Coolant::Off) {
        line(out, numberedBlock(sequence, "M9"));
        sequence += kSequenceStep;
    }
    line(out, numberedBlock(sequence, "M5"));
    sequence += kSequenceStep;
    // M5 stops the spindle (and M9, when printed, the coolant): the next
    // operation has to start its own spindle and its own coolant.
    state.coolantOn = false;
    state.spindleRpm = 0.0;
    if (std::fabs(parked.z - program.safeZ) > kEps) {
        line(out, numberedBlock(sequence, "G0 Z" + coord(program.safeZ)));
        sequence += kSequenceStep;
    }
}

} // namespace

std::string emitProgram(const Program& program, const Path& path) {
    std::string out;
    out.reserve(512 + path.moves.size() * 28);

    int sequence = 10;
    emitHeader(out, program, sequence);
    EmitState state;
    for (std::size_t i = 0; i < path.operations.size(); ++i) {
        const OperationSpan& span = path.operations[i];
        const Operation& operation = program.operations[static_cast<std::size_t>(span.opIndex)];
        // A span runs up to the next one: disabled operations have no span, so
        // the move ranges stay contiguous and no move is printed twice.
        const std::size_t last =
            i + 1 < path.operations.size() ? path.operations[i + 1].first : path.moves.size();
        emitOperationStart(out, program, operation, sequence,
                           operationUuid(path.uuid, static_cast<int>(i)), state);
        // The header, the tool change and the coolant block all leave the tool
        // at safe Z: only Z is known, XY is not, and a move that shares X and
        // Y with this virtual point is not treated as a no-op.
        const Point3 park{std::nan(""), std::nan(""), program.safeZ};
        if (isDrill(operation.geometry)) {
            // The cycle is a machine feature, not a path: the moves of a drill
            // span are only the rapids, so the holes are printed from the same
            // layout the planner drew.
            const DrillStats drill = drillStats(program, operation);
            DrillCycle cycle;
            cycle.peck = drill.cycle == DrillKind::Peck;
            cycle.depthZ = drill.depthMm;
            cycle.rZ = drill.rMm;
            cycle.peckMm = drill.peckMm;
            cycle.feedMmMin = operation.cutting.feedMmMin;
            const std::vector<Point3> holes = drillHoles(operation);
            if (!holes.empty()) {
                // The tool stands at safe Z; the cycle block runs from there
                // and parks the tool back at safe Z on G80.
                out += numberedBlock(sequence, "G0 Z" + coord(drill.rMm)) + "\n";
                sequence += kSequenceStep;
                emitCycle(out, holes, cycle, sequence, state);
            }
        } else {
            emitMoves(out, path, span.first, last, sequence, park, state);
        }
        Point3 end = span.first < last ? path.moves[last - 1].to : park;
        if (isDrill(operation.geometry)) {
            // G80 brings the drill back to the R plane, not to safe Z; the
            // trailer parks it, so the end of the block is the R plane.
            end.z = drillStats(program, operation).rMm;
        }
        emitOperationEnd(out, program, operation, sequence, end, state);
    }
    emitFooter(out, sequence);
    return out;
}

} // namespace qp
