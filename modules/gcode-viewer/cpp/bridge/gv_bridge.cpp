// bridge/gv_bridge.cpp - реализация C-ABI (см. gv_bridge.h). Слой тонкий: вся
// работа лежит в gvcore (нормали, топология силуэта, касательные, парсер и
// подготовка траектории) и gvstep (STEP: модель, поверхности), здесь только
// буферы, коды возврата и текст ошибки.
//
// Память: каждый выходной буфер выделяется std::malloc и освобождается gv_free,
// массивы структур - своей функцией (gv_free_surfaces, gv_free_mesh_topology,
// gv_free_tangent_families, gv_free_path_data). Исключения ядра (std::runtime_error
// из чтения STEP и любые другие) не выходят за границу ABI: они превращаются в
// код возврата и текст в gv_last_error.
#include "bridge/gv_bridge.h"

#include "core/gcode_parser.h"
#include "core/normals.h"
#include "core/path_prep.h"
#include "core/silhouette.h"
#include "core/tangents.h"
#include "occt/step_bundle.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {

// Текст ошибки на поток: OCCT и питоновский воркер читают модель из разных
// потоков, общая строка на всех перепутала бы сообщения.
thread_local std::string g_lastError;

void setError(const std::string &text)
{
    g_lastError = text;
}

// Копия вектора в память библиотеки. Пустой вектор - NULL: длину вызывающий
// знает из out-параметра, а нулевое выделение malloc может вернуть чем угодно.
template <typename T>
T *copyToLibrary(const std::vector<T> &src)
{
    if (src.empty())
        return nullptr;
    T *mem = static_cast<T *>(std::malloc(src.size() * sizeof(T)));
    if (mem == nullptr)
        return nullptr;
    std::memcpy(mem, src.data(), src.size() * sizeof(T));
    return mem;
}

// Наружу массивы ходят как int (ядро хранит std::int32_t): размер и разрядность
// те же, а вызывающему не нужен заголовок <cstdint>.
template <typename T>
std::vector<int> toInt(const std::vector<T> &src)
{
    std::vector<int> out(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        out[i] = static_cast<int>(src[i]);
    }
    return out;
}

const char *safeWhat(const std::exception &exc)
{
    return exc.what() != nullptr ? exc.what() : "неизвестная ошибка";
}

// --- аналитические поверхности ---------------------------------------------

// Раскладка дескрипторов - общая с пробником cpp/tangents_probe.cpp: 0 - цилиндр,
// 1 - конус; 17 чисел на цилиндр (o,d,e1,e2,r,t0,t1,a0,a1, полный круг - NaN);
// 9 чисел на конус (a,d,psi,s0,s1).
constexpr int CYL_ROW = 17;
constexpr int CONE_ROW = 9;
constexpr int STAGE_ROW = 16;

void clearSurfaces(GvSurfaces *s)
{
    if (s == nullptr)
        return;
    s->kinds = nullptr;
    s->surface_count = 0;
    s->cylinders = nullptr;
    s->cylinder_count = 0;
    s->cones = nullptr;
    s->cone_count = 0;
}

// Список поверхностей ядра -> плоские массивы ABI. Разделение на три массива
// повторяет дампы пробника: так сверка с эталоном у моста и у пробника одна.
int fillSurfaces(const gv::StepSurfaceList &surfs, GvSurfaces *out)
{
    std::vector<int> kinds;
    std::vector<double> cyls;
    std::vector<double> cones;
    kinds.reserve(surfs.size());
    for (const gv::StepSurface &s : surfs) {
        if (s.is_cylinder) {
            const gv::CylinderSurface &c = s.cyl;
            kinds.push_back(0);
            cyls.insert(cyls.end(), c.o, c.o + 3);
            cyls.insert(cyls.end(), c.d, c.d + 3);
            cyls.insert(cyls.end(), c.e1, c.e1 + 3);
            cyls.insert(cyls.end(), c.e2, c.e2 + 3);
            cyls.push_back(c.r);
            cyls.push_back(c.t0);
            cyls.push_back(c.t1);
            const double none = std::numeric_limits<double>::quiet_NaN();
            cyls.push_back(c.has_arc ? c.arc0 : none);
            cyls.push_back(c.has_arc ? c.arc1 : none);
        } else {
            const gv::ConeSurface &c = s.cone;
            kinds.push_back(1);
            cones.insert(cones.end(), c.a, c.a + 3);
            cones.insert(cones.end(), c.d, c.d + 3);
            cones.push_back(c.psi);
            cones.push_back(c.s0);
            cones.push_back(c.s1);
        }
    }
    int *kp = copyToLibrary(kinds);
    double *cp = copyToLibrary(cyls);
    double *np = copyToLibrary(cones);
    if ((kp == nullptr && !kinds.empty()) || (cp == nullptr && !cyls.empty())
            || (np == nullptr && !cones.empty())) {
        std::free(kp);
        std::free(cp);
        std::free(np);
        setError("поверхности STEP: не удалось выделить память");
        return GV_ERR_MEM;
    }
    out->kinds = kp;
    out->surface_count = kinds.size();
    out->cylinders = cp;
    out->cylinder_count = cyls.size() / CYL_ROW;
    out->cones = np;
    out->cone_count = cones.size() / CONE_ROW;
    return GV_OK;
}

