// tests/checks_emit_coolant.cpp - the coolant words of the .ngc (7.1, P2).
//
// `M8` is printed only when an operation asks for the coolant, `M9` only for
// the operation that switched it on: an operation with the coolant off has no
// coolant word at all. The next operation that floods starts it again.
#include <string>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"

#include "tests/harness.h"

namespace coolant_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::faceProgram;

void checkCoolant() {
    qp::Program off = faceProgram();
    off.operations.front().coolant = qp::Coolant::Off;
    const std::string offCode = qp::emitProgram(off, qp::planPath(off));
    check(!contains(offCode, "M8"), "coolant.off_no_m8",
          "a coolant that is off must not switch M8 on");
    check(!contains(offCode, "M9"), "coolant.off_no_m9",
          "a coolant that was never on must not print an M9");
    // The next operation asks for the coolant: it must start again, because
    // the off one never left it on.
    qp::Program second = off;
    qp::Operation withCoolant = off.operations.front();
    withCoolant.id = "Второй проход";
    withCoolant.coolant = qp::Coolant::Flood;
    second.operations.push_back(withCoolant);
    const std::string mixed = qp::emitProgram(second, qp::planPath(second));
    check(count(mixed, "M8\n") == 1 && count(mixed, "M9\n") == 1, "coolant.off_then_flood",
          "an off operation followed by a flood one needs one M8 and one M9, got M8=" +
              std::to_string(count(mixed, "M8\n")) + " M9=" + std::to_string(count(mixed, "M9\n")));

    qp::Program flood = faceProgram();
    flood.operations.front().coolant = qp::Coolant::Flood;
    const std::string floodCode = qp::emitProgram(flood, qp::planPath(flood));
    check(count(floodCode, "M8\n") == 1, "coolant.flood_m8", "flood must keep one M8");
    check(count(floodCode, "M9\n") == 1, "coolant.flood_m9", "flood must keep one M9");
    check(contains(floodCode, "M8\n") && floodCode.find("M8\n") < floodCode.find(" G1 "),
          "coolant.m8_before_cut", "the coolant must start before the first cut");
}

} // namespace coolant_checks

void runCoolantChecks() { coolant_checks::checkCoolant(); }
