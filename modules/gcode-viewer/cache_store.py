"""Дисковый кэш подготовленных данных: траектория, меш, обводка.

Вынесено из mock_backend.py (этап 0 плана переноса на C++). Ключ включает допуски
парсера, поэтому модуль зависит от gcode_parser.
"""
import hashlib
import json
import os

from gcode_parser import _DECIMATE_TOL, _ARC_DECIMATE_TOL

from gv_common import _np

np = _np          # код слоя читает numpy как np

# ---- prepared-data disk cache -------------------------------------------
# Re-opening the same program / model must not re-parse the G-code or recompute
# the feature-edge outline. Cached blobs live in CACHE_DIR, keyed by the source
# file (path + size + mtime) and the decimation tolerances. CNC_NO_CACHE=1
# disables it entirely; it is used only when numpy is available.
CACHE_VER = 9
CACHE_DIR = os.path.join(os.path.expanduser("~"), ".cache", "gcode_viewer")
CACHE_MAX_BYTES = 200 * 1024 * 1024


def _cache_enabled():
    return os.environ.get("CNC_NO_CACHE") != "1"


def _cache_key(filepath, kind):
    """sha1 key over CACHE_VER|abspath|size|mtime_ns|tolerances|kind (or None)."""
    try:
        st = os.stat(filepath)
    except OSError:
        return None
    raw = "|".join((str(CACHE_VER), os.path.abspath(filepath), str(st.st_size),
                    str(st.st_mtime_ns), repr(_DECIMATE_TOL),
                    repr(_ARC_DECIMATE_TOL), kind))
    return hashlib.sha1(raw.encode("utf-8")).hexdigest()


def _cache_file(key):
    return os.path.join(CACHE_DIR, key + ".npz")


def _cache_prune():
    """Drop the oldest .npz files until the cache dir fits CACHE_MAX_BYTES."""
    try:
        entries = []
        total = 0
        for name in os.listdir(CACHE_DIR):
            if not name.endswith(".npz"):
                continue
            p = os.path.join(CACHE_DIR, name)
            try:
                st = os.stat(p)
            except OSError:
                continue
            entries.append((st.st_mtime, st.st_size, p))
            total += st.st_size
        if total <= CACHE_MAX_BYTES:
            return
        entries.sort(key=lambda e: e[0])          # oldest first
        for _mt, size, p in entries:
            if total <= CACHE_MAX_BYTES:
                break
            try:
                os.remove(p)
                total -= size
            except OSError:
                pass
    except OSError:
        pass


def _cache_read_path(key):
    """Restore the _prepare_ordered() dict from disk, or None (cache miss)."""
    np = _np
    path = _cache_file(key)
    try:
        with np.load(path, allow_pickle=False) as z:
            # .copy(): см. _cache_read_model - буферы npz-членов живут только
            # пока открыт NpzFile; pts/cum/speeds/meta читаются после with.
            pts = z["pts"].copy()
            cum = z["cum"].copy()
            rapid = z["rapid"].astype(bool)
            speeds = z["speeds"].copy()
            rapid_bytes = z["rapid_bytes"].tobytes()
            feed_bytes = z["feed_bytes"].tobytes()
            last = tuple(float(v) for v in z["last"])
            center = [float(v) for v in z["center"]]
            meta = z["meta"].copy()
            bounds_arr = z["bounds"].copy()
            stages = json.loads(bytes(z["stages"]).decode("utf-8"))
    except Exception:
        try:
            os.remove(path)                       # damaged -> drop and recompute
        except OSError:
            pass
        return None
    has_bounds = bool(meta[6])
    return {
        "seg_count": int(meta[0]), "last": last,
        "pts": pts, "cum": cum, "rapid": rapid, "speeds": speeds,
        "total": float(meta[1]),
        "rapid_bytes": rapid_bytes, "rapid_nv": int(meta[2]),
        "feed_bytes": feed_bytes, "feed_nv": int(meta[3]),
        "bounds": tuple(float(v) for v in bounds_arr) if has_bounds else None,
        "center": center,
        "radius": float(meta[4]), "min_y": float(meta[5]),
        "stages": stages,
    }


