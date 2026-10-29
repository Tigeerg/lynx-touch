// cli/run.h - the shared tail of every console run (7.2).
//
// Every mode ends the same way: the document is checked, planned and emitted,
// the operator reads the summary, and the two files are written. The tail is
// one function so the four operations and the demo cannot drift apart in the
// wording of the summary or in the order of the output lines. The streams are
// the caller's: main.cpp gives `std::cout` and `std::cerr`, the check gives its
// own buffers.
#pragma once

#include <iosfwd>
#include <string>

#include "core/ops.h"

namespace qpcli {

// Validates, plans, emits, prints the summary and writes the files. `output` is
// the .ngc path, `savePath` the optional .qprog path (empty means "do not write
// a .qprog"). Returns the exit code of core/cli.h.
int finish(const qp::Program& program, const std::string& output, const std::string& savePath,
           std::ostream& out, std::ostream& err);

// The bytes `--save-qprog` writes for the document: the schema of the file
// format, the writer of core/serialize.cpp. One function, so the flag and the
// check of the round-trip cannot take different paths to the file.
std::string documentText(const qp::Program& program);

} // namespace qpcli
