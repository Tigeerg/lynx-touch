// core/emit.h - Program + Path -> G-code text (PLAN-QUICKPROG, 7.1).
//
// The emitter is pure: it takes the document and the planned path and returns
// the whole file as a string. Writing the file is the caller's job, so the
// check can compare the text without touching the disk.
#pragma once

#include <string>

#include "core/ops.h"
#include "core/path.h"

namespace qp {

// The complete .ngc: header (modals, work offset, stock tags), one block per
// operation with its moves, footer. An operation without planned moves still
// gets its UI_OP/UI_TOOL block: the viewer then shows the tool of an empty
// operation instead of silently dropping it.
std::string emitProgram(const Program& program, const Path& path);

} // namespace qp
