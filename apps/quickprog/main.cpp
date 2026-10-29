// main.cpp - console entry point of the quick programming demo (PLAN-QUICKPROG, 7.2).
//
// The demo proves the pipeline operations -> Path -> .ngc without a screen and
// without Qt. Exit codes: 0 - done, 1 - the document does not pass the checks,
// a file is missing or was not written, 2 - the arguments were not understood
// (the usual console convention, so a script can tell a usage error from a
// failure).
//
// The console is a thin shell over the core: it builds a document, validates
// it, plans it, emits it and writes two files - the editable `.qprog` and the
// ready `.ngc`. Nothing here decides about a toolpath. The shell is split by
// meaning into cli/ (the help text, the dispatcher, the documents and the
// shared tail), so this file only collects the arguments, hands them over with
// the console streams and flushes them before the exit code is returned.
#include <iostream>
#include <string>
#include <vector>

#include "cli/app.h"

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const int code = qpcli::run(args, std::cout, std::cerr);
    std::cout.flush();
    std::cerr.flush();
    return code;
}
