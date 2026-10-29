// tests/quickprog_check.cpp - self-check of the quickprog core.
//
// No test framework: the demo must build on any machine with a C++ compiler
// and nothing else. Each case prints "PASS <id>" or "FAIL <id>: <reason>" and
// the exit code is 0 when every case passed, 1 otherwise (PLAN-QUICKPROG,
// section 7.2).
//
// The cases are split by meaning into one file per group; tests/checks.h
// declares the runners and this file calls them in the order of the slices.
// The ids of the cases are the contract of the console output: a moved case
// keeps its name.
#include <cstdio>

#include "tests/checks.h"
#include "tests/harness.h"

int main() {
    // The earlier slices first: the header, the face, the emit words and the
    // rules; the pocket block of this slice last. The round-trip last: it reads
    // the bytes the emitter of every slice produces.
    runFaceChecks();
    runEmitHeaderChecks();
    runEmitMovesChecks();
    runEmitFaceChecks();
    runEmitToolsChecks();
    runQprogChecks();
    runHelpChecks();
    runPocketLayoutChecks();
    runPocketRasterChecks();
    runPocketRulesChecks();
    runPocketQprogChecks();
    runCircleChecks();
    runCircleRuleChecks();
    runDrillLayoutChecks();
    runDrillCycleChecks();
    runDrillRulesChecks();
    runSummaryChecks();
    runSummaryRulesChecks();
    runCoolantChecks();
    roundtrip_checks::runRoundtripChecks();
    roundtrip_checks::runMixedChecks();
    roundtrip_checks::runTagChecks();
    runOpRoundTripChecks();
    if (qpcheck::failures() != 0) {
        std::printf("%d case(s) failed\n", qpcheck::failures());
        return 1;
    }
    std::printf("all cases passed\n");
    return 0;
}
