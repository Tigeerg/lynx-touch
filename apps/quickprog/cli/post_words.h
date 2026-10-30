// cli/post_words.h - the post-processor words of every console mode (7.3).
//
// `--h`, `--step-over`, `--g64`, `--stop` and `--no-stop` mean the same thing
// in every mode, so they are taken out of the argument list before the mode
// parser sees it: the face, pocket, circle and drill parsers then keep their
// own keys and a post word cannot be rejected as unknown in one mode and
// accepted in another. `--help` and `--version` already work this way.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"
#include "core/post_options.h"

namespace qpcli {

// Takes every post-processor word out of `args` and fills `out`. False when a
// post word has no value or a number that does not parse: the caller then
// reports a usage error. The order of the remaining words is kept.
bool takePostWords(std::vector<std::string>& args, qp::PostArgs& out);

// Applies the post words to the program: the tolerance, the stop flag and -
// when given - the H row and the raster step of every operation. A pocket keeps
// its step from the document when `--step-over` was not typed; a face keeps its
// own step, because `--op face` has had `--step-over` since the first slice and
// it belongs to that parser.
void applyPostWords(const qp::PostArgs& args, qp::Program& program);

} // namespace qpcli
