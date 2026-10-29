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
#include <vector>

#include "core/path.h"

namespace qp {

// Step of the block numbers, the one of the reference post file.
constexpr int kSequenceStep = 5;

// The tag body number: "%g" with a trailing dot ("XMIN=-20.", "D=10."), the
// spelling of the reference files. The dot is kept so that the byte-for-byte
// goldens of later slices compare against the reference format.
std::string num(double value);

// Coordinates of moves: three digits after the decimal point, always.
std::string coord(double value);

// One numbered block: "N100 G1 X1.000".
std::string numberedBlock(int sequence, const std::string& words);

// A feed word: an integer when the value is whole ("F860"), else up to three
// digits. Shared by the moves and the cycles.
std::string feedWord(double value);

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

// Parameters of one canned drilling cycle as the emitter writes it: the first
// block carries the cycle word, the depth, the R plane and (for G83) Q, the
// following blocks carry only X/Y, and G80 closes the cycle.
struct DrillCycle {
    bool peck = false;      // true: G83, false: G81
    double depthZ = 0.0;    // Z of the hole bottom, mm
    double rZ = 0.0;        // R plane, mm
    double peckMm = 0.0;    // Q of G83, mm
    double feedMmMin = 0.0;
};

// Prints the holes as one cycle block set and closes it with G80.
void emitCycle(std::string& out, const std::vector<Point3>& holes, const DrillCycle& cycle,
               int& sequence, EmitState& state);

// Prints the moves [first, last) as numbered blocks, then advances `sequence`.
// `park` is the point the header left the tool at: safe Z, XY unknown. A move
// to the point the printer already stands at is skipped. `state` carries the
// modal feed in and out, so a later block knows whether F has to be repeated.
void emitMoves(std::string& out, const Path& path, std::size_t first, std::size_t last,
               int& sequence, const Point3& park, EmitState& state);

} // namespace qp
