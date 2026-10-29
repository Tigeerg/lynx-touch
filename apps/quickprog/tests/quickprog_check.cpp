// tests/quickprog_check.cpp - self-check of the quickprog core.
//
// No test framework: the demo must build on any machine with a C++ compiler
// and nothing else. Each case prints "PASS <id>" or "FAIL <id>: <reason>" and
// the exit code is 0 when every case passed, 1 otherwise (PLAN-QUICKPROG,
// section 7.2).
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string& id, const std::string& reason) {
    if (ok) {
        std::printf("PASS %s\n", id.c_str());
        return;
    }
    std::printf("FAIL %s: %s\n", id.c_str(), reason.c_str());
    ++g_failed;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// A minimal program: the stock block only, without operations. It is what
// `--demo` writes today.
qp::Program demoProgram() {
    qp::Program program;
    program.name = "QUICKPROG DEMO";
    program.wcs = "G54";
    program.stock.min = {-50.0, -40.0, -20.0};
    program.stock.max = {50.0, 40.0, 0.0};
    program.safeZ = 5.0;
    return program;
}

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
    check(contains(gcode, "N915 M30\n"), "emit.footer", "the footer M30 block is missing");
    check(!contains(gcode, "G54 USED"), "emit.header_wcs_valid", "a valid WCS still gets the invalid warning");
    check(path.moves.empty(), "path.empty", "the planner must return a path without moves");
}

} // namespace

int main() {
    checkHeader();
    if (g_failed != 0) {
        std::printf("%d case(s) failed\n", g_failed);
        return 1;
    }
    std::printf("all cases passed\n");
    return 0;
}
