// core/silhouette.h - mesh topology for the silhouette and the silhouette at an
// eye: mirrors core/silhouette.cpp.
//
// The layer is pure: neither Qt nor OCCT - only arithmetic over the vertex soup,
// like normals and packing. That is why it lives in gvcore rather than in the
// OCCT target: the topology is built from the soup, wherever the soup comes from
// (STEP - stage 4a, STL or the skin mesh).
#pragma once

#include <cstdint>
#include <vector>

#include "core/quality.h"     // gv::QualityPreset - weld and candidate tolerances

namespace gv {

// Mesh topology for the silhouette (MeshTopology in core/gv_types.h).
//
// ea/eb (K,3) float32 - ends of candidate silhouette edges, f0/f1 (K,) int32 -
// adjacent faces (f0 < f1), n (F,3) float32 - face normals, d (F,) float32 -
// precomputed n·cen (face centroids): per frame only the sign test
// s = n·E - d remains. Candidates are only edges of a smooth horizon: sharp
// transitions (steeper than 20 degrees) and degenerate faces are filtered out,
// see silhouette.cpp.
struct MeshTopology {
    std::vector<float> ea;
    std::vector<float> eb;
    std::vector<std::int32_t> f0;
    std::vector<std::int32_t> f1;
    std::vector<float> n;
    std::vector<float> d;
    std::size_t faces = 0;

    std::size_t edgeCount() const { return f0.size(); }
};

// Topology from the vertex soup (F*9 double: three vertices per triangle, flat
// row, like load_step_model and load_stl_vertices).
//
// A bit-for-bit repeat of _build_silhouette_topology, including the order of
// operations:
// * vertex welding - on the weld grid (np.round(V / weld), round half to even),
//   clusters and their numbers in lexicographic key order, like
//   np.unique(axis=0);
// * triangle edges (a,b) with a < b, groups of exactly two faces - candidates in
//   lexicographic key sort order;
// * normals and areas - from the triangles themselves (smoothing would blur the
//   horizon), d - n·cen in float32 with the same summation order as numpy.
// Tolerances come from the preset (core/quality.h): at the med level they are
// the same as the former hard-coded numbers.
MeshTopology buildSilhouetteTopology(const std::vector<double>& soup,
                                     const QualityPreset& quality = QualityPreset{});

// Silhouette edges at eye eyeMm (raw model frame): (K,2,3) float32 flat row -
// candidate ends in topology order; an empty vector if there is no silhouette.
// An edge is a silhouette edge when the adjacent faces lie on opposite sides of
// the eye (their sign s differs): the edge separates a visible and an invisible
// face.
std::vector<float> meshSilhouetteSegments(const MeshTopology& topo, const double eyeMm[3]);

// Frame silhouette buffers that survive the frame: Test_model used to allocate 20
// buffers and 63 KB per frame, 45 KB of them the per-face sign. The capacities
// live here, so a frame only fills them instead of creating buffers.
struct MeshSilhouetteScratch {
    std::vector<std::int8_t> sign;   // (F,) sign s = n*E - d per face
    std::vector<float> out;          // (K,2,3) float32 flat row
};

// Silhouette into the given buffer: scratch.out is cleared and filled with the
// same order of operations as the version above (the same numbers bit for bit);
// the return is a reference to it.
const std::vector<float>& meshSilhouetteSegments(const MeshTopology& topo,
                                                 const double eyeMm[3],
                                                 MeshSilhouetteScratch& scratch);

} // namespace gv
