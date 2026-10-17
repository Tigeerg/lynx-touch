// core/tangents.cpp - семейства касательных STEP и кадровый проход по ним.
//
// Порядок операций - часть контракта с эталоном: массивы семейств и отрезки
// касательных сверяются с numpy побитово, поэтому файл компилируется с
// /fp:precise (MSVC) и -ffp-contract=off (GCC/Clang), а суммы, средние и
// произведения считаются тем же порядком слагаемых, что у numpy.
#include "core/tangents.h"

#include "core/np_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace gv {
namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;          // как 2 * math.pi в эталоне
constexpr double DEG_TO_RAD = PI / 180.0;    // как math.radians: x * (pi / 180)
constexpr double NAN_ = std::numeric_limits<double>::quiet_NaN();

// Скалярное произведение, векторное произведение и остаток по знаку делителя -
// общие тела в core/np_helpers.h.
using gv::np::cross3;
using gv::np::dot3;
using gv::np::npMod;

// Норма 3-вектора (np.linalg.norm): корень из суммы квадратов. Тело - общее в
// core/np_helpers.h.
inline double norm3(const double* v) {
    return gv::np::norm3Sqrt(v);
}

// Округление пол-к-чётному: np.round(x, n) в numpy умножает на 10^n, округляет
// rint и делит обратно; builtin round(x, n) в Python даёт тот же результат на
// нетривиальных значениях (nearbyint в режиме FE_TONEAREST).
inline double roundDecimals(double x, double multiplier) {
    return std::nearbyint(x * multiplier) / multiplier;
}

