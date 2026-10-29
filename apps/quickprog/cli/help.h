// cli/help.h - the console help text of quickprog (PLAN-QUICKPROG, 7.2).
//
// The text is one string constant, byte for byte the one the operator reads:
// the usage lines, the keys of every operation, the examples and the exit
// codes. It lives apart from main.cpp so a change of a word is a change of one
// file, and the check can compare the printed bytes without running the
// binary.
#pragma once

namespace qpcli {

// The whole help text, ending with a newline. Printed to stdout for `--help`
// and to stderr when the arguments were not understood.
extern const char* const kHelp;

} // namespace qpcli
