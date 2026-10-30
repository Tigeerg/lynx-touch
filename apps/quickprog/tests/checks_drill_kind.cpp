// tests/checks_drill_kind.cpp - the drill table and the tool material (7.2).
//
// The drill is a table row now, not a rule of thumb: Vc and the feed per
// revolution (`k * D`) per material and per drill material (HSS or carbide).
// The cases fix the numbers of every row, the Russian source line that names
// the drill material, the two spellings of the drill material (`--drill-kind`
// and `op.drill`) and the default of an older .qprog without the field.
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "cli/app.h"
#include "cli/operation_program.h"
#include "cli/run.h"
#include "core/drill_options.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/serialize.h"
#include "core/summary.h"
#include "core/validate.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace drill_kind_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::demoProgram;

// The expected numbers are the table of feeds.cpp computed by hand with the
// two calc.ts formulas: n = round(1000 * Vc / (pi * D)), F = round(k * D * n).
struct Row {
    qp::Material material;
    qp::DrillBody kind;
    double vc;
    double k;          // f = k * D, mm/rev
    double rpm;
    double feed;
    const char* word;  // the drill kind word of the source line
    const char* id;
};

void checkTable() {
    const Row rows[] = {
        {qp::Material::Aluminium, qp::DrillBody::Hss, 40.0, 0.02, 2122.0, 255.0, "HSS",
         "alu_hss"},
        {qp::Material::Aluminium, qp::DrillBody::Carbide, 120.0, 0.03, 6366.0, 1146.0, "карбид",
         "alu_carbide"},
        {qp::Material::Steel, qp::DrillBody::Hss, 20.0, 0.015, 1061.0, 95.0, "HSS", "steel_hss"},
        {qp::Material::Steel, qp::DrillBody::Carbide, 70.0, 0.02, 3714.0, 446.0, "карбид",
         "steel_carbide"},
    };
    for (const Row& row : rows) {
        const qp::Feeds out = qp::feedsForDrill(row.material, 6.0, row.kind);
        const std::string id = std::string("drillkind.") + row.id;
        check(out.rpm == row.rpm, id + ".rpm",
              "D6, Vc " + std::to_string(row.vc) + " must be " + std::to_string(row.rpm) +
                  " rpm, got " + std::to_string(out.rpm));
        check(out.feedMmMin == row.feed, id + ".feed",
              "D6, f = " + std::to_string(row.k) + " * D at " + std::to_string(row.rpm) +
                  " rpm must be " + std::to_string(row.feed) + " mm/min, got " +
                  std::to_string(out.feedMmMin));
        check(out.source == std::string("таблица: сверло ") + row.word + ", " +
                                 qp::materialName(row.material) + ", ф6",
              id + ".source", "the source line must name the drill material, got '" + out.source +
                                  "'");
    }
}

// The same rows on D10: the feed follows the diameter, the speed does not.
void checkDiameter() {
    const qp::Feeds hss = qp::feedsForDrill(qp::Material::Aluminium, 10.0, qp::DrillBody::Hss);
    check(hss.rpm == 1273.0 && hss.feedMmMin == 255.0, "drillkind.alu_hss_d10",
          "Vc 40 on D10 is 1273 rpm, f = 0.02 * 10 = 0.2 mm/rev gives 255 mm/min, got " +
              std::to_string(hss.rpm) + " rpm, " + std::to_string(hss.feedMmMin) + " mm/min");
    const qp::Feeds steel = qp::feedsForDrill(qp::Material::Steel, 10.0, qp::DrillBody::Hss);
    check(steel.rpm == 637.0 && steel.feedMmMin == 96.0, "drillkind.steel_hss_d10",
          "Vc 20 on D10 is 637 rpm, f = 0.015 * 10 at 637 rpm gives 95.55 -> 96 mm/min, got " +
              std::to_string(steel.rpm) + " rpm, " + std::to_string(steel.feedMmMin) + " mm/min");
}

// The two console words: `hss` and `carbide`, nothing else.
void checkWords() {
    qp::DrillBody kind = qp::DrillBody::Hss;
    check(qp::parseDrillKind("carbide", kind) && kind == qp::DrillBody::Carbide,
          "drillkind.word_carbide", "the word 'carbide' must select the carbide drill");
    check(qp::parseDrillKind("hss", kind) && kind == qp::DrillBody::Hss, "drillkind.word_hss",
          "the word 'hss' must select the HSS drill");
    check(!qp::parseDrillKind("diamond", kind), "drillkind.word_bad",
          "an unknown drill word must be refused");
}

