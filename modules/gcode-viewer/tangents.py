"""Аналитические касательные контура: семейства цилиндров и конусов.

Вынесено из mock_backend.py (этап 0 плана переноса на C++). Оставлено для
сравнения с силуэтом по мешу (переменная окружения CNC_SILHOUETTE=analytic).
"""
import math

from gv_common import _np
from gv_types import ConeSurface, CylinderFamilies, CylinderSurface, TangentFamilies

np = _np          # код слоя читает numpy как np

def _pt_tri_dist2(p, tri):
    """Squared distance from point p to each triangle in tri (K,3,3).
    Interior uses the normal-equation projection; outside falls back to
    the nearest vertex (a slight overestimate, conservative here)."""
    a = tri[:, 0]
    e1 = tri[:, 1] - a
    e2 = tri[:, 2] - a
    v = p - a
    a11 = _np.einsum("ij,ij->i", e1, e1)
    a12 = _np.einsum("ij,ij->i", e1, e2)
    a22 = _np.einsum("ij,ij->i", e2, e2)
    d1 = _np.einsum("ij,ij->i", e1, v)
    d2 = _np.einsum("ij,ij->i", e2, v)
    det = a11 * a22 - a12 * a12
    det = _np.where(_np.abs(det) < 1e-12, 1.0, det)
    s = (a22 * d1 - a12 * d2) / det
    t = (a11 * d2 - a12 * d1) / det
    inside = (s >= 0.0) & (t >= 0.0) & (s + t <= 1.0)
    q = a + e1 * s[:, None] + e2 * t[:, None]
    d2i = ((p - q) ** 2).sum(axis=1)
    dv = _np.minimum.reduce([((p - a) ** 2).sum(axis=1),
                             ((p - tri[:, 1]) ** 2).sum(axis=1),
                             ((p - tri[:, 2]) ** 2).sum(axis=1)])
    return _np.where(inside, d2i, dv)


_CONE_MIN_SLANT_MM = 2.5     # короче - фаска/раззенковка, не силуэт отверстия
_CONE_PROBE_AZIMUTHS = 8     # азимутов пробы вогнутости на обороте конуса


def _cone_keeps_silhouette(cone, skin_tris):
    """Политика отбора силуэтов конусов - та же, что у цилиндров.

    Конус получает касательные образующие, если он (1) не короче
    _CONE_MIN_SLANT_MM по образующей и (2) вогнутый: нормали кожи у его
    поверхности смотрят к оси (конус на дне отверстия - кончик сверла).
    45-градусные фаски кромок (скос 0.7 мм) и выпуклые конусы дают пару
    образующих, вращающуюся вслед за камерой в 0.35-1.5 мм от статического
    ребра перехода - то самое «призрачное» удвоение. Без кожи (skin_tris is
    None) проверка вогнутости пропускается, остаётся порог длины."""
    slant = abs(float(cone.s1) - float(cone.s0))
    if slant < _CONE_MIN_SLANT_MM:
        return False
    if skin_tris is None:
        return True
    sktri, skcen, skirm = skin_tris
    d = _np.array(cone.d, dtype=float)
    d = d / max(float(_np.linalg.norm(d)), 1e-12)
    apex = _np.array(cone.a, dtype=float)
    psi = float(cone.psi)
    # любые два ортогональных к оси орта: проба идёт по обороту конуса
    ref = _np.array([0.0, 0.0, 1.0]) if abs(d[2]) < 0.9 else _np.array([1.0, 0.0, 0.0])
    u1 = ref - d * float(d @ ref)
    u1 /= max(float(_np.linalg.norm(u1)), 1e-12)
    u2 = _np.cross(d, u1)
    s_mid = (float(cone.s0) + float(cone.s1)) / 2.0
    side = 1.0 if s_mid >= 0.0 else -1.0       # вторая полость конуса: радиус наружу меняет знак
    try:
        for k in range(_CONE_PROBE_AZIMUTHS):
            ph = 2.0 * math.pi * k / _CONE_PROBE_AZIMUTHS
            rad = (u1 * math.cos(ph) + u2 * math.sin(ph)) * side
            p = apex + (d * math.cos(psi) + rad * side * math.sin(psi)) * s_mid
            w = skcen - p
            dd = _np.einsum("ij,ij->i", w, w)
            cand = _np.argpartition(dd, 15)[:16]
            dv = _pt_tri_dist2(p, sktri[cand])
            keep = cand[dv < 1.5]
            if not keep.size:
                continue                        # проба упала мимо грани
            dots = skirm[keep] @ rad
            own = int(_np.count_nonzero(dots < -0.05))
            other = int(_np.count_nonzero(dots > 0.05))
            if own > 0 and own >= other:
                return True
    except Exception:
        return True                             # проверка не удалась: не отбрасываем
    return False


