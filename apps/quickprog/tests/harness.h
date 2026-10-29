// tests/harness.h - the case counter and the small readers of the self-check.
//
// No test framework: the demo must build on any machine with a C++ compiler
// and nothing else. A case prints "PASS <id>" or "FAIL <id>: <reason>", the
// exit code of the check is 0 when every case passed and 1 otherwise
// (PLAN-QUICKPROG, section 7.2). The cases are grouped by meaning:
//
//   checks_emit.cpp    the .ngc words: header, footer, modals, feeds, rules
//   checks_face.cpp    the face planner and its numbers
//   checks_pocket.cpp  the rectangular pocket planner, its numbers and .qprog
//   checks_qprog.cpp   the console arguments and the safety rules
//
// A case keeps its name when it moves between files: the console output of the
// check is the contract of the previous slices, and the names are how a
// regression is found.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"

namespace qpcheck {

// Number of failed cases of the whole check; the files share it.
inline int& failures() {
    static int count = 0;
    return count;
}

inline void check(bool ok, const std::string& id, const std::string& reason) {
    if (ok) {
        std::printf("PASS %s\n", id.c_str());
        return;
    }
    std::printf("FAIL %s: %s\n", id.c_str(), reason.c_str());
    ++failures();
}

inline bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

inline bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

// Lines of the file that carry a motion word. Comments and the header block
// (modals, work offset, parking) are not moves: a "G0" there is a mode, not a
// trajectory.
inline std::vector<std::string> moveBlocks(const std::string& gcode) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < gcode.size()) {
        const std::size_t end = gcode.find('\n', at);
        const std::string one =
            gcode.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (one.find(" G0 ") != std::string::npos || one.find(" G1 ") != std::string::npos) {
            out.push_back(one);
        }
        if (end == std::string::npos) { break; }
        at = end + 1;
    }
    return out;
}

// The modal state of the file while the check reads it: a word is present in a
// line and the previous value is known.
struct ModalState {
    bool has[3] = {false, false, false};  // X, Y, Z in this order
    double value[3] = {0.0, 0.0, 0.0};
};

inline void setAxis(ModalState& state, int axis, double value) {
    state.has[axis] = true;
    state.value[axis] = value;
}

// True when the block carries an axis word, "X12.000" after a space.
inline bool hasWord(const std::string& block, char axis) {
    return block.find(std::string(" ") + axis) != std::string::npos;
}

// Value of the axis word of a block, NaN when the word is absent.
inline double axisValue(const std::string& block, char axis) {
    const std::size_t at = block.find(std::string(" ") + axis);
    if (at == std::string::npos) { return std::nan(""); }
    return std::atof(block.c_str() + at + 2);
}

inline int axisSlot(char axis) {
    return axis == 'X' ? 0 : (axis == 'Y' ? 1 : 2);
}

// A minimal program: the stock block only, without operations. It is the
// frame the cases build on.
inline qp::Program demoProgram() {
    qp::Program program;
    program.name = "QUICKPROG DEMO";
    program.wcs = "G54";
    program.stock.min = {-50.0, -40.0, -20.0};
    program.stock.max = {50.0, 40.0, 0.0};
    program.safeZ = 5.0;
    return program;
}

// Face milling of a 100x80x20 stock top, 10 mm end mill, 5 mm down in 2 mm
// steps. The stock is the demo one, so the top is z = 0.
inline qp::Operation faceOp() {
    qp::Operation op;
    op.id = "Плоскость";
    op.tool.number = 2;
    op.tool.diameterMm = 10.0;
    op.cutting.spindleRpm = 4775.0;
    op.cutting.feedMmMin = 860.0;
    op.cutting.plungeFeedMmMin = 215.0;
    op.topZ = 0.0;
    op.depthMm = 5.0;
    op.stepDownMm = 2.0;
    qp::FaceOp face;
    face.z = -5.0;
    face.stepOverPct = 70.0;
    op.geometry = face;
    return op;
}

inline qp::Program faceProgram() {
    qp::Program program = demoProgram();
    program.name = "FACE DEMO";
    program.operations.push_back(faceOp());
    return program;
}

// Counts how many times `needle` occurs in `text`: the tool-change cases count
// T/M6 and S/M3, so a word written once too many is a failure, not a pass.
inline int count(const std::string& text, const std::string& needle) {
    int found = 0;
    std::size_t at = text.find(needle);
    while (at != std::string::npos) {
        ++found;
        at = text.find(needle, at + needle.size());
    }
    return found;
}

// The bytes the console printed: the dispatcher writes to the streams a caller
// hands it, and the check reads the text instead of the binary. The capture is
// an ostringstream, so the check does not touch stdout and cannot hide a case
// from the report.
struct ConsoleCapture {
    std::ostringstream out;
    std::ostringstream err;
};

} // namespace qpcheck
