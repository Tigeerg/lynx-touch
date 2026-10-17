// core/outline.cpp - обводка меша по двугранному углу (зеркало outline.py,
// функция _feature_edges_np).
//
// Порядок операций - часть контракта: результат сверяется с питоновским
// эталоном побайтово (test_stl_core.py, оракул stl.feature_edges), а он зависит
// от нумерации сваренных вершин, устойчивости сортировки рёбер и порядка
// цепочек. Компилируется с /fp:precise (MSVC) и -ffp-contract=off (GCC/Clang):
// переассоциация и FMA сдвинули бы младшие биты нормалей и расстояний.
#include "core/outline.h"

#include "core/np_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gv {
namespace {

using Vec3 = std::array<double, 3>;

constexpr double WELD_MM = 1000.0;                  // сварка вершин: сетка 1/1000 мм
constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
constexpr double RDP_DEGENERATE = 1e-12;            // короче - отрезок вырожден: мерим расстояние до точки
constexpr std::uint32_t NO_VERTEX = 0xffffffffu;

// numpy.round: пол-к-чётному (std::nearbyint в режиме по умолчанию). Тело -
// общее в core/np_helpers.h.
using gv::np::roundToInt;

// Корень из суммы трёх квадратов: тем же порядком слагаемых, что numpy
// (сложение слева направо, длина вектора мала - парного суммирования нет).
// Тело - общее в core/np_helpers.h.
inline double norm3(const double v[3]) {
    return gv::np::norm3Sqrt(v);
}

// Индексы точек, остающихся после упрощения полилинии (Дуглас-Пекер).
// Итеративный вариант: цепочки рёбер меша насчитывают тысячи точек.
std::vector<std::size_t> rdpIndices(const std::vector<Vec3>& points, double tol) {
    const std::size_t n = points.size();
    if (n < 2) {
        return {0};
    }
    if (n == 2) {
        return {0, 1};
    }
    std::vector<unsigned char> keep(n, 0);
    keep[0] = 1;
    keep[n - 1] = 1;
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    stack.emplace_back(0, n - 1);
    while (!stack.empty()) {
        const std::size_t i = stack.back().first;
        const std::size_t j = stack.back().second;
        stack.pop_back();
        if (j <= i + 1) {
            continue;
        }
        const Vec3& a = points[i];
        const double ab[3] = {points[j][0] - a[0], points[j][1] - a[1], points[j][2] - a[2]};
        const double ln = norm3(ab);
        std::size_t best = i + 1;
        double best_d = -1.0;
        for (std::size_t t = i + 1; t < j; ++t) {
            const double seg[3] = {points[t][0] - a[0], points[t][1] - a[1], points[t][2] - a[2]};
            double d = 0.0;
            if (ln < RDP_DEGENERATE) {
                d = norm3(seg);
            } else {
                const double cr[3] = {seg[1] * ab[2] - seg[2] * ab[1],
                                      seg[2] * ab[0] - seg[0] * ab[2],
                                      seg[0] * ab[1] - seg[1] * ab[0]};
                d = norm3(cr) / ln;
            }
            if (d > best_d) {                       // np.argmax: первое наибольшее
                best_d = d;
                best = t;
            }
        }
        if (best_d > tol) {
            keep[best] = 1;
            stack.emplace_back(i, best);
            stack.emplace_back(best, j);
        }
    }
    std::vector<std::size_t> idx;
    for (std::size_t t = 0; t < n; ++t) {
        if (keep[t]) {
            idx.push_back(t);
        }
    }
    return idx;
}

// Склейка цепочек смежных рёбер и их упрощение (зеркало _simplify_edge_chains).
// Цепочка собирается обходом смежности по свареным вершинам: развилки и концы -
// начала цепочек, оставшиеся рёбра - замкнутые петли. Возвращает концы
// упрощённых звеньев как номера вершин.
void simplifyEdgeChains(const std::vector<std::uint32_t>& lo_ids,
                        const std::vector<std::uint32_t>& hi_ids,
                        const std::vector<Vec3>& pos, double tol,
                        std::vector<std::uint32_t>& out_lo,
                        std::vector<std::uint32_t>& out_hi) {
    const std::int64_t pair_mult = static_cast<std::int64_t>(pos.size()) + 1;
    const auto pairKey = [pair_mult](std::uint32_t v, std::uint32_t w) {
        const std::uint32_t lo = v < w ? v : w;
        const std::uint32_t hi = v < w ? w : v;
        return static_cast<std::uint64_t>(lo) * static_cast<std::uint64_t>(pair_mult)
            + static_cast<std::uint64_t>(hi);
    };

    // Смежность: у вершины ровно список соседей в порядке появления рёбер.
    // Порядок ключей важен - обход цепочек идёт по нему, как по dict в эталоне.
    std::vector<std::vector<std::uint32_t>> adj(pos.size());
    std::vector<unsigned char> seen(pos.size(), 0);
    std::vector<std::uint32_t> first_seen;
    first_seen.reserve(lo_ids.size() * 2);
    for (std::size_t i = 0; i < lo_ids.size(); ++i) {
        const std::uint32_t a = lo_ids[i];
        const std::uint32_t b = hi_ids[i];
        adj[a].push_back(b);
        adj[b].push_back(a);
        if (!seen[a]) {
            seen[a] = 1;
            first_seen.push_back(a);
        }
        if (!seen[b]) {
            seen[b] = 1;
            first_seen.push_back(b);
        }
    }

    std::unordered_set<std::uint64_t> used;
    used.reserve(lo_ids.size() * 2);
    const auto step = [&used, &adj, &pairKey](std::vector<std::uint32_t>& chain,
                                             std::uint32_t first, std::uint32_t second) {
        std::uint32_t prev = first;
        std::uint32_t cur = second;
        while (true) {
            used.insert(pairKey(prev, cur));
            chain.push_back(cur);
            if (cur == first || adj[cur].size() != 2) {
                break;
            }
            std::uint32_t nxt = NO_VERTEX;
            for (const std::uint32_t x : adj[cur]) {
                if (x != prev && used.find(pairKey(cur, x)) == used.end()) {
                    nxt = x;
                    break;
                }
            }
            if (nxt == NO_VERTEX) {
                break;
            }
            prev = cur;
            cur = nxt;
        }
    };

    std::vector<std::vector<std::uint32_t>> chains;
    for (const std::uint32_t v : first_seen) {
        if (adj[v].size() == 2) {
            continue;
        }
        for (const std::uint32_t w : adj[v]) {
            if (used.find(pairKey(v, w)) != used.end()) {
                continue;
            }
            chains.emplace_back();
            chains.back().push_back(v);
            step(chains.back(), v, w);
        }
    }
    for (std::size_t i = 0; i < lo_ids.size(); ++i) {    // замкнутые петли
        const std::uint32_t a = lo_ids[i];
        const std::uint32_t b = hi_ids[i];
        if (used.find(pairKey(a, b)) != used.end() || adj[a].size() != 2) {
            continue;
        }
        chains.emplace_back();
        chains.back().push_back(a);
        step(chains.back(), a, b);
    }

    std::vector<Vec3> pts;
    for (const std::vector<std::uint32_t>& chain : chains) {
        pts.resize(chain.size());
        for (std::size_t t = 0; t < chain.size(); ++t) {
            pts[t] = pos[chain[t]];
        }
        const std::vector<std::size_t> idx = rdpIndices(pts, tol);
        for (std::size_t k = 0; k + 1 < idx.size(); ++k) {
            const std::size_t i = idx[k];
            const std::size_t j = idx[k + 1];
            if (j > i) {
                out_lo.push_back(chain[i]);
                out_hi.push_back(chain[j]);
            }
        }
    }
}

} // namespace