// Зеркало _pt_tri_dist2: квадрат расстояния от точки p до каждого из k
// треугольников tri (плоский ряд по 9 координат). Внутри треугольника -
// проекция по уравнению нормали, снаружи - до ближайшей вершины.
void pointTriangleDist2(const double p[3], const double* tri, std::size_t k, double* out) {
    for (std::size_t i = 0; i < k; ++i) {
        const double* t = tri + i * 9;
        const double a[3] = {t[0], t[1], t[2]};
        const double e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
        const double e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
        const double v[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
        const double a11 = dot3(e1, e1);
        const double a12 = dot3(e1, e2);
        const double a22 = dot3(e2, e2);
        const double d1 = dot3(e1, v);
        const double d2 = dot3(e2, v);
        double det = a11 * a22 - a12 * a12;
        if (std::fabs(det) < 1e-12) {
            det = 1.0;
        }
        const double s = (a22 * d1 - a12 * d2) / det;
        const double tt = (a11 * d2 - a12 * d1) / det;
        const bool inside = (s >= 0.0) && (tt >= 0.0) && (s + tt <= 1.0);
        const double q[3] = {a[0] + e1[0] * s + e2[0] * tt,
                             a[1] + e1[1] * s + e2[1] * tt,
                             a[2] + e1[2] * s + e2[2] * tt};
        const double pq[3] = {p[0] - q[0], p[1] - q[1], p[2] - q[2]};
        const double d2i = pq[0] * pq[0] + pq[1] * pq[1] + pq[2] * pq[2];
        double dv = 0.0;
        for (int c = 0; c < 3; ++c) {
            const double dx = p[0] - t[c * 3 + 0];
            const double dy = p[1] - t[c * 3 + 1];
            const double dz = p[2] - t[c * 3 + 2];
            const double dc = dx * dx + dy * dy + dz * dz;
            if (c == 0 || dc < dv) {
                dv = dc;
            }
        }
        out[i] = inside ? d2i : dv;
    }
}

// Зеркало _cone_keeps_silhouette: конус получает касательные образующие, если
// он (1) не короче порога длины образующей пресета и (2) вогнутый (нормали
// кожи у его поверхности смотрят к оси). Без кожи проверка вогнутости
// пропускается, остаётся порог длины.
bool coneKeepsSilhouette(const ConeSurface& cone, const SkinTriangles* skin,
                         const QualityPreset& quality) {
    const double slant = std::fabs(cone.s1 - cone.s0);
    if (slant < quality.coneMinSlantMm) {
        return false;
    }
    if (skin == nullptr || !skin->valid) {
        return true;
    }
    double d[3] = {cone.d[0], cone.d[1], cone.d[2]};
    const double dlen = std::max(norm3(d), 1e-12);
    for (int j = 0; j < 3; ++j) {
        d[j] /= dlen;
    }
    const double apex[3] = {cone.a[0], cone.a[1], cone.a[2]};
    const double psi = cone.psi;
    // любые два ортогональных к оси орта: проба идёт по обороту конуса
    const double ref[3] = {std::fabs(d[2]) < 0.9 ? 0.0 : 1.0, 0.0,
                           std::fabs(d[2]) < 0.9 ? 1.0 : 0.0};
    const double dref = dot3(d, ref);
    double u1[3] = {ref[0] - d[0] * dref, ref[1] - d[1] * dref, ref[2] - d[2] * dref};
    const double u1len = std::max(norm3(u1), 1e-12);
    for (int j = 0; j < 3; ++j) {
        u1[j] /= u1len;
    }
    double u2[3];
    cross3(d, u1, u2);
    const double sMid = (cone.s0 + cone.s1) / 2.0;
    const double side = sMid >= 0.0 ? 1.0 : -1.0;   // вторая полость конуса
    // numpy.argpartition(dd, 15) на массиве короче 16 элементов бросает
    // исключение, и оно глотается - конус остаётся. Тот же исход.
    if (skin->count < 16) {
        return true;
    }
    const double cosPsi = std::cos(psi);
    const double sinPsi = std::sin(psi);
    std::vector<double> dd(skin->count);
    std::vector<std::size_t> order(skin->count);
    std::vector<double> tri16(16 * 9);
    std::vector<double> dv16(16);
    for (int k = 0; k < quality.coneProbeAzimuths; ++k) {
        const double ph = TWO_PI * k / quality.coneProbeAzimuths;
        const double cph = std::cos(ph);
        const double sph = std::sin(ph);
        double rad[3];
        for (int j = 0; j < 3; ++j) {
            rad[j] = (u1[j] * cph + u2[j] * sph) * side;
        }
        double p[3];
        for (int j = 0; j < 3; ++j) {
            const double along = d[j] * cosPsi + rad[j] * side * sinPsi;
            p[j] = apex[j] + along * sMid;
        }
        for (std::size_t f = 0; f < skin->count; ++f) {
            const double w[3] = {skin->skcen[f * 3 + 0] - p[0],
                                 skin->skcen[f * 3 + 1] - p[1],
                                 skin->skcen[f * 3 + 2] - p[2]};
            dd[f] = dot3(w, w);
            order[f] = f;
        }
        std::partial_sort(order.begin(), order.begin() + 16, order.end(),
                          [&dd](std::size_t a, std::size_t b) {
                              if (dd[a] != dd[b]) {
                                  return dd[a] < dd[b];
                              }
                              return a < b;
                          });
        for (std::size_t c = 0; c < 16; ++c) {
            std::memcpy(&tri16[c * 9], &skin->sktri[order[c] * 9], 9 * sizeof(double));
        }
        pointTriangleDist2(p, tri16.data(), 16, dv16.data());
        int own = 0;
        int other = 0;
        bool any = false;
        for (std::size_t c = 0; c < 16; ++c) {
            if (!(dv16[c] < 1.5)) {
                continue;
            }
            any = true;
            const double* irm = &skin->skirm[order[c] * 3];
            const double dot = dot3(irm, rad);
            if (dot < -0.05) {
                ++own;
            }
            if (dot > 0.05) {
                ++other;
            }
        }
        if (!any) {
            continue;                       // проба упала мимо грани
        }
        if (own > 0 && own >= other) {
            return true;
        }
    }
    return false;
}

// Ключ склейки совпадающих конических поверхностей (_dedupe_cones): вершина,
// каноническое направление оси, полуугол и отсортированный срез s.
struct ConeKey {
    double a[3];
    double d[3];
    double psi;
    double slo;
    double shi;

    bool operator==(const ConeKey& o) const {
        for (int j = 0; j < 3; ++j) {
            if (a[j] != o.a[j] || d[j] != o.d[j]) {
                return false;
            }
        }
        return psi == o.psi && slo == o.slo && shi == o.shi;
    }
};

// Зеркало _dedupe_cones: одна геометрия конуса в STEP кроится несколькими face
// TShape, и без склейки каждая образующая рисуется дважды в одной точке.
std::vector<ConeSurface> dedupeCones(const std::vector<ConeSurface>& cones) {
    std::vector<ConeSurface> out;
    std::vector<ConeKey> seen;
    for (const ConeSurface& c : cones) {
        double d[3] = {c.d[0], c.d[1], c.d[2]};
        const double dlen = std::max(norm3(d), 1e-12);
        for (int j = 0; j < 3; ++j) {
            d[j] /= dlen;
        }
        std::vector<int> sig;
        for (int j = 0; j < 3; ++j) {
            if (std::fabs(d[j]) > 1e-4) {
                sig.push_back(j);
            }
        }
        double s0 = c.s0;
        double s1 = c.s1;
        if (!sig.empty() && d[sig[0]] < 0.0) {
            for (int j = 0; j < 3; ++j) {
                d[j] = -d[j];
            }
            const double ns0 = -s1;             // при флипе оси диапазон отражается
            const double ns1 = -s0;
            s0 = ns0;
            s1 = ns1;
        }
        ConeKey key;
        for (int j = 0; j < 3; ++j) {
            key.a[j] = roundDecimals(c.a[j], 1000.0);
            key.d[j] = roundDecimals(d[j], 10000.0);
        }
        key.psi = roundDecimals(c.psi, 10000.0);
        const double r0 = roundDecimals(s0, 1000.0);
        const double r1 = roundDecimals(s1, 1000.0);
        key.slo = std::min(r0, r1);
        key.shi = std::max(r0, r1);
        bool dup = false;
        for (const ConeKey& k : seen) {
            if (k == key) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        seen.push_back(key);
        out.push_back(c);
    }
    return out;
}

// Одна запись семейства: ось, базис сечения, пределы вдоль оси, полнота и
// покрытие дуги. Списки этих записей эталон ведёт раздельно (_tangent_families).
struct Entry {
    double o[3] = {0.0, 0.0, 0.0};
    double d[3] = {0.0, 0.0, 1.0};
    double e1[3] = {1.0, 0.0, 0.0};
    double e2[3] = {0.0, 0.0, 1.0};
    bool full = false;
    double r = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
    std::array<bool, 64> arc{};
    double a0 = 0.0;
    double a1 = 0.0;
};

// _union_a: объединение дуг двух секций; NaN у любой из них даёт NaN.
void unionArc(double& a0, double& a1, double c0, double c1) {
    if (!(std::isfinite(a0) && std::isfinite(c0))) {
        a0 = NAN_;
        a1 = NAN_;
        return;
    }
    a0 = std::min(a0, c0);
    a1 = std::max(a1, c1);
}

} // namespace

SkinTriangles buildSkinTriangles(const std::vector<double>& soup,
                                 const std::vector<float>& normals) {
    SkinTriangles out;
    // asarray(..., float).reshape(-1, 3, 3) в эталоне бросает исключение, если
    // длина не делится на 9; исключение глотается, и кожи нет вовсе.
    if (soup.size() % 9 != 0 || normals.size() % 9 != 0) {
        return out;
    }
    out.valid = true;
    out.count = soup.size() / 9;
    out.sktri = soup;
    out.skcen.resize(out.count * 3);
    out.skirm.resize(out.count * 3);
    for (std::size_t f = 0; f < out.count; ++f) {
        for (int j = 0; j < 3; ++j) {
            out.skcen[f * 3 + std::size_t(j)] = (soup[f * 9 + std::size_t(j)]
                                                 + soup[f * 9 + 3 + std::size_t(j)]
                                                 + soup[f * 9 + 6 + std::size_t(j)]) / 3.0;
            out.skirm[f * 3 + std::size_t(j)] = static_cast<double>(normals[f * 9 + std::size_t(j)]);
        }
    }
    return out;
}

TangentFamilies buildTangentFamilies(const StepSurfaceList& surfs,
                                     const PackedEdges* packedEdges,
                                     const SkinTriangles* skin,
                                     const QualityPreset& quality) {
    TangentFamilies out;
    std::vector<ConeSurface> cones;

    // Цилиндры сгруппированы по ключу (ось с округлением, каноническое
    // направление, радиус); порядок групп - порядок первого появления, как у
    // словаря Python. Ключ - 180 значений на Test_model, линейный поиск дешевле
    // хеш-таблицы и не зависит от хеша double.
    struct GroupKey {
        double op[3];
        double dr[3];
        double r;
    };
    std::vector<GroupKey> keys;
    std::vector<std::vector<const CylinderSurface*>> groups;

    for (const StepSurface& s : surfs) {
        if (!s.is_cylinder) {
            cones.push_back(s.cone);
            continue;
        }
        const CylinderSurface& c = s.cyl;
        double d[3] = {c.d[0], c.d[1], c.d[2]};
        const double dn = norm3(d);
        if (dn <= 1e-12) {
            continue;
        }
        for (int j = 0; j < 3; ++j) {
            d[j] /= dn;
        }
        double oPerp[3];
        const double al = dot3(d, c.o);
        for (int j = 0; j < 3; ++j) {
            oPerp[j] = c.o[j] - d[j] * al;
        }
        double dref[3] = {d[0], d[1], d[2]};
        for (int j = 0; j < 3; ++j) {
            if (std::fabs(d[j]) > 1e-9) {
                if (d[j] < 0.0) {               // лексикографическое направление
                    for (int k = 0; k < 3; ++k) {
                        dref[k] = -d[k];
                    }
                }
                break;
            }
        }
        GroupKey key;
        for (int j = 0; j < 3; ++j) {
            key.op[j] = roundDecimals(oPerp[j], 10.0);
            key.dr[j] = roundDecimals(dref[j], 100.0);
        }
        key.r = roundDecimals(c.r, 100.0);
        std::size_t gi = keys.size();
        for (std::size_t k = 0; k < keys.size(); ++k) {
            if (keys[k].r == key.r && keys[k].op[0] == key.op[0] && keys[k].op[1] == key.op[1]
                    && keys[k].op[2] == key.op[2] && keys[k].dr[0] == key.dr[0]
                    && keys[k].dr[1] == key.dr[1] && keys[k].dr[2] == key.dr[2]) {
                gi = k;
                break;
            }
        }
        if (gi == keys.size()) {
            keys.push_back(key);
            groups.push_back({});
        }
        groups[gi].push_back(&c);
    }

    std::vector<Entry> entries;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const std::vector<const CylinderSurface*>& faces = groups[g];
        // каноническое направление ключа, а не faces[0]: слитые по нему грани
        // могут смотреть вдоль оси в разные стороны
        double d[3] = {keys[g].dr[0], keys[g].dr[1], keys[g].dr[2]};
        const double dlen = std::max(norm3(d), 1e-12);
        for (int j = 0; j < 3; ++j) {
            d[j] /= dlen;
        }
        // точный след оси, а не округлённый ключ: ключ с шагом 0.1 мм уводил
        // образующую с истинной оси
        double o[3] = {0.0, 0.0, 0.0};
        for (const CylinderSurface* s : faces) {
            const double al = dot3(d, s->o);
            for (int j = 0; j < 3; ++j) {
                o[j] += s->o[j] - d[j] * al;
            }
        }
        for (int j = 0; j < 3; ++j) {
            o[j] /= static_cast<double>(faces.size());
        }
        bool full = false;
        for (const CylinderSurface* s : faces) {
            if (!s->has_arc) {
                full = true;
                break;
            }
            if (npMod(s->arc1 - s->arc0, TWO_PI) >= TWO_PI - 1e-6) {
                full = true;
                break;
            }
        }
        // осевые кластеры: разные отверстия на одной оси с одним радиусом не
        // должны слиться в одну длинную образующую через пустоту
        std::vector<std::array<double, 2>> tbs;
        for (const CylinderSurface* s : faces) {
            double lo = s->t0;
            double hi = s->t1;
            if (dot3(d, s->d) < 0.0) {          // ось грани против канонической
                const double nlo = -hi;
                const double nhi = -lo;
                lo = nlo;
                hi = nhi;
            }
            const double w[3] = {s->o[0] - o[0], s->o[1] - o[1], s->o[2] - o[2]};
            const double c = dot3(d, w);
            tbs.push_back({c + lo, c + hi});
        }
        std::sort(tbs.begin(), tbs.end());
        std::vector<std::array<double, 2>> spans;
        for (const std::array<double, 2>& tb : tbs) {
            if (!spans.empty() && tb[0] <= spans.back()[1] + 1e-6) {
                spans.back()[1] = std::max(spans.back()[1], tb[1]);
            } else {
                spans.push_back(tb);
            }
        }
        double rsum = 0.0;
        for (const CylinderSurface* s : faces) {
            rsum += s->r;
        }
        const double rrep = rsum / static_cast<double>(faces.size());
        // Политика силуэта: порог дуги и минимальный пролёт пресета (у med -
        // 85 гр и 2.5 мм) оставляют образующую только у настоящих тел вращения.
        double wmax = 0.0;
        for (const CylinderSurface* s : faces) {
            if (!s->has_arc) {
                wmax = TWO_PI;
                break;
            }
            wmax = std::max(wmax, npMod(s->arc1 - s->arc0, TWO_PI));
        }
        bool tangented = full || wmax >= quality.silhouetteArcDeg * DEG_TO_RAD;
        if (tangented) {
            for (const std::array<double, 2>& sp : spans) {
                if ((sp[1] - sp[0]) < quality.silhouetteMinSpanMm) {
                    tangented = false;
                    break;
                }
            }
        }
        // азимутальное покрытие - из аналитической дуги грани, а не из меша
        std::array<bool, 64> abins{};
        double fa0 = NAN_;
        double fa1 = NAN_;
        for (const CylinderSurface* s : faces) {
            if (!s->has_arc) {
                abins.fill(true);
                break;
            }
            const double spn = npMod(s->arc1 - s->arc0, TWO_PI);
            if (spn >= TWO_PI - 1e-6) {
                abins.fill(true);
                break;
            }
            const int nb = std::max(1, static_cast<int>(std::nearbyint(spn / TWO_PI * 64.0)));
            for (int k = 0; k < nb; ++k) {
                const double ang = npMod(s->arc0 + (k + 0.5) / nb * spn, TWO_PI);
                abins[static_cast<std::size_t>(ang / TWO_PI * 64.0) % 64] = true;
            }
            fa0 = s->arc0;
            fa1 = s->arc0 + spn;
        }
        for (const std::array<double, 2>& sp : spans) {
            Entry e;
            for (int j = 0; j < 3; ++j) {
                e.o[j] = o[j];
                e.d[j] = d[j];
                e.e1[j] = faces[0]->e1[j];
                e.e2[j] = faces[0]->e2[j];
            }
            e.full = tangented;
            e.r = rrep;
            e.t0 = sp[0];
            e.t1 = sp[1];
            e.arc = abins;
            e.a0 = fa0;
            e.a1 = fa1;
            entries.push_back(e);
        }
    }

    // Слияние семейств на одной оси: шум моделирования делит одно отверстие на
    // два ключа. Сливаются только равные радиусы - настоящая ступень сохраняет
    // свой силуэт на истинном радиусе стенки.
    struct Merged {
        std::size_t idx;
        double t0;
        double t1;
        bool full;
        std::array<bool, 64> arc;
        double a0;
        double a1;
    };
    std::vector<Merged> merged;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        bool placed = false;
        for (Merged& m : merged) {
            const Entry& rep = entries[m.idx];
            if (std::fabs(entries[i].r - rep.r) > 0.05) {
                continue;
            }
            if (std::fabs(dot3(entries[i].d, rep.d)) < 0.999) {
                continue;
            }
            const double w[3] = {entries[i].o[0] - rep.o[0], entries[i].o[1] - rep.o[1],
                                 entries[i].o[2] - rep.o[2]};
            const double al = dot3(w, rep.d);
            const double p[3] = {w[0] - rep.d[0] * al, w[1] - rep.d[1] * al,
                                 w[2] - rep.d[2] * al};
            if (norm3(p) > 0.4) {
                continue;
            }
            // пролёты должны касаться или перекрываться: проверка идёт по
            // исходному пролёту представителя, как в эталоне
            if (rep.t1 + 0.5 < entries[i].t0 || entries[i].t1 + 0.5 < rep.t0) {
                continue;
            }
            m.t0 = std::min(m.t0, entries[i].t0);
            m.t1 = std::max(m.t1, entries[i].t1);
            m.full = m.full || entries[i].full;
            for (int k = 0; k < 64; ++k) {
                m.arc[std::size_t(k)] = m.arc[std::size_t(k)] || entries[i].arc[std::size_t(k)];
            }
            unionArc(m.a0, m.a1, entries[i].a0, entries[i].a1);
            placed = true;
            break;
        }
        if (!placed) {
            merged.push_back({i, entries[i].t0, entries[i].t1, entries[i].full, entries[i].arc,
                              entries[i].a0, entries[i].a1});
        }
    }
    std::vector<Entry> fam;
    if (!merged.empty() && merged.size() < entries.size()) {
        for (const Merged& m : merged) {
            Entry e = entries[m.idx];
            e.t0 = m.t0;
            e.t1 = m.t1;
            e.full = m.full;
            e.arc = m.arc;
            e.a0 = m.a0;
            e.a1 = m.a1;
            fam.push_back(e);
        }
    } else {
        fam = entries;
    }

    const std::size_t n = fam.size();
    // DROP: азимуты генераторов, дублирующих статическую стыковую линию B-rep
    // (та же ось, тот же радиус, перекрытие по оси больше порога пресета, у med
    // 2 мм). Ширина строки - (N,8), как в эталоне, поэтому азимутов не больше 8.
    std::vector<double> drop(n * 8, NAN_);
    if (packedEdges != nullptr && n > 0) {
        const std::size_t floats = packedEdges->raw.size() / sizeof(float);
        if (!packedEdges->raw.empty() && packedEdges->nv >= 2 && floats >= 10) {
            std::vector<float> vf(floats);
            std::memcpy(vf.data(), packedEdges->raw.data(), floats * sizeof(float));
            const std::size_t vcount = floats / 5;
            const std::size_t segCount = vcount / 2;
            std::vector<double> sa(segCount * 3);
            std::vector<double> sb(segCount * 3);
            std::vector<double> su(segCount * 3);
            std::vector<std::size_t> segIdx;
            for (std::size_t j = 0; j < segCount; ++j) {
                double sd[3];
                for (int c = 0; c < 3; ++c) {
                    sa[j * 3 + std::size_t(c)] = static_cast<double>(vf[(2 * j) * 5 + std::size_t(c)]);
                    sb[j * 3 + std::size_t(c)] = static_cast<double>(vf[(2 * j + 1) * 5 + std::size_t(c)]);
                    sd[c] = sb[j * 3 + std::size_t(c)] - sa[j * 3 + std::size_t(c)];
                }
                const double sl = norm3(sd);
                if (!(sl > 1e-6)) {
                    continue;
                }
                for (int c = 0; c < 3; ++c) {
                    su[j * 3 + std::size_t(c)] = sd[c] / sl;
                }
                segIdx.push_back(j);
            }
            std::vector<std::size_t> par;
            std::vector<double> perp;
            std::vector<double> rr;
            std::vector<std::size_t> sel;
            std::vector<double> lo2;
            std::vector<double> hi2;
            std::vector<double> ang;
            for (std::size_t i = 0; i < n; ++i) {
                par.clear();
                for (std::size_t j : segIdx) {
                    if (std::fabs(dot3(&su[j * 3], fam[i].d)) > 0.9993) {
                        par.push_back(j);
                    }
                }
                if (par.empty()) {
                    continue;
                }
                perp.assign(par.size() * 3, 0.0);
                rr.assign(par.size(), 0.0);
                for (std::size_t k = 0; k < par.size(); ++k) {
                    const std::size_t j = par[k];
                    const double w[3] = {sa[j * 3 + 0] - fam[i].o[0], sa[j * 3 + 1] - fam[i].o[1],
                                         sa[j * 3 + 2] - fam[i].o[2]};
                    const double al = dot3(w, fam[i].d);
                    for (int c = 0; c < 3; ++c) {
                        perp[k * 3 + std::size_t(c)] = w[c] - fam[i].d[c] * al;
                    }
                    rr[k] = norm3(&perp[k * 3]);
                }
                sel.clear();
                for (std::size_t k = 0; k < par.size(); ++k) {
                    // 0.35 мм: только рёбра, лежащие на этом цилиндре; шире
                    // нельзя - в коаксиальной группе захватывался шов соседней
                    // ступени
                    if (std::fabs(rr[k] - fam[i].r) < 0.35) {
                        sel.push_back(k);
                    }
                }
                if (sel.empty()) {
                    continue;
                }
                lo2.assign(sel.size(), 0.0);
                hi2.assign(sel.size(), 0.0);
                ang.clear();
                for (std::size_t k = 0; k < sel.size(); ++k) {
                    const std::size_t j = par[sel[k]];
                    const double wa[3] = {sa[j * 3 + 0] - fam[i].o[0], sa[j * 3 + 1] - fam[i].o[1],
                                          sa[j * 3 + 2] - fam[i].o[2]};
                    const double wb[3] = {sb[j * 3 + 0] - fam[i].o[0], sb[j * 3 + 1] - fam[i].o[1],
                                          sb[j * 3 + 2] - fam[i].o[2]};
                    const double ta = dot3(wa, fam[i].d);
                    const double tb = dot3(wb, fam[i].d);
                    lo2[k] = std::min(ta, tb);
                    hi2[k] = std::max(ta, tb);
                    const double ov = std::min(hi2[k], fam[i].t1) - std::max(lo2[k], fam[i].t0);
                    if (!(ov > quality.dropOverlapMm) || !(rr[sel[k]] > 1e-6)) {
                        continue;
                    }
                    ang.push_back(npMod(std::atan2(dot3(&perp[sel[k] * 3], fam[i].e2),
                                                   dot3(&perp[sel[k] * 3], fam[i].e1)), TWO_PI));
                }
                if (ang.empty()) {
                    continue;
                }
                std::sort(ang.begin(), ang.end());
                std::vector<double> uniq;
                for (double a : ang) {
                    if (uniq.empty() || a - uniq.back() > 0.03) {
                        uniq.push_back(a);
                    }
                }
                // копия того же стыка через ноль (0.001 и 6.283): рантайм
                // сравнивает по кругу, поэтому остаётся только первая
                if (uniq.size() > 1 && uniq[0] + TWO_PI - uniq.back() < 0.03) {
                    uniq.pop_back();
                }
                for (std::size_t k = 0; k < uniq.size() && k < 8; ++k) {
                    drop[i * 8 + k] = uniq[k];
                }
            }
        }
    }

    CylinderFamilies famOut;
    famOut.count = n;
    famOut.O.resize(n * 3);
    famOut.D.resize(n * 3);
    famOut.E1.resize(n * 3);
    famOut.E2.resize(n * 3);
    famOut.R.resize(n);
    famOut.T0.resize(n);
    famOut.T1.resize(n);
    famOut.RREF.resize(n);
    famOut.A0.resize(n);
    famOut.A1.resize(n);
    famOut.FULL.assign(n, 0);
    famOut.ARC.assign(n * 64, 0);
    famOut.DROP = drop;
    for (std::size_t i = 0; i < n; ++i) {
        for (int j = 0; j < 3; ++j) {
            famOut.O[i * 3 + std::size_t(j)] = fam[i].o[j];
            famOut.D[i * 3 + std::size_t(j)] = fam[i].d[j];
            famOut.E1[i * 3 + std::size_t(j)] = fam[i].e1[j];
            famOut.E2[i * 3 + std::size_t(j)] = fam[i].e2[j];
        }
        famOut.R[i] = fam[i].r;
        famOut.T0[i] = fam[i].t0;
        famOut.T1[i] = fam[i].t1;
        famOut.A0[i] = fam[i].a0;
        famOut.A1[i] = fam[i].a1;
        famOut.FULL[i] = fam[i].full ? 1 : 0;
    }

    // Коаксиальные секции ступенчатого отверстия получают общий азимут: их
    // собственные касательные расходятся на ~0.2 гр, и контур рисовался двумя
    // смещёнными обрубками. Опорный радиус - наибольший на этой оси.
    for (std::size_t i = 0; i < n; ++i) {
        double best = -1.0;
        for (std::size_t j = 0; j < n; ++j) {
            bool coax = (i == j);
            if (!coax) {
                const double pd = std::fabs(dot3(&famOut.D[i * 3], &famOut.D[j * 3]));
                const double w[3] = {famOut.O[j * 3 + 0] - famOut.O[i * 3 + 0],
                                     famOut.O[j * 3 + 1] - famOut.O[i * 3 + 1],
                                     famOut.O[j * 3 + 2] - famOut.O[i * 3 + 2]};
                const double al = dot3(w, &famOut.D[j * 3]);
                const double p[3] = {w[0] - famOut.D[i * 3 + 0] * al,
                                     w[1] - famOut.D[i * 3 + 1] * al,
                                     w[2] - famOut.D[i * 3 + 2] * al};
                coax = (pd > 0.999) && (norm3(p) < 0.4);
            }
            if (coax) {
                best = std::max(best, famOut.R[j]);
            }
        }
        famOut.RREF[i] = best;
    }

    // Азимутальное покрытие: маску по мешу ограничиваем аналитической дугой
    // грани, но только у частичных дуг - полное отверстие не режем.
    if (skin != nullptr && skin->valid && n > 0) {
        std::vector<double> pp(skin->count * 9);    // (F,3,3): вершина минус осевая часть
        std::vector<double> alv(skin->count * 3);   // (F,3): те же смещения вдоль оси
        for (std::size_t i = 0; i < n; ++i) {
            if (!fam[i].full) {
                continue;
            }
            const double* u = fam[i].d;
            for (std::size_t f = 0; f < skin->count; ++f) {
                for (int c = 0; c < 3; ++c) {
                    const double* vx = &skin->sktri[f * 9 + std::size_t(c) * 3];
                    double wv[3];
                    for (int k = 0; k < 3; ++k) {
                        wv[k] = vx[k] - fam[i].o[k];
                    }
                    const double along = dot3(wv, u);
                    alv[f * 3 + std::size_t(c)] = along;
                    for (int k = 0; k < 3; ++k) {
                        pp[f * 9 + std::size_t(c) * 3 + std::size_t(k)] = wv[k] - u[k] * along;
                    }
                }
            }
            for (std::size_t f = 0; f < skin->count; ++f) {
                bool onTri = true;
                double amin = alv[f * 3];
                double amax = alv[f * 3];
                for (int c = 0; c < 3; ++c) {
                    // все три вершины на радиусе этой грани: треугольник лежит
                    // на самом цилиндре, а не на соседнем
                    if (!(std::fabs(norm3(&pp[f * 9 + std::size_t(c) * 3]) - fam[i].r) < 0.25)) {
                        onTri = false;
                    }
                    amin = std::min(amin, alv[f * 3 + std::size_t(c)]);
                    amax = std::max(amax, alv[f * 3 + std::size_t(c)]);
                }
                if (!onTri || !(amin > fam[i].t0 - 0.5) || !(amax < fam[i].t1 + 0.5)) {
                    continue;
                }
                double cnt[3];
                for (int k = 0; k < 3; ++k) {
                    cnt[k] = (pp[f * 9 + std::size_t(k)] + pp[f * 9 + 3 + std::size_t(k)]
                              + pp[f * 9 + 6 + std::size_t(k)]) / 3.0;
                }
                const double a = npMod(std::atan2(dot3(cnt, fam[i].e2), dot3(cnt, fam[i].e1)),
                                       TWO_PI);
                famOut.ARC[i * 64 + static_cast<std::size_t>(a / TWO_PI * 64.0) % 64] = 1;
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (!fam[i].full) {
                continue;
            }
            int msum = 0;
            int asum = 0;
            for (int k = 0; k < 64; ++k) {
                msum += famOut.ARC[i * 64 + std::size_t(k)];
                asum += fam[i].arc[std::size_t(k)] ? 1 : 0;
            }
            if (msum >= 56) {
                continue;                       // полное отверстие - не режем
            }
            if (asum >= 4) {
                int cut = 0;
                for (int k = 0; k < 64; ++k) {
                    if (famOut.ARC[i * 64 + std::size_t(k)] && fam[i].arc[std::size_t(k)]) {
                        ++cut;
                    }
                }
                for (int k = 0; k < 64; ++k) {
                    const bool keep = cut > 0
                        ? (famOut.ARC[i * 64 + std::size_t(k)] && fam[i].arc[std::size_t(k)])
                        : fam[i].arc[std::size_t(k)];
                    famOut.ARC[i * 64 + std::size_t(k)] = keep ? 1 : 0;
                }
            }
        }
        // непрерывная дуга (a0, a0 + span) для точной обрезки на скользящих
        // углах; полные отверстия (маска ~360 гр) помечаем NaN - их не режем
        for (std::size_t i = 0; i < n; ++i) {
            int msum = 0;
            for (int k = 0; k < 64; ++k) {
                msum += famOut.ARC[i * 64 + std::size_t(k)];
            }
            if (msum >= 56) {
                famOut.A0[i] = NAN_;
                famOut.A1[i] = NAN_;
            }
        }
    }

    out.cylinders = std::move(famOut);
    // Политика конусов, затем склейка совпадающих: порядок как в эталоне.
    std::vector<ConeSurface> kept;
    kept.reserve(cones.size());
    for (const ConeSurface& c : cones) {
        if (coneKeepsSilhouette(c, skin, quality)) {
            kept.push_back(c);
        }
    }
    out.cones = dedupeCones(kept);
    return out;
}