def _dedupe_cones(cones):
    """Склейка совпадающих конических поверхностей.

    Одна геометрия конуса в STEP кроится несколькими face TShape (seam-
    разделение): дескрипторы совпадают побитово, и без склейки каждая
    образующая рисуется дважды в одной точке - ленты накладываются
    треугольник-в-треугольник и z-fight'ят. Ключ: вершина, каноническое
    направление оси (первая значимая компонента > 0, при флипе знака оси
    диапазон образующей отражается), полуугол и срез s; округление до
    1e-3/1e-4 ловит и дескрипторы, различающиеся только параметризацией."""
    out = []
    seen = set()
    for c in cones:
        d = _np.array(c.d, dtype=float)
        d = d / max(float(_np.linalg.norm(d)), 1e-12)
        sig = [j for j in range(3) if abs(float(d[j])) > 1e-4]
        if sig and float(d[sig[0]]) < 0.0:
            d = -d
            s0, s1 = -float(c.s1), -float(c.s0)
        else:
            s0, s1 = float(c.s0), float(c.s1)
        key = (tuple(round(float(v), 3) for v in c.a),
               tuple(round(float(v), 4) for v in d),
               round(float(c.psi), 4),
               tuple(sorted((round(s0, 3), round(s1, 3)))))
        if key in seen:
            continue
        seen.add(key)
        out.append(c)
    return out


