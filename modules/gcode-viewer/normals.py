"""Crease-aware нормали кожи: сварка вершин и кластеризация по углу.

Вынесено из mock_backend.py (этап 0 плана переноса на C++). Ориентацию оболочки
задаёт вызывающий (step_import), поэтому пофейсового разворота здесь нет.
"""
import math

from gv_common import _np

np = _np          # код слоя читает numpy как np

def _smooth_face_normals(T, crease_deg=20.0):
    """Per-triangle-vertex crease-aware smooth normals (vectorised).

    Each welded vertex seeds from its largest-area adjacent face and averages
    (area-weighted) only adjacent normals within `crease_deg` of the seed:
    planar faces stay exactly planar (a larger group angle was tilting the
    vertices beside tangent fillets - the "fan" streaks radiating from holes
    on top faces), while moderately curved regions shade continuously.
    Fillet facet steps (~23 deg) are irrelevant downstream: the edge
    detector's pow() response + 0.55 threshold silently ignore them.
    Returns (F,3,3) float32. Ориентация берётся из обхода треугольников:
    оболочка разворачивается наружу один раз на модель
    (_orient_shell_outward)."""
    np = _np
    F = T.shape[0]
    fn = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    # Ориентацию задаёт вызывающий (см. _orient_shell_outward): пофейсовый
    # разворот по центроиду выворачивал нормали вогнутых участков внутрь.
    area = np.linalg.norm(fn, axis=1)
    ok = area > 1e-12
    face_n = np.empty((F, 3), dtype=np.float64)
    face_n[ok] = fn[ok] / area[ok][:, None]
    face_n[~ok] = (0.0, 0.0, 1.0)
    q = np.round(T * 1000.0).astype(np.int64)
    _u, inv = np.unique(q.reshape(-1, 3), axis=0, return_inverse=True)
    s_vid = inv.reshape(-1)                       # (3F,) welded vertex id
    s_face = np.repeat(np.arange(F), 3)
    s_n = face_n[s_face]                          # (3F,3) face normal per slot
    s_w = np.repeat(area, 3)
    order = np.argsort(s_vid, kind="stable")
    sv = s_vid[order]
    # Group starts by explicit boundary scan: np.unique(sv, return_index=True)
    # returns first-occurrence indices [0,1,2,...] on NumPy 2.5.3 for this
    # sorted array with many repeats (verified against a manual diff scan),
    # and silently lies; flatnonzero on the sorted diffs is exact.
    starts = np.flatnonzero(np.diff(sv, prepend=-1))
    ends = np.append(starts[1:], sv.shape[0])
    cos_lim = math.cos(math.radians(crease_deg))
    out = np.empty((s_vid.shape[0], 3), dtype=np.float64)
    # Per welded vertex: greedy clustering of adjacent face normals by the
    # crease angle, starting from the largest-area face. Each face takes the
    # weighted average of ITS OWN cluster - tiny planar slivers beside holes
    # therefore keep the plate normal instead of inheriting a bigger wall
    # facet's tilt (that seed-of-largest artifact made "fans" radiating from
    # hole rims on flat faces).
    # Groups of the same size are clustered together in one batch (the
    # per-group Python loop cost 0.79 s over 44.6k groups on the Test_model
    # soup; the batches run the same float ops in the same order, verified
    # bit-identical by test_normals_golden.py):
    gs = ends - starts
    for m in range(1, int(gs.max()) + 1):
        rows = np.nonzero(gs == m)[0]
        if not rows.size:
            continue
        idx = starts[rows][:, None] + np.arange(m)[None, :]
        gg = order[idx]                               # (G, m) slot ids
        gnn = s_n[gg]                                 # (G, m, 3)
        gw = s_w[gg]                                  # (G, m)
        perm = np.argsort(-gw, axis=1, kind="stable")
        gg = np.take_along_axis(gg, perm, 1)
        gnn = np.take_along_axis(gnn, perm[:, :, None], 1)
        gw = np.take_along_axis(gw, perm, 1)
        free = np.ones(gw.shape, dtype=bool)
        res = np.empty_like(gnn)
        for i in range(m):
            active = free[:, i]
            if not active.any():
                continue
            dots = np.einsum("gkd,gd->gk", gnn, gnn[:, i, :])
            sel = free & (dots >= cos_lim) & active[:, None]
            # zeros for unselected slots keep the ascending sum order of the
            # original per-group loop (x + 0.0 adds are exact)
            avg = np.where(sel[..., None], gnn * gw[..., None], 0.0).sum(axis=1)
            ln = np.linalg.norm(avg, axis=1)
            good = ln > 1e-9
            vals = np.where(good[:, None], avg / np.where(good, ln, 1.0)[:, None],
                            gnn[:, i, :])
            res = np.where(sel[..., None], vals[:, None, :], res)
            free = np.where(sel, False, free)
        out[gg.reshape(-1)] = res.reshape(-1, 3)
    return out.astype(np.float32).reshape(F, 3, 3)