def _cache_write_path(key, data):
    """Store the _prepare_ordered() dict in one .npz (atomic write)."""
    if data.get("pts") is None:
        return
    np = _np
    path = _cache_file(key)
    tmp = path + ".tmp.npz"
    try:
        os.makedirs(CACHE_DIR, exist_ok=True)
        bounds = data["bounds"]
        stages = np.frombuffer(json.dumps(data.get("stages", [])).encode("utf-8"),
                               dtype=np.uint8)
        meta = np.asarray([data["seg_count"], data["total"], data["rapid_nv"],
                           data["feed_nv"], data["radius"], data["min_y"],
                           1.0 if bounds is not None else 0.0], dtype=np.float64)
        np.savez(tmp,
                 pts=np.asarray(data["pts"], dtype=np.float64),
                 cum=np.asarray(data["cum"], dtype=np.float64),
                 rapid=np.asarray(data["rapid"], dtype=bool),
                 speeds=np.asarray(data["speeds"], dtype=np.float64),
                 rapid_bytes=np.frombuffer(data["rapid_bytes"], dtype=np.uint8),
                 feed_bytes=np.frombuffer(data["feed_bytes"], dtype=np.uint8),
                 last=np.asarray(data["last"], dtype=np.float64),
                 bounds=np.asarray(bounds if bounds is not None
                                   else (0.0,) * 6, dtype=np.float64),
                 center=np.asarray(data["center"], dtype=np.float64),
                 meta=meta, stages=stages)
        os.replace(tmp, path)
        _cache_prune()
    except Exception:
        try:
            os.remove(tmp)
        except OSError:
            pass


def _cache_read_edges(key):
    """Restore the (raw_bytes, nv, bounds) outline tuple, or None (miss)."""
    np = _np
    path = _cache_file(key)
    try:
        with np.load(path, allow_pickle=False) as z:
            raw = z["raw"].tobytes()
            nv = int(z["nv"])
            has_bounds = bool(z["has_bounds"])
            bounds_arr = z["bounds"].copy()   # читается после with - копия
    except Exception:
        try:
            os.remove(path)
        except OSError:
            pass
        return None
    bounds = tuple(float(v) for v in bounds_arr) if has_bounds else None
    return (raw, nv, bounds)


def _cache_write_edges(key, packed):
    """Store the (raw_bytes, nv, bounds) outline tuple (atomic write)."""
    np = _np
    raw, nv, bounds = packed
    path = _cache_file(key)
    tmp = path + ".tmp.npz"
    try:
        os.makedirs(CACHE_DIR, exist_ok=True)
        np.savez(tmp,
                 raw=np.frombuffer(raw or b"", dtype=np.uint8),
                 nv=np.int64(nv),
                 bounds=np.asarray(bounds if bounds is not None
                                   else (0.0,) * 6, dtype=np.float64),
                 has_bounds=np.int64(1 if bounds is not None else 0))
        os.replace(tmp, path)
        _cache_prune()
    except Exception:
        try:
            os.remove(tmp)
        except OSError:
            pass


def _cache_read_model(key):
    """Restore the STEP model (verts, packed outline segments, smooth
    normals), or None on miss/corrupt."""
    np = _np
    path = _cache_file(key)
    try:
        with np.load(path, allow_pickle=False) as z:
            # .copy(): массивы npz-члена указывают на буфер декомпрессии
            # NpzFile, который освобождается при выходе из with. Без копии
            # verts/normals - use-after-free: worker читает их позже в
            # _tangent_families, а освобожденную страницу переиспользует
            # любой аллокейтор (в т.ч. GUI-поток) -> access violation.
            verts = z["verts"].copy()
            normals = z["normals"].copy()
            raw = z["raw"].tobytes()
            nv = int(z["nv"])
            has_bounds = bool(z["has_bounds"])
            bounds_arr = z["bounds"].copy()
    except Exception:
        try:
            os.remove(path)
        except OSError:
            pass
        return None
    bounds = tuple(float(v) for v in bounds_arr) if has_bounds else None
    return verts, (raw, nv, bounds), normals


def _cache_write_model(key, verts, normals, packed):
    """Store the STEP model triple (atomic write, mirrors _cache_write_edges)."""
    np = _np
    if np is None:
        return
    raw, nv, bounds = packed
    path = _cache_file(key)
    tmp = path + ".tmp.npz"
    try:
        os.makedirs(CACHE_DIR, exist_ok=True)
        np.savez(tmp,
                 verts=np.asarray(verts, dtype=np.float32),
                 normals=np.asarray(normals, dtype=np.float32),
                 raw=np.frombuffer(raw or b"", dtype=np.uint8),
                 nv=np.int64(nv),
                 bounds=np.asarray(bounds if bounds is not None
                                   else (0.0,) * 6, dtype=np.float64),
                 has_bounds=np.int64(1 if bounds is not None else 0))
        os.replace(tmp, path)
        _cache_prune()
    except Exception:
        try:
            os.remove(tmp)
        except OSError:
            pass