TangentFamilies profileTangentFamilies(const std::vector<ProfilePoint>& profile)
{
    TangentFamilies out;
    if (profile.size() < 2) {
        return out;
    }
    // Секция профиля: либо цилиндр (радиус постоянен), либо конус (радиус
    // меняется линейно). Плоский торец (dy = 0) и участок на оси генераторов не
    // дают - их рисует кольцо.
    struct Section {
        bool cylinder = true;
        double r = 0.0;
        double t0 = 0.0;
        double t1 = 0.0;
        ConeSurface cone;
    };
    const double eps = 1e-9;
    std::vector<Section> sections;
    for (std::size_t i = 0; i + 1 < profile.size(); ++i) {
        const double r0 = profile[i].radius_mm;
        const double y0 = profile[i].height_mm;
        const double r1 = profile[i + 1].radius_mm;
        const double y1 = profile[i + 1].height_mm;
        const double dr = r1 - r0;
        const double dy = y1 - y0;
        if (std::fabs(dy) < eps || (r0 <= eps && r1 <= eps)) {
            continue;
        }
        Section s;
        if (std::fabs(dr) < eps) {
            s.r = r0;
            s.t0 = std::min(y0, y1);
            s.t1 = std::max(y0, y1);
            // Соседние цилиндры одного радиуса (тело и шейка на одном диаметре)
            // дают одну общую образующую, а не две наложенные линии.
            if (!sections.empty() && sections.back().cylinder
                    && std::fabs(sections.back().r - s.r) < eps
                    && std::fabs(sections.back().t1 - s.t0) < 1e-6) {
                sections.back().t1 = s.t1;
            } else {
                sections.push_back(s);
            }
            continue;
        }
        // Ось конуса смотрит от вершины к секции: радиус вдоль неё растёт, поэтому
        // параметр s обоих концов положителен, как у ConeSurface из STEP.
        const double sgn = dr > 0.0 ? 1.0 : -1.0;
        const double yApex = y0 - r0 * dy / dr;
        const double psi = std::atan(std::fabs(dr / dy));
        const double cosPsi = std::cos(psi);
        if (!(cosPsi > eps) || !std::isfinite(yApex)) {
            continue;
        }
        s.cylinder = false;
        s.cone.a[1] = yApex;
        // Ось задаётся целиком: ConeSurface по умолчанию несёт ось (0, 0, 1), и от
        // одной записи d[1] ось уезжает в (0, sgn, 1) - образующая встала бы на
        // радиус ~38 мм вместо настоящих 11.5..23 (замерено на оправке).
        s.cone.d[0] = 0.0;
        s.cone.d[1] = sgn;
        s.cone.d[2] = 0.0;
        s.cone.psi = psi;
        s.cone.s0 = (y0 - yApex) * sgn / cosPsi;
        s.cone.s1 = (y1 - yApex) * sgn / cosPsi;
        sections.push_back(s);
    }

    std::size_t ncyl = 0;
    for (const Section& s : sections) {
        if (s.cylinder) {
            ++ncyl;
        }
    }
    CylinderFamilies& cyls = out.cylinders;
    cyls.count = ncyl;
    cyls.O.assign(ncyl * 3, 0.0);
    cyls.D.assign(ncyl * 3, 0.0);
    cyls.E1.assign(ncyl * 3, 0.0);
    cyls.E2.assign(ncyl * 3, 0.0);
    cyls.R.assign(ncyl, 0.0);
    cyls.T0.assign(ncyl, 0.0);
    cyls.T1.assign(ncyl, 0.0);
    cyls.RREF.assign(ncyl, 0.0);
    cyls.A0.assign(ncyl, NAN_);
    cyls.A1.assign(ncyl, NAN_);
    cyls.FULL.assign(ncyl, 1);
    cyls.DROP.assign(ncyl * 8, NAN_);
    cyls.ARC.assign(ncyl * 64, 0);
    std::size_t ic = 0;
    for (const Section& s : sections) {
        if (!s.cylinder) {
            out.cones.push_back(s.cone);
            continue;
        }
        cyls.D[ic * 3 + 1] = 1.0;
        cyls.E1[ic * 3 + 0] = 1.0;
        cyls.E2[ic * 3 + 2] = 1.0;
        cyls.R[ic] = s.r;
        cyls.T0[ic] = s.t0;
        cyls.T1[ic] = s.t1;
        cyls.RREF[ic] = s.r;
        ++ic;
    }
    return out;
}

