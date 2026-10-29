// core/serialize_internal.h - the words the .qprog writer and reader share.
//
// One spelling of a number, a word, a quote and a key: a writer that writes a
// token the reader rejects is a round-trip failure caught by the check, not by
// the operator. The header is internal to core/serialize.cpp and
// core/serialize_read.cpp; neither is included outside the library.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/ops.h"

namespace qp {

// ---- words of a line ------------------------------------------------------

// One token of a line: a run of non-blank characters. Empty words are dropped.
std::vector<std::string> splitWords(const std::string& line);

// The rest of the line after `count` words: the id of an operation, the name
// of the program and a note may carry spaces.
std::string restAfter(const std::string& line, std::size_t count);

// The writer's text spelling: quoted, so leading and trailing spaces survive.
std::string quoteText(const std::string& text);
// The reader's side: one pair of outer quotes is dropped.
std::string unquoteText(const std::string& text);

// The .qprog spelling of a number: a full token with a decimal point, so "1x"
// and "1,5" are errors and not 1. The names carry the `qprog` prefix because
// the console has its own word parser (`core/option_words.h`) with a stricter
// acceptance; the two must not be confused at link time.
bool parseQprogNumber(const std::string& text, double& out);
// True when the token is a number the .qprog reader accepts.
bool qprogNumber(const std::string& text);
// A full-token integer of the .qprog file.
bool parseQprogInt(const std::string& text, int& out);
// A double in the round-trip-safe spelling ("10", "0.2", "-6").
std::string number(double value);

const char* coolantName(Coolant coolant);
bool parseCoolant(const std::string& text, Coolant& out);

// ---- the operation table --------------------------------------------------

// The word of a kind on the `op` line: face, pocket, circle, drill, ...
const char* kindName(const OperationGeometry& geometry);
// The `op.*` keys of a kind, without the `op.` prefix; for the drill and the
// circle kinds the shape key is listed too.
std::vector<std::string> keysOf(const OperationGeometry& geometry);
// The keys every operation writes, in file order.
const std::vector<std::string>& commonKeys();

} // namespace qp
