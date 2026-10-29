// core/path.cpp - the planner stub for the first slice.
//
// The real planner (face, pockets, drilling) is the next slices of
// PLAN-QUICKPROG, 7.2. Until then it returns an empty Path, so the whole
// pipeline operations -> Path -> emit already runs end to end and the console
// prints "moves: 0" instead of pretending to have a trajectory.
#include "core/path.h"

#include <atomic>
#include <cstdio>

#include "core/ops.h"

namespace qp {
namespace {

// Planning results are told apart by content, not by time: a counter keeps the
// uuid unique within the process and reproducible between runs, so a test can
// compare it.
std::atomic<unsigned long long> g_pathSerial{0};

std::string nextPathUuid() {
    const unsigned long long serial = ++g_pathSerial;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "path-%06llu", serial);
    return std::string(buf);
}

} // namespace

// Empty stub: no operation is planned yet, so the path has no moves and no
// bounds. `program` is taken by const reference on purpose - the planner must
// not change the document (CLAUDE.md, section 3).
Path planPath(const Program& program) {
    (void)program;
    Path path;
    path.uuid = nextPathUuid();
    return path;
}

} // namespace qp
