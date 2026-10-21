// occt/step_bundle.h - everything there is to know about a STEP model, in one call
// and without Qt: the vertex soup, crease-aware normals and pre-coalesce edges.
//
// Needed by the viewer controller (cpp/qmlbackend/gv_toolpath.cpp): it takes this set
// and repeats the rest of the load path (edge coalescing and buffer packing), so the
// outline buffer stays bit-for-bit identical to the reference.
#pragma once

#include <string>
#include <vector>

#include "core/tangents.h"     // gv::StepSurfaceList - analytic surfaces

namespace gv {

struct StepBundle {
    std::vector<double> verts;     // (F*3,3) float64, flat row, as in the reference
    std::vector<float> normals;    // (F,3,3) float32; empty when there is no soup
    std::vector<double> segments;  // (N,2,3) float64 - pre-coalesce edges, mm
};

// A single STEP pass (readStepShape), tessellation, flipping the shell outward,
// crease-aware normals (creaseDeg = 20, as in the reference) and shape edges with a
// linear edge deflection edgeDeflection (0.05 mm, as in the reference). The path is
// UTF-8: that is how OCCT accepts it. A read error -> std::runtime_error.
StepBundle loadStepBundle(const std::string &utf8Path, double deflection = 0.05,
                          double edgeDeflection = 0.05, double creaseDeg = 20.0);

// Model and analytic surfaces of revolution of one file in a single read:
// a STEPControl_Reader pass costs about 0.8 s per model, and a second pass for the
// surfaces would double it (the reference pays exactly that - see _step_model in
// qmlbackend/gv_toolpath.cpp). The order of work is the same as in the Python pair
// load_step_model + load_step_surfaces on a shared shape: skin tessellation, then
// face traversal.
struct StepBundleAll {
    StepBundle bundle;
    StepSurfaceList surfaces;
};

StepBundleAll loadStepBundleAll(const std::string &utf8Path, double deflection = 0.05,
                                double edgeDeflection = 0.05, double creaseDeg = 20.0);

// Only the analytic surfaces of the file (the STEP read happens inside). Needed
// where the STEP branch is still in Python while C++ already computes the surfaces.
StepSurfaceList loadStepSurfacesBundle(const std::string &utf8Path,
                                       double deflection = 0.05);

} // namespace gv
