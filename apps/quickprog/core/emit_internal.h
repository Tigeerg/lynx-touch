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

// Prints the moves [first, last) as numbered blocks, then advances `sequence`.
// `park` is the point the header left the tool at: safe Z, XY unknown. A move
// to the point the printer already stands at is skipped.
void emitMoves(std::string& out, const Path& path, std::size_t first, std::size_t last,
               int& sequence, const Point3& park);

} // namespace qp
