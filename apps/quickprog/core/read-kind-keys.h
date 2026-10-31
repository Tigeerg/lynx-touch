// core/read-kind-keys.h - the `op.*` keys of the kinds the demo does not plan
// (PLAN-QUICKPROG, 7.6).
//
// The demo reads the whole panel file; only the four kinds of its planner are
// planned. The shape keys of the other kinds therefore live in their own file,
// next to the key table of `serialize_shared.cpp`: a reader of one more kind is
// one branch there and one field in `ops.h`, and the planner of the demo does
// not grow a line for a kind it never cuts. `op.peck`, `op.r` and the drill
// layout keys stay in `qprog_operation.cpp`, where the drill reads them.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"

namespace qp {

// Reads a shape key of a kind the demo does not plan into `op`. False and a
// message in `error` for a missing value or a word the key does not take;
// `where` carries the line number and `line` the whole line (for the text of
// an engraving and the lines of a custom block).
bool readKindKey(Operation& op, const std::string& sub, const std::string& key,
                 const std::string& line, const std::vector<std::string>& words,
                 const std::string& where, std::string& error);

} // namespace qp
