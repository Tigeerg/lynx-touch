// occt/step_bundle.h - всё, что питоновский load_step_model отдаёт про модель
// STEP, одним вызовом и без Qt: суп вершин, crease-aware нормали и рёбра до
// склейки.
//
// Нужен C-ABI-мосту gvbridge (cpp/bridge/gv_bridge.cpp): шим берёт
// этот набор и повторяет остальной путь Python (склейку рёбер и упаковку
// буфера), поэтому буфер обводки остаётся бит-в-бит таким же, как у эталона.
#pragma once

#include <string>
#include <vector>

#include "core/tangents.h"     // gv::StepSurfaceList - аналитические поверхности

namespace gv {

struct StepBundle {
    std::vector<double> verts;     // (F*3,3) float64, плоский ряд, как у эталона
    std::vector<float> normals;    // (F,3,3) float32; пусто, если супа нет
    std::vector<double> segments;  // (N,2,3) float64 - рёбра до склейки, мм
};

// Один проход STEP (readStepShape), тесселяция, разворот оболочки наружу,
// crease-aware нормали (creaseDeg = 20, как у эталона) и рёбра формы с линейным
// допуском рёбер edgeDeflection (0.05 мм, как у эталона). Путь - в UTF-8: так его
// принимает OCCT. Ошибка чтения - std::runtime_error.
StepBundle loadStepBundle(const std::string &utf8Path, double deflection = 0.05,
                          double edgeDeflection = 0.05, double creaseDeg = 20.0);

// Модель и аналитические поверхности вращения одного файла за одно чтение:
// проход STEPControl_Reader стоит около 0.8 с на модель, и второй проход ради
// поверхностей удваивал бы его (эталон платит именно так - см. _step_model в
// qmlbackend/gv_toolpath.cpp). Порядок работ тот же, что у питоновской пары
// load_step_model + load_step_surfaces на общей форме: тесселяция кожи, затем
// обход граней.
struct StepBundleAll {
    StepBundle bundle;
    StepSurfaceList surfaces;
};

StepBundleAll loadStepBundleAll(const std::string &utf8Path, double deflection = 0.05,
                                double edgeDeflection = 0.05, double creaseDeg = 20.0);

// Только аналитические поверхности файла (чтение STEP внутри). Нужны там, где
// STEP-ветка осталась на Python, а поверхности уже считает C++.
StepSurfaceList loadStepSurfacesBundle(const std::string &utf8Path,
                                       double deflection = 0.05);

} // namespace gv
