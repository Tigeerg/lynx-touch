// core/emit_internal.h - shared block and number formatting of the emitter.
//
// The sequence number, the coordinate spelling and the parking point are the
// contract between emit.cpp and emit_moves.cpp: one place keeps a block number
// from jumping between files, and the golden files of later slices compare the
// same spelling everywhere.
#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

#include "core/path.h"

namespace qp {

// Step of the block numbers, the one of the reference post file.
constexpr int kSequenceStep = 5;

// Coordinates of moves: three digits after the decimal point, always.
std::string coord(double value);

// One numbered block: "N100 G1 X1.000".
std::string numberedBlock(int sequence, const std::string& words);

// State of the machine between two operations, needed by the tool-change rule
// (PLAN-QUICKPROG, 7.1): the modal motion is dropped at the end of an
// operation (M5 stops the spindle, the next move writes its own G), but the
// feed, the tool and the coolant are modal words of the file and the next
// block may legitimately omit them.
struct EmitState {
    bool hasFeed = false;
    double feed = 0.0;
    // Tool number the machine holds; 0 - none yet (the header changed nothing).
    int tool = 0;
    // Spindle speed the machine runs at after the last operation; 0 - stopped.
    double spindleRpm = 0.0;
    bool coolantOn = false;
};

// Prints the moves [first, last) as numbered blocks, then advances `sequence`.
// `park` is the point the header left the tool at: safe Z, XY unknown. A move
// to the point the printer already stands at is skipped. `state` carries the
// modal feed in and out, so a later block knows whether F has to be repeated.
void emitMoves(std::string& out, const Path& path, std::size_t first, std::size_t last,
               int& sequence, const Point3& park, EmitState& state);

} // namespace qp
