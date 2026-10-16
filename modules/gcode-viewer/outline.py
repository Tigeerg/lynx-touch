"""Обводка меша: рёбра по двугранному углу + склейка цепочек.

Вынесено из mock_backend.py (этап 0 плана переноса на C++).
"""
import math

from packing import _pack_edges_np, _pack_edge_segments
from gv_types import PackedEdges

from gv_common import _np

np = _np          # код слоя читает numpy как np

_FEATURE_SIMPLIFY_MM = 0.15     # допуск упрощения цепочек контура (см. ниже)


def _rdp_indices(points, tol):
    """Индексы точек, которые остаются после упрощения полилинии (Дуглас-Пекер).

    Итеративный вариант: цепочки рёбер меша насчитывают тысячи точек, рекурсия
    упёрлась бы в предел глубины."""
    n = points.shape[0]
    if n < 2:
        return [0]
    if n == 2:
        return [0, 1]
    keep = {0, n - 1}
    stack = [(0, n - 1)]
    while stack:
        i, j = stack.pop()
        if j <= i + 1:
            continue
        a = points[i]
        ab = points[j] - a
        seg = points[i + 1:j] - a
        ln = math.sqrt(float(ab[0]) ** 2 + float(ab[1]) ** 2 + float(ab[2]) ** 2)
        if ln < 1e-12:
            d = _np.linalg.norm(seg, axis=1)
        else:
            d = _np.linalg.norm(_np.cross(seg, ab), axis=1) / ln
        m = i + 1 + int(_np.argmax(d))
        if float(d[m - i - 1]) > tol:
            keep.add(m)
            stack.append((i, m))
            stack.append((m, j))
    return sorted(keep)


def _simplify_edge_chains(lo_ids, hi_ids, pos, tol_mm):
    """Склеить цепочки смежных рёбер в полилинии и упростить их.

    Отобранные рёбра приходят цепочками субмиллиметровых звеньев (медиана ребра
    Test_model.STL - 0.07 мм), а лента обводки строится по двум концам отрезка:
    на субпиксельном звене её направление - шум, и контур вырождается в бледную
    рябь вместо линии. Цепочка собирается обходом смежности по свареным вершинам
    (развилки и концы - начала цепочек, оставшиеся рёбра - замкнутые петли) и
    упрощается по Дугласу-Пекеру с допуском tol_mm: прямой участок становится
    одним отрезком, дуга - несколькими. Возвращает (lo, hi) - индексы вершин
    упрощённых звеньев."""
    adj = {}
    edges = []
    for a, b in zip(lo_ids.tolist(), hi_ids.tolist()):
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)
        edges.append((a, b))
    used = set()

    def key(v, w):
        return (v, w) if v < w else (w, v)

    def step(chain, first, second):
        prev, cur = first, second
        while True:
            used.add(key(prev, cur))
            chain.append(cur)
            if cur == first or len(adj[cur]) != 2:
                break
            nxt = [x for x in adj[cur] if x != prev and key(cur, x) not in used]
            if not nxt:
                break
            prev, cur = cur, nxt[0]

    chains = []
    for v in list(adj.keys()):
        if len(adj[v]) == 2:
            continue
        for w in list(adj[v]):
            if key(v, w) in used:
                continue
            chain = [v]
            step(chain, v, w)
            chains.append(chain)
    for a, b in edges:                      # замкнутые петли
        if key(a, b) in used or len(adj[a]) != 2:
            continue
        chain = [a]
        step(chain, a, b)
        chains.append(chain)

    out_lo = []
    out_hi = []
    for chain in chains:
        idx = _rdp_indices(pos[chain], tol_mm)
        for i, j in zip(idx[:-1], idx[1:]):
            if j > i:
                out_lo.append(chain[i])
                out_hi.append(chain[j])
    return (_np.asarray(out_lo, dtype=_np.int64),
            _np.asarray(out_hi, dtype=_np.int64))


def _feature_edges(scene, angle_deg=25.0):
    """Return a packed Lines buffer (PackedEdges) for the
    feature-edge outline of a triangle soup.

    Keeps boundary edges and edges whose dihedral angle exceeds `angle_deg`,
    so smooth surfaces don't get a dense wireframe. Отобранные рёбра склеиваются
    в цепочки и упрощаются (_simplify_edge_chains): у мелкого меша контур - это
    звенья в доли миллиметра, на которых лента обводки вырождается. Uses a
    vectorised numpy path (упрощение цепочек делает только numpy-путь). The buffer is built
    directly (no intermediate tuple list)."""
    return _feature_edges_np(scene, angle_deg)


def _feature_edges_np(scene, angle_deg):
    np = _np
    V = np.asarray(scene, dtype=float).reshape(-1, 3)
    F = V.shape[0] // 3
    if F == 0:
        return PackedEdges(b'', 0, None)
    T = V.reshape(F, 3, 3)

    # Unique vertex ids via a quantised grid (0.001 units). Computed once (also
    # reused for the output positions).
    q = np.round(V * 1000.0).astype(np.int64)
    uq, inv = np.unique(q, axis=0, return_inverse=True)
    vid = inv.reshape(F, 3)

    # Face normals
    e1 = T[:, 1] - T[:, 0]
    e2 = T[:, 2] - T[:, 0]
    nrm = np.cross(e1, e2)
    ln = np.linalg.norm(nrm, axis=1)
    ln[ln == 0.0] = 1.0
    nrm = nrm / ln[:, None]

    a, b, c = vid[:, 0], vid[:, 1], vid[:, 2]
    edge = np.concatenate([np.stack([a, b], 1),
                           np.stack([b, c], 1),
                           np.stack([c, a], 1)], 0)
    # `edge` is laid out as [all (a,b)] [all (b,c)] [all (c,a)], so slot k
    # belongs to face (k % F) — np.tile, not np.repeat.
    face = np.tile(np.arange(F), 3)
    lo = edge.min(axis=1)
    hi = edge.max(axis=1)
    key = lo * (int(vid.max()) + 1) + hi
    order = np.argsort(key, kind="stable")
    key_s = key[order]

    uniq, start, counts = np.unique(key_s, return_index=True, return_counts=True)
    keep_lo = []
    keep_hi = []

    # Boundary edges (one adjacent face)
    bnd = counts == 1
    if bnd.any():
        e = edge[order][start[bnd]]
        keep_lo.append(e[:, 0])
        keep_hi.append(e[:, 1])

    # Non-manifold edges (more than two faces): keep them all
    nm = counts > 2
    if nm.any():
        e = edge[order][start[nm]]
        keep_lo.append(e[:, 0])
        keep_hi.append(e[:, 1])

    # Manifold edges: keep when the dihedral angle exceeds the threshold
    two = counts == 2
    if two.any():
        s = start[two]
        f0 = face[order][s]
        f1 = face[order][s + 1]
        dot = np.abs(np.einsum("ij,ij->i", nrm[f0], nrm[f1]))
        thr = math.cos(math.radians(angle_deg))
        sel = dot < thr
        if sel.any():
            e = edge[order][s[sel]]
            keep_lo.append(e[:, 0])
            keep_hi.append(e[:, 1])

    if not keep_lo:
        return PackedEdges(b'', 0, None)
    u = uq.astype(float) / 1000.0
    lo_ids, hi_ids = _simplify_edge_chains(np.concatenate(keep_lo),
                                           np.concatenate(keep_hi), u,
                                           _FEATURE_SIMPLIFY_MM)
    if lo_ids.size == 0:
        return PackedEdges(b'', 0, None)
    return _pack_edges_np(u[lo_ids], u[hi_ids])

