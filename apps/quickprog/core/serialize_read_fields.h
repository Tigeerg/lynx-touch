// core/serialize_read_fields.h - the document header of a .qprog file (6, 7.2).
//
// The header keys (`schema`, `name`, `units`, `wcs`, `stock`, `safeZ`) are
// handled in one place: the line loop of serialize_read.cpp knows when a key
// is a header key, this part knows its arity and the field it fills. It is
// internal to the library: the console and the check call `parseProgram`.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"

namespace qp {

// What the reader has already seen of the header: the keys are required, so
// the reader names the missing one at the end instead of "broken file".
struct HeaderSeen {
    bool schema = false;
    bool name = false;
    bool units = false;
    bool wcs = false;
    bool stock = false;
    bool safeZ = false;
};

// Reads the header key `key` into `program` and marks it in `seen`. Returns
// false and fills `error` on an unknown key, a wrong arity, a number that does
// not parse or an unsupported schema; `where` carries the line number.
bool readHeaderKey(Program& program, HeaderSeen& seen, const std::string& key,
                   const std::string& line, const std::vector<std::string>& words,
                   const std::string& where, std::string& error);

// True when every required header key was read. `error` names the first
// missing one: the message has to say which field is missing.
bool headerComplete(const HeaderSeen& seen, std::string& error);

} // namespace qp