def _tangent_families(surfs, packed_edges=None, skin=None):
    """Precompute logical cylinders (axis, radius, family basis, coverage
    grid, axial union) as flat arrays + a small cone list. Built once per
    model load so the per-camera-move pass is a handful of numpy calls on
    ~O(150) cylinders (<1 ms); the old per-family Python loop cost ~14 ms
    per mouse-move event and tanking the orbit framerate.
    packed_edges (the STEP outline pack, if known) marks generator angles
    that already coincide with a real B-rep junction line: the per-frame
    pass drops those (within ~1.3 deg) so a fillet boundary is never drawn
    twice as an edge + an offset silhouette twin. skin=(verts, normals)
    feeds the cone concavity policy (_cone_keeps_silhouette)."""
    skcen = skirm = sktri = None
    skin_tris = None
    if skin is not None and skin[0] is not None \
            and skin[1] is not None:
        try:
            vv = _np.asarray(skin[0], dtype=float).reshape(-1, 3, 3)
            sktri = vv
            skcen = vv.mean(axis=1)
            skirm = _np.asarray(skin[1], dtype=float).reshape(-1, 3, 3)[:, 0, :]
            skin_tris = (sktri, skcen, skirm)
        except Exception:
            sktri = skcen = skirm = None
            skin_tris = None
    cyls = {}
    cones = []
    for s in surfs:
        if not isinstance(s, CylinderSurface):
            cones.append(s)
            continue
        d = _np.array(s.d, dtype=float)
        dn = float(_np.linalg.norm(d))
        if dn <= 1e-12:
            continue
        d = d / dn
        o = _np.array(s.o, dtype=float)
        o_perp = o - d * float(d @ o)
        # lexicographically canonical axis direction - the old rule missed
        # Z-parallel lines ((0,0,±1) kept both signs), so one cylinder split
        # into two families and drew a doubled, slightly offset tangent pair
        dref = d
        for comp in (d[0], d[1], d[2]):
            if abs(comp) > 1e-9:
                if comp < 0:
                    dref = -d
                break
        key = (tuple(_np.round(o_perp, 1)), tuple(_np.round(dref, 2)),
               round(float(s.r), 2))
        cyls.setdefault(key, []).append(s)
    O = []; D = []; E1 = []; E2 = []; FULL = []; R = []
    T0 = []; T1 = []; ARCS = []; A0 = []; A1 = []
    for (oq, dd, r), faces in cyls.items():
        # the canonical key direction, not faces[0]: members merged by the
        # lexicographic rule may point both ways along the axis line
        d = _np.array(dd, dtype=float)
        d = d / max(float(_np.linalg.norm(d)), 1e-12)
        # exact axis foot and radius, not the rounded key: a key rounded to
        # 0.1 mm slid every generator off the true axis, visibly offset from
        # the static junction edges it is supposed to coincide with
        feet = [_np.array(s.o, dtype=float)
                - d * float(d @ _np.array(s.o, dtype=float)) for s in faces]
        o = sum(feet) / len(feet)
        e10 = _np.array(faces[0].e1, dtype=float)
        e20 = _np.array(faces[0].e2, dtype=float)
        full = False
        for s in faces:
            a0, a1 = s.arc
            if a0 is None:
                full = True
                break
            if (a1 - a0) % (2 * math.pi) >= 2 * math.pi - 1e-6:
                full = True
                break
        # axial clusters: distinct bores on one axis line with the same
        # radius must NOT merge into one long tangent spanning empty space
        spans = []
        tbs = []
        for s in faces:
            lo, hi = float(s.t0), float(s.t1)
            # t0/t1 are measured along the face's own axis: a member whose
            # axis opposes the canonical direction has its span mirrored
            # (tangents floated past the bore - the "shifted duplicates")
            if float(d @ _np.array(s.d, dtype=float)) < 0.0:
                lo, hi = -hi, -lo
            c = float(d @ (_np.array(s.o, dtype=float) - o))
            tbs.append((c + lo, c + hi))
        for tb in sorted(tbs):
            if spans and tb[0] <= spans[-1][1] + 1e-6:
                spans[-1][1] = max(spans[-1][1], tb[1])
            else:
                spans.append([tb[0], tb[1]])
        rrep = float(sum(float(s.r) for s in faces) / len(faces))
        # Политика силуэта: view-зависимая образующая - силуэт тела вращения,
        # её получают и вогнутые стенки (отверстия), и выпуклые наружные
        # галтели (скругления). Порог arc >= 85 гр и пролёт >= 2.5 мм оставляют
        # силуэт только у настоящих тел вращения (у 45-гр фасок и мелких лунок
        # образующей нет). Тангенциальные кромки галтелей рисуются статикой
        # (это вертикальные рёбра детали), поэтому у скругления могут стоять
        # рядом образующая и стыковая кромка - это силуэт и кромка, не дубль.
        wmax = 0.0
        for s in faces:
            a0b, a1b = s.arc
            if a0b is None:
                wmax = 2 * math.pi
                break
            wmax = max(wmax, (float(a1b) - float(a0b)) % (2 * math.pi))
        tangented = full or wmax >= math.radians(85)
        # absolute depth floor: a recess shallower than 2.5 mm is a
        # spot-drill dash whose rotating pair reads as an artifact, and its
        # visible boundary is the rim circle (static edge); a real
        # counterbore (5 mm+) keeps its silhouette regardless of radius
        if tangented:
            for lo, hi in spans:
                if (hi - lo) < 2.5:
                    tangented = False
                    break
        # азимутальное покрытие из АНАЛИТИЧЕСКОЙ дуги грани, а не из меша:
        # меш соседнего отверстия давал лишние корзины (fam3 получал 118 гр
        # вместо 90 гр), образующая вылезала за дугу на плоскость рядом с
        # отверстием - «фантомная линия».
        abins = _np.zeros(64, dtype=bool)
        fa0 = math.nan; fa1 = math.nan
        for s in faces:
            a0b, a1b = s.arc
            if a0b is None:
                abins[:] = True
                break
            spn = (float(a1b) - float(a0b)) % (2 * math.pi)
            if spn >= 2 * math.pi - 1e-6:
                abins[:] = True
                break
            nb = max(1, int(round(spn / (2 * math.pi) * 64)))
            for k in range(nb):
                ang = (float(a0b) + (k + 0.5) / nb * spn) % (2 * math.pi)
                abins[int(ang / (2 * math.pi) * 64) % 64] = True
            fa0 = float(a0b); fa1 = float(a0b) + spn
        for lo, hi in spans:
            O.append(o); D.append(d); E1.append(e10); E2.append(e20)
            FULL.append(tangented)
            R.append(rrep); T0.append(lo); T1.append(hi)
            ARCS.append(abins.copy()); A0.append(fa0); A1.append(fa1)
    # coalesce families on one axis line: modeling noise splits one bore into
    # two keys -> two almost-coincident tangent pairs; merge only equal
    # radii, a genuine step (spotface over pilot) keeps its own silhouette
    # per section at the true wall radius - the envelope merge floated the
    # line 1-1.25 mm off the pilot wall ('offset from the true boundary')
    Oa, Da, Ea, Eb, Fa, Ra, Ta, Tb, ARCSa, A0a, A1a = (
        O, D, E1, E2, FULL, R, T0, T1, ARCS, A0, A1)

    def _union_a(a, b, c, dd):
        if not (math.isfinite(a) and math.isfinite(c)):
            return math.nan, math.nan
        return min(a, c), max(b, dd)

    merged = []
    for i in range(len(Oa)):
        placed = False
        for m in merged:
            mi = m[0]
            if abs(Ra[i] - Ra[mi]) > 0.05:
                continue
            if abs(float(Da[i] @ Da[mi])) < 0.999:
                continue
            gap = float(_np.linalg.norm(Oa[i] - Oa[mi]
                                        - Da[mi] * float(Da[mi] @ (Oa[i] - Oa[mi]))))
            if gap > 0.4:
                continue
            # spans must touch or overlap to share one tangent line
            if Tb[mi] + 0.5 < Ta[i] or Tb[i] + 0.5 < Ta[mi]:
                continue
            m[1] = min(m[1], Ta[i]); m[2] = max(m[2], Tb[i])
            m[3] = m[3] or Fa[i]
            m[4] = m[4] | ARCSa[i]
            m[5], m[6] = _union_a(m[5], m[6], A0a[i], A1a[i])
            placed = True
            break
        if not placed:
            merged.append([i, Ta[i], Tb[i], Fa[i], ARCSa[i].copy(),
                           A0a[i], A1a[i]])
    if merged and len(merged) < len(Oa):
        keep = [m[0] for m in merged]
        O2 = [Oa[k] for k in keep]; D2 = [Da[k] for k in keep]
        E12 = [Ea[k] for k in keep]; E22 = [Eb[k] for k in keep]
        F2 = [m[3] for m in merged]
        A2 = [m[4] for m in merged]
        B0 = [m[5] for m in merged]; B1 = [m[6] for m in merged]
        R2 = [Ra[k] for k in keep]
        T02 = [m[1] for m in merged]; T12 = [m[2] for m in merged]
        O, D, E1, E2, FULL, R, T0, T1 = (O2, D2, E12, E22, F2, R2, T02, T12)
        ARCS = A2; A0 = B0; A1 = B1
    # generator angles that duplicate an existing static (B-rep) junction
    # line: same direction as the axis, same radius, axially overlapping.
    DROP = _np.full((len(O), 8), math.nan)
    if packed_edges is not None and O:
        raw = packed_edges.raw
        nv = packed_edges.nv
        if raw and nv >= 2:
            v = _np.frombuffer(raw, dtype=_np.float32).reshape(-1, 5)
            SA, SB = v[0::2, 0:3].astype(float), v[1::2, 0:3].astype(float)
            SD = SB - SA
            SL = _np.linalg.norm(SD, axis=1)
            good = SL > 1e-6
            SA, SB = SA[good], SB[good]
            SD = SD[good] / SL[good][:, None]
            Oa = _np.array(O); Da = _np.array(D); Ea = _np.array(E1)
            Eb2 = _np.array(E2); Ra = _np.array(R)
            T0a = _np.array(T0); T1a = _np.array(T1)
            two_pi = 2.0 * math.pi
            for i in range(Oa.shape[0]):
                u = Da[i]
                par = _np.nonzero(_np.abs(SD @ u) > 0.9993)[0]
                if par.size == 0:
                    continue
                w = SA[par] - Oa[i]
                perp = w - u * (w @ u)[:, None]
                rr = _np.linalg.norm(perp, axis=1)
                # 0.35 мм: только рёбра, лежащие на этом цилиндре. Шире
                # нельзя: в коаксиальной группе захватывался шов соседней
                # ступени (зенковка 3.42 рядом с пилотом 2.75) и генератор
                # пилота нырял у чужого шва (drift 51). Обрезанные дуги
                # (скругления, арки) теперь без генераторов вовсе — см.
                # гейт покрытия ARC.
                near = _np.abs(rr - Ra[i]) < 0.35
                if not near.any():
                    continue
                sel = _np.nonzero(near)[0]
                jj = par[sel]
                pp = perp[sel]
                lo2 = _np.minimum((SA[jj] - Oa[i]) @ u, (SB[jj] - Oa[i]) @ u)
                hi2 = _np.maximum((SA[jj] - Oa[i]) @ u, (SB[jj] - Oa[i]) @ u)
                ov = _np.minimum(hi2, T1a[i]) - _np.maximum(lo2, T0a[i])
                rr2 = rr[sel]
                use = (ov > 2.0) & (rr2 > 1e-6)
                if not use.any():
                    continue
                ang = _np.arctan2(pp[use] @ Eb2[i], pp[use] @ Ea[i])
                ang = _np.sort(ang % two_pi)
                uniq = []
                for a in ang.tolist():
                    if not uniq or a - uniq[-1] > 0.03:
                        uniq.append(a)
                # wrap-around copy of the same junction (0.001 and 6.283):
                # the runtime compares circularly, keep only the first
                if len(uniq) > 1 and uniq[0] + two_pi - uniq[-1] < 0.03:
                    uniq.pop()
                DROP[i, :min(len(uniq), 8)] = uniq[:8]
    O_arr = _np.array(O, dtype=float).reshape(-1, 3)
    D_arr = _np.array(D, dtype=float).reshape(-1, 3)
    E1_arr = _np.array(E1, dtype=float).reshape(-1, 3)
    E2_arr = _np.array(E2, dtype=float).reshape(-1, 3)
    FULL_arr = _np.array(FULL, dtype=bool)
    R_arr = _np.array(R, dtype=float)
    T0_arr = _np.array(T0, dtype=float)
    T1_arr = _np.array(T1, dtype=float)
    # coaxial sections (stepped bores) must share one tangent azimuth: each
    # cylinder's own eye-tangent angle differs by ~0.2 deg, so a step like
    # R=3.42 over R=2.75 drew its contour as two offset stubs - the "doubled,
    # shifted lines that rotate with the camera". Reference = largest radius
    # on the same axis line; sections render placed on their own radius but
    # at the group azimuth, making the outline one straight line per side.
    Rr = R_arr.copy()
    if O_arr.shape[0]:
        PD = _np.abs(D_arr @ D_arr.T)
        wo = O_arr[None, :, :] - O_arr[:, None, :]
        alo = _np.einsum("ijk,jk->ij", wo, D_arr)
        pfo = wo - D_arr[:, None, :] * alo[:, :, None]
        gap = _np.linalg.norm(pfo, axis=2)
        coax = (PD > 0.999) & (gap < 0.4)
        coax |= _np.eye(O_arr.shape[0], dtype=bool)
        Rr = _np.where(coax, R_arr[None, :], -1.0).max(axis=1)
    # Азимутальное покрытие цилиндра. Меш кожи даёт честные 360 гр у
    # сквозного отверстия, но у скругления рядом с отверстием ловит лишние
    # корзины (118 гр вместо 90 гр) - образующая вылезает за дугу на плоскость
    # («фантом»). Поэтому маску по мешу ОГРАНИЧИВАЕМ аналитической дугой грани
    # (ARCS), но только у частичных дуг: полное отверстие (меш ~64 корзины)
    # оставляем как есть - его полу-грани дают одинаковые 180-гр дуги.
    ARC = _np.zeros((O_arr.shape[0], 64), dtype=bool)
    if sktri is not None and O_arr.shape[0]:
        two_pi = 2.0 * math.pi
        for i in range(O_arr.shape[0]):
            if not FULL_arr[i]:
                continue
            u = D_arr[i]
            o = O_arr[i]
            e1 = E1_arr[i]
            e2 = E2_arr[i]
            Ri = R_arr[i]
            lo, hi = T0_arr[i], T1_arr[i]
            wv = sktri - o[None, None, :]
            al = _np.einsum("mjk,k->mj", wv, u)
            pp = wv - u[None, None, :] * al[:, :, None]
            rr = _np.linalg.norm(pp, axis=2)
            ontri = (_np.abs(rr - Ri) < 0.25).all(axis=1) \
                & (al.min(axis=1) > lo - 0.5) & (al.max(axis=1) < hi + 0.5)
            if ontri.any():
                cen = pp[ontri].mean(axis=1)
                ang = _np.arctan2(cen @ e2, cen @ e1) % two_pi
                ARC[i, _np.unique((ang / two_pi * 64).astype(int) % 64)] = True
        for i in range(O_arr.shape[0]):
            if not FULL_arr[i]:
                continue
            m = ARC[i]
            if m.sum() >= 56:
                continue                    # полное отверстие - не режем
            an = ARCS[i] if i < len(ARCS) else None
            if an is not None and an.sum() >= 4:
                cut = m & an
                ARC[i] = cut if cut.any() else an
            # слишком редкую маску не форсим в полную: честная дуга режет
            # образующую по границе скругления (иначе был «фантомный» край)
    # непрерывная дуга (a0, a0+span) для точной обрезки на скользящих углах;
    # полные отверстия (маска ~360 гр) помечаем NaN - их не режем
    a0 = _np.array(A0, dtype=float); a1 = _np.array(A1, dtype=float)
    fullm = ARC.sum(axis=1) >= 56
    a0[fullm] = _np.nan; a1[fullm] = _np.nan
    # Примечание: полный генератор НЕ запрещается обрезанным дугам -
    # гейт по покрытию убирал и законный контур скруглений. Рельсовый
    # culling по корзинам ARC ниже в рантайме: линия живёт, пока её
    # азимут внутри дуги, и сходит за её край в стыковое ребро.
    families = CylinderFamilies(O=O_arr, D=D_arr, E1=E1_arr, E2=E2_arr,
                                FULL=FULL_arr, DROP=DROP, R=R_arr, T0=T0_arr,
                                T1=T1_arr, RREF=Rr, ARC=ARC, A0=a0, A1=a1)
    cones = _dedupe_cones([c for c in cones
                           if _cone_keeps_silhouette(c, skin_tris)])
    return TangentFamilies(families, cones)


