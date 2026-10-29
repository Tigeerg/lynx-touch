// core/emit.cpp - the G-code emitter (PLAN-QUICKPROG, 7.1).
//
// The emitter reads Program and Path and returns the whole file as text. It
// never recomputes a coordinate: a number in the file is a number of a move.
// The tags and the block order repeat data/cad-testdata/linuxcnc_ui.cps, so the
// viewer and the operator read the same file the way they read a CAM file:
//
//   (UI_OP: <name>)                        operation marker
//   (UI_STAGE: <path uuid>/op<n>)          stable block identifier
//   (UI_TOOL: T=.. TYPE=.. D=..)           tool geometry, before the change
//   G0 Z<safe> T.. M6                      change, only when the tool differs
//   S.. M3 / M8                            spindle and coolant, when they differ
//   G0 / G1 X.. Y.. Z.. F..                moves of the operation
//   M9 / M5 / G0 Z<safe>                   end of the operation (M9 when on)
//
// One rule decides the preamble of every operation - the tool change and the
// spindle - and it lives in `emitOperationStart` alone (PLAN-QUICKPROG, 7.1): a
// second tool change repeats T/M6, the same tool keeps its T but starts a new
// spindle speed when it differs. M8 is written only when the coolant was off;
// M9 turns it off at the end of an operation that asked for it, so no coolant
// word is printed for an operation with the coolant switched off.
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
//   * the tool differs from the one the machine holds -> park at safe Z, then
//     T.. M6;
//   * the tool is the same -> no T/M6 at all: the machine already holds it;
//   * the spindle was off or its speed differs -> S.. M3;
//   * the coolant is on and was off -> M8.
//
// The order is the one of the reference post: UI_TOOL, park, change, spindle,
// coolant. The park before the change is mandatory: M6 releases the tool, and
// a tool inside the material is a crash whatever the machine's own retract does.
void emitOperationStart(std::string& out, const Program& program, const Operation& operation,
                        int& sequence, const std::string& uuid, EmitState& state) {
    tag(out, "UI_OP: " + commentSafe(operation.id.empty() ? std::string("Операция") : operation.id));
    // The viewer takes the label after "UI_OP:" and splits stages there; the
    // stable identifier travels in its own tag, so a stale preview can be told
    // from the fresh one without breaking the label.
    tag(out, "UI_STAGE: " + uuid);
    tag(out, "UI_TOOL: T=" + std::to_string(operation.tool.number) + " TYPE=" +
                 toolType(operation.geometry) + " D=" + num(operation.tool.diameterMm));
    if (state.tool != operation.tool.number) {
        line(out, numberedBlock(sequence, "G0 Z" + coord(program.safeZ) + " T" +
                                               std::to_string(operation.tool.number) + " M6"));
        sequence += kSequenceStep;
        state.tool = operation.tool.number;
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