const std::vector<double>& tangentFromFamilies(const TangentFamilies& fam, const double eyeMm[3],
                                               TangentScratch& scratch) {
    scratch.segments.clear();
    // Псевдонимы на буферы переживающего кадр объекта: тело ниже не меняется,
    // меняются только места заведения этих векторов.
    std::vector<double>& along = scratch.along;
    std::vector<double>& perp = scratch.perp;
    std::vector<double>& rp = scratch.rp;
    std::vector<double>& mh = scratch.mh;
    std::vector<double>& rot = scratch.rot;
    std::vector<double>& nrm = scratch.nrm;
    std::vector<double>& cg = scratch.cg;
    std::vector<double>& sg = scratch.sg;
    std::vector<double>& phi = scratch.phi;
    std::vector<double>& baseA = scratch.baseA;
    std::vector<double>& baseB = scratch.baseB;
    std::vector<std::uint8_t>& ok = scratch.ok;
    std::vector<double>& segments = scratch.segments;
    const CylinderFamilies& cyls = fam.cylinders;
    const std::size_t n = cyls.count;
    if (n > 0) {
        along.assign(n, 0.0);
        perp.assign(n * 3, 0.0);
        rp.assign(n, 0.0);
        mh.assign(n * 3, 0.0);
        rot.assign(n * 3, 0.0);
        nrm.assign(n * 3, 0.0);
        cg.assign(n, 0.0);
        sg.assign(n, 0.0);
        bool anyValid = false;
        for (std::size_t i = 0; i < n; ++i) {
            const double w[3] = {eyeMm[0] - cyls.O[i * 3 + 0], eyeMm[1] - cyls.O[i * 3 + 1],
                                 eyeMm[2] - cyls.O[i * 3 + 2]};
            along[i] = dot3(&cyls.D[i * 3], w);
            for (int c = 0; c < 3; ++c) {
                perp[i * 3 + std::size_t(c)] = w[c] - cyls.D[i * 3 + std::size_t(c)] * along[i];
            }
            rp[i] = norm3(&perp[i * 3]);
            anyValid = anyValid || (rp[i] > cyls.R[i] + 1e-6);
        }
        if (anyValid) {
            for (std::size_t i = 0; i < n; ++i) {
                // азимут касания - по наибольшему радиусу коаксиальной группы:
                // секции ступенчатого отверстия встают на один контур
                const double rref = cyls.RREF[i];
                const double rpSafe = std::max(rp[i], 1e-9);
                const double rang = rp[i] > rref ? rref : cyls.R[i];
                double c = rang / rpSafe;
                c = std::min(1.0, std::max(-1.0, c));
                cg[i] = c;
                sg[i] = std::sqrt(std::max(1.0 - c * c, 0.0));
                const double div = std::max(rp[i], 1e-9);
                for (int k = 0; k < 3; ++k) {
                    mh[i * 3 + std::size_t(k)] = perp[i * 3 + std::size_t(k)] / div;
                }
                cross3(&cyls.D[i * 3], &mh[i * 3], &rot[i * 3]);
            }
            phi.assign(n, 0.0);
            baseA.assign(n * 3, 0.0);
            baseB.assign(n * 3, 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                for (int k = 0; k < 3; ++k) {
                    baseA[i * 3 + std::size_t(k)] = cyls.O[i * 3 + std::size_t(k)]
                        + cyls.D[i * 3 + std::size_t(k)] * cyls.T0[i];
                    baseB[i * 3 + std::size_t(k)] = cyls.O[i * 3 + std::size_t(k)]
                        + cyls.D[i * 3 + std::size_t(k)] * cyls.T1[i];
                }
            }
            const double tol = 2.0 * DEG_TO_RAD;
            for (int side = 0; side < 2; ++side) {
                const double sgn = side == 0 ? 1.0 : -1.0;
                for (std::size_t i = 0; i < n; ++i) {
                    for (int k = 0; k < 3; ++k) {
                        nrm[i * 3 + std::size_t(k)] = mh[i * 3 + std::size_t(k)] * cg[i]
                            + rot[i * 3 + std::size_t(k)] * (sgn * sg[i]);
                    }
                    phi[i] = npMod(std::atan2(dot3(&nrm[i * 3], &cyls.E2[i * 3]),
                                              dot3(&nrm[i * 3], &cyls.E1[i * 3])), TWO_PI);
                }
                ok.assign(n, 0);
                for (std::size_t i = 0; i < n; ++i) {
                    ok[i] = cyls.FULL[i];
                    // обрезка по аналитической дуге (a0..a1): без неё линия
                    // вылезает за край скругления на плоскую грань
                    const bool part = std::isfinite(cyls.A0[i]);
                    if (part) {
                        const double rel = npMod(phi[i] - cyls.A0[i], TWO_PI);
                        const double span = npMod(cyls.A1[i] - cyls.A0[i], TWO_PI);
                        const bool arcok = (rel <= span + tol) || (rel >= TWO_PI - tol);
                        if (!arcok) {
                            ok[i] = 0;
                        }
                    }
                }
                // генератор, дублирующий стыковое ребро B-rep: полоса шириной
                // ~1.5 мм по радиусу
                for (std::size_t i = 0; i < n; ++i) {
                    if (!ok[i]) {
                        continue;
                    }
                    bool anyDrop = false;
                    for (int k = 0; k < 8; ++k) {
                        if (std::isfinite(cyls.DROP[i * 8 + std::size_t(k)])) {
                            anyDrop = true;
                            break;
                        }
                    }
                    if (!anyDrop) {
                        continue;
                    }
                    const double band = std::max(std::atan(1.5 / std::max(cyls.R[i], 0.05)), 0.025);
                    for (int k = 0; k < 8; ++k) {
                        const double dphi = std::fabs(npMod(phi[i] - cyls.DROP[i * 8 + std::size_t(k)]
                                                            + PI, TWO_PI) - PI);
                        if (std::isfinite(dphi) && dphi < band) {
                            ok[i] = 0;
                            break;
                        }
                    }
                }
                for (std::size_t i = 0; i < n; ++i) {
                    if (!(rp[i] > cyls.R[i] + 1e-6) || !ok[i]) {
                        continue;
                    }
                    double pa[3];
                    double pb[3];
                    for (int k = 0; k < 3; ++k) {
                        pa[k] = baseA[i * 3 + std::size_t(k)]
                            + nrm[i * 3 + std::size_t(k)] * cyls.R[i];
                        pb[k] = baseB[i * 3 + std::size_t(k)]
                            + nrm[i * 3 + std::size_t(k)] * cyls.R[i];
                    }
                    for (int k = 0; k < 3; ++k) {
                        segments.push_back(pa[k]);
                    }
                    for (int k = 0; k < 3; ++k) {
                        segments.push_back(pb[k]);
                    }
                }
            }
        }
    }
    for (const ConeSurface& s : fam.cones) {
        double sd[3] = {s.d[0], s.d[1], s.d[2]};
        const double dn = norm3(sd);
        if (dn <= 1e-12) {
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            sd[k] /= dn;
        }
        const double psi = s.psi;
        const double w[3] = {eyeMm[0] - s.a[0], eyeMm[1] - s.a[1], eyeMm[2] - s.a[2]};
        const double wlen = norm3(w);
        if (wlen < 1e-6 || std::fabs(std::cos(psi)) <= 1e-6) {
            continue;
        }
        const double wh[3] = {w[0] / wlen, w[1] / wlen, w[2] / wlen};
        const double ct = dot3(sd, wh);
        if (std::fabs(ct) >= 1.0) {
            continue;
        }
        const double st = std::sqrt(std::max(1.0 - ct * ct, 1e-12));
        double u1[3];
        for (int k = 0; k < 3; ++k) {
            u1[k] = (wh[k] - sd[k] * ct) / st;
        }
        double u2[3];
        cross3(sd, u1, u2);
        const double cb = ct / std::cos(psi);
        if (std::fabs(cb) > 1.0) {
            continue;
        }
        const double den = std::max(st * std::sin(psi), 1e-9);
        const double cf = (cb - ct * std::cos(psi)) / den;
        if (std::fabs(cf) > 1.0) {
            continue;
        }
        const double sf = std::sqrt(std::max(1.0 - cf * cf, 0.0));
        for (int side = 0; side < 2; ++side) {
            const double sgn = side == 0 ? 1.0 : -1.0;
            double gdir[3];
            for (int k = 0; k < 3; ++k) {
                gdir[k] = sd[k] * std::cos(psi)
                    + (u1[k] * cf + u2[k] * (sgn * sf)) * std::sin(psi);
            }
            double pa[3];
            double pb[3];
            for (int k = 0; k < 3; ++k) {
                pa[k] = s.a[k] + gdir[k] * s.s0;
                pb[k] = s.a[k] + gdir[k] * s.s1;
            }
            for (int k = 0; k < 3; ++k) {
                segments.push_back(pa[k]);
            }
            for (int k = 0; k < 3; ++k) {
                segments.push_back(pb[k]);
            }
        }
    }
    return scratch.segments;
}

std::vector<double> tangentFromFamilies(const TangentFamilies& fam, const double eyeMm[3]) {
    TangentScratch scratch;
    tangentFromFamilies(fam, eyeMm, scratch);
    return std::move(scratch.segments);   // перенос, а не копия: как был возврат segments
}

} // namespace gv
