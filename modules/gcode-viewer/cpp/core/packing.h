// core/packing.h - packing segments into a Lines buffer.
//
// The buffer layout is a contract with the outline shader: 5 float32 numbers per
// vertex (position 3f + texcoord 2f: visibility 1.0/0.0), that is stride 20
// bytes and two vertices per segment. The core is checked against the reference
// byte for byte, so the vertex order and the amount of padding are part of the
// result, not formatting.
#pragma once

#include <vector>

#include "core/path_prep.h"     // gv::Aabb - bounds, as in _pack_edges_np

namespace gv {

// Packed Lines buffer of segments: vertex bytes, their count and the bounds
// (mirrors PackedEdges from core/gv_types.h). `has_bounds` is false for an empty
// buffer: the reference bounds can be None.
struct PackedEdges {
    std::vector<unsigned char> raw;
    int nv = 0;
    bool has_bounds = false;
    Aabb bounds;
};

// Mirrors _pack_edges_np: segment ends p0 and p1, three float64 coordinates per
// segment, into a float32 buffer. The bounds are computed from the float64
// input, as in the reference (the float32 buffer does not affect them).
PackedEdges packEdgesNp(const std::vector<double>& p0, const std::vector<double>& p1);

// Mirrors packed_positions: segment ends from the buffer as (M, 2, 3) float32, a
// flat list of 6 numbers per segment. Empty buffer -> empty list.
std::vector<float> packedPositions(const PackedEdges& packed);

} // namespace gv
