// core/serialize_read_op.h - the per-kind part of the .qprog reader (6, 7.2).
//
// `serialize_read.cpp` owns the line loop and the document header; this part
// owns the two things that depend on the kind of an operation: the `op` head
// (the kind word and the document of the operation) and the set of keys the
// kind accepts. It is internal to the library: the console and the check call
// `parseProgram`, not these.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"

namespace qp {

// Turns the kind word after `op` ("face", "pocket", "circle", "drill",
// "drill-grid", "drill-circle") into the geometry of the operation. False and
// a message in `error` for an unknown kind; `where` carries the line number.
bool readOpHead(const std::string& kind, OperationGeometry& geometry, const std::string& where,
                std::string& error);

// True when the key `sub` (without the `op.` prefix) belongs to the kind: the
// common keys, the keys of the shape, or the optional R plane of a drill.
// `stored` is false for the optional R plane: the reader accepts it and the
// value reader knows the field.
bool opKeyKnown(const OperationGeometry& geometry, const std::string& sub);

} // namespace qp
