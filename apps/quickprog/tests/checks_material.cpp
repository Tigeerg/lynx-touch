// tests/checks_material.cpp - the material of an operation (7.2).
//
// The material is a field of the operation, not of the console: the numbers and
// the source line of the summary are read from the document, so a saved .qprog
// keeps the row it was planned by. The cases fix the mill and the drill row of
// steel on D10, the source words the operator reads, the console word, the two
// .qprog cases (no field and `op.material steel`) and the refusal of an unknown
// material.
#include <string>
#include <vector>

#include "cli/app.h"
#include "cli/operation_program.h"
#include "cli/run.h"
#include "core/drill_options.h"
#include "core/face_options.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/serialize.h"
#include "core/summary.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace material_checks {

using qpcheck::check;
using qpcheck::contains;

// The operation summary of the single operation of a console document.
std::string summaryOf(const qp::Program& program) {
    const qp::Path planned = qp::planPath(program);
    return qp::operationSummary(program, planned, 0, 0, planned.moves.size());
}

// The default of the document is the aluminium row, as before the field.
void checkDefault() {
    qp::Operation op;
    check(op.material == qp::Material::Aluminium, "material.default_aluminium",
          "an operation without the field must keep the aluminium row");

    qp::FaceArgs args;
    qp::parseFaceArgs({"--op", "face", "f.ngc"}, 2, args);
    const qp::Program program = qpcli::faceProgram(args);
    check(!program.operations.empty() &&
              program.operations.front().material == qp::Material::Aluminium,
          "material.face_default", "a face document without --material must be aluminium");
}

// `--material steel` on a flat end mill: the numbers of the steel row of
// calc.ts (Vc 90, fz 0.03, 3 flutes) and the steel word of the source line.
void checkMillSteel() {
    qpcheck::ConsoleCapture capture;
    const int code = qpcli::run({"--op", "face", "--diameter", "10", "--stock", "100x80x20",
                                 "--top", "0", "--depth", "2", "--step-down", "2",
                                 "--material", "steel", "/tmp/opencode/material-face.ngc"},
                                capture.out, capture.err);
    const std::string out = capture.out.str();
    check(code == 0, "material.mill_steel_exit",
          "a steel face must build, got " + std::to_string(code) + ": " + capture.err.str());
    check(contains(out, "режимы: 2865 об/мин, подача 258 мм/мин"), "material.mill_steel_numbers",
          "D10 steel is Vc 90 -> 2865 rpm and 0.03 x 3 x 2865 -> 258 mm/min, got:\n" + out);
    check(contains(out, "таблица: сталь, ф10"), "material.mill_steel_source",
          "the source line must name steel, got:\n" + out);
    check(!contains(out, "таблица: алюминий"), "material.mill_no_aluminium",
          "the source line must not name aluminium for a steel operation, got:\n" + out);
}

// The same word on a drill: the HSS row of steel in the source line and the
// numbers of that row (Vc 20 on D10 is 637 rpm, 0.015 * 10 -> 96 mm/min).
void checkDrillSteel() {
    qpcheck::ConsoleCapture capture;
    const int code = qpcli::run({"--op", "drill", "--diameter", "10", "--depth", "12",
                                 "--material", "steel", "/tmp/opencode/material-drill.ngc"},
                                capture.out, capture.err);
    const std::string out = capture.out.str();
    check(code == 0, "material.drill_steel_exit",
          "a steel drill must build, got " + std::to_string(code) + ": " + capture.err.str());
    check(contains(out, "таблица: сверло HSS, сталь, ф10"), "material.drill_steel_source",
          "the drill source must name HSS, steel and D10, got:\n" + out);
    check(contains(out, "режимы: 637 об/мин, подача 96 мм/мин"), "material.drill_steel_numbers",
          "D10 HSS in steel is 637 rpm and 96 mm/min, got:\n" + out);
}

