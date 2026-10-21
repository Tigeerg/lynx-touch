#include "occt/step_bundle.h"

#include "occt/step_edges.h"
#include "occt/step_import.h"
#include "occt/step_surfaces.h"

#include <TopoDS_Shape.hxx>

#include <utility>

namespace gv {
namespace {

// Model from an already read shape: tessellation, shell orientation, normals and
// edges before welding. A separate function so that the STEP read in
// loadStepBundle and loadStepBundleAll is the same, not two copies in a row.
StepBundle bundleFromShape(const TopoDS_Shape &shape, double deflection,
                           double edgeDeflection, double creaseDeg)
{
    StepModel model = loadStepModel(shape, deflection, creaseDeg);

    StepBundle out;
    // Move, not copy: the soup is about 3 MB, the normals about 1.6 MB - the
    // model->soup assembly used to materialise them again.
    out.verts = std::move(model.verts);
    out.normals = std::move(model.normals);

    // The reference sets the edge tolerance separately from tessellation, and the
    // welding is done by the caller: in the reference _coalesce_segments drives
    // it, and the outline buffer order stays the same.
    const std::vector<EdgeSegment> segs = stepRawSegments(shape, edgeDeflection, true);
    out.segments.resize(segs.size() * 6);
    for (std::size_t i = 0; i < segs.size(); ++i) {
        double *dst = &out.segments[i * 6];
        for (std::size_t k = 0; k < 3; ++k) {
            dst[k] = segs[i].a[k];
            dst[3 + k] = segs[i].b[k];
        }
    }
    return out;
}

} // namespace

StepBundle loadStepBundle(const std::string &utf8Path, double deflection,
                          double edgeDeflection, double creaseDeg)
{
    return bundleFromShape(readStepShape(utf8Path), deflection, edgeDeflection, creaseDeg);
}

StepBundleAll loadStepBundleAll(const std::string &utf8Path, double deflection,
                                double edgeDeflection, double creaseDeg)
{
    const TopoDS_Shape shape = readStepShape(utf8Path);

    StepBundleAll out;
    out.bundle = bundleFromShape(shape, deflection, edgeDeflection, creaseDeg);
    // Surfaces - after tessellation: the reference also reads the shape once and
    // drives the skin tessellation and the face traversal over it. The shape is
    // already meshed (loadStepModel above), so tessellation is skipped: the same
    // call with the same parameters already ran, and a repeat would be an empty
    // pass.
    out.surfaces = loadStepSurfaces(shape, deflection, true);
    return out;
}

StepSurfaceList loadStepSurfacesBundle(const std::string &utf8Path, double deflection)
{
    return loadStepSurfaces(readStepShape(utf8Path), deflection);
}

} // namespace gv
