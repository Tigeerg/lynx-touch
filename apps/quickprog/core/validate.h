// core/validate.h - the safety rules of PLAN-QUICKPROG, section 8.
//
// Every rule produces a Check bound to the field the operator sees, not one
// line at the bottom of the screen. `reason` is Russian, in the operator's
// words: it says what will happen and how to fix it, not the name of a C++
// type.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"

namespace qp {

struct Check {
    // Field path of the form: "tool.number", "tool.diameterMm", "depthMm",
    // "stepDownMm", "safeZ", "cutting.feedMmMin", "cutting.spindleRpm",
    // "geometry".
    std::string field;
    std::string reason;
};

// All problems of the program, in the fixed order tool -> spindle -> depth ->
// step -> stock -> safe Z -> geometry. An empty result means the program can
// be planned and emitted. The order is stable so a form can scroll to the
// first problem and a test can compare the first field.
std::vector<Check> validate(const Program& program);

// True when `text` is one of G54..G59.
bool validWcs(const std::string& text);

} // namespace qp
