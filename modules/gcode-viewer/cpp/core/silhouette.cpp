#include "core/silhouette.h"

#include <algorithm>
#include <cmath>
#include <numeric>

// Порядок операций здесь - часть контракта: результат сверяется с питоновским
// эталоном (silhouette.py) бит-в-бит, а не «примерно». Компилируется с
// /fp:precise (MSVC) и -ffp-contract=off (GCC/Clang): переассоциация и FMA
// сдвигают младшие биты float32-результата.

namespace gv {
namespace {

constexpr double MAX_EDGE_DEG = 20.0;    // круче - острая кромка (её слой - статические рёбра)
constexpr double MIN_FACE_AREA = 1e-6;   // мм^2: ниже - вырожденная грань, нормаль - мусор
constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;

// Ключ сварки вершины: позиция, округлённая до сетки weld (np.round(V / weld)).
struct WeldKey {
    double v[3];
};

// Лексикографический порядок ключей, как у np.unique(axis=0): поля слева
// направо. Равенство поэлементное (==), поэтому -0.0 и 0.0 - один узел.
inline bool keyLess(const WeldKey& a, const WeldKey& b) {
    for (int i = 0; i < 3; ++i) {
        if (a.v[i] < b.v[i]) { return true; }
        if (a.v[i] > b.v[i]) { return false; }
    }
    return false;
}

inline bool keyEqual(const WeldKey& a, const WeldKey& b) {
    return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2];
}

// Ребро треугольника: канонически упорядоченные концы (a < b) и номер грани.
struct EdgeRec {
    std::uint32_t a;
    std::uint32_t b;
    std::uint32_t face;
};

// Кандидатное силуэтное ребро: концы и две смежные грани (f0 < f1).
struct Candidate {
    std::uint32_t a;
    std::uint32_t b;
    std::int32_t f0;
    std::int32_t f1;
};

// np.clip(x, -1.0, 1.0) без ветки на NaN: NaN проходит насквозь, как у numpy
// (std::min/std::max вернули бы из NaN единицу).
inline double clipUnit(double x) {
    if (x < -1.0) { return -1.0; }
    if (x > 1.0) { return 1.0; }
    return x;
}

} // namespace

