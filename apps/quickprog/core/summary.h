// core/summary.h - the operator summary of one planned operation (7.2, Q0).
//
// The console prints what the planner computed, not a second arithmetic: the
// lines are built here from the same stats the checks read, so the screen and
// the plan cannot disagree. The text is Russian, as every message of the
// operator.
#pragma once

#include <cstddef>
#include <string>

#include "core/ops.h"
#include "core/path.h"

namespace qp {

// Summary of one operation of `planned`: the title line, the shape numbers of
// its kind, the move and cutting length, and the regime line with the source
// of the cutting data. `first`/`last` delimit the moves of the operation in the
// path. The returned text ends with a newline.
std::string operationSummary(const Program& program, const Path& planned, std::size_t index,
                             std::size_t first, std::size_t last);

} // namespace qp
