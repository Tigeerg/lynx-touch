// tests/quickprog_check.cpp - self-check of the quickprog core.
//
// No test framework: the demo must build on any machine with a C++ compiler
// and nothing else. Each case prints "PASS <id>" or "FAIL <id>: <reason>" and
// the exit code is 0 when every case passed, 1 otherwise (PLAN-QUICKPROG,
// section 7.2).
//
// The cases are split by meaning into four files, each in its own namespace;
// this file declares the four runners and calls them in the order of the
// slices. The ids of the cases are the contract of the console output: a moved
// case keeps its name.
#include <cstdio>

#include "tests/harness.h"

// The four case files, one runner each. The names are the file names in the
// order the console prints them: the face and the emit words of the earlier
// slices first, then the rules and the pocket of this one.
void runFaceChecks();
void runEmitChecks();
void runQprogChecks();
void runPocketChecks();
void runCircleChecks();
void runDrillChecks();
void runSummaryChecks();
void runCoolantChecks();

// The viewer core round-trip (PLAN-QUICKPROG, section 10): the generated file
// through gv::parseGcode and gv::parseUi*.
namespace roundtrip_checks {
void runRoundtripChecks();
void runMixedChecks();
void runTagChecks();
} // namespace roundtrip_checks

int main() {
    // The earlier slices first: the header, the face, the emit words and the
    // rules; the pocket block of this slice last. The round-trip last: it reads
    // the bytes the emitter of every slice produces.
    runFaceChecks();
    runEmitChecks();
    runQprogChecks();
    runPocketChecks();
    runCircleChecks();
    runDrillChecks();
    runSummaryChecks();
    runCoolantChecks();
    roundtrip_checks::runRoundtripChecks();
    roundtrip_checks::runMixedChecks();
    roundtrip_checks::runTagChecks();
    if (qpcheck::failures() != 0) {
        std::printf("%d case(s) failed\n", qpcheck::failures());
        return 1;
    }
    std::printf("all cases passed\n");
    return 0;
}
