// tests/checks_qprog.cpp - the console arguments and the safety rules.
//
// The word parsers of `--op face` and `--op pocket`: defaults of the help text,
// a full set of typed keys and the usage errors. The check does not run the
// binary, so a broken parser fails here and not in a manual run. The rules that
// name a field (a depth below the stock bottom, a zero step, a safe Z below the
// top, a zero diameter) stay with the face document they were written for.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/face_options.h"
#include "core/ops.h"
#include "core/pocket_options.h"
#include "core/validate.h"

#include "tests/harness.h"

namespace qprog_checks {

using qpcheck::check;
using qpcheck::faceProgram;
using qpcheck::near;

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

// `--op pocket`: the defaults of the help text, the keys of the shape and the
// usage errors. The parser decides nothing about the toolpath; the shape words
// land in their fields and the output file is the bare word.
void checkPocketArgs() {
    {
        qp::PocketArgs args;
        const std::vector<std::string> words = {"--op", "pocket", "p.hot"};
        check(qp::parsePocketArgs(words, 2, args) && args.output == "p.hot", "pocket_args.output",
              "the bare word must be the output file");
        check(args.tool == 1 && near(args.diameter, 10.0) && near(args.width, 60.0) &&
                  near(args.height, 40.0) && !args.safeGiven,
              "pocket_args.defaults", "the defaults must be T1, D10, 60x40");
    }
    {
        qp::PocketArgs args;
        const std::vector<std::string> words = {"--op",    "pocket", "--size",    "50x30",
                                                "--x",     "-10",    "--y",       "5",
                                                "--corner", "2.5",   "--allowance", "0.2",
                                                "--depth", "8",      "p2.ngc"};
        const bool ok = qp::parsePocketArgs(words, 2, args);
        check(ok && near(args.width, 50.0) && near(args.height, 30.0) && near(args.x, -10.0) &&
                  near(args.y, 5.0) && near(args.cornerRadius, 2.5) &&
                  near(args.finishAllowance, 0.2) && near(args.depth, 8.0) && args.output == "p2.ngc",
              "pocket_args.shape", "the shape keys must land in their fields");
    }
    {
        qp::PocketArgs args;
        const std::vector<std::string> badSize = {"--op", "pocket", "--size", "50", "p.ngc"};
        check(!qp::parsePocketArgs(badSize, 2, args), "pocket_args.bad_size",
              "a size without the second number must fail");
        qp::PocketArgs noOut;
        const std::vector<std::string> onlyKeys = {"--op", "pocket", "--depth", "2"};
        check(!qp::parsePocketArgs(onlyKeys, 2, noOut), "pocket_args.no_output",
              "no output file must fail");
    }
}

} // namespace qprog_checks

// Runs the cases of this file: the entry point calls the four runners in the
// order of the slices.
void runQprogChecks() {
    qprog_checks::checkFaceArgs();
    qprog_checks::checkPocketArgs();
    qprog_checks::checkValidate();
    qprog_checks::checkValidateFields();
}