MeshTopology buildSilhouetteTopology(const std::vector<double>& soup, double weldMm) {
    MeshTopology topo;
    const std::size_t faces = soup.size() / 9;
    topo.faces = faces;
    if (faces == 0) {
        return topo;
    }

    // Сварка вершин: одна позиция на сетке weld - один индекс. Без неё соседние
    // грани одной кромки B-rep дают разные индексы и карта смежности рёбер не
    // собирается. Ключ - позиция в единицах weld; равенство ключей и есть сварка.
    const std::size_t slots = faces * 3;
    std::vector<WeldKey> key(slots);
    for (std::size_t i = 0; i < slots; ++i) {
        for (int d = 0; d < 3; ++d) {
            key[i].v[d] = std::nearbyint(soup[i * 3 + std::size_t(d)] / weldMm);
        }
    }
    std::vector<std::uint32_t> order(slots);
    std::iota(order.begin(), order.end(), std::uint32_t(0));
    std::sort(order.begin(), order.end(), [&key](std::uint32_t x, std::uint32_t y) {
        return keyLess(key[x], key[y]);
    });

    // Представитель кластера - первый ключ группы в отсортированном порядке (у
    // numpy - тоже первый: mask по aux[1:] != aux[:-1]). Равные ключи на супе
    // STEP побитово одинаковы (нулевые ключи пришли все с -0.0), поэтому
    // неустойчивость сортировки numpy на ea/eb не влияет.
    std::vector<std::uint32_t> inv(slots);
    std::vector<WeldKey> uniq;
    uniq.reserve(slots / 6 + 1);
    for (std::size_t i = 0; i < slots; ++i) {
        const WeldKey& k = key[order[i]];
        if (uniq.empty() || !keyEqual(uniq.back(), k)) {
            uniq.push_back(k);
        }
        inv[order[i]] = static_cast<std::uint32_t>(uniq.size() - 1);
    }

    // Позиция узла - представитель кластера: расхождение с исходной точкой
    // <= weld/2 (5e-5 мм), зато детерминированно.
    std::vector<double> pos(uniq.size() * 3);
    for (std::size_t g = 0; g < uniq.size(); ++g) {
        for (int d = 0; d < 3; ++d) {
            pos[g * 3 + std::size_t(d)] = uniq[g].v[d] * weldMm;
        }
    }

    // Три ребра каждой грани (a < b) в порядке эталона: грань за гранью, внутри
    // грани - (0,1), (1,2), (2,0).
    std::vector<EdgeRec> edge(slots);
    for (std::size_t f = 0; f < faces; ++f) {
        for (std::uint32_t j = 0; j < 3; ++j) {
            const std::uint32_t v0 = inv[f * 3 + j];
            const std::uint32_t v1 = inv[f * 3 + (j + 1) % 3];
            EdgeRec& rec = edge[f * 3 + j];
            rec.a = std::min(v0, v1);
            rec.b = std::max(v0, v1);
            rec.face = static_cast<std::uint32_t>(f);
        }
    }
    // Ключ ребра - (a,b), как у numpy; третий ключ - грань: в эталоне группа
    // собрана устойчивой сортировкой, поэтому внутри группы грани идут по
    // возрастанию, и f0 - меньшая из них.
    std::sort(edge.begin(), edge.end(), [](const EdgeRec& x, const EdgeRec& y) {
        if (x.a != y.a) { return x.a < y.a; }
        if (x.b != y.b) { return x.b < y.b; }
        return x.face < y.face;
    });

    // Кандидаты - рёбра ровно с двумя смежными гранями: силуэт живёт на стыке
    // двух граней. Рёбра с другой кратностью (на супе STEP такие есть: 1, 4, 6)
    // отсеяны, как и в эталоне.
    std::vector<Candidate> cand;
    for (std::size_t i = 0; i < edge.size();) {
        std::size_t j = i + 1;
        while (j < edge.size() && edge[j].a == edge[i].a && edge[j].b == edge[i].b) {
            ++j;
        }
        if (j - i == 2) {
            cand.push_back(Candidate{edge[i].a, edge[i].b,
                                     static_cast<std::int32_t>(edge[i].face),
                                     static_cast<std::int32_t>(edge[i + 1].face)});
        }
        i = j;
    }

    // Нормали - геометрические, по самим треугольникам: горизонт силуэта задаёт
    // переворот нормали грани, сглаживание его размывало бы. Порядок слагаемых
    // тот же, что у numpy (cross, затем корень из суммы квадратов слева направо).
    std::vector<double> nrm(faces * 3, 0.0);
    std::vector<double> area(faces, 0.0);
    for (std::size_t f = 0; f < faces; ++f) {
        const double* t = &soup[f * 9];
        const double d0[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
        const double d1[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
        const double cr[3] = {d0[1] * d1[2] - d0[2] * d1[1],
                              d0[2] * d1[0] - d0[0] * d1[2],
                              d0[0] * d1[1] - d0[1] * d1[0]};
        const double len = std::sqrt(((cr[0] * cr[0] + cr[1] * cr[1]) + cr[2] * cr[2]));
        area[f] = 0.5 * len;
        const double div = (len == 0.0) ? 1.0 : len;   // вырожденная грань: нормаль как есть
        for (int d = 0; d < 3; ++d) {
            nrm[f * 3 + std::size_t(d)] = cr[d] / div;
        }
    }

    // Фильтр кандидатов: только гладкие горизонты (без острых кромок - их рисует
    // слой статических рёбер) и без вырожденных граней (их нормаль - численный
    // мусор, знак скачет и родит короткие ложные усы).
    const double angle_lim = MAX_EDGE_DEG * DEG_TO_RAD;
    topo.ea.reserve(cand.size() * 3);
    topo.eb.reserve(cand.size() * 3);
    topo.f0.reserve(cand.size());
    topo.f1.reserve(cand.size());
    for (const Candidate& c : cand) {
        const std::size_t f0 = std::size_t(c.f0);
        const std::size_t f1 = std::size_t(c.f1);
        if (!(area[f0] > MIN_FACE_AREA) || !(area[f1] > MIN_FACE_AREA)) {
            continue;
        }
        const double* n0 = &nrm[f0 * 3];
        const double* n1 = &nrm[f1 * 3];
        // Скалярное произведение нормалей слева направо: np.einsum для трёх
        // слагаемых даёт местами другой младший бит, но решение о 20 градусах от
        // этого не зависит - ближайший к порогу угол отстоит на 0.054 рад.
        const double dot = ((n0[0] * n1[0] + n0[1] * n1[1]) + n0[2] * n1[2]);
        if (!(std::acos(clipUnit(dot)) < angle_lim)) {
            continue;
        }
        topo.f0.push_back(c.f0);
        topo.f1.push_back(c.f1);
        for (int d = 0; d < 3; ++d) {
            topo.ea.push_back(static_cast<float>(pos[std::size_t(c.a) * 3 + std::size_t(d)]));
            topo.eb.push_back(static_cast<float>(pos[std::size_t(c.b) * 3 + std::size_t(d)]));
        }
    }

    // n и d: d = n·cen в float32, cen - центроид грани (среднее трёх вершин,
    // double, затем float32). Единый порядок слагаемых с numpy: (n0*c0 + n1*c1)
    // + n2*c2, сложение слева направо.
    topo.n.resize(faces * 3);
    topo.d.resize(faces);
    for (std::size_t f = 0; f < faces; ++f) {
        const double* t = &soup[f * 9];
        const float cen[3] = {static_cast<float>(((t[0] + t[3]) + t[6]) / 3.0),
                              static_cast<float>(((t[1] + t[4]) + t[7]) / 3.0),
                              static_cast<float>(((t[2] + t[5]) + t[8]) / 3.0)};
        const float nv[3] = {static_cast<float>(nrm[f * 3 + 0]),
                             static_cast<float>(nrm[f * 3 + 1]),
                             static_cast<float>(nrm[f * 3 + 2])};
        for (int d = 0; d < 3; ++d) {
            topo.n[f * 3 + std::size_t(d)] = nv[d];
        }
        topo.d[f] = ((nv[0] * cen[0] + nv[1] * cen[1]) + nv[2] * cen[2]);
    }
    return topo;
}

std::vector<float> meshSilhouetteSegments(const MeshTopology& topo, const double eyeMm[3]) {
    std::vector<float> out;
    const std::size_t faces = topo.n.size() / 3;
    if (faces == 0 || topo.edgeCount() == 0) {
        return out;
    }

    // Глаз - float32, как в эталоне (np.asarray(E, dtype=float32)).
    const float ex = static_cast<float>(eyeMm[0]);
    const float ey = static_cast<float>(eyeMm[1]);
    const float ez = static_cast<float>(eyeMm[2]);

    // Знак s = n·E - d по граням. Грань с s == 0 (глаз в её плоскости, включая
    // -0.0) получает 0 и силуэтной не считается - как произведение < 0 у эталона.
    std::vector<std::int8_t> sign(faces, 0);
    for (std::size_t f = 0; f < faces; ++f) {
        const float* nf = &topo.n[f * 3];
        const float s = ((nf[0] * ex + nf[1] * ey) + nf[2] * ez) - topo.d[f];
        sign[f] = static_cast<std::int8_t>(s < 0.0f ? -1 : (s > 0.0f ? 1 : 0));
    }

    const std::size_t edges = topo.edgeCount();
    for (std::size_t k = 0; k < edges; ++k) {
        const std::int32_t f0 = topo.f0[k];
        const std::int32_t f1 = topo.f1[k];
        if (sign[std::size_t(f0)] * sign[std::size_t(f1)] >= 0) {
            continue;
        }
        for (int d = 0; d < 3; ++d) {
            out.push_back(topo.ea[k * 3 + std::size_t(d)]);
        }
        for (int d = 0; d < 3; ++d) {
            out.push_back(topo.eb[k * 3 + std::size_t(d)]);
        }
    }
    return out;
}

} // namespace gv
