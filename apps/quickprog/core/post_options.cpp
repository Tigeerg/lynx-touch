// core/post_options.cpp - the post-processor words of the console (7.3).
//
// The keys are the ones of the help text: `--h`, `--step-over`, `--stop`,
// `--no-stop` and `--g64` with two numbers. A number is read by the shared word
// helper of `option_words.h`, so `--h` accepts nothing but a whole number and
// `--step-over 2,5` is a usage error, the same as every other key.
#include "core/post_options.h"

#include <cstddef>
#include <string>
#include <vector>

#include "core/option_words.h"

namespace qp {
namespace {

const char* const kPostWords[] = {"--h", "--step-over", "--stop", "--no-stop", "--g64"};

// A word that starts like a number: "0.05", "-1", "1e-3". The caller needs it
// to tell "`--g64` with no numbers" from "`--g64` with one number", which is a
// usage error.
bool isNumberWord(const std::string& word) {
    double value = 0.0;
    return parseDouble(word, value);
}

bool inTable(const std::string& word) {
    for (const char* known : kPostWords) {
        if (word == known) { return true; }
    }
    return false;
}

} // namespace

bool isPostWord(const std::string& word, std::size_t wordIndex, const std::vector<std::string>& args) {
    if (!inTable(word)) { return false; }
    // `--op face` has had its own `--step-over` since the first slice: the
    // strips of a face read it, not the pockets. The mode word decides who
    // owns the key, so one line cannot mean two steps at once.
    const bool faceMode = args.size() >= 2 && args[0] == "--op" && args[1] == "face";
    return !(word == "--step-over" && faceMode && wordIndex > 1);
}

bool takePostWord(const std::vector<std::string>& args, std::size_t& i, PostArgs& out) {
    const std::string& key = args[i];
    std::string value;
    if (key == "--h") {
        int h = 0;
        if (!takeValue(args, i, value) || !parseInt(value, h)) { return false; }
        out.hGiven = true;
        out.h = h;
        return true;
    }
    if (key == "--step-over") {
        double pct = 0.0;
        if (!takeValue(args, i, value) || !parseDouble(value, pct)) { return false; }
        out.stepOverGiven = true;
        out.stepOverPct = pct;
        return true;
    }
    if (key == "--stop") {
        out.stopAfterOp = true;
        i += 1;
        return true;
    }
    if (key == "--no-stop") {
        out.stopAfterOp = false;
        i += 1;
        return true;
    }
    if (key == "--g64") {
        // Two numbers: P first, then Q. A single number is a usage error, not
        // "P given, Q default": the operator who types one number expects one
        // meaning, and the help text says "два числа". No number at all is
        // the default of the model, written as a zero: `validate` refuses a
        // zero tolerance on a run, and the .qprog keeps the empty key.
        const bool followedByNumber =
            i + 1 < args.size() && isNumberWord(args[i + 1]);
        if (!followedByNumber) {
            out.g64Given = true;
            out.g64P = 0.0;
            out.g64Q = 0.0;
            i += 1;
            return true;
        }
        if (i + 2 >= args.size()) { return false; }
        double p = 0.0;
        double q = 0.0;
        if (!parseDouble(args[i + 1], p) || !parseDouble(args[i + 2], q)) { return false; }
        out.g64Given = true;
        out.g64P = p;
        out.g64Q = q;
        i += 3;
        return true;
    }
    // The caller of `isPostWord` never passes another word; a new key needs one
    // branch above and one entry in the table.
    return true;
}

} // namespace qp
