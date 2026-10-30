// tests/checks_post_qprog.cpp - the post-processor fields through the .qprog (7.3).
//
// The new fields of the document survive the round-trip, and a document of the
// previous build - one without a single new key - is read with the defaults of
// the model. The second half matters more than the first: an operator's file
// from the last version has to open, not to be refused as a typo.
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "core/ops.h"
#include "core/serialize.h"

#include "tests/checks.h"
#include "tests/checks_pocket_ops.h"
#include "tests/harness.h"

namespace post_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::near;

namespace {

// The reference pocket plus an explicit H row on a face operation: every new
// field of this slice set to a value that differs from its default.
qp::Program fullDocument() {
    qp::Program program = qpcheck::faceProgram();
    program.g64P = 0.05;
    program.g64Q = 0.02;
    program.stopAfterOp = false;
    program.operations.front().tool.hNumber = 6;
    program.operations.push_back(pocket_checks::pocketOp());
    std::get<qp::RectPocketOp>(program.operations[1].geometry).stepOverPct = 55.0;
    return program;
}

} // namespace

namespace {

// The words of a line, split on blanks: the first is the key, the second the
// kind of an `op` head.
std::vector<std::string> wordsOf(const std::string& line) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && line[at] == ' ') { ++at; }
        const std::size_t start = at;
        while (at < line.size() && line[at] != ' ') { ++at; }
        if (at > start) { out.push_back(line.substr(start, at - start)); }
    }
    return out;
}

// A file of the previous build: no g64P/g64Q/stopAfterOp and no op.h. The
// faces keep their `op.stepOver` (the key has been theirs since the first
// slice); the pockets had no key of that name, so it is dropped from the pocket
// lines only.
std::string oldDocument(const std::string& text) {
    static const std::vector<std::string> dropped = {"g64P", "g64Q", "stopAfterOp", "op.h"};
    std::string out;
    std::size_t at = 0;
    bool inPocket = false;
    while (at < text.size()) {
        const std::size_t end = text.find('\n', at);
        const std::string line =
            text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        const std::vector<std::string> words = wordsOf(line);
        const std::string key = words.empty() ? std::string() : words.front();
        if (key == "op" && words.size() > 1) { inPocket = words[1] == "pocket"; }
        const bool drop = std::find(dropped.begin(), dropped.end(), key) != dropped.end();
        const bool pocketStep = inPocket && key == "op.stepOver";
        if (!drop && !pocketStep) { out += line + "\n"; }
        if (end == std::string::npos) { break; }
        at = end + 1;
    }
    return out;
}

} // namespace

// Every new field is written, read back and re-written byte for byte.
void checkRoundTrip() {
    const qp::Program program = fullDocument();
    const std::string text = qp::serialize(program);
    check(contains(text, "g64P 0.05\n") && contains(text, "g64Q 0.02\n"), "post.qprog_g64",
          "the tolerance must be written as two header keys");
    check(contains(text, "stopAfterOp off\n"), "post.qprog_stop",
          "the stop flag must be written as on/off");
    check(contains(text, "op.h 6\n"), "post.qprog_h", "an explicit H row must be written");
    check(contains(text, "op.stepOver 55\n"), "post.qprog_step",
          "the pocket step must be written as op.stepOver");

    const qp::ParseOutcome parsed = qp::parseProgram(text);
    check(parsed.ok, "post.qprog_readable", parsed.ok ? "" : parsed.error);
    if (!parsed.ok) { return; }
    const qp::Program& back = parsed.program;
    check(near(back.g64P, 0.05) && near(back.g64Q, 0.02) && !back.stopAfterOp, "post.qprog_doc",
          "the program fields must come back unchanged");
    check(back.operations.size() == 2 && back.operations[0].tool.hNumber == 6,
          "post.qprog_op_h", "the H row must come back on its operation");
    check(back.operations.size() == 2 &&
              near(std::get<qp::RectPocketOp>(back.operations[1].geometry).stepOverPct, 55.0),
          "post.qprog_op_step", "the pocket step must come back on its operation");
    check(qp::serialize(back) == text, "post.qprog_stable",
          "a second serialization of the read document must be byte-identical");
}

// A file of the previous build: every new key is missing and comes back as the
// default of the model.
void checkOldDocument() {
    const std::string text = oldDocument(qp::serialize(fullDocument()));
    const std::string pocketStart = "op pocket";
    check(text.find("op.stepOver", text.find(pocketStart)) == std::string::npos,
          "post.qprog_old_pocket_step", "the old pocket must carry no step");
    const qp::ParseOutcome parsed = qp::parseProgram(text);
    check(parsed.ok, "post.qprog_old_reads", parsed.ok ? "" : parsed.error);
    if (!parsed.ok) { return; }
    const qp::Program& back = parsed.program;
    check(near(back.g64P, 0.01) && near(back.g64Q, 0.01), "post.qprog_old_g64",
          "a document without a tolerance must get the 0.01 default");
    check(back.stopAfterOp, "post.qprog_old_stop",
          "a document without the flag must stop between operations");
    check(!back.operations.empty() && back.operations[0].tool.hNumber == 0,
          "post.qprog_old_h", "a document without op.h must mean H = the tool number");
    check(back.operations.size() == 2 &&
              near(std::get<qp::RectPocketOp>(back.operations[1].geometry).stepOverPct, 70.0),
          "post.qprog_old_step", "a pocket without op.stepOver must get 70 %");
}

// A hand edit that breaks a new field is named, not guessed away.
void checkNewFieldErrors() {
    const std::string good = qp::serialize(fullDocument());
    std::string broken = good;
    const std::size_t at = broken.find("g64P ");
    broken.replace(at, 5, "g64P abc");
    const qp::ParseOutcome badNumber = qp::parseProgram(broken);
    check(!badNumber.ok && badNumber.error.find("g64P") != std::string::npos,
          "post.qprog_g64_number", "a broken g64P must name the field, got '" +
                                        badNumber.error + "'");

    std::string stop = good;
    const std::size_t stopAt = stop.find("stopAfterOp off");
    stop.replace(stopAt, std::string("stopAfterOp off").size(), "stopAfterOp maybe");
    const qp::ParseOutcome badStop = qp::parseProgram(stop);
    check(!badStop.ok && badStop.error.find("stopAfterOp") != std::string::npos,
          "post.qprog_stop_word", "a word other than on/off must name the field");

    std::string h = good;
    const std::size_t hAt = h.find("op.h 6");
    h.replace(hAt, std::string("op.h 6").size(), "op.h x");
    const qp::ParseOutcome badH = qp::parseProgram(h);
    check(!badH.ok && badH.error.find("op.h") != std::string::npos, "post.qprog_h_number",
          "a broken op.h must name the field");
}

} // namespace post_checks

// Runs the .qprog cases of the new fields.
void runPostQprogChecks() {
    post_checks::checkRoundTrip();
    post_checks::checkOldDocument();
    post_checks::checkNewFieldErrors();
}