// `--drill-kind carbide` changes the numbers of the document and the summary
// prints the drill material; without the word the document keeps HSS.
void checkConsole() {
    qp::DrillArgs hss;
    check(qp::parseDrillArgs({"--op", "drill", "d.ngc"}, 2, hss), "drillkind.args_default",
          "the drill words must parse");
    check(hss.kind == qp::DrillBody::Hss, "drillkind.args_default_hss",
          "a drill without --drill-kind must be HSS");

    qp::DrillArgs carbide;
    check(qp::parseDrillArgs({"--op", "drill", "--drill-kind", "carbide", "d.ngc"}, 2, carbide),
          "drillkind.args_carbide", "the carbide word must parse");
    check(carbide.kind == qp::DrillBody::Carbide, "drillkind.args_carbide_value",
          "--drill-kind carbide must land in the field");

    qp::DrillArgs bad;
    check(!qp::parseDrillArgs({"--op", "drill", "--drill-kind", "diamond", "d.ngc"}, 2, bad),
          "drillkind.args_bad", "an unknown --drill-kind must be a usage error");

    qpcheck::ConsoleCapture capture;
    const int code = qpcli::run({"--op", "drill", "--diameter", "6", "--drill-kind", "carbide",
                                 "/tmp/opencode/drill-kind.ngc"},
                                capture.out, capture.err);
    const std::string out = capture.out.str();
    check(code == 0, "drillkind.cli_exit", "the carbide drill must build, got " +
                                               std::to_string(code) + ": " + capture.err.str());
    check(contains(out, "6366 об/мин"), "drillkind.cli_rpm",
          "the summary must print the carbide rpm, got no '6366 об/мин'");
    check(contains(out, "таблица: сверло карбид, алюминий, ф6"), "drillkind.cli_source",
          "the summary must print the drill material in the source, got no carbide source");
}

// The .qprog: `op.drill carbide` is written only for the carbide drill, an
// older file without the key reads as HSS, and an unknown word is an error.
void checkQprog() {
    qp::DrillArgs args;
    args.tool = 5;
    args.diameter = 6.0;
    args.depth = 12.0;
    args.kind = qp::DrillBody::Carbide;
    const std::string text = qpcli::documentText(qpcli::drillProgram(args));
    check(contains(text, "op.drill carbide\n"), "drillkind.qprog_written",
          "a carbide drill must write op.drill carbide, got:\n" + text);
    const qp::ParseOutcome back = qp::parseProgram(text);
    check(back.ok, "drillkind.qprog_readable", back.ok ? "" : back.error);
    if (back.ok && !back.program.operations.empty()) {
        check(std::get<qp::DrillHoleOp>(back.program.operations.front().geometry).params.kind ==
                  qp::DrillBody::Carbide,
              "drillkind.qprog_read_carbide", "op.drill carbide must come back as carbide");
    }

    args.kind = qp::DrillBody::Hss;
    const std::string hssText = qpcli::documentText(qpcli::drillProgram(args));
    check(!contains(hssText, "op.drill"), "drillkind.qprog_hss_omitted",
          "an HSS drill is the default and writes no op.drill line");
    const qp::ParseOutcome hss = qp::parseProgram(hssText);
    check(hss.ok && !hss.program.operations.empty() &&
              std::get<qp::DrillHoleOp>(hss.program.operations.front().geometry).params.kind ==
                  qp::DrillBody::Hss,
          "drillkind.qprog_default_hss", "a file without op.drill must read as HSS");

    std::string broken = text;
    broken.replace(broken.find("op.drill carbide"), std::string("op.drill carbide").size(),
                   "diamond");
    const qp::ParseOutcome bad = qp::parseProgram(broken);
    check(!bad.ok, "drillkind.qprog_bad_word", "an unknown op.drill word must be refused");
}

// The operation summary reads the material of the operation, so a carbide
// document prints the carbide source without any extra argument.
void checkSummary() {
    qp::DrillArgs args;
    args.tool = 5;
    args.diameter = 6.0;
    args.depth = 12.0;
    args.kind = qp::DrillBody::Carbide;
    qp::Program program = demoProgram();
    program.operations.push_back(qpcli::drillProgram(args).operations.front());
    const qp::Path planned = qp::planPath(program);
    const std::string text = qp::operationSummary(program, planned, 0, 0, planned.moves.size());
    check(contains(text, "режимы: 6366 об/мин"), "drillkind.summary_rpm",
          "the operation summary must print the carbide rpm, got:\n" + text);
    check(contains(text, "сверло карбид"), "drillkind.summary_source",
          "the operation summary must name the drill material, got:\n" + text);
    check(qp::validate(program).empty(), "drillkind.summary_valid",
          "a carbide drill document must pass the checks");

    // A steel HSS drill on the demo spindle: Vc 20 on D6 is 1061 rpm, the
    // summary carries both numbers of the steel row.
    qp::DrillArgs steel;
    steel.tool = 5;
    steel.diameter = 6.0;
    steel.depth = 12.0;
    steel.material = qp::Material::Steel;
    qp::Program steelProgram = demoProgram();
    steelProgram.operations.push_back(qpcli::drillProgram(steel).operations.front());
    const qp::Path steelPlan = qp::planPath(steelProgram);
    const std::string steelText =
        qp::operationSummary(steelProgram, steelPlan, 0, 0, steelPlan.moves.size());
    check(contains(steelText, "режимы: 1061 об/мин"), "drillkind.summary_steel_rpm",
          "a steel drill must print its steel speed, got:\n" + steelText);
    check(qp::validate(steelProgram).empty(), "drillkind.summary_steel_valid",
          "a steel HSS drill on the demo spindle must pass the checks");
}

} // namespace drill_kind_checks

// Runs the drill table cases: the rows, the words, the console, the file and
// the summary.
void runDrillKindChecks() {
    drill_kind_checks::checkTable();
    drill_kind_checks::checkDiameter();
    drill_kind_checks::checkWords();
    drill_kind_checks::checkConsole();
    drill_kind_checks::checkQprog();
    drill_kind_checks::checkSummary();
}