// The .qprog: a file without `op.material` reads as aluminium, a file with
// `op.material steel` reads as steel, and an unknown word is an error naming
// the field.
void checkQprog() {
    qp::FaceArgs steelArgs;
    steelArgs.material = qp::Material::Steel;
    const qp::Program steel = qpcli::faceProgram(steelArgs);
    const std::string steelText = qpcli::documentText(steel);
    check(contains(steelText, "op.material steel\n"), "material.qprog_written",
          "a steel operation must write op.material steel, got:\n" + steelText);
    const qp::ParseOutcome steelBack = qp::parseProgram(steelText);
    check(steelBack.ok && !steelBack.program.operations.empty() &&
              steelBack.program.operations.front().material == qp::Material::Steel,
          "material.qprog_read_steel",
          steelBack.ok ? "op.material steel must come back as steel" : steelBack.error);
    if (steelBack.ok) {
        check(contains(summaryOf(steelBack.program), "таблица: сталь, ф10"),
              "material.qprog_summary_steel",
              "the summary of the read steel document must name steel");
    }

    qp::FaceArgs aluArgs;
    const std::string aluText = qpcli::documentText(qpcli::faceProgram(aluArgs));
    const qp::ParseOutcome aluBack = qp::parseProgram(aluText);
    check(aluBack.ok && !aluBack.program.operations.empty() &&
              aluBack.program.operations.front().material == qp::Material::Aluminium,
          "material.qprog_default_aluminium",
          aluBack.ok ? "a file without op.material must read as aluminium" : aluBack.error);
    if (aluBack.ok) {
        check(contains(summaryOf(aluBack.program), "таблица: алюминий, ф10"),
              "material.qprog_summary_aluminium",
              "the summary of the default document must name aluminium");
    }

    std::string broken = aluText;
    broken.replace(broken.find("op.feed "), 0, "op.material titan\n");
    const qp::ParseOutcome bad = qp::parseProgram(broken);
    check(!bad.ok && contains(bad.error, "material") && contains(bad.error, "aluminium") &&
              contains(bad.error, "steel"),
          "material.qprog_bad_word",
          "an unknown op.material must fail on 'material' and list the words, got: " +
              (bad.ok ? std::string("no error") : bad.error));

    // A document of the previous build: the line is removed, not just unset.
    std::string older = steelText;
    const std::string line = "op.material steel\n";
    const std::size_t at = older.find(line);
    if (at != std::string::npos) { older.erase(at, line.size()); }
    const qp::ParseOutcome olderBack = qp::parseProgram(older);
    check(olderBack.ok && !olderBack.program.operations.empty() &&
              olderBack.program.operations.front().material == qp::Material::Aluminium,
          "material.qprog_older_file",
          olderBack.ok ? "a steel line removed by hand must read as aluminium" : olderBack.error);
}

// The console word: two values, an unknown value is a usage error.
void checkWords() {
    qp::FaceArgs steel;
    check(qp::parseFaceArgs({"--op", "face", "--material", "steel", "f.ngc"}, 2, steel) &&
              steel.material == qp::Material::Steel,
          "material.args_steel", "--material steel must land in the field");
    qp::DrillArgs alu;
    check(qp::parseDrillArgs({"--op", "drill", "--material", "aluminium", "d.ngc"}, 2, alu) &&
              alu.material == qp::Material::Aluminium,
          "material.args_aluminium", "--material aluminium must land in the field");
    qp::FaceArgs bad;
    check(!qp::parseFaceArgs({"--op", "face", "--material", "titan", "f.ngc"}, 2, bad),
          "material.args_bad", "an unknown --material must be a usage error");

    // The demo stays aluminium: its summary names the aluminium rows.
    qpcheck::ConsoleCapture capture;
    const int code = qpcli::run({"--demo", "/tmp/opencode/material-demo.ngc"}, capture.out,
                                capture.err);
    check(code == 0 && contains(capture.out.str(), "таблица: алюминий"),
          "material.demo_aluminium", "the demo must use aluminium and say so");
}

} // namespace material_checks

// Runs the material cases: the default, the mill, the drill, the file and the
// words.
void runMaterialChecks() {
    material_checks::checkDefault();
    material_checks::checkMillSteel();
    material_checks::checkDrillSteel();
    material_checks::checkQprog();
    material_checks::checkWords();
}
