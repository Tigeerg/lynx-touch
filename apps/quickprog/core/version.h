// core/version.h - the generator version written into the header of every .ngc.
//
// The file is marked "generated": an operator who edits the G-code by hand
// must see where it came from and that a re-generation overwrites the edit
// (PLAN-QUICKPROG, 11). The number is a plain constant here; wiring it to the
// repository VERSION is the Lead's call, because the semver of the product is
// not the semver of the demo.
#pragma once

namespace qp {

constexpr const char* kGeneratorName = "quickprog";
constexpr const char* kGeneratorVersion = "0.1.0";

} // namespace qp
