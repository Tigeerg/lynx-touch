// tests/checks.h - the case files of the self-check and the runners (7.2).
//
// Every file below keeps its fixtures private and exposes one runner without
// arguments. `quickprog_check.cpp` only calls the runners in the order the
// slices were added, so the console output of the check stays the contract of
// the previous slices when a file is split into two. The ids of the cases
// ("PASS <id>") are the contract too: a case keeps its name when it moves
// between files.
#pragma once

// ---- the document, the planner and the emitter ---------------------------

void runFaceChecks();
void runEmitHeaderChecks();
void runEmitMovesChecks();
void runEmitFaceChecks();
void runEmitToolsChecks();
void runCoolantChecks();
void runPocketLayoutChecks();
void runPocketRasterChecks();
void runPocketRulesChecks();
void runPocketQprogChecks();
void runCircleChecks();
void runCircleRuleChecks();
void runDrillLayoutChecks();
void runDrillCycleChecks();
void runDrillRulesChecks();
void runSummaryChecks();
void runSummaryRulesChecks();

// ---- the .qprog round-trip and the console -------------------------------

void runQprogChecks();

// The viewer core round-trip (PLAN-QUICKPROG, section 10): the generated file
// through gv::parseGcode and gv::parseUi*.
namespace roundtrip_checks {
void runRoundtripChecks();
void runMixedChecks();
void runTagChecks();
} // namespace roundtrip_checks
