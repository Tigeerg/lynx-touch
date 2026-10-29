// core/qprog_reader.h - the value reader of one `op.*` line of a .qprog file.
//
// `serialize_read.cpp` owns the order of the keys and `serialize_read_op.cpp`
// the table of the kinds; this function owns the arity of a key and the field
// it fills. It is internal to the library: the console and the check call
// `parseProgram`, not this.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"

namespace qp {

// Reads the values of the operation key `sub` (without the `op.` prefix) into
// `op`. `line` is the whole line for messages, `words` its tokens. Returns
// false and fills `error` when the arity or a number is wrong; the caller
// prefixes nothing, `where` already carries the line number.
bool readOpValue(Operation& op, const std::string& sub, const std::string& key,
                 const std::string& line, const std::vector<std::string>& words,
                 const std::string& where, std::string& error);

} // namespace qp
