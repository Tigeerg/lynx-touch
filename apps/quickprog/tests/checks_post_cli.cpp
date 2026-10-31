// tests/checks_post_cli.cpp - the post-processor keys and the summary (7.3).
//
// The keys of the console - `--h`, `--step-over`, `--stop`, `--no-stop`,
// `--g64` - land in the document of every mode, and the summary prints the
// program lines the operator reads before a run. The check calls the dispatcher
// with its own streams, so the exact text of the summary is a case, not a
// manual look at the console.
#include <cstddef>
#include <string>
#include <vector>

#include "cli/app.h"
#include "cli/post_words.h"
#include "cli/run.h"
#include "core/ops.h"
#include "core/post_options.h"
#include "core/serialize.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace post_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::near;

namespace {

// The words after `--op pocket` and one output file, with the post words in
// front: the dispatcher takes them out before the kind parser sees the line.
std::string runConsole(const std::vector<std::string>& words) {
    qpcheck::ConsoleCapture capture;
    const int code = qpcli::run(words, capture.out, capture.err);
    check(code == 0, "post.cli_exit", "the console must exit 0, got " + std::to_string(code) +
                                          ": " + capture.err.str());
    return capture.out.str();
}

} // namespace

// The words are taken out of the line in every mode and land in the fields.
void checkTakeWords() {
    std::vector<std::string> args = {"--op", "pocket", "--h", "7", "--step-over", "50",
                                     "--g64", "0.05", "0.02", "--no-stop", "p.ngc"};
    qp::PostArgs post;
    check(qpcli::takePostWords(args, post), "post.cli_take_ok", "the words must parse");
    check(args.size() == 3 && args[0] == "--op" && args[2] == "p.ngc", "post.cli_take_rest",
          "the kind parser must see its own words only, got " + std::to_string(args.size()));
    check(post.hGiven && post.h == 7 && post.stepOverGiven && near(post.stepOverPct, 50.0) &&
              post.g64Given && near(post.g64P, 0.05) && near(post.g64Q, 0.02) &&
              !post.stopAfterOp,
          "post.cli_take_fields", "every word must land in its field");

    std::vector<std::string> oneNumber = {"--op", "face", "--g64", "0.05", "f.ngc"};
    qp::PostArgs bad;
    check(!qpcli::takePostWords(oneNumber, bad), "post.cli_g64_one_number",
          "one number of --g64 must be a usage error");
    std::vector<std::string> noValue = {"--op", "face", "--h"};
    qp::PostArgs empty;
    check(!qpcli::takePostWords(noValue, empty), "post.cli_h_no_value",
          "--h without a value must be a usage error");
    std::vector<std::string> text = {"--h", "seven", "f.ngc"};
    qp::PostArgs word;
    check(!qpcli::takePostWords(text, word), "post.cli_h_number",
          "a non-number of --h must be a usage error");
}

// The console run of `--op pocket` writes the document the words asked for.
void checkPocketDocument() {
    const std::string out = runConsole({"--op", "pocket", "--tool", "4", "--diameter", "8",
                                        "--size", "60x40", "--depth", "6", "--step-down", "3",
                                        "--h", "9", "--step-over", "50", "--g64", "0.05", "0.02",
                                        "--no-stop", qpcheck::tempPath("post-pocket.ngc")});
    check(contains(out, "коррекция H9"), "post.cli_h_summary",
          "the summary must print the H row of the tool, got no 'коррекция H9'");
    check(contains(out, "G64 P0.050 Q0.020"), "post.cli_g64_summary",
          "the summary must print the tolerance");
    check(contains(out, "Остановка между операциями: нет"), "post.cli_stop_summary",
          "the summary must print the disabled stop");
    check(!contains(out, "Проверки не прошли"), "post.cli_valid", "the document must pass");
}

// `--op face --step-over` keeps its own meaning: the face parser reads it (the
// strips), the post-processor words leave it alone.
void checkFaceStep() {
    const std::string out = runConsole({"--op", "face", "--diameter", "10", "--step-over", "50",
                                        qpcheck::tempPath("post-face.ngc")});
    check(contains(out, "полос: 15"), "post.cli_face_step",
          "a 100x80 stock at 50 % of D10 needs 15 strips, got no 'полос: 15' in the summary");
}

// The demo: the summary lines of the program and the H row of both tools.
void checkDemoSummary() {
    const std::string out = runConsole({"--demo", qpcheck::tempPath("post-demo.ngc")});
    check(contains(out, "Смещение: G54"), "post.cli_demo_wcs",
          "the summary must print the work offset");
    check(contains(out, "G64 P0.010 Q0.010"), "post.cli_demo_g64",
          "the summary must print the default tolerance");
    check(contains(out, "Остановка между операциями: да"), "post.cli_demo_stop",
          "the summary must print the enabled stop");
    check(contains(out, "коррекция H2") && contains(out, "коррекция H3"), "post.cli_demo_h",
          "each operation must print its H row");
}

} // namespace post_checks

// Runs the console and summary cases of the new fields.
void runPostCliChecks() {
    post_checks::checkTakeWords();
    post_checks::checkPocketDocument();
    post_checks::checkFaceStep();
    post_checks::checkDemoSummary();
}
