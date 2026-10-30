// core/post_options.h - the post-processor words of the console (7.3).
//
// The words that belong to the whole program, not to one operation: the H row
// of G43, the raster step of a pocket, the M1 stop and the G64 tolerance. They
// are read in one place, so `--op face`, `--op pocket`, `--op circle-pocket`,
// `--op drill` and `--demo` accept the same spelling and cannot drift apart in
// the meaning of a key.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace qp {

// What the operator asked for: a value given or not given, so the default of
// the model stays in the model, not in a copy here.
struct PostArgs {
    // `--h N`: a row of the length table; a zero means "the tool number".
    bool hGiven = false;
    int h = 0;
    // `--step-over P`: the raster step of a pocket, % of the diameter. A face
    // has had its own `--step-over` since the first slice, so a face keeps the
    // step its parser read and this value is not applied to it.
    bool stepOverGiven = false;
    double stepOverPct = 70.0;
    // `--no-stop` / `--stop`: the M1 between operations.
    bool stopAfterOp = true;
    // `--g64 P Q`: the path tolerance, mm. Both words or none.
    bool g64Given = false;
    double g64P = 0.01;
    double g64Q = 0.01;
};

// Reads the post-processor word at `i` and moves the cursor past it. The caller
// asks `isPostWord` first: a known word that does not parse (a missing value, a
// number that is not a number, one number of `--g64`) returns false and the
// caller reports a usage error.
bool takePostWord(const std::vector<std::string>& args, std::size_t& i, PostArgs& out);

// True when the word at `wordIndex` of `args` is a post-processor key of this
// mode: the caller checks before it hands the line to an operation parser,
// because a parser rejects an unknown dash word. `--step-over` of `--op face`
// belongs to the face parser (its own key since the first slice) and is not a
// post-processor word there.
bool isPostWord(const std::string& word, std::size_t wordIndex,
                const std::vector<std::string>& args);

} // namespace qp
