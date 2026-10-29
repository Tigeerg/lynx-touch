// cli/app.h - the top-level dispatcher of the console (7.2).
//
// The function reads the argument list once, decides the mode - the help, the
// version, the demo, a `.qprog` run or one of the four `--op` kinds - and hands
// the work over. It returns the exit code of core/cli.h: 0 - done, 1 - the
// document did not pass the checks or a file was not written, 2 - the arguments
// were not understood.
//
// The output streams are parameters, not globals: the check reads the exact
// bytes of the help and the version (the `--help`/`--version` cases) without
// running the binary and without a second copy of the text. main.cpp hands
// `std::cout` and `std::cerr`, so the console behaves as before.
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace qpcli {

// The mode the command line asks for, decided by the same words in any place:
// `--help` wins over `--version`, both win over a run.
enum class Mode { Help, Version, Run, BadArgs };

// The mode of the word list: `--help`/`-h` anywhere means Help, else
// `--version` anywhere means Version, else Run. Empty arguments are BadArgs (no
// word to read).
Mode decideMode(const std::vector<std::string>& args);

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace qpcli
