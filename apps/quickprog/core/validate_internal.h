// core/validate_internal.h - the rule groups validate.cpp calls (8).
//
// The rules are split by meaning: the common ones (tool, cutting data, depth,
// step, safe Z) live in validate-common.cpp, the shape rules of every
// operation kind in validate-shape.cpp. validate.cpp keeps the order of the
// groups and the public `validate()`. The header is internal to the library:
// nothing outside core/ includes it.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"
#include "core/path_drill.h"
#include "core/validate.h"

namespace qp {
namespace valdetail {

// The .qprog-independent text of a number: "%g", the spelling the form shows.
std::string fmt(double value);

// Appends one problem bound to the field of the form.
void add(std::vector<Check>& out, const std::string& field, const std::string& reason);

// ---- validate-common.cpp --------------------------------------------------

// Tool number, diameter and the H row of G43 of the operation.
void checkTool(const Operation& op, std::vector<Check>& out);
// The G64 tolerance of the program and the M1 flag (PLAN-QUICKPROG, 7.3).
void checkProgramPost(const Program& program, std::vector<Check>& out);
// Spindle speed, working and plunge feed against the machine and the table.
void checkCutting(const Operation& op, std::vector<Check>& out);
// The pass depth against the stock height.
void checkDepth(const Program& program, const Operation& op, std::vector<Check>& out);
// The axial step: a drill descends in one cycle and is not checked.
void checkStep(const Operation& op, std::vector<Check>& out);
// Safe Z above the top of the stock.
void checkSafeZ(const Program& program, std::vector<Check>& out);

// ---- validate-shape.cpp ---------------------------------------------------

// The geometry rules of the kind of the operation: face, pocket, circle
// pocket or the three drilling kinds.
void checkGeometry(const Program& program, const Operation& op, std::vector<Check>& out);
// The G83 peck against the hole depth. A zero peck is "the planner decides".
void checkPeck(const Operation& op, std::vector<Check>& out);

} // namespace valdetail
} // namespace qp