def _empty_families():
    """Пустые цилиндрические массивы: силуэт только по конусам (проверки)."""
    e = _np.empty((0,), dtype=float)
    return CylinderFamilies(O=e, D=e, E1=e, E2=e, FULL=e, DROP=e, R=e,
                            T0=e, T1=e, RREF=e, ARC=e, A0=e, A1=e)


def _tangent_from_families(fam, E):
    """Vectorized per-frame pass over precomputed families (see
    _tangent_families): both tangent sides of every logical cylinder/cone are
    emitted for any camera angle - hole and bore silhouettes are always
    present, never censored per arc (that made them flicker/appear
    intermittently), and they are drawn in the occlusion-free edgeView
    layer. Cones keep the scalar path."""
    cyls = fam.cylinders
    cones = fam.cones
    segments = []
    O, D, R = cyls.O, cyls.D, cyls.R
    Ev = _np.asarray(E, dtype=float)
    if O.shape[0]:
        w = Ev[None, :] - O
        along = _np.einsum("ij,ij->i", D, w)
        perp = w - D * along[:, None]
        Rp = _np.linalg.norm(perp, axis=1)
        valid = Rp > R + 1e-6
        if valid.any():
            # tangent azimuth from the coaxial group's largest radius so all
            # sections of one stepped bore line up on a single outline per
            # side; fall back to the section's own radius when the eye sits
            # inside the reference cylinder (reference tangent undefined)
            Rref = cyls.RREF
            Rp_ = _np.maximum(Rp, 1e-9)
            Rang = _np.where(Rp > Rref, Rref, R)
            cg = _np.clip(Rang / Rp_, -1.0, 1.0)                # cos(gamma)
            sg = _np.sqrt(_np.maximum(1.0 - cg * cg, 0.0))
            mh = perp / _np.maximum(Rp, 1e-9)[:, None]
            rot = _np.cross(D, mh)
            e1, e2 = cyls.E1, cyls.E2
            full = cyls.FULL
            drop = cyls.DROP
            anydrop = _np.isfinite(drop).any(axis=1)
            T0, T1 = cyls.T0, cyls.T1
            base_a = O + D * T0[:, None]
            base_b = O + D * T1[:, None]
            for sgn in (1.0, -1.0):
                n = mh * cg[:, None] + rot * (sgn * sg)[:, None]
                phi = _np.arctan2(_np.einsum("ij,ij->i", n, e2),
                                  _np.einsum("ij,ij->i", n, e1)) % (2 * math.pi)
                # малые секции ступенчатого отверстия остаются на СВОЁМ
                # радиусе (силуэт пилота лежит на стенке пилота - иначе
                # линия «отъезжает» от реального контура), но азимут у
                # всей коаксиальной группы общий (RREF выше): 0.2-градусный
                # разброс своих азимутов и давал «смещённые дубли»
                # only full circles get view tangents; a censored family is a
                # fillet/pad whose own silhouette slides over its arc as the
                # camera orbits - those were the "ghost lines that rotate
                # with the camera", offset 0.35-1.5 mm from a real static
                # junction edge. SW "shaded with edges" draws no view-
                # dependent tangents on fillets: the plane<->round transitions
                # there are B-rep edges already present in partEdges.
                ok = full.copy()
                # генератор допустим только там, где на цилиндре реально
                # есть материал (см. ARC в _tangent_families): вне дуги
                # скругления силуэт «вылазит» на плоскую грань
                # точная корзина без запаса: запас +/-1 (~1 мм на R10)
                # позволял генератору вылезать за край дуги на плоскость —
                # «сдвоенная вертикаль» у скругления. Без запаса линия
                # обрывается ровно в стыковом ребре
                # непрерывная обрезка по аналитической дуге (a0..a1) с малым
                # допуском: корзиночная маска теряла до 5.6 гр на границе и
                # контур скругления пропадал на скользящих углах раньше времени
                aa0 = cyls.A0
                arcok = _np.ones(len(phi), dtype=bool)
                part = _np.isfinite(aa0)
                rel = (phi - aa0) % (2.0 * math.pi)
                span = (cyls.A1 - aa0) % (2.0 * math.pi)
                tol = math.radians(2.0)
                arcok = (~part) | (rel <= span + tol) \
                    | (rel >= 2.0 * math.pi - tol)
                ok &= arcok
                drows = _np.nonzero(ok & anydrop)[0]
                if drows.size:
                    dphi = phi[drows][:, None] - drop[drows]
                    dphi = _np.abs((dphi + math.pi) % (2 * math.pi) - math.pi)
                    # band scales with radius so a view tangent never draws a
                    # twin line closer than ~1.5 mm to the junction edge it
                    # slides past; wider bands ate neighbours in coaxial
                    # groups (см. допуск сбора DROP выше)
                    band = _np.maximum(_np.arctan(1.5 / _np.maximum(R[drows], 0.05)), 0.025)
                    bad = _np.any(_np.isfinite(dphi) & (dphi < band[:, None]), axis=1)
                    ok[drows[bad]] = False
                keep = _np.nonzero(valid & ok)[0]
                if keep.size:
                    a = base_a[keep] + n[keep] * R[keep][:, None]
                    b = base_b[keep] + n[keep] * R[keep][:, None]
                    segments.extend((tuple(p), tuple(q)) for p, q in zip(a, b))
    for s in cones:
        sd = _np.array(s.d, dtype=float)
        dn = float(_np.linalg.norm(sd))
        if dn <= 1e-12:
            continue
        sd = sd / dn
        A = _np.array(s.a, dtype=float)
        psi = float(s.psi)
        w = Ev - A
        W = float(_np.linalg.norm(w))
        if W < 1e-6 or abs(math.cos(psi)) <= 1e-6:
            continue
        wh = w / W
        ct = float(sd @ wh)
        if abs(ct) >= 1.0:
            continue
        st = math.sqrt(max(1.0 - ct * ct, 1e-12))
        u1 = (wh - sd * ct) / st
        u2 = _np.cross(sd, u1)
        cb = ct / math.cos(psi)
        if abs(cb) > 1.0:
            continue
        den = max(st * math.sin(psi), 1e-9)
        cf = (cb - ct * math.cos(psi)) / den
        if abs(cf) > 1.0:
            continue
        sf = math.sqrt(max(1.0 - cf * cf, 0.0))
        for sign in (1.0, -1.0):
            gdir = (sd * math.cos(psi)
                    + (u1 * cf + u2 * (sign * sf)) * math.sin(psi))
            a = A + gdir * float(s.s0)
            b = A + gdir * float(s.s1)
            segments.append((tuple(a), tuple(b)))
    return segments