PackedEdges featureEdgesNp(const std::vector<double>& soup, double angle_deg,
                           double simplifyMm) {
    const std::size_t nverts = soup.size() / 3;
    const std::size_t faces = nverts / 3;
    if (faces == 0) {
        return PackedEdges{};
    }

    // Сварка вершин: узел, округлённый до 0.001 мм. Нумерация - по порядку
    // отсортированных строк (np.unique(q, axis=0)), поэтому она устойчива к
    // порядку входа и повторяет эталон.
    std::vector<std::int64_t> q(nverts * 3);
    for (std::size_t i = 0; i < q.size(); ++i) {
        q[i] = roundToInt(soup[i] * WELD_MM);
    }
    std::vector<std::uint32_t> order(nverts);
    std::iota(order.begin(), order.end(), 0u);
    const std::int64_t* qd = q.data();
    std::sort(order.begin(), order.end(), [qd](std::uint32_t a, std::uint32_t b) {
        const std::int64_t* pa = qd + static_cast<std::size_t>(a) * 3;
        const std::int64_t* pb = qd + static_cast<std::size_t>(b) * 3;
        if (pa[0] != pb[0]) {
            return pa[0] < pb[0];
        }
        if (pa[1] != pb[1]) {
            return pa[1] < pb[1];
        }
        return pa[2] < pb[2];
    });
    std::vector<std::uint32_t> vid(nverts, 0);
    std::vector<Vec3> u;
    u.reserve(nverts);
    for (std::size_t k = 0; k < nverts; ++k) {
        const std::size_t row = order[k];
        const std::int64_t* r = qd + row * 3;
        bool same = false;
        if (k > 0) {
            const std::int64_t* prev = qd + static_cast<std::size_t>(order[k - 1]) * 3;
            same = prev[0] == r[0] && prev[1] == r[1] && prev[2] == r[2];
        }
        if (!same) {
            u.push_back(Vec3{static_cast<double>(r[0]) / WELD_MM,
                             static_cast<double>(r[1]) / WELD_MM,
                             static_cast<double>(r[2]) / WELD_MM});
        }
        vid[row] = static_cast<std::uint32_t>(u.size() - 1);
    }

    // Нормали граней: вырожденная грань оставляет нулевую нормаль (ln == 0 -> 1.0).
    std::vector<Vec3> nrm(faces);
    for (std::size_t f = 0; f < faces; ++f) {
        const double* t = &soup[f * 9];
        const double d0[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
        const double d1[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
        double fn[3] = {d0[1] * d1[2] - d0[2] * d1[1],
                        d0[2] * d1[0] - d0[0] * d1[2],
                        d0[0] * d1[1] - d0[1] * d1[0]};
        double ln = norm3(fn);
        if (ln == 0.0) {
            ln = 1.0;
        }
        nrm[f] = Vec3{fn[0] / ln, fn[1] / ln, fn[2] / ln};
    }

    // Рёбра: слоты идут блоками (a,b), (b,c), (c,a), поэтому грань слота - slot % F.
    const std::size_t slots = faces * 3;
    std::vector<std::uint32_t> e0(slots);
    std::vector<std::uint32_t> e1(slots);
    for (std::size_t f = 0; f < faces; ++f) {
        const std::uint32_t a = vid[f * 3];
        const std::uint32_t b = vid[f * 3 + 1];
        const std::uint32_t c = vid[f * 3 + 2];
        e0[f] = a;
        e1[f] = b;
        e0[faces + f] = b;
        e1[faces + f] = c;
        e0[2 * faces + f] = c;
        e1[2 * faces + f] = a;
    }
    const std::int64_t mult = static_cast<std::int64_t>(u.size());   // int(vid.max()) + 1
    std::vector<std::int64_t> key(slots);
    for (std::size_t i = 0; i < slots; ++i) {
        const std::uint32_t a = e0[i];
        const std::uint32_t b = e1[i];
        const std::int64_t lo = static_cast<std::int64_t>(a < b ? a : b);
        const std::int64_t hi = static_cast<std::int64_t>(a < b ? b : a);
        key[i] = lo * mult + hi;
    }
    std::vector<std::uint32_t> srt(slots);
    std::iota(srt.begin(), srt.end(), 0u);
    const std::int64_t* kd = key.data();
    std::stable_sort(srt.begin(), srt.end(),
                     [kd](std::uint32_t a, std::uint32_t b) { return kd[a] < kd[b]; });

    const auto faceOf = [faces](std::uint32_t slot) { return std::size_t(slot) % faces; };
    const double thr = std::cos(angle_deg * DEG_TO_RAD);
    std::vector<std::uint32_t> bnd_lo, bnd_hi, nm_lo, nm_hi, mn_lo, mn_hi;
    std::size_t k = 0;
    while (k < slots) {
        std::size_t j = k;
        while (j < slots && kd[srt[j]] == kd[srt[k]]) {
            ++j;
        }
        const std::size_t cnt = j - k;
        const std::uint32_t slot = srt[k];
        if (cnt == 1) {                             // граница: одна смежная грань
            bnd_lo.push_back(e0[slot]);
            bnd_hi.push_back(e1[slot]);
        } else if (cnt > 2) {                       // неразнообразность: больше двух граней
            nm_lo.push_back(e0[slot]);
            nm_hi.push_back(e1[slot]);
        } else {
            const Vec3& n0 = nrm[faceOf(srt[k])];
            const Vec3& n1 = nrm[faceOf(srt[k + 1])];
            const double dot = std::fabs((n0[0] * n1[0] + n0[1] * n1[1]) + n0[2] * n1[2]);
            if (dot < thr) {                        // двугранный угол больше порога
                mn_lo.push_back(e0[slot]);
                mn_hi.push_back(e1[slot]);
            }
        }
        k = j;
    }

    std::vector<std::uint32_t> keep_lo;
    std::vector<std::uint32_t> keep_hi;
    keep_lo.reserve(bnd_lo.size() + nm_lo.size() + mn_lo.size());
    keep_hi.reserve(keep_lo.capacity());
    keep_lo.insert(keep_lo.end(), bnd_lo.begin(), bnd_lo.end());
    keep_hi.insert(keep_hi.end(), bnd_hi.begin(), bnd_hi.end());
    keep_lo.insert(keep_lo.end(), nm_lo.begin(), nm_lo.end());
    keep_hi.insert(keep_hi.end(), nm_hi.begin(), nm_hi.end());
    keep_lo.insert(keep_lo.end(), mn_lo.begin(), mn_lo.end());
    keep_hi.insert(keep_hi.end(), mn_hi.begin(), mn_hi.end());
    if (keep_lo.empty()) {
        return PackedEdges{};
    }

    std::vector<std::uint32_t> lo_ids;
    std::vector<std::uint32_t> hi_ids;
    simplifyEdgeChains(keep_lo, keep_hi, u, simplifyMm, lo_ids, hi_ids);
    if (lo_ids.empty()) {
        return PackedEdges{};
    }
    std::vector<double> p0(lo_ids.size() * 3);
    std::vector<double> p1(hi_ids.size() * 3);
    for (std::size_t i = 0; i < lo_ids.size(); ++i) {
        const Vec3& a = u[lo_ids[i]];
        const Vec3& b = u[hi_ids[i]];
        p0[i * 3] = a[0];
        p0[i * 3 + 1] = a[1];
        p0[i * 3 + 2] = a[2];
        p1[i * 3] = b[0];
        p1[i * 3 + 1] = b[1];
        p1[i * 3 + 2] = b[2];
    }
    return packEdgesNp(p0, p1);
}

} // namespace gv
