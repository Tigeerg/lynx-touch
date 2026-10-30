// core/validate.cpp - the safety rules of PLAN-QUICKPROG, section 8.
//
// The rules are guard clauses over the document, not over the planned path:
// the form runs them after every keystroke, before a path exists. The planner
// is allowed to assume a valid document; the caller validates first.
//
// This file keeps the order of the groups and the entry point only: the
// common rules (tool, cutting data, depth, step, safe Z) are in
// validate-common.cpp, the shape and cycle rules in validate-shape.cpp.
#include "core/validate.h"

#include <string>
#include <vector>

#include "core/emit_frame.h"
#include "core/validate_internal.h"

namespace qp {

std::vector<Check> validate(const Program& program) {
    std::vector<Check> out;
    if (!validWcs(program.wcs)) {
        valdetail::add(out, "wcs", "Смещение " + program.wcs + " не найдено: выберите G54..G59.");
    }
    if (program.operations.empty()) {
        valdetail::add(out, "operations", "В программе нет ни одной операции.");
        return out;
    }
    for (const Operation& op : program.operations) {
        if (!op.enabled) { continue; }
        valdetail::checkTool(op, out);
        valdetail::checkCutting(op, out);
        valdetail::checkDepth(program, op, out);
        valdetail::checkStep(op, out);
        valdetail::checkGeometry(program, op, out);
        valdetail::checkPeck(op, out);
    }
    valdetail::checkSafeZ(program, out);
    valdetail::checkProgramPost(program, out);
    return out;
}

} // namespace qp
