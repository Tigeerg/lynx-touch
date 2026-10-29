// tests/checks_emit_header.cpp - the words of the .ngc header (7.1).
//
// The modal block, the work offset, the stock tags and the footer keep the
// generated file readable for the existing viewer: they are checked here; the
// moves are in checks_emit_moves.cpp, the tool changes in checks_emit_tools.cpp.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace emit_checks {

using namespace qpcheck;

using qpcheck::axisSlot;
using qpcheck::axisValue;
using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::demoProgram;
using qpcheck::faceOp;
using qpcheck::faceProgram;
using qpcheck::hasWord;
using qpcheck::ModalState;
using qpcheck::moveBlocks;
using qpcheck::near;
using qpcheck::setAxis;


// Header parity with PLAN-QUICKPROG, section 7.1: the same words in the same
// order keep the generated file readable for the existing viewer.
void checkHeader() {
    const qp::Program program = demoProgram();
    const qp::Path path = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, path);

    check(contains(gcode, "N10 G21 G90 G94 G17 G40 G49 G80"),
          "emit.header_modals",
          "the modal block G21 G90 G94 G17 G40 G49 G80 is missing");
    // A bare "G54" would also match the "WCS INVALID ... G54 USED" comment and
    // the "N15G54" spelling a broken separator gives; the block has to be a
    // word of its own line.
    check(contains(gcode, "N15 G54\n"), "emit.header_wcs", "the work offset block N15 G54 is missing");
    check(contains(gcode, "(UI_STOCK:"), "emit.ui_stock", "the UI_STOCK tag is missing");
    check(contains(gcode, "XMIN=-50."), "emit.ui_stock_bounds", "UI_STOCK has no XMIN from the stock");
    // The footer M30 is the last numbered block; the number depends on the
    // operations before it, so the case matches the block, not the number.
    check(contains(gcode, " M30\n"), "emit.footer", "the footer M30 block is missing");
    check(!contains(gcode, "G54 USED"), "emit.header_wcs_valid", "a valid WCS still gets the invalid warning");
    check(path.moves.empty(), "path.empty", "the planner must return a path without moves");
}

} // namespace emit_checks

// Header parity of PLAN-QUICKPROG, section 7.1.
void runEmitHeaderChecks() { emit_checks::checkHeader(); }