// Плоские массивы ABI -> список поверхностей ядра (обратная к fillSurfaces).
// false - массивов не хватило на список (ошибка вызывающего, а не данных).
bool surfacesFromFlat(const GvSurfaces *in, gv::StepSurfaceList *out)
{
    out->clear();
    if (in == nullptr || in->surface_count == 0)
        return true;
    if (in->kinds == nullptr)
        return false;
    out->reserve(in->surface_count);
    std::size_t ci = 0;
    std::size_t ni = 0;
    for (std::size_t i = 0; i < in->surface_count; ++i) {
        gv::StepSurface s;
        if (in->kinds[i] == 1) {
            if (in->cones == nullptr || ni >= in->cone_count)
                return false;
            const double *r = &in->cones[ni * CONE_ROW];
            s.is_cylinder = false;
            for (int k = 0; k < 3; ++k) {
                s.cone.a[k] = r[k];
                s.cone.d[k] = r[3 + k];
            }
            s.cone.psi = r[6];
            s.cone.s0 = r[7];
            s.cone.s1 = r[8];
            ++ni;
        } else {
            if (in->cylinders == nullptr || ci >= in->cylinder_count)
                return false;
            const double *r = &in->cylinders[ci * CYL_ROW];
            s.is_cylinder = true;
            for (int k = 0; k < 3; ++k) {
                s.cyl.o[k] = r[k];
                s.cyl.d[k] = r[3 + k];
                s.cyl.e1[k] = r[6 + k];
                s.cyl.e2[k] = r[9 + k];
            }
            s.cyl.r = r[12];
            s.cyl.t0 = r[13];
            s.cyl.t1 = r[14];
            // Полный круг вместо дуги едет как NaN в a0 и a1: у эталона это
            // arc = (None, None), и другого отличия от дуги в дескрипторе нет.
            s.cyl.has_arc = !(r[15] != r[15] && r[16] != r[16]);
            s.cyl.arc0 = s.cyl.has_arc ? r[15] : 0.0;
            s.cyl.arc1 = s.cyl.has_arc ? r[16] : 0.0;
            ++ci;
        }
        out->push_back(s);
    }
    return true;
}

// --- топология силуэта ------------------------------------------------------

void clearTopology(GvMeshTopology *t)
{
    if (t == nullptr)
        return;
    t->ea = nullptr;
    t->eb = nullptr;
    t->f0 = nullptr;
    t->f1 = nullptr;
    t->n = nullptr;
    t->d = nullptr;
    t->edge_count = 0;
    t->face_count = 0;
}

// --- семейства касательных --------------------------------------------------

void clearFamilies(GvTangentFamilies *f)
{
    if (f == nullptr)
        return;
    f->O = nullptr;
    f->D = nullptr;
    f->E1 = nullptr;
    f->E2 = nullptr;
    f->R = nullptr;
    f->T0 = nullptr;
    f->T1 = nullptr;
    f->RREF = nullptr;
    f->A0 = nullptr;
    f->A1 = nullptr;
    f->FULL = nullptr;
    f->DROP = nullptr;
    f->ARC = nullptr;
    f->count = 0;
    f->cones = nullptr;
    f->cone_count = 0;
}

// Плоские массивы ABI -> семейства ядра: у моста нет ручки на C++-объект, поэтому
// семейства едут массивами в обе стороны, как поверхности и топология.
bool familiesFromFlat(const GvTangentFamilies *in, gv::CylinderFamilies *cy)
{
    const std::size_t n = in->count;
    cy->count = n;
    if (n == 0)
        return true;
    if (in->O == nullptr || in->D == nullptr || in->E1 == nullptr || in->E2 == nullptr
            || in->R == nullptr || in->T0 == nullptr || in->T1 == nullptr
            || in->RREF == nullptr || in->A0 == nullptr || in->A1 == nullptr
            || in->FULL == nullptr || in->DROP == nullptr || in->ARC == nullptr) {
        return false;
    }
    cy->O.assign(in->O, in->O + n * 3);
    cy->D.assign(in->D, in->D + n * 3);
    cy->E1.assign(in->E1, in->E1 + n * 3);
    cy->E2.assign(in->E2, in->E2 + n * 3);
    cy->R.assign(in->R, in->R + n);
    cy->T0.assign(in->T0, in->T0 + n);
    cy->T1.assign(in->T1, in->T1 + n);
    cy->RREF.assign(in->RREF, in->RREF + n);
    cy->A0.assign(in->A0, in->A0 + n);
    cy->A1.assign(in->A1, in->A1 + n);
    cy->FULL.assign(in->FULL, in->FULL + n);
    cy->DROP.assign(in->DROP, in->DROP + n * 8);
    cy->ARC.assign(in->ARC, in->ARC + n * 64);
    return true;
}

