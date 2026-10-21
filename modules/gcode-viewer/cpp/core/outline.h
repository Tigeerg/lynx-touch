// core/outline.h - mesh outline: edges by dihedral angle.
//
// The selected edges are welded into adjacency chains and simplified with
// Douglas-Peucker: on a fine mesh (median edge of Test_model.STL - 0.07 mm) the
// contour otherwise turns into a cloud of sub-millimetre links. The port is
// checked against the reference _feature_edges_np byte for byte, so the order of
// chains and simplification here is part of the result.
#pragma once

#include <vector>

#include "core/packing.h"

namespace gv {

// Chain simplification tolerance, mm (mirrors _FEATURE_SIMPLIFY_MM).
constexpr double FEATURE_SIMPLIFY_MM = 0.15;

// Mirrors _feature_edges_np: triangle soup (9 float64 coordinates per triangle)
// -> packed Lines buffer of edges. Boundary edges, edges with more than two
// faces and edges with a dihedral angle above angle_deg are kept.
// simplifyMm - chain simplification tolerance (FEATURE_SIMPLIFY_MM at the med
// level).
PackedEdges featureEdgesNp(const std::vector<double>& soup, double angle_deg,
                           double simplifyMm = FEATURE_SIMPLIFY_MM);

} // namespace gv
