#include "occt/step_bundle.h"

#include "occt/step_edges.h"
#include "occt/step_import.h"

#include <TopoDS_Shape.hxx>

namespace gv {

StepBundle loadStepBundle(const std::string &utf8Path, double deflection)
{
    const TopoDS_Shape shape = readStepShape(utf8Path);
    const StepModel model = loadStepModel(shape, deflection);

    StepBundle out;
    out.verts = model.verts;
    out.normals = model.normals;

    // Допуск рёбер у эталона задан отдельно от тесселяции (0.05 мм жёстко), а
    // склейку делает вызывающий: у Python её ведёт _coalesce_segments, и порядок
    // буфера обводки остаётся тем же.
    const std::vector<EdgeSegment> segs = stepRawSegments(shape, 0.05, true);
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

} // namespace gv
