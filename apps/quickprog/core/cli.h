// core/cli.h - shared console pieces of quickprog: exit codes and file output.
//
// One write path and one exit-code table, so the scripts and the help text
// cannot disagree about what "1" means.
#pragma once

#include <string>

namespace qp {

constexpr int kExitOk = 0;
// The document did not pass the checks, or the file was not written.
constexpr int kExitFailed = 1;
// The arguments were not understood: a usage error, not a failure.
constexpr int kExitBadArgs = 2;

// Writes the whole text to `path`; false on a short write or a close error.
bool writeFile(const std::string& path, const std::string& text);

} // namespace qp
