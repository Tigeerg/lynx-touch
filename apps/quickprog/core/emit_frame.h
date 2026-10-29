// core/emit_frame.h - the header, the trailer and the tag spelling (7.1).
//
// The frame of the emitted file is written in one place: the modal header with
// the stock tags, the trailer with M30, and the small spelling helpers the
// operations of emit.cpp need too (a comment-safe text, a tag, a numbered
// line, the tool family of the stage label). Internal to the library.
#pragma once

#include <string>

#include "core/ops.h"

namespace qp {

// The first half of the .ngc: the percent sign, the machine and the stock
// tags, the modals, the work offset and the parking move. `sequence` is the
// next block number, stepped after every block it writes.
void emitHeader(std::string& out, const Program& program, int& sequence);

// The last blocks of the .ngc: M30 and the percent sign.
void emitFooter(std::string& out, int& sequence);

// True when `text` is one of G54..G59.
bool validWcs(const std::string& text);

// ---- spelling helpers of the frame ----------------------------------------

// G-code comments may not carry '(' or ')': an unbalanced bracket ends the
// comment early and the rest of the line becomes program words.
std::string commentSafe(const std::string& text);

// The viewer labels the stage by tool family: FLAT for the milling kinds,
// DRILL otherwise.
std::string toolType(const OperationGeometry& geometry);

} // namespace qp
