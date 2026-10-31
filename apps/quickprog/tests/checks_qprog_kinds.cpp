// tests/checks_qprog_kinds.cpp - the kinds the panel writes and the demo does
// not plan (PLAN-QUICKPROG, 7.6).
//
// The panel writes every kind of the document into the same schema-1 `.qprog`;
// the demo is the reader of that file, not its planner. A kind the demo does
// not plan (everything but face, pocket, circle-pocket and drill) must be read
// into the document, named in the summary as skipped, and must not stop the
// supported operations from being planned and emitted. A kind word the schema
// does not know is still a refusal: the reader names the line.
//
// The files below are written by hand in the panel spelling (the bytes the
// panel `serialize-write.ts` produces), so the case does not lean on the demo
// writer: a file the demo never writes must still read.
#include <string>
#include <vector>

#include "cli/app.h"
#include "core/cli.h"
#include "core/ops.h"
#include "core/serialize.h"
#include "core/serialize_internal.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace qprog_kinds_checks {

using qpcheck::check;
using qpcheck::contains;

namespace {

// The header of every file of this case: the demo stock, the console defaults.
std::string header(const std::string& name) {
    return "schema 1\nname \"" + name +
           "\"\nunits mm\nwcs G54\nstock -50 -40 -20 50 40 0\nsafeZ 5\n"
           "g64P 0.01\ng64Q 0.01\nstopAfterOp off\n";
}

// The common `op.*` lines of a kind: the same thirteen keys for every kind.
// A drill of the table is written with one feed, the milling kinds with their
// own plunge; `plunge` is the number on the line.
std::string common(int tool, const std::string& plunge) {
    return "op.enabled on\nop.tool " + std::to_string(tool) +
           " 8\nop.spindle 6000\nop.feed 1000\nop.plunge " + plunge +
           "\nop.coolant flood\nop.topZ 0\nop.depth 3\nop.stepDown 2\n";
}

// One operation of a new kind: the `op` head, the common lines and its own.
struct KindCase {
    std::string kind;     // the word on the `op` line
    std::string shape;    // the shape lines of the kind
    std::string message;  // the skip line the summary must carry
};

std::vector<KindCase> newKinds() {
    return {
        {"contour", "op.points 10 0 30 0 30 20 10 20\nop.closed on\nop.allowance 0\nop.side material\n",
         "Операция 2: вид contour не поддерживается демо, пропущена"},
        {"slot", "op.from -10 20 0\nop.to 20 20 0\nop.width 8\nop.stepOver 70\n",
         "Операция 2: вид slot не поддерживается демо, пропущена"},
        {"chamfer", "op.from 0 -30 0\nop.to 20 -30 0\nop.chamfer 1\n",
         "Операция 2: вид chamfer не поддерживается демо, пропущена"},
        {"tap", "op.pitch 1.25\nop.pattern grid\nop.grid -20 -20 2 2 10 10\nop.peck 0\n",
         "Операция 2: вид tap не поддерживается демо, пропущена"},
        {"counterbore", "op.seat 13\nop.seatDepth 6.5\nop.dwell 0.2\nop.r 1\nop.pattern hole\n"
                        "op.hole -20 -20 3.4\n",
         "Операция 2: вид counterbore не поддерживается демо, пропущена"},
        {"thread", "op.hole 20 -20 3.4\nop.pitch 1.25\nop.internal on\nop.threadDepth 12\n"
                   "op.allowance 0.1\nop.stepOver 70\n",
         "Операция 2: вид thread не поддерживается демо, пропущена"},
        {"engrave", "op.text \"DETAIL 42\"\nop.x -40\nop.y 30\nop.height 5\nop.angle 0\n",
         "Операция 2: вид engrave не поддерживается демо, пропущена"},
        {"pocket-island", "op.rect 0 0 40 30\nop.island 15 10 10 10\nop.islandAllowance 0.1\n"
                          "op.stepOver 70\n",
         "Операция 2: вид pocket-island не поддерживается демо, пропущена"},
        {"rest", "op.source 0\nop.rect -30 -20 60 40\nop.allowance 0.1\nop.stepOver 70\n",
         "Операция 2: вид rest не поддерживается демо, пропущена"},
        {"probe", "op.points 0 0 0\nop.range 30\nop.touch 100\nop.axis Z\n",
         "Операция 2: вид probe не поддерживается демо, пропущена"},
        {"custom", "op.gcode \"G0 X0 Y0\"\nop.gcode \"(тест)\"\n",
         "Операция 2: вид custom не поддерживается демо, пропущена"},
    };
}

// A file of a face (planned) and one new kind after it (skipped): the order of
// the skip line is the order of the operation in the file.
std::string document(const KindCase& one) {
    std::string text = header("KINDS " + one.kind);
    text += "op face \"Торцевание\"\n" + common(1, "250") + "op.z -1\nop.stepOver 70\n";
    text += "op " + one.kind + " \"Новый\"\n" + common(4, "1000") + one.shape;
    return text;
}

} // namespace

