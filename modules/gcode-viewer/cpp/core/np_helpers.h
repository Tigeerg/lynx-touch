// core/np_helpers.h - общие помощники «как в numpy» для чистых слоёв ядра:
// скалярное и векторное произведение, остаток по знаку делителя, норма вектора
// и округление до целого. Реализации живут в одном месте, чтобы трижды
// повторённый код (normals, outline, silhouette, tangents, step_surfaces) не
// расходился при правках.
//
// Норма оставлена двумя функциями, а не одной: у эталона np.linalg.norm в одном
// месте это sqrt, в другом - pow(sum, 0.5), и на значениях около единицы они
// расходятся в младшем бите (видно в нормализованном направлении оси). Место
// вызова решает, какая нужна, - обе реализации здесь, чтобы не копировать тело.
#pragma once

#include <cmath>
#include <cstdint>

namespace gv {
namespace np {

// Скалярное произведение: у numpy это a @ b (сумма слева направо).
inline double dot3(const double* a, const double* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Векторное произведение, как np.cross для 3-векторов.
inline void cross3(const double* a, const double* b, double* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// Остаток как numpy %: знак результата совпадает со знаком делителя, а fmod
// берёт знак делимого. Разница видна там, где вход бывает отрицательным.
inline double npMod(double x, double y) {
    const double r = std::fmod(x, y);
    if (r != 0.0 && ((r < 0.0) != (y < 0.0))) {
        return r + y;
    }
    return r;
}

// Корень из суммы трёх квадратов (np.linalg.norm): сложение слева направо.
inline double norm3Sqrt(const double* v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// Норма как (сумма квадратов) ** 0.5: у эталона это pow, и на значениях около
// единицы он расходится с sqrt в младшем бите.
inline double norm3Pow(const double* v) {
    return std::pow(v[0] * v[0] + v[1] * v[1] + v[2] * v[2], 0.5);
}

// numpy.round до целого: пол-к-чётному (std::nearbyint в режиме по умолчанию).
inline std::int64_t roundToInt(double v) {
    return static_cast<std::int64_t>(std::nearbyint(v));
}

} // namespace np
} // namespace gv
