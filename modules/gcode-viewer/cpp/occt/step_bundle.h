// occt/step_bundle.h - всё, что питоновский load_step_model отдаёт про модель
// STEP, одним вызовом и без Qt: суп вершин, crease-aware нормали и рёбра до
// склейки.
//
// Нужен C-ABI-мосту gvbridge (cpp/bridge/gv_bridge.cpp): шим gv_cpp.py берёт
// этот набор и повторяет остальной путь Python (склейку рёбер и упаковку
// буфера), поэтому буфер обводки остаётся бит-в-бит таким же, как у эталона.
#pragma once

#include <string>
#include <vector>

namespace gv {

struct StepBundle {
    std::vector<double> verts;     // (F*3,3) float64, плоский ряд, как у эталона
    std::vector<float> normals;    // (F,3,3) float32; пусто, если супа нет
    std::vector<double> segments;  // (N,2,3) float64 - рёбра до склейки, мм
};

// Один проход STEP (readStepShape), тесселяция, разворот оболочки наружу,
// crease-aware нормали (creaseDeg = 20, как у эталона) и рёбра формы с линейным
// допуском рёбер 0.05 мм. Путь - в UTF-8: так его принимает OCCT. Ошибка чтения
// - std::runtime_error.
StepBundle loadStepBundle(const std::string &utf8Path, double deflection = 0.05);

} // namespace gv
