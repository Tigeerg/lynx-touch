// cli/post_words.cpp - the post-processor words of every console mode (7.3).
//
// The words are taken out in one pass, then applied to the built document.
// Splitting the two steps keeps a usage error of a number apart from the
// document: the argument parser never decides about the toolpath, and a typed
// value lands in the field of the document, which is what `--save-qprog`
// writes.
#include "cli/post_words.h"

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include "core/ops.h"

namespace qpcli {

bool takePostWords(std::vector<std::string>& args, qp::PostArgs& out) {
    std::vector<std::string> rest;
    rest.reserve(args.size());
    std::size_t i = 0;
    while (i < args.size()) {
        if (!qp::isPostWord(args[i], i, args)) {
            rest.push_back(args[i]);
            ++i;
            continue;
        }
        if (!qp::takePostWord(args, i, out)) { return false; }
    }
    args.swap(rest);
    return true;
}

void applyPostWords(const qp::PostArgs& args, qp::Program& program) {
    if (args.g64Given) {
        program.g64P = args.g64P;
        program.g64Q = args.g64Q;
    }
    program.stopAfterOp = args.stopAfterOp;
    for (qp::Operation& op : program.operations) {
        if (args.hGiven) { op.tool.hNumber = args.h; }
        if (!args.stepOverGiven) { continue; }
        // The pocket step is the field of this slice; the face keeps the step
        // its own parser read, so a `--step-over` typed with `--op face` is
        // applied there and not here.
        if (auto* pocket = std::get_if<qp::RectPocketOp>(&op.geometry)) {
            pocket->stepOverPct = args.stepOverPct;
        } else if (auto* circle = std::get_if<qp::CirclePocketOp>(&op.geometry)) {
            circle->stepOverPct = args.stepOverPct;
        }
    }
}

} // namespace qpcli