// --- подготовка траектории --------------------------------------------------

void clearPathData(GvPathData *d)
{
    if (d == nullptr)
        return;
    d->seg_count = 0;
    d->last[0] = d->last[1] = d->last[2] = 0.0;
    d->pts = nullptr;
    d->cum = nullptr;
    d->rapid = nullptr;
    d->speeds = nullptr;
    d->point_count = 0;
    d->total = 0.0;
    d->rapid_stream = nullptr;
    d->rapid_bytes = 0;
    d->rapid_nv = 0;
    d->feed_stream = nullptr;
    d->feed_bytes = 0;
    d->feed_nv = 0;
    d->has_bounds = 0;
    for (int k = 0; k < 6; ++k) {
        d->bounds[k] = 0.0;
    }
    for (int k = 0; k < 3; ++k) {
        d->center[k] = 0.0;
    }
    d->radius = 50.0;                 // умолчание PathData: путь без точек
    d->min_y = 0.0;
    d->stages = nullptr;
    d->stage_count = 0;
    d->stage_labels = nullptr;
    d->stage_label_at = nullptr;
    d->stage_types = nullptr;
    d->stage_type_at = nullptr;
}

// Подписи и типы этапов: две строки на этап. Строку через границу ABI не
// передать, поэтому обе едут блоками UTF-8 и таблицей смещений (S+1 значений).
void fillStageText(const std::vector<gv::StageRange> &stages, std::vector<char> *labels,
                   std::vector<std::size_t> *labelAt, std::vector<char> *types,
                   std::vector<std::size_t> *typeAt)
{
    labelAt->push_back(0);
    typeAt->push_back(0);
    for (const gv::StageRange &st : stages) {
        labels->insert(labels->end(), st.label.begin(), st.label.end());
        labelAt->push_back(labels->size());
        types->insert(types->end(), st.info.type.begin(), st.info.type.end());
        typeAt->push_back(types->size());
    }
}

// 12 числовых полей UI_TOOL в строку этапа: порядок - порядок полей ToolInfo.
void fillStageTool(const gv::ToolInfo &info, double *row)
{
    row[2] = info.present ? 1.0 : 0.0;
    row[3] = info.number;
    row[4] = info.diameter;
    row[5] = info.corner_radius;
    row[6] = info.taper_deg;
    row[7] = info.tip_angle_deg;
    row[8] = info.tip_diameter;
    row[9] = info.flute_length;
    row[10] = info.shoulder_length;
    row[11] = info.body_length;
    row[12] = info.shank_diameter;
    row[13] = info.overall_length;
    row[14] = info.flutes;
    row[15] = 0.0;                    // запас: строка кратна 16
}

} // namespace

