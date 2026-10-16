"""Упаковка отрезков и дуг в буферы геометрии.

Вынесено из mock_backend.py (этап 0 плана переноса на C++).
"""
import math
from array import array

from gv_common import _np

np = _np          # код слоя читает numpy как np

def _pack_edges_np(p0, p1):
    """Pack edge endpoints (M,3) float arrays into a Lines buffer
    (position 3f + texcoord 2f, fully visible). Returns (bytes, nv, bounds)."""
    np = _np
    m = p0.shape[0]
    buf = np.empty((m, 2, 5), dtype=np.float32)
    buf[:, 0, 0:3] = p0
    buf[:, 1, 0:3] = p1
    buf[:, :, 3] = 1.0
    buf[:, :, 4] = 0.0
    allp = np.concatenate([p0, p1], axis=0)
    lo = allp.min(axis=0)
    hi = allp.max(axis=0)
    return (buf.tobytes(), m * 2,
            (float(lo[0]), float(lo[1]), float(lo[2]),
             float(hi[0]), float(hi[1]), float(hi[2])))


def packed_positions(packed):
    """Extract (M,2,3) endpoint positions from a packed Lines buffer."""
    raw, nv, _bounds = packed
    if not raw or nv < 2 or _np is None:
        return None
    f = _np.frombuffer(raw, dtype=_np.float32)
    stride = f.size // (nv // 2 * 2) if nv else 5
    return f.reshape(-1, 2, stride)[:, :, 0:3].copy()


def _pack_edge_segments(segments):
    """Pack a list of (p0, p1) segments into a Lines buffer (fallback path)."""
    if not segments:
        return (b'', 0, None)
    if _np is not None:
        arr = _np.asarray(segments, dtype=float)          # (M, 2, 3)
        return _pack_edges_np(arr[:, 0], arr[:, 1])
    flat = array('f')
    ap = flat.append
    lo = [math.inf, math.inf, math.inf]
    hi = [-math.inf, -math.inf, -math.inf]
    for a, b in segments:
        for p in (a, b):
            ap(p[0]); ap(p[1]); ap(p[2]); ap(1.0); ap(0.0)
            if p[0] < lo[0]: lo[0] = p[0]
            if p[1] < lo[1]: lo[1] = p[1]
            if p[2] < lo[2]: lo[2] = p[2]
            if p[0] > hi[0]: hi[0] = p[0]
            if p[1] > hi[1]: hi[1] = p[1]
            if p[2] > hi[2]: hi[2] = p[2]
    return (flat.tobytes(), len(flat) // 5,
            (lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]))


def _coalesce_segments(segments, dist_tol=0.12, gap_tol=0.5):
    """Merge near-coincident straight edge segments. STEP files (especially
    exported assemblies) regularly carry the same geometry as several distinct
    edge TShapes, and the outline explorer emits every one of them: the skin
    border then gets stroked twice, and copies offset by 0.02-0.1 mm read as
    'doubled, shifted' lines on zoom. Cluster segments by canonical direction
    + perpendicular foot distance, union their axial intervals.
    Vectorized per direction bucket on a foot-cell grid (0.12 mm cells): the
    earlier per-segment numpy calls cost 1.05 s over the raw 4884-segment
    outline of Test_model - more than reading the STEP and meshing it."""
    if _np is None or not segments:
        return segments
    S = _np.asarray([[a, b] for a, b in segments], dtype=float)
    A, B = S[:, 0], S[:, 1]
    D = B - A
    L = _np.linalg.norm(D, axis=1)
    keep = L > 1e-6
    A, B = A[keep], B[keep]
    if A.shape[0] == 0:
        return []
    U = (B - A) / L[keep][:, None]
    az = _np.abs(U) > 1e-6
    first = _np.argmax(az, axis=1)
    firstc = U[_np.arange(U.shape[0]), first]
    flip = firstc < 0.0
    U = _np.where(flip[:, None], -U, U)
    P = _np.einsum("ij,ij->i", U, A)
    F = A - U * P[:, None]
    buckets = {}
    for i in range(A.shape[0]):
        buckets.setdefault(tuple(_np.round(U[i], 3)), []).append(i)
    out = []
    for key, idxs in buckets.items():
        idxs = _np.asarray(idxs)
        if idxs.shape[0] == 1:
            i = int(idxs[0])
            out.append((tuple(A[i].tolist()), tuple(B[i].tolist())))
            continue
        u = U[idxs].mean(axis=0)
        u = u / max(float(_np.linalg.norm(u)), 1e-12)
        # perpendicular in-plane basis for the foot grid
        e1 = _np.array([0.0, 0.0, 1.0]) if abs(u[2]) < 0.9 else \
            _np.array([1.0, 0.0, 0.0])
        e1 = e1 - u * float(u @ e1)
        e1 /= max(float(_np.linalg.norm(e1)), 1e-12)
        e2 = _np.cross(u, e1)
        Ai, Bi = A[idxs], B[idxs]
        tA = Ai @ u
        tB = Bi @ u
        lo0 = _np.minimum(tA, tB)
        hi0 = _np.maximum(tA, tB)
        Fi = Ai - u * (tA)[:, None]
        fx = Fi @ e1
        fy = Fi @ e2
        # grid-cell greedy clustering: candidates live in the 3x3 cell
        # neighborhood (clusters are at most dist_tol from their seed foot)
        cells = {}
        members = []                # per cluster: [fxm, fym, lo, hi, count]
        order = _np.argsort(lo0)
        for i in order:
            cx = int(_np.floor(fx[i] / dist_tol))
            cy = int(_np.floor(fy[i] / dist_tol))
            best = -1
            bestd = dist_tol
            for gx in (cx - 1, cx, cx + 1):
                for gy in (cy - 1, cy, cy + 1):
                    for c in cells.get((gx, gy), ()):
                        dx = fx[i] - members[c][0]
                        dy = fy[i] - members[c][1]
                        dd = _np.hypot(dx, dy)
                        if dd >= bestd:
                            continue
                        if lo0[i] - members[c][3] > gap_tol or \
                                hi0[i] + gap_tol < members[c][2]:
                            continue
                        best = c
                        bestd = dd
            if best < 0:
                c = len(members)
                members.append([float(fx[i]), float(fy[i]),
                                float(lo0[i]), float(hi0[i]), 1])
                cells.setdefault((cx, cy), []).append(c)
            else:
                m = members[best]
                n = m[4] + 1
                m[0] = (m[0] * m[4] + float(fx[i])) / n
                m[1] = (m[1] * m[4] + float(fy[i])) / n
                m[2] = min(m[2], float(lo0[i]))
                m[3] = max(m[3], float(hi0[i]))
                m[4] = n
                # the mean foot drifts as members join; keep the cluster
                # findable from the cell of its current mean as well
                mx = int(_np.floor(m[0] / dist_tol))
                my = int(_np.floor(m[1] / dist_tol))
                if (mx, my) != (cx, cy):
                    lst = cells.setdefault((mx, my), [])
                    if best not in lst:
                        lst.append(best)
        for m in members:
            foot = e1 * m[0] + e2 * m[1]
            a = foot + u * m[2]
            b = foot + u * m[3]
            out.append((tuple(a.tolist()), tuple(b.tolist())))
    return out