// Every kind the panel writes is read into the document with its own fields:
// the reader does not turn a new kind into an unknown one and does not drop the
// file. The shape line of each kind is read by its own key table, so one
// branching error shows as a parse failure of that kind only.
void checkKindsRead() {
    for (const KindCase& one : newKinds()) {
        const std::string text = document(one);
        const qp::ParseOutcome parsed = qp::parseProgram(text);
        check(parsed.ok, "qprog_kinds.read." + one.kind,
              parsed.ok ? "" : "the file of " + one.kind + " did not read: " + parsed.error);
        if (!parsed.ok) { continue; }
        const qp::Program& program = parsed.program;
        check(program.operations.size() == 2, "qprog_kinds.count." + one.kind,
              "the file must read two operations, got " + std::to_string(program.operations.size()));
        if (program.operations.size() != 2) { continue; }
        check(program.operations[1].id == "Новый", "qprog_kinds.id." + one.kind,
              "the id of the second operation is '" + program.operations[1].id + "'");
        check(program.operations[1].tool.number == 4, "qprog_kinds.tool." + one.kind,
              "the tool of the second operation must be 4");
        check(qp::kindName(program.operations[1].geometry) == one.kind,
              "qprog_kinds.kind." + one.kind,
              "the kind word of the second operation is '" +
                  std::string(qp::kindName(program.operations[1].geometry)) + "'");
    }
}

// The shaped values of the new kinds come back: a text with a space, the
// points of a contour, the seat of a counterbore, the source list of a rest.
void checkKindsFields() {
    const qp::ParseOutcome parsed = qp::parseProgram(document(newKinds()[6]));
    check(parsed.ok, "qprog_kinds.fields.ok", parsed.ok ? "" : parsed.error);
    if (!parsed.ok || parsed.program.operations.size() != 2) { return; }
    const qp::Operation& op = parsed.program.operations[1];
    // The new kinds are the data-only geometry: the reader fills the keys and
    // the demo never looks at the fields it does not plan.
    const auto* engrave = std::get_if<qp::EngraveOp>(&op.geometry);
    check(engrave != nullptr, "qprog_kinds.fields.kind", "the engrave must read as an engrave");
    if (engrave == nullptr) { return; }
    check(engrave->text == "DETAIL 42", "qprog_kinds.fields.text",
          "the text with a space came back as '" + engrave->text + "'");
    check(qpcheck::near(engrave->heightMm, 5.0), "qprog_kinds.fields.height", "the height must be 5");
}

// The `--qprog` mode of the console: the supported operation is planned and
// written, the new kind is named as skipped, and the exit code stays 0.
void checkKindsConsole() {
    const std::string output = "/tmp/opencode/qprog-kinds.out.ngc";
    for (const KindCase& one : newKinds()) {
        const std::string input = "/tmp/opencode/qprog-kinds." + one.kind + ".qprog";
        check(qp::writeFile(input, document(one)), "qprog_kinds.write." + one.kind,
              "the case must write its file");
        qpcheck::ConsoleCapture capture;
        const int code = qpcli::run({"--qprog", input, output}, capture.out, capture.err);
        const std::string out = capture.out.str();
        const std::string err = capture.err.str();
        check(code == 0, "qprog_kinds.exit." + one.kind,
              "the console must exit 0, got " + std::to_string(code) + ": " + err);
        check(contains(out, one.message), "qprog_kinds.skip." + one.kind,
              "the summary must name the skipped operation '" + one.message + "', got: " + out);
        check(contains(out, "Операция 1/1: Торцевание"), "qprog_kinds.planned." + one.kind,
              "the planned face must stay the only planned operation in the summary");
        std::string written;
        const bool read = qp::readFile(output, written);
        check(read && contains(written, "UI_OP: Торцевание"), "qprog_kinds.file." + one.kind,
              "the .ngc must carry the planned face");
        check(read && !contains(written, "UI_OP: Новый"), "qprog_kinds.file_skipped." + one.kind,
              "the skipped operation must not reach the .ngc");
    }
}

// A disabled planned operation is not "unsupported": the summary of the file
// names only the kinds the demo cannot plan, and the disabled face keeps its
// place in the count.
void checkDisabledIsNotSkipped() {
    std::string text = header("DISABLED");
    text += "op face \"Торцевание\"\n" + common(1, "250") + "op.z -1\nop.stepOver 70\n";
    std::string off = common(1, "250");
    off.replace(off.find("op.enabled on"), std::string("op.enabled on").size(), "op.enabled off");
    text += "op circle \"Круг\"\n" + off + "op.circle 0 0 15\nop.finishAllowance 0\nop.stepOver 70\n";
    const std::string input = "/tmp/opencode/qprog-kinds.disabled.qprog";
    check(qp::writeFile(input, text), "qprog_kinds.disabled.write", "the case must write its file");
    qpcheck::ConsoleCapture capture;
    const int code = qpcli::run({"--qprog", input, "/tmp/opencode/qprog-kinds.disabled.ngc"},
                                capture.out, capture.err);
    const std::string out = capture.out.str();
    check(code == 0, "qprog_kinds.disabled.exit", "a disabled operation must not block the run: " +
                                                     capture.err.str());
    check(!contains(out, "не поддерживается"), "qprog_kinds.disabled.no_skip",
          "a disabled but supported operation is not a skip, got: " + out);
}

// A word the schema does not know is still named with its line: the reader
// does not guess a kind it was not told about.
void checkUnknownKind() {
    std::string text = header("UNKNOWN");
    text += "op face \"Торцевание\"\n" + common(1, "250") + "op.z -1\nop.stepOver 70\n";
    text += "op laser \"Лазер\"\n" + common(1, "250");
    const qp::ParseOutcome parsed = qp::parseProgram(text);
    check(!parsed.ok, "qprog_kinds.unknown.refused", "an unknown kind must be refused");
    check(contains(parsed.error, "неизвестный вид операции 'laser'"), "qprog_kinds.unknown.message",
          "the refusal must name the kind, got: " + parsed.error);
}

void runQprogKindsChecks() {
    checkKindsRead();
    checkKindsFields();
    checkKindsConsole();
    checkDisabledIsNotSkipped();
    checkUnknownKind();
}

} // namespace qprog_kinds_checks
