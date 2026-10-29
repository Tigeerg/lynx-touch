// core/serialize.h - the .qprog file: a flat text document, schema 1 (PLAN-QUICKPROG, 6, 7.2).
//
// The file is the editable model, not the machine program: the operator can
// open it, read it and put it back on the stand. It is therefore one `key value`
// per line, ordered top down, with no nesting, quoting or escaping beyond what
// a line shows: an eye can follow it and a diff of two files names the field
// that changed.
//
//   schema 1
//   name POCKET DEMO
//   units mm
//   wcs G54
//   stock -50 -40 -20 50 40 0
//   safeZ 5
//   op pocket "Карман 60x40"
//   op.enabled on
//   op.tool 2 8
//   ...
//
// An unknown key, a missing required key or a number that does not parse is an
// error with the line number (or the field name when the key is absent), never
// a silent default: a typo in `safeZ` must not become "safe Z = 0".
#pragma once

#include <string>

#include "core/ops.h"

namespace qp {

// Result of parsing: either the document or a Russian error message that names
// the line number or the missing field. Both fields are never filled at once.
struct ParseOutcome {
    bool ok = false;
    Program program;
    // "строка 7: неизвестное поле 'safezn'." or
    // "нет обязательного поля 'wcs'."
    std::string error;
};

// Writes the document of schema 1. Deterministic: the same document gives the
// same bytes, fields in a fixed order, numbers in one spelling, one field per
// line. Every value of the document is written, so parse of the output returns
// the same document (round-trip).
std::string serialize(const Program& program);

// Reads a schema 1 document. Empty lines and lines starting with '#' are
// skipped; a UTF-8 byte order mark is dropped. Unknown keys, duplicate keys,
// a missing required key, a broken number and an unsupported schema are
// errors, not warnings.
ParseOutcome parseProgram(const std::string& text);

} // namespace qp