extern "C" {

int gv_abi_version(void)
{
    return GV_ABI_VERSION;
}

const char *gv_last_error(void)
{
    return g_lastError.c_str();
}

void gv_free(void *mem)
{
    std::free(mem);
}

int gv_smooth_face_normals(const double *verts, size_t vert_count, double crease_deg,
                           float **out_normals, size_t *out_count)
{
    if (out_normals != nullptr)
        *out_normals = nullptr;
    if (out_count != nullptr)
        *out_count = 0;
    if (verts == nullptr || out_normals == nullptr || out_count == nullptr) {
        setError("gv_smooth_face_normals: пустой указатель на вход или выход");
        return GV_ERR_ARG;
    }
    if (vert_count == 0 || vert_count % 9 != 0) {
        setError("gv_smooth_face_normals: вершин " + std::to_string(vert_count)
                 + ", ожидалось кратно 9 (суп по три вершины на треугольник)");
        return GV_ERR_ARG;
    }
    if (!(crease_deg > 0.0) || crease_deg >= 180.0) {
        setError("gv_smooth_face_normals: порог сглаживания вне (0, 180) градусов");
        return GV_ERR_ARG;
    }
    try {
        const std::vector<double> soup(verts, verts + vert_count);
        const std::vector<float> normals = gv::smoothFaceNormals(soup, crease_deg);
        float *mem = copyToLibrary(normals);
        if (mem == nullptr && !normals.empty()) {
            setError("gv_smooth_face_normals: не удалось выделить память");
            return GV_ERR_MEM;
        }
        *out_normals = mem;
        *out_count = normals.size();
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_smooth_face_normals: ") + safeWhat(exc));
        return GV_ERR;
    } catch (...) {
        setError("gv_smooth_face_normals: неизвестное исключение");
        return GV_ERR;
    }
}

int gv_load_step_model(const char *utf8_path, double deflection,
                       double **out_verts, size_t *out_vert_count,
                       float **out_normals, size_t *out_normal_count,
                       double **out_segments, size_t *out_segment_count)
{
    double *verts = nullptr;
    float *normals = nullptr;
    double *segments = nullptr;
    if (out_verts != nullptr)
        *out_verts = nullptr;
    if (out_vert_count != nullptr)
        *out_vert_count = 0;
    if (out_normals != nullptr)
        *out_normals = nullptr;
    if (out_normal_count != nullptr)
        *out_normal_count = 0;
    if (out_segments != nullptr)
        *out_segments = nullptr;
    if (out_segment_count != nullptr)
        *out_segment_count = 0;
    if (utf8_path == nullptr || out_verts == nullptr || out_vert_count == nullptr
            || out_normals == nullptr || out_normal_count == nullptr
            || out_segments == nullptr || out_segment_count == nullptr) {
        setError("gv_load_step_model: пустой указатель на путь или на выход");
        return GV_ERR_ARG;
    }
    if (!(deflection > 0.0)) {
        setError("gv_load_step_model: допуск тесселяции должен быть больше нуля");
        return GV_ERR_ARG;
    }

    try {
        const gv::StepBundle bundle = gv::loadStepBundle(utf8_path, deflection);
        verts = copyToLibrary(bundle.verts);
        normals = copyToLibrary(bundle.normals);
        segments = copyToLibrary(bundle.segments);
        if ((verts == nullptr && !bundle.verts.empty())
                || (normals == nullptr && !bundle.normals.empty())
                || (segments == nullptr && !bundle.segments.empty())) {
            std::free(verts);
            std::free(normals);
            std::free(segments);
            setError("gv_load_step_model: не удалось выделить память");
            return GV_ERR_MEM;
        }
        *out_verts = verts;
        *out_vert_count = bundle.verts.size();
        *out_normals = normals;
        *out_normal_count = bundle.normals.size();
        *out_segments = segments;
        *out_segment_count = bundle.segments.size();
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_load_step_model: ") + safeWhat(exc));
        return GV_ERR_STEP;
    } catch (...) {
        setError("gv_load_step_model: неизвестное исключение");
        return GV_ERR_STEP;
    }
}

int gv_load_step_surfaces(const char *utf8_path, double deflection, GvSurfaces *out)
{
    clearSurfaces(out);
    if (utf8_path == nullptr || out == nullptr) {
        setError("gv_load_step_surfaces: пустой указатель на путь или на выход");
        return GV_ERR_ARG;
    }
    if (!(deflection > 0.0)) {
        setError("gv_load_step_surfaces: допуск тесселяции должен быть больше нуля");
        return GV_ERR_ARG;
    }
    try {
        const gv::StepSurfaceList surfs = gv::loadStepSurfacesBundle(utf8_path, deflection);
        return fillSurfaces(surfs, out);
    } catch (const std::exception &exc) {
        setError(std::string("gv_load_step_surfaces: ") + safeWhat(exc));
        return GV_ERR_STEP;
    } catch (...) {
        setError("gv_load_step_surfaces: неизвестное исключение");
        return GV_ERR_STEP;
    }
}

int gv_load_step_all(const char *utf8_path, double deflection,
                     double **out_verts, size_t *out_vert_count,
                     float **out_normals, size_t *out_normal_count,
                     double **out_segments, size_t *out_segment_count,
                     GvSurfaces *out_surfaces)
{
    double *verts = nullptr;
    float *normals = nullptr;
    double *segments = nullptr;
    if (out_verts != nullptr)
        *out_verts = nullptr;
    if (out_vert_count != nullptr)
        *out_vert_count = 0;
    if (out_normals != nullptr)
        *out_normals = nullptr;
    if (out_normal_count != nullptr)
        *out_normal_count = 0;
    if (out_segments != nullptr)
        *out_segments = nullptr;
    if (out_segment_count != nullptr)
        *out_segment_count = 0;
    clearSurfaces(out_surfaces);
    if (utf8_path == nullptr || out_verts == nullptr || out_vert_count == nullptr
            || out_normals == nullptr || out_normal_count == nullptr
            || out_segments == nullptr || out_segment_count == nullptr
            || out_surfaces == nullptr) {
        setError("gv_load_step_all: пустой указатель на путь или на выход");
        return GV_ERR_ARG;
    }
    if (!(deflection > 0.0)) {
        setError("gv_load_step_all: допуск тесселяции должен быть больше нуля");
        return GV_ERR_ARG;
    }

    try {
        const gv::StepBundleAll all = gv::loadStepBundleAll(utf8_path, deflection);
        verts = copyToLibrary(all.bundle.verts);
        normals = copyToLibrary(all.bundle.normals);
        segments = copyToLibrary(all.bundle.segments);
        if ((verts == nullptr && !all.bundle.verts.empty())
                || (normals == nullptr && !all.bundle.normals.empty())
                || (segments == nullptr && !all.bundle.segments.empty())) {
            std::free(verts);
            std::free(normals);
            std::free(segments);
            setError("gv_load_step_all: не удалось выделить память");
            return GV_ERR_MEM;
        }
        *out_verts = verts;
        *out_vert_count = all.bundle.verts.size();
        *out_normals = normals;
        *out_normal_count = all.bundle.normals.size();
        *out_segments = segments;
        *out_segment_count = all.bundle.segments.size();
        return fillSurfaces(all.surfaces, out_surfaces);
    } catch (const std::exception &exc) {
        setError(std::string("gv_load_step_all: ") + safeWhat(exc));
        return GV_ERR_STEP;
    } catch (...) {
        setError("gv_load_step_all: неизвестное исключение");
        return GV_ERR_STEP;
    }
}

void gv_free_surfaces(GvSurfaces *surfaces)
{
    if (surfaces == nullptr)
        return;
    std::free(surfaces->kinds);
    std::free(surfaces->cylinders);
    std::free(surfaces->cones);
    clearSurfaces(surfaces);
}

int gv_build_silhouette_topology(const double *soup, size_t soup_count,
                                 GvMeshTopology *out)
{
    clearTopology(out);
    if (soup == nullptr || out == nullptr) {
        setError("gv_build_silhouette_topology: пустой указатель на суп или на выход");
        return GV_ERR_ARG;
    }
    if (soup_count == 0 || soup_count % 9 != 0) {
        setError("gv_build_silhouette_topology: вершин " + std::to_string(soup_count)
                 + ", ожидалось кратно 9 (суп по три вершины на треугольник)");
        return GV_ERR_ARG;
    }
    try {
        const std::vector<double> flat(soup, soup + soup_count);
        const gv::MeshTopology topo = gv::buildSilhouetteTopology(flat);
        const std::vector<int> f0 = toInt(topo.f0);
        const std::vector<int> f1 = toInt(topo.f1);
        float *ea = copyToLibrary(topo.ea);
        float *eb = copyToLibrary(topo.eb);
        int *p0 = copyToLibrary(f0);
        int *p1 = copyToLibrary(f1);
        float *nn = copyToLibrary(topo.n);
        float *dd = copyToLibrary(topo.d);
        if ((ea == nullptr && !topo.ea.empty()) || (eb == nullptr && !topo.eb.empty())
                || (p0 == nullptr && !f0.empty()) || (p1 == nullptr && !f1.empty())
                || (nn == nullptr && !topo.n.empty()) || (dd == nullptr && !topo.d.empty())) {
            std::free(ea);
            std::free(eb);
            std::free(p0);
            std::free(p1);
            std::free(nn);
            std::free(dd);
            setError("gv_build_silhouette_topology: не удалось выделить память");
            return GV_ERR_MEM;
        }
        out->ea = ea;
        out->eb = eb;
        out->f0 = p0;
        out->f1 = p1;
        out->n = nn;
        out->d = dd;
        out->edge_count = topo.edgeCount();
        out->face_count = topo.faces;
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_build_silhouette_topology: ") + safeWhat(exc));
        return GV_ERR;
    } catch (...) {
        setError("gv_build_silhouette_topology: неизвестное исключение");
        return GV_ERR;
    }
}

void gv_free_mesh_topology(GvMeshTopology *topology)
{
    if (topology == nullptr)
        return;
    std::free(topology->ea);
    std::free(topology->eb);
    std::free(topology->f0);
    std::free(topology->f1);
    std::free(topology->n);
    std::free(topology->d);
    clearTopology(topology);
}

int gv_mesh_silhouette_segments(const GvMeshTopology *topology, const double eye_mm[3],
                                float **out_segments, size_t *out_count)
{
    if (out_segments != nullptr)
        *out_segments = nullptr;
    if (out_count != nullptr)
        *out_count = 0;
    if (topology == nullptr || eye_mm == nullptr || out_segments == nullptr
            || out_count == nullptr) {
        setError("gv_mesh_silhouette_segments: пустой указатель на топологию или на выход");
        return GV_ERR_ARG;
    }
    if (topology->face_count == 0 || topology->edge_count == 0) {
        return GV_OK;                 // модель без топологии: силуэта нет
    }
    if (topology->ea == nullptr || topology->eb == nullptr || topology->f0 == nullptr
            || topology->f1 == nullptr || topology->n == nullptr || topology->d == nullptr) {
        setError("gv_mesh_silhouette_segments: топология без массивов");
        return GV_ERR_ARG;
    }
    try {
        gv::MeshTopology topo;
        topo.faces = topology->face_count;
        topo.ea.assign(topology->ea, topology->ea + topology->edge_count * 3);
        topo.eb.assign(topology->eb, topology->eb + topology->edge_count * 3);
        topo.f0.assign(topology->f0, topology->f0 + topology->edge_count);
        topo.f1.assign(topology->f1, topology->f1 + topology->edge_count);
        topo.n.assign(topology->n, topology->n + topology->face_count * 3);
        topo.d.assign(topology->d, topology->d + topology->face_count);
        const std::vector<float> segs = gv::meshSilhouetteSegments(topo, eye_mm);
        float *mem = copyToLibrary(segs);
        if (mem == nullptr && !segs.empty()) {
            setError("gv_mesh_silhouette_segments: не удалось выделить память");
            return GV_ERR_MEM;
        }
        *out_segments = mem;
        *out_count = segs.size();
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_mesh_silhouette_segments: ") + safeWhat(exc));
        return GV_ERR;
    } catch (...) {
        setError("gv_mesh_silhouette_segments: неизвестное исключение");
        return GV_ERR;
    }
}

int gv_tangent_families(const GvSurfaces *surfaces,
                        const unsigned char *packed_raw, size_t packed_bytes,
                        int packed_nv,
                        const double *skin_soup, size_t skin_soup_count,
                        const float *skin_normals, size_t skin_normal_count,
                        GvTangentFamilies *out)
{
    clearFamilies(out);
    if (surfaces == nullptr || out == nullptr) {
        setError("gv_tangent_families: пустой указатель на поверхности или на выход");
        return GV_ERR_ARG;
    }
    if (skin_soup_count % 9 != 0) {
        setError("gv_tangent_families: вершин кожи " + std::to_string(skin_soup_count)
                 + ", ожидалось кратно 9");
        return GV_ERR_ARG;
    }
    if (packed_bytes % (5 * sizeof(float)) != 0) {
        setError("gv_tangent_families: обводка " + std::to_string(packed_bytes)
                 + " байт, ожидалось кратно 20 (5 чисел float32 на вершину)");
        return GV_ERR_ARG;
    }
    try {
        gv::StepSurfaceList surfs;
        if (!surfacesFromFlat(surfaces, &surfs)) {
            setError("gv_tangent_families: массивы поверхностей не полны");
            return GV_ERR_ARG;
        }

        // nullopt вместо пустой упаковки: в эталоне packed_edges = None, и
        // отсутствие пометок DROP - часть политики, а не пустой список.
        gv::PackedEdges packed;
        const bool havePacked = packed_raw != nullptr && packed_nv > 0;
        if (havePacked) {
            packed.raw.assign(packed_raw, packed_raw + packed_bytes);
            packed.nv = packed_nv;
            packed.has_bounds = false;
        }

        gv::SkinTriangles skin;
        gv::SkinTriangles *skinPtr = nullptr;
        if (skin_soup != nullptr && skin_normals != nullptr && skin_soup_count > 0) {
            const std::vector<double> soup(skin_soup, skin_soup + skin_soup_count);
            const std::vector<float> normals(skin_normals, skin_normals + skin_normal_count);
            skin = gv::buildSkinTriangles(soup, normals);
            skinPtr = &skin;
        }

        const gv::TangentFamilies fam =
            gv::buildTangentFamilies(surfs, havePacked ? &packed : nullptr, skinPtr);
        const gv::CylinderFamilies &cy = fam.cylinders;

        // Столбцы конуса: a(3), d(3), psi, s0, s1 - девять чисел на конус.
        std::vector<double> cones(fam.cones.size() * 9);
        for (std::size_t i = 0; i < fam.cones.size(); ++i) {
            const gv::ConeSurface &c = fam.cones[i];
            for (int k = 0; k < 3; ++k) {
                cones[i * 9 + std::size_t(k)] = c.a[k];
                cones[i * 9 + 3 + std::size_t(k)] = c.d[k];
            }
            cones[i * 9 + 6] = c.psi;
            cones[i * 9 + 7] = c.s0;
            cones[i * 9 + 8] = c.s1;
        }

        double *O = copyToLibrary(cy.O);
        double *D = copyToLibrary(cy.D);
        double *E1 = copyToLibrary(cy.E1);
        double *E2 = copyToLibrary(cy.E2);
        double *R = copyToLibrary(cy.R);
        double *T0 = copyToLibrary(cy.T0);
        double *T1 = copyToLibrary(cy.T1);
        double *RREF = copyToLibrary(cy.RREF);
        double *A0 = copyToLibrary(cy.A0);
        double *A1 = copyToLibrary(cy.A1);
        unsigned char *FULL = copyToLibrary(cy.FULL);
        double *DROP = copyToLibrary(cy.DROP);
        unsigned char *ARC = copyToLibrary(cy.ARC);
        double *coneMem = copyToLibrary(cones);
        const bool memMissing =
            (O == nullptr && !cy.O.empty()) || (D == nullptr && !cy.D.empty())
            || (E1 == nullptr && !cy.E1.empty()) || (E2 == nullptr && !cy.E2.empty())
            || (R == nullptr && !cy.R.empty()) || (T0 == nullptr && !cy.T0.empty())
            || (T1 == nullptr && !cy.T1.empty()) || (RREF == nullptr && !cy.RREF.empty())
            || (A0 == nullptr && !cy.A0.empty()) || (A1 == nullptr && !cy.A1.empty())
            || (FULL == nullptr && !cy.FULL.empty()) || (DROP == nullptr && !cy.DROP.empty())
            || (ARC == nullptr && !cy.ARC.empty())
            || (coneMem == nullptr && !cones.empty());
        if (memMissing) {
            std::free(O);
            std::free(D);
            std::free(E1);
            std::free(E2);
            std::free(R);
            std::free(T0);
            std::free(T1);
            std::free(RREF);
            std::free(A0);
            std::free(A1);
            std::free(FULL);
            std::free(DROP);
            std::free(ARC);
            std::free(coneMem);
            setError("gv_tangent_families: не удалось выделить память");
            return GV_ERR_MEM;
        }
        out->O = O;
        out->D = D;
        out->E1 = E1;
        out->E2 = E2;
        out->R = R;
        out->T0 = T0;
        out->T1 = T1;
        out->RREF = RREF;
        out->A0 = A0;
        out->A1 = A1;
        out->FULL = FULL;
        out->DROP = DROP;
        out->ARC = ARC;
        out->count = cy.count;
        out->cones = coneMem;
        out->cone_count = fam.cones.size();
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_tangent_families: ") + safeWhat(exc));
        return GV_ERR;
    } catch (...) {
        setError("gv_tangent_families: неизвестное исключение");
        return GV_ERR;
    }
}

void gv_free_tangent_families(GvTangentFamilies *families)
{
    if (families == nullptr)
        return;
    std::free(families->O);
    std::free(families->D);
    std::free(families->E1);
    std::free(families->E2);
    std::free(families->R);
    std::free(families->T0);
    std::free(families->T1);
    std::free(families->RREF);
    std::free(families->A0);
    std::free(families->A1);
    std::free(families->FULL);
    std::free(families->DROP);
    std::free(families->ARC);
    std::free(families->cones);
    clearFamilies(families);
}

int gv_tangent_segments(const GvTangentFamilies *families, const double eye_mm[3],
                        double **out_segments, size_t *out_count)
{
    if (out_segments != nullptr)
        *out_segments = nullptr;
    if (out_count != nullptr)
        *out_count = 0;
    if (families == nullptr || eye_mm == nullptr || out_segments == nullptr
            || out_count == nullptr) {
        setError("gv_tangent_segments: пустой указатель на семейства или на выход");
        return GV_ERR_ARG;
    }
    try {
        gv::TangentFamilies fam;
        if (!familiesFromFlat(families, &fam.cylinders)) {
            setError("gv_tangent_segments: массивы семейств не полны");
            return GV_ERR_ARG;
        }
        if (families->cone_count > 0 && families->cones == nullptr) {
            setError("gv_tangent_segments: конусы без массива");
            return GV_ERR_ARG;
        }
        fam.cones.resize(families->cone_count);
        for (std::size_t i = 0; i < families->cone_count; ++i) {
            const double *r = &families->cones[i * 9];
            gv::ConeSurface &c = fam.cones[i];
            for (int k = 0; k < 3; ++k) {
                c.a[k] = r[k];
                c.d[k] = r[3 + k];
            }
            c.psi = r[6];
            c.s0 = r[7];
            c.s1 = r[8];
        }
        const std::vector<double> segs = gv::tangentFromFamilies(fam, eye_mm);
        double *mem = copyToLibrary(segs);
        if (mem == nullptr && !segs.empty()) {
            setError("gv_tangent_segments: не удалось выделить память");
            return GV_ERR_MEM;
        }
        *out_segments = mem;
        *out_count = segs.size();
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_tangent_segments: ") + safeWhat(exc));
        return GV_ERR;
    } catch (...) {
        setError("gv_tangent_segments: неизвестное исключение");
        return GV_ERR;
    }
}

int gv_prepare_path(const char *utf8_text, GvPathData *out)
{
    clearPathData(out);
    if (utf8_text == nullptr || out == nullptr) {
        setError("gv_prepare_path: пустой указатель на текст или на выход");
        return GV_ERR_ARG;
    }
    try {
        const gv::ParseResult res = gv::parseGcode(utf8_text);
        const gv::PathData pd = gv::prepareOrdered(res.ordered, res.stages);

        std::vector<double> stages(pd.stages.size() * STAGE_ROW, 0.0);
        for (std::size_t i = 0; i < pd.stages.size(); ++i) {
            double *row = &stages[i * STAGE_ROW];
            row[0] = pd.stages[i].start;
            row[1] = pd.stages[i].end;
            fillStageTool(pd.stages[i].info, row);
        }        std::vector<char> labels;
        std::vector<char> types;
        std::vector<std::size_t> labelAt;
        std::vector<std::size_t> typeAt;
        fillStageText(pd.stages, &labels, &labelAt, &types, &typeAt);

        std::vector<unsigned char> rapidBytes;
        rapidBytes.assign(pd.rapid.begin(), pd.rapid.end());

        double *pts = copyToLibrary(pd.pts);
        double *cum = copyToLibrary(pd.cum);
        unsigned char *rapid = copyToLibrary(rapidBytes);
        double *speeds = copyToLibrary(pd.speeds);
        unsigned char *rapidStreamMem = copyToLibrary(pd.rapid_stream.bytes);
        unsigned char *feedStreamMem = copyToLibrary(pd.feed_stream.bytes);
        double *stageRows = copyToLibrary(stages);
        char *labelText = copyToLibrary(labels);
        std::size_t *labelOffsets = copyToLibrary(labelAt);
        char *typeText = copyToLibrary(types);
        std::size_t *typeOffsets = copyToLibrary(typeAt);
        const bool memMissing =
            (pts == nullptr && !pd.pts.empty()) || (cum == nullptr && !pd.cum.empty())
            || (rapid == nullptr && !rapidBytes.empty())
            || (speeds == nullptr && !pd.speeds.empty())
            || (rapidStreamMem == nullptr && !pd.rapid_stream.bytes.empty())
            || (feedStreamMem == nullptr && !pd.feed_stream.bytes.empty())
            || (stageRows == nullptr && !stages.empty())
            || (labelText == nullptr && !labels.empty())
            || (labelOffsets == nullptr && !labelAt.empty())
            || (typeText == nullptr && !types.empty())
            || (typeOffsets == nullptr && !typeAt.empty());
        if (memMissing) {
            std::free(pts);
            std::free(cum);
            std::free(rapid);
            std::free(speeds);
            std::free(rapidStreamMem);
            std::free(feedStreamMem);
            std::free(stageRows);
            std::free(labelText);
            std::free(labelOffsets);
            std::free(typeText);
            std::free(typeOffsets);
            setError("gv_prepare_path: не удалось выделить память");
            return GV_ERR_MEM;
        }

        out->seg_count = pd.seg_count;
        for (int k = 0; k < 3; ++k) {
            out->last[k] = res.last[static_cast<std::size_t>(k)];
        }
        out->pts = pts;
        out->cum = cum;
        out->rapid = rapid;
        out->speeds = speeds;
        out->point_count = pd.pts.size() / 3;
        out->total = pd.total;
        out->rapid_stream = rapidStreamMem;
        out->rapid_bytes = pd.rapid_stream.bytes.size();
        out->rapid_nv = pd.rapid_stream.nv;
        out->feed_stream = feedStreamMem;
        out->feed_bytes = pd.feed_stream.bytes.size();
        out->feed_nv = pd.feed_stream.nv;
        out->has_bounds = pd.has_bounds ? 1 : 0;
        if (pd.has_bounds) {
            out->bounds[0] = pd.bounds.lo[0];
            out->bounds[1] = pd.bounds.lo[1];
            out->bounds[2] = pd.bounds.lo[2];
            out->bounds[3] = pd.bounds.hi[0];
            out->bounds[4] = pd.bounds.hi[1];
            out->bounds[5] = pd.bounds.hi[2];
        }
        for (int k = 0; k < 3; ++k) {
            out->center[k] = pd.center[static_cast<std::size_t>(k)];
        }
        out->radius = pd.radius;
        out->min_y = pd.min_y;
        out->stages = stageRows;
        out->stage_count = pd.stages.size();
        out->stage_labels = labelText;
        out->stage_label_at = labelOffsets;
        out->stage_types = typeText;
        out->stage_type_at = typeOffsets;
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_prepare_path: ") + safeWhat(exc));
        return GV_ERR;
    } catch (...) {
        setError("gv_prepare_path: неизвестное исключение");
        return GV_ERR;
    }
}

void gv_free_path_data(GvPathData *data)
{
    if (data == nullptr)
        return;
    std::free(data->pts);
    std::free(data->cum);
    std::free(data->rapid);
    std::free(data->speeds);
    std::free(data->rapid_stream);
    std::free(data->feed_stream);
    std::free(data->stages);
    std::free(data->stage_labels);
    std::free(data->stage_label_at);
    std::free(data->stage_types);
    std::free(data->stage_type_at);
    clearPathData(data);
}

} // extern "C"
