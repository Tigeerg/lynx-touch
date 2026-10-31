// tests/checks.h - the case files of the self-check and the runners (7.2).
//
// Every file below keeps its fixtures private and exposes one runner without
// arguments. `quickprog_check.cpp` only calls the runners in the order the
// slices were added, so the console output of the check stays the contract of
// the previous slices when a file is split into two. The ids of the cases
// ("PASS <id>") are the contract too: a case keeps its name when it moves
// between files.
#pragma once

#include <filesystem>

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
void runDrillKindChecks();
void runMaterialChecks();
void runSummaryChecks();
void runSummaryRulesChecks();

// ---- the post processor of PLAN-QUICKPROG, 7.3 ---------------------------

void runPostEmitChecks();
void runPostParkingChecks();
void runPostHeadChecks();
void runPostQprogChecks();
void runPostCliChecks();

// The kinds the panel writes and the demo does not plan (PLAN-QUICKPROG, 7.6):
// every one of them reads, is skipped with a warning and does not stop the
// planned operations from reaching the file.
namespace qprog_kinds_checks {
void runQprogKindsChecks();
} // namespace qprog_kinds_checks

// ---- the .qprog round-trip and the console -------------------------------

void runQprogChecks();
void runHelpChecks();
void runOpRoundTripChecks();

// The generator version against the manifest projects.json of the repository.
// findRoot walks up to the directory with projects.json; empty outside the
// repository. Shared by checks_imports.cpp, which reads the same tree.
namespace version_checks {
std::filesystem::path findRoot();
void runVersionChecks();
} // namespace version_checks

// The import graph of core/ and cli/: no circles, no layer edges. The circle
// once existed through constexpr tables and only the separate build saw it.
namespace import_checks {
void runImportChecks();
} // namespace import_checks

// The viewer core round-trip (PLAN-QUICKPROG, section 10): the generated file
// through gv::parseGcode and gv::parseUi*.
namespace roundtrip_checks {
void runRoundtripChecks();
void runPostChecks();
void runMixedChecks();
void runTagChecks();
} // namespace roundtrip_checks
