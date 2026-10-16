"""Подготовка траектории к отрисовке: потоки вершин, этапы, границы.

Вынесено из mock_backend.py (этап 0 плана переноса на C++): слой не зависит ни от
Qt, ни от OCCT, поэтому переносится вместе с парсером (этап 2 плана).
"""
import math

from gv_common import _np, _to_3d
from gv_types import Aabb, PathData

np = _np          # код слоя читает numpy как np


# ==========================================================================
#  Tool-path preparation (pure, thread-safe: usable from a worker thread)
# ==========================================================================
def _scene_bounds_lo_hi(lo, hi):
    """Machine (X, Y, Z) bounds -> scene (X, Z, -Y) Aabb."""
    return Aabb((lo[0], lo[2], -hi[1]), (hi[0], hi[2], -lo[1]))


def _pack_stream_np(pts, flags, cum, total, want_rapid, machine=True):
    """Pack one line stream (rapid or feed) as float32 (pos3 + texcoord2[:t,0]).

    `pts` (N,3) machine coords, `flags` (N,) per-point outgoing-rapid flag,
    `cum` (N,) cumulative distance. Returns (bytes, vertex_count)."""
    np = _np
    inv = (1.0 / total) if total > 0.0 else 0.0
    if pts.shape[0] < 2:
        return b'', 0
    mask = flags[:-1] == want_rapid          # outgoing segment i uses flag[i]
    if not mask.any():
        return b'', 0
    p0 = pts[:-1][mask]
    p1 = pts[1:][mask]
    t0 = cum[:-1][mask] * inv
    t1 = cum[1:][mask] * inv
    if machine:
        s0 = p0[:, [0, 2, 1]].copy()
        s0[:, 2] = -p0[:, 1]
        s1 = p1[:, [0, 2, 1]].copy()
        s1[:, 2] = -p1[:, 1]
    else:
        s0, s1 = p0, p1
    m = s0.shape[0]
    buf = np.empty((m, 2, 5), dtype=np.float32)
    buf[:, 0, 0:3] = s0
    buf[:, 1, 0:3] = s1
    buf[:, 0, 3] = t0
    buf[:, 1, 3] = t1
    buf[:, :, 4] = 0.0
    return buf.tobytes(), m * 2


def _stages_norm(stages, cum, total):
    """Convert (first_point_index, label) stages to normalized [start, end] ranges."""
    out = []
    n = len(stages) if stages else 0
    if n == 0:
        return out
    inv = (1.0 / total) if total > 0.0 else 0.0

    def at(idx):
        if cum is None or idx < 0 or idx >= len(cum):
            return 0.0
        return float(cum[idx]) * inv

    for i, st in enumerate(stages):
        idx = st.index
        label = st.label
        info = st.info
        s = at(idx)
        e = at(stages[i + 1].index) if i + 1 < n else 1.0
        s = max(0.0, min(1.0, s))
        e = max(s, min(1.0, e))
        out.append({"start": s, "end": e, "label": label, "tool": info})
    return out


def _prepare_ordered(ordered, stages=None):
    """Turn a parsed ordered path into everything needed to display it.

    Pure and thread-safe (no Qt objects): returns a PathData with the packed
    line buffers for both streams, the animation polyline and its metrics. This
    is the heavy part of a load, so it may run on a worker thread."""
    n = len(ordered)
    if n == 0:
        return PathData(
            seg_count=0, last=(0.0, 0.0, 0.0),
            pts=None, cum=None, rapid=None, speeds=None, total=0.0,
            rapid_bytes=b"", rapid_nv=0,
            feed_bytes=b"", feed_nv=0,
            bounds=None, center=[0.0, 0.0, 0.0],
            radius=50.0, min_y=0.0, stages=[])
    last = tuple(ordered[-1].point)

    np = _np
    pts = np.array([p.point for p in ordered], dtype=float)          # (N, 3)
    flags = np.fromiter((bool(p.is_rapid) for p in ordered), dtype=bool, count=n)
    spds = np.fromiter((float(p.speed_mm_s) for p in ordered), dtype=float, count=n)
    if n > 1:
        d = np.diff(pts, axis=0)
        seg = np.sqrt((d * d).sum(axis=1))
        cum_np = np.concatenate(([0.0], np.cumsum(seg)))
    else:
        cum_np = np.zeros(1)
    total = float(cum_np[-1]) if n > 1 else 0.0
    lo = pts.min(axis=0).tolist()
    hi = pts.max(axis=0).tolist()

    # Scene-space polyline for tool animation, sampled from C++ (QML never
    # receives the per-point lists): machine (X,Y,Z) -> scene (X,Z,-Y).
    scene = np.empty_like(pts)
    scene[:, 0] = pts[:, 0]
    scene[:, 1] = pts[:, 2]
    scene[:, 2] = -pts[:, 1]

    rapid_bytes, rapid_nv = _pack_stream_np(pts, flags, cum_np, total, True)
    feed_bytes, feed_nv = _pack_stream_np(pts, flags, cum_np, total, False)
    cm = [(lo[i] + hi[i]) / 2.0 for i in range(3)]
    return PathData(
        seg_count=n - 1, last=last,
        pts=scene, cum=cum_np, rapid=flags, speeds=spds,
        total=total,
        rapid_bytes=rapid_bytes, rapid_nv=rapid_nv,
        feed_bytes=feed_bytes, feed_nv=feed_nv,
        bounds=_scene_bounds_lo_hi(lo, hi),
        center=list(_to_3d(cm)),
        radius=0.5 * math.sqrt(sum((hi[i] - lo[i]) ** 2 for i in range(3))),
        min_y=float(pts[:, 2].min()),       # scene Y = machine Z
        stages=_stages_norm(stages, cum_np, total))

