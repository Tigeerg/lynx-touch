// cli/app.h - the top-level dispatcher of the console (7.2).
//
// The function reads the argument list once, decides the mode - the help, the
// version, the demo, a `.qprog` run or one of the four `--op` kinds - and hands
// the work over. It returns the exit code of core/cli.h: 0 - done, 1 - the
// document did not pass the checks or a file was not written, 2 - the arguments
// were not understood.
#pragma once

#include <string>
#include <vector>

namespace qpcli {

int run(const std::vector<std::string>& args);

} // namespace qpcli
