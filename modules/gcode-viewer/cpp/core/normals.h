// core/normals.h - crease-aware skin normals.
//
// The layer is pure: neither Qt nor OCCT - only arithmetic over arrays, like
// path_prep. That is why it lives in gvcore rather than in the OCCT target: the
// same normals are needed by the STL outline (stage 4 of the port) and by both
// import branches.
#pragma once

#include <vector>

namespace gv {

// Skin normals (F,3,3) float32 for a vertex soup (F*9 double: three vertices per
// triangle, flat row, like load_stl_vertices).
//
// A bit-for-bit repeat of _smooth_face_normals, including the order of
// operations:
// * face normal and area - cross and the square root of the sum of squares in
//   the same order as numpy (three terms, left to right);
// * vertex welding - on a 0.001 mm grid (np.round(T * 1000.0), round half to
//   even), int64 row equality;
// * inside a weld the cluster seed is the largest-area face (stable sort by -w:
//   equal weights keep the slot order), and adjacent normals within creaseDeg
//   (20 degrees) join the cluster;
// * the average is area-weighted, summed over the group slots in ascending slot
//   order (zeros at non-adjacent slots: x + 0.0 is exact).
// The caller (step_import) sets the shell orientation: a per-face flip by
// centroid would turn the normals of concave areas inward.
std::vector<float> smoothFaceNormals(const std::vector<double>& soup, double creaseDeg = 20.0);

} // namespace gv
