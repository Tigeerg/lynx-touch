#include "occt/step_bundle.h"

#include "occt/step_edges.h"
#include "occt/step_import.h"
#include "occt/step_surfaces.h"

#include <TopoDS_Shape.hxx>

#include <utility>

namespace gv {
namespace {

// Модель по уже прочитанной форме: тесселяция, ориентация оболочки, нормали и
// рёбра до склейки. Отдельная функция - чтобы чтение STEP у loadStepBundle и
// loadStepBundleAll было одно и то же, а не две редакции подряд.
StepBundle bundleFromShape(const TopoDS_Shape &shape, double deflection)
{
    StepModel model = loadStepModel(shape, deflection);

    StepBundle out;
    // Перемещение, а не копия: суп около 3 МБ, нормали около 1.6 МБ - набор
    // модель->суп раньше материализовал их повторно.
    out.verts = std::move(model.verts);
    out.normals = std::move(model.normals);

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

} // namespace

StepBundle loadStepBundle(const std::string &utf8Path, double deflection)
{
    return bundleFromShape(readStepShape(utf8Path), deflection);
}

StepBundleAll loadStepBundleAll(const std::string &utf8Path, double deflection)
{
    const TopoDS_Shape shape = readStepShape(utf8Path);

    StepBundleAll out;
    out.bundle = bundleFromShape(shape, deflection);
    // Поверхности - после тесселяции: эталон тоже читает форму один раз, а
    // тесселяцию кожи и обход граней ведёт по ней же. Форма уже размещена
    // (loadStepModel выше), поэтому тесселяция пропускается: тот же вызов с теми
    // же параметрами уже прошёл, повтор был бы пустым проходом.
    out.surfaces = loadStepSurfaces(shape, deflection, true);
    return out;
}

StepSurfaceList loadStepSurfacesBundle(const std::string &utf8Path, double deflection)
{
    return loadStepSurfaces(readStepShape(utf8Path), deflection);
}

} // namespace gv
