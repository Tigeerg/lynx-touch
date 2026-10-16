"""Импорт STL (бинарный и ASCII) в суп треугольников.

Вынесено из mock_backend.py (этап 0 плана переноса на C++).
"""
import struct

from gv_common import _np

np = _np          # код слоя читает numpy как np

# ==========================================================================
#  STL loading (binary + ASCII) -> flat list of triangle vertices
# ==========================================================================
def load_stl_vertices(path):
    """Return machine-space triangle vertices from an STL file.

    With numpy available returns an (N, 3) float64 array; otherwise a Python
    list of (x, y, z) tuples.
    """
    with open(path, "rb") as fh:
        data = fh.read()

    head = data[:1024]
    if b"facet" in head and b"vertex" in head:            # ASCII STL
        verts = []
        for line in data.decode("utf-8", "ignore").splitlines():
            s = line.strip()
            if s.startswith("vertex"):
                p = s.split()
                if len(p) >= 4:
                    verts.append((float(p[1]), float(p[2]), float(p[3])))
        return _np.asarray(verts, dtype=float)

    # binary STL
    if len(data) < 84:
        return _np.zeros((0, 3))
    n = struct.unpack_from("<I", data, 80)[0]
    n = min(n, (len(data) - 84) // 50)
    # Each triangle record is 50 bytes: 4 vec3 floats + a 2-byte attribute.
    rec = _np.dtype([("n", "<f4", 3), ("a", "<f4", 3), ("b", "<f4", 3),
                     ("c", "<f4", 3), ("attr", "<u2")])
    tri = _np.frombuffer(data, dtype=rec, count=n, offset=84)
    return _np.stack([tri["a"], tri["b"], tri["c"]], axis=1).reshape(-1, 3) \
        .astype(_np.float64)

def _verts_bounds(verts):
    """Return (lo, hi) as Python lists for a list or numpy (N, 3) vertex array."""
    if isinstance(verts, _np.ndarray):
        if verts.size == 0:
            return [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
        lo = verts.min(axis=0).tolist()
        hi = verts.max(axis=0).tolist()
        return lo, hi
    if not verts:
        return [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]
    return lo, hi
