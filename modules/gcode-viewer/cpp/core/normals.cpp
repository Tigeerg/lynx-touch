#include "core/normals.h"

#include "core/np_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Порядок операций здесь - часть контракта: результат сверяется с питоновским
// эталоном (normals.py) бит-в-бит, а не «примерно». Компилируется с /fp:precise
// (MSVC) и -ffp-contract=off (GCC/Clang): переассоциация и FMA сдвигают младшие
// биты float32-результата.

namespace gv {
namespace {

constexpr double WELD_MM = 1000.0;      // сварка вершин: шаг сетки 1/1000 мм
constexpr double AREA_EPS = 1e-12;      // ниже - грань вырождена, нормаль (0,0,1)
constexpr double LEN_EPS = 1e-9;        // ниже - среднее не нормируется
constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;

// Ключ сварки: узел, округлённый до 0.001 мм (np.round(T * 1000.0)).
struct WeldKey {
    std::int64_t v[3] = {0, 0, 0};

    bool operator==(const WeldKey& o) const {
        return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2];
    }
};

struct WeldKeyHash {
    std::size_t operator()(const WeldKey& k) const {
        std::size_t h = 1469598103934665603ull;
        for (int i = 0; i < 3; ++i) {
            h = (h ^ static_cast<std::size_t>(k.v[i])) * 1099511628211ull;
        }
        return h;
    }
};

// numpy.round: пол-к-чётному (std::nearbyint в режиме по умолчанию) - тот же
// приём, что у ключей прямых рёбер в stepledgesgeometry.cpp. Тело живёт в
// core/np_helpers.h, чтобы не копироваться в outline.cpp и silhouette.cpp.
using gv::np::roundToInt;

using Vec3 = std::array<double, 3>;

} // namespace

std::vector<float> smoothFaceNormals(const std::vector<double>& soup, double creaseDeg) {
    const std::size_t faces = soup.size() / 9;
    std::vector<float> out(soup.size(), 0.0f);          // (3F, 3) float32, слот = face*3+corner
    if (faces == 0) {
        return out;
    }

    // Нормаль и площадь грани: cross и корень из суммы квадратов тем же
    // порядком слагаемых, что numpy (сложение слева направо).
    std::vector<Vec3> face_n(faces, Vec3{0.0, 0.0, 1.0});
    std::vector<double> area(faces, 0.0);
    for (std::size_t f = 0; f < faces; ++f) {
        const double* t = &soup[f * 9];
        const double d0[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
        const double d1[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
        const double fn[3] = {d0[1] * d1[2] - d0[2] * d1[1],
                              d0[2] * d1[0] - d0[0] * d1[2],
                              d0[0] * d1[1] - d0[1] * d1[0]};
        const double a = std::sqrt(((fn[0] * fn[0] + fn[1] * fn[1]) + fn[2] * fn[2]));
        area[f] = a;
        if (a > AREA_EPS) {
            face_n[f] = Vec3{fn[0] / a, fn[1] / a, fn[2] / a};
        }
    }

    // Сварка вершин: равенство узлов после округления до 0.001 мм. Порядок
    // групп в таблице не важен: итог группы пишется по своим слотам, а внутри
    // группы слоты идут по возрастанию - как у устойчивой сортировки в эталоне.
    std::unordered_map<WeldKey, std::vector<int>, WeldKeyHash> welded;
    welded.reserve(faces * 2);
    for (std::size_t f = 0; f < faces; ++f) {
        for (int c = 0; c < 3; ++c) {
            const double* p = &soup[f * 9 + c * 3];
            WeldKey key;
            key.v[0] = roundToInt(p[0] * WELD_MM);
            key.v[1] = roundToInt(p[1] * WELD_MM);
            key.v[2] = roundToInt(p[2] * WELD_MM);
            welded[key].push_back(static_cast<int>(f) * 3 + c);
        }
    }

    const double cos_lim = std::cos(creaseDeg * DEG_TO_RAD);
    std::vector<Vec3> n2;                               // нормали группы в порядке обхода
    std::vector<double> w2;                             // их площади
    std::vector<int> perm;
    std::vector<double> neg_w;
    std::vector<char> free_slot;
    std::vector<char> sel;
    std::vector<Vec3> res;

    for (const auto& item : welded) {
        const std::vector<int>& slots = item.second;
        const std::size_t m = slots.size();

        // Сид кластера - грань наибольшей площади: perm = argsort(-gw), устойчивый.
        perm.resize(m);
        neg_w.resize(m);
        for (std::size_t k = 0; k < m; ++k) {
            neg_w[k] = -area[std::size_t(slots[k]) / 3];
            perm[k] = static_cast<int>(k);
        }
        std::stable_sort(perm.begin(), perm.end(), [&neg_w](int x, int y) {
            return neg_w[std::size_t(x)] < neg_w[std::size_t(y)];
        });

        n2.resize(m);
        w2.resize(m);
        for (std::size_t j = 0; j < m; ++j) {
            const std::size_t f = std::size_t(slots[std::size_t(perm[j])]) / 3;
            n2[j] = face_n[f];
            w2[j] = area[f];
        }

        free_slot.assign(m, 1);
        sel.assign(m, 0);
        res.assign(m, Vec3{0.0, 0.0, 0.0});

        for (std::size_t i = 0; i < m; ++i) {
            if (!free_slot[i]) {
                continue;                    // слот уже в чужом кластере
            }
            bool any_sel = false;
            for (std::size_t k = 0; k < m; ++k) {
                const double dot = ((n2[k][0] * n2[i][0] + n2[k][1] * n2[i][1]) + n2[k][2] * n2[i][2]);
                const bool hit = free_slot[k] != 0 && dot >= cos_lim;
                sel[k] = hit ? 1 : 0;
                any_sel = any_sel || hit;
            }
            if (!any_sel) {
                continue;
            }
            // Среднее по площади: суммирование по слотам группы по возрастанию
            // номера слота, несмежные дают ровно 0.0 (x + 0.0 точно) - как
            // np.where(sel, gnn * gw, 0.0).sum(axis=1) в эталоне.
            double avg[3] = {0.0, 0.0, 0.0};
            for (std::size_t k = 0; k < m; ++k) {
                for (int d = 0; d < 3; ++d) {
                    avg[d] += sel[k] ? n2[k][std::size_t(d)] * w2[k] : 0.0;
                }
            }
            const double ln = std::sqrt(((avg[0] * avg[0] + avg[1] * avg[1]) + avg[2] * avg[2]));
            const bool good = ln > LEN_EPS;      // вырожденное среднее остаётся как есть
            const Vec3 vals{good ? avg[0] / ln : n2[i][0],
                            good ? avg[1] / ln : n2[i][1],
                            good ? avg[2] / ln : n2[i][2]};
            for (std::size_t k = 0; k < m; ++k) {
                if (sel[k]) {
                    res[k] = vals;
                    free_slot[k] = 0;
                }
            }
        }

        for (std::size_t j = 0; j < m; ++j) {
            const std::size_t slot = std::size_t(slots[std::size_t(perm[j])]);
            out[slot * 3 + 0] = static_cast<float>(res[j][0]);
            out[slot * 3 + 1] = static_cast<float>(res[j][1]);
            out[slot * 3 + 2] = static_cast<float>(res[j][2]);
        }
    }
    return out;
}

} // namespace gv
