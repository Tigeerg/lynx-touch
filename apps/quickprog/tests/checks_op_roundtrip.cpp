// tests/checks_op_roundtrip.cpp - every `--op` document survives a .qprog
// round-trip (7.2).
//
// The console builds the document of each kind, the writer writes the bytes the
// `--save-qprog` flag writes, and the reader reads them back. The check compares
// the document field by field and the plan of the two documents move by move:
// the point is that a saved file is never rejected by `--qprog` and that the
// toolpath does not move with the file. The bytes are not compared: the writer
// fills the defaults of the model, and the reader returns them, so a byte
// comparison would only repeat the writer.
#include <cstddef>
#include <string>
#include <vector>

#include "cli/operation_program.h"
#include "cli/run.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/serialize.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace op_roundtrip_checks {

using qpcheck::check;
using qpcheck::near;

// The four documents with one typed key of the kind, so the case is not the
// bare default either: the feed, the shape and the tool change on every kind.
qp::FaceArgs faceArgs() {
    qp::FaceArgs args;
    args.tool = 2;
    args.diameter = 10.0;
    args.depth = 5.0;
    args.stepDown = 2.0;
    return args;
}

qp::PocketArgs pocketArgs() {
    qp::PocketArgs args;
    args.tool = 3;
    args.diameter = 8.0;
    args.width = 50.0;
    args.height = 30.0;
    args.depth = 6.0;
    args.stepDown = 2.5;
    args.finishAllowance = 0.2;
    return args;
}

qp::CirclePocketArgs circleArgs() {
    qp::CirclePocketArgs args;
    args.tool = 4;
    args.diameter = 8.0;
    args.diameterPocket = 20.0;
    args.depth = 4.0;
    args.stepDown = 2.0;
    return args;
}

qp::DrillArgs drillArgs() {
    qp::DrillArgs args;
    args.tool = 5;
    args.diameter = 6.0;
    args.pattern = qp::DrillPattern::Grid;
    args.cols = 3;
    args.rows = 4;
    args.dx = 15.0;
    args.dy = 15.0;
    args.depth = 12.0;
    return args;
}

// One planned move is the other: the kind, the point, the feed and the arc.
bool sameMove(const qp::Move& a, const qp::Move& b) {
    return a.kind == b.kind && near(a.to.x, b.to.x) && near(a.to.y, b.to.y) && near(a.to.z, b.to.z) &&
           a.hasFeed == b.hasFeed && near(a.feedMmMin, b.feedMmMin) && a.hasArc == b.hasArc &&
           (!a.hasArc || (a.arc.cw == b.arc.cw && near(a.arc.i, b.arc.i) && near(a.arc.j, b.arc.j)));
}

// The same toolpath: the same moves in the same order, the same operation spans
// and the same cutting length.
bool samePlan(const qp::Path& a, const qp::Path& b) {
    if (a.moves.size() != b.moves.size() || a.operations.size() != b.operations.size() ||
        !near(a.lengthMm, b.lengthMm)) {
        return false;
    }
    for (std::size_t i = 0; i < a.moves.size(); ++i) {
        if (!sameMove(a.moves[i], b.moves[i])) { return false; }
    }
    for (std::size_t i = 0; i < a.operations.size(); ++i) {
        if (a.operations[i].opIndex != b.operations[i].opIndex ||
            a.operations[i].first != b.operations[i].first) {
            return false;
        }
    }
    return true;
}

// The document of the kind is written by `qpcli::documentText`, the very text
// `--save-qprog` puts on disk,
// read back and planned: the schema is the one of the format, the operation is
// the same, and the plan does not move.
void checkKind(const std::string& kind, const qp::Program& program) {
    const qp::ParseOutcome parsed = qp::parseProgram(qpcli::documentText(program));
    check(parsed.ok, "op." + kind + ".readable",
          parsed.ok ? "" : "the saved document is not readable: " + parsed.error);
    if (!parsed.ok) { return; }
    const qp::Program& back = parsed.program;
    check(back.schema == qp::kQprogSchema, "op." + kind + ".schema",
          "the saved document must carry schema " + std::to_string(qp::kQprogSchema) + ", got " +
              std::to_string(back.schema));
    check(back.operations.size() == program.operations.size() && !back.operations.empty(),
          "op." + kind + ".count", "the saved document must keep the operation");
    if (back.operations.size() != program.operations.size() || back.operations.empty()) { return; }
    const qp::Operation& a = program.operations.front();
    const qp::Operation& b = back.operations.front();
    check(a.geometry.index() == b.geometry.index() && a.id == b.id &&
              a.tool.number == b.tool.number && near(a.tool.diameterMm, b.tool.diameterMm) &&
              near(a.topZ, b.topZ) && near(a.depthMm, b.depthMm) &&
              near(a.stepDownMm, b.stepDownMm) &&
              near(a.cutting.feedMmMin, b.cutting.feedMmMin) &&
              near(a.cutting.spindleRpm, b.cutting.spindleRpm),
          "op." + kind + ".fields", "the header and the cutting data must come back unchanged");
    check(back.name == program.name && back.wcs == program.wcs && near(back.safeZ, program.safeZ) &&
              near(back.stock.min.x, program.stock.min.x) &&
              near(back.stock.max.z, program.stock.max.z),
          "op." + kind + ".document", "the document header must come back unchanged");
    check(samePlan(qp::planPath(program), qp::planPath(back)), "op." + kind + ".plan",
          "the toolpath of the read document must be the toolpath of the built one");
}

} // namespace op_roundtrip_checks

// Runs the round-trip of the four `--op` documents.
void runOpRoundTripChecks() {
    op_roundtrip_checks::checkKind("face", qpcli::faceProgram(op_roundtrip_checks::faceArgs()));
    op_roundtrip_checks::checkKind("pocket", qpcli::pocketProgram(op_roundtrip_checks::pocketArgs()));
    op_roundtrip_checks::checkKind("circle-pocket",
                                   qpcli::circlePocketProgram(op_roundtrip_checks::circleArgs()));
    op_roundtrip_checks::checkKind("drill", qpcli::drillProgram(op_roundtrip_checks::drillArgs()));
}
