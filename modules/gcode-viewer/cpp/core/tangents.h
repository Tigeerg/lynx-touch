// core/tangents.h - аналитические касательные контура: семейства цилиндров и
// конусов (зеркало modules/gcode-viewer/tangents.py).
//
// Слой чистый: ни Qt, ни OCCT - только арифметика над дескрипторами
// поверхностей, упакованными рёбрами STEP и супом кожи. Поэтому он живёт в
// gvcore, а дескрипторы поверхностей даёт OCCT-слой gvstep
// (cpp/occt/step_surfaces.*): разделение то же, что у силуэта и STEP-тесселяции.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/packing.h"     // gv::PackedEdges - упакованные рёбра STEP

namespace gv {

// Аналитическая цилиндрическая грань STEP (CylinderSurface в gv_types.py):
// o - точка на оси, d - единичное направление оси, e1/e2 - базис поперечного
// сечения, arc - дуга грани в радианах (has_arc = false у полного круга),
// r - радиус, t0/t1 - пределы вдоль оси по вершинам грани, мм.
struct CylinderSurface {
    double o[3] = {0.0, 0.0, 0.0};
    double d[3] = {0.0, 0.0, 1.0};
    double e1[3] = {1.0, 0.0, 0.0};
    double e2[3] = {0.0, 0.0, 1.0};
    bool has_arc = false;
    double arc0 = 0.0;
    double arc1 = 0.0;
    double r = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
};

// Аналитическая коническая грань STEP (ConeSurface в gv_types.py): a - вершина,
// d - единичное направление оси, psi - полуугол (рад), s0/s1 - пределы
// параметра образующей (длина вдоль оси / cos psi), мм.
struct ConeSurface {
    double a[3] = {0.0, 0.0, 0.0};
    double d[3] = {0.0, 0.0, 1.0};
    double psi = 0.0;
    double s0 = 0.0;
    double s1 = 0.0;
};

// Один элемент списка поверхностей. В эталоне это список объектов двух типов, и
// его порядок (обход граней TopExp_Explorer) виден в порядке семейств, поэтому
// список хранит вид поверхности, а не два раздельных массива.
struct StepSurface {
    bool is_cylinder = true;
    CylinderSurface cyl;
    ConeSurface cone;
};

using StepSurfaceList = std::vector<StepSurface>;

// Порог длины образующей конуса и число азимутов пробы вогнутости: те же
// _CONE_MIN_SLANT_MM = 2.5 мм и _CONE_PROBE_AZIMUTHS = 8, что в эталоне.
constexpr double CONE_MIN_SLANT_MM = 2.5;
constexpr int CONE_PROBE_AZIMUTHS = 8;

// Суп кожи для политики конусов: треугольники (F,3,3), их центроиды (F,3) и
// нормаль первого угла грани (F,3) - всё float64, как в эталоне после asarray.
// `valid` = false повторяет skin_tris = None (кожа не передана или не делится
// на тройки вершин).
struct SkinTriangles {
    bool valid = false;
    std::size_t count = 0;
    std::vector<double> sktri;   // (F,3,3)
    std::vector<double> skcen;   // (F,3)
    std::vector<double> skirm;   // (F,3)
};

// Предвычисленные цилиндрические семейства (CylinderFamilies в gv_types.py).
// Плоские массивы: O/D/E1/E2 - (N,3), R/T0/T1/RREF/A0/A1 - (N,), FULL - (N,),
// DROP - (N,8) с NaN в незанятых ячейках, ARC - (N,64).
struct CylinderFamilies {
    std::vector<double> O;
    std::vector<double> D;
    std::vector<double> E1;
    std::vector<double> E2;
    std::vector<double> R;
    std::vector<double> T0;
    std::vector<double> T1;
    std::vector<double> RREF;
    std::vector<double> A0;
    std::vector<double> A1;
    std::vector<std::uint8_t> FULL;
    std::vector<double> DROP;
    std::vector<std::uint8_t> ARC;
    std::size_t count = 0;
};

// Семейства касательных STEP (TangentFamilies в gv_types.py).
struct TangentFamilies {
    CylinderFamilies cylinders;
    std::vector<ConeSurface> cones;
};

// Суп кожи -> SkinTriangles: зеркало куска _tangent_families, который готовит
// sktri/skcen/skirm из skin=(verts, normals). Центроид - среднее трёх вершин,
// нормаль - первый угол грани; порядок слагаемых как у numpy.
SkinTriangles buildSkinTriangles(const std::vector<double>& soup,
                                 const std::vector<float>& normals);

// Предвычисление семейств: зеркало _tangent_families. surfs - в порядке граней
// STEP; packedEdges (STEP-обводка, если известна) помечает азимуты генераторов,
// совпадающие со стыковыми рёбрами B-rep; skin - политика конусов. nullptr
// означает, что данных нет (в эталоне None).
TangentFamilies buildTangentFamilies(const StepSurfaceList& surfs,
                                     const PackedEdges* packedEdges = nullptr,
                                     const SkinTriangles* skin = nullptr);

// Кадровый проход: обе касательные стороны каждого логического цилиндра и
// образующие конусов на глазу eyeMm (raw-кадр модели). Результат - плоский ряд
// (M,2,3) float64: две точки на отрезок, порядок тот же, что у эталона.
std::vector<double> tangentFromFamilies(const TangentFamilies& fam, const double eyeMm[3]);

} // namespace gv
