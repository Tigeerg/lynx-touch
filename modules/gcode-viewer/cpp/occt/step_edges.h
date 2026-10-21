// occt/step_edges.h - edges of a STEP B-Rep shape as segments. A layer without Qt:
// the same algorithm is needed both by the CNC.Edges QML plugin
// (cpp/stepledgesgeometry.cpp) and by the model assembly (cpp/occt/step_bundle.cpp),
// so it lives in gvstep as a single copy.
//
// A repeat of the reference edge geometry of the model
// (occt/step_import.cpp): traversal of TopAbs_EDGE in
// TopExp_Explorer order, the polyline of each edge - GCPnts_UniformDeflection with a
// linear deflection of 0.05 mm (the angular criterion is not used: the reference does
// not set one), then coalescing of nearly coincident segments (_coalesce_segments,
// core/packing.cpp). Seam lines of curved faces (a straight edge whose adjacent faces
// are all curved) are dropped: SolidWorks does not draw them either.
//
// Agreement with the reference is checked by cpp/stepledges_probe.cpp and
// (segments before coalescing on
// Test_model: 4884, after coalescing 2426).
#pragma once

#include <array>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace gv {

// An outline segment in model coordinates, mm.
struct EdgeSegment {
    std::array<double, 3> a{};
    std::array<double, 3> b{};
};

// Pre-coalesce shape edges: the traversal order and the point order inside an edge
// are part of the contract with the reference and must not be changed. dropSeams
// removes seam lines of curved faces (the reference has no way to switch it off).
std::vector<EdgeSegment> stepRawSegments(const TopoDS_Shape &shape,
                                         double deflection = 0.05,
                                         bool dropSeams = true);

// Coalescing of nearly coincident straight segments: buckets by direction, inside a
// bucket - clusters by perpendicular distance (distTol mm grid), the axial intervals
// of a cluster are merged. The default values are as in the reference.
std::vector<EdgeSegment> coalesceStepSegments(const std::vector<EdgeSegment> &segments,
                                              double distTol = 0.12,
                                              double gapTol = 0.5);

} // namespace gv
