# -*- coding: utf-8 -*-
"""
mock_backend.py — PySide6 / Qt 6.x backend for a 4-axis CNC (X, Y, Z, A).

Pure QML/Python, no LinuxCNC widgets.

Types (registered into QML module "MockBackend" 1.0 via @QmlElement):
  * ToolpathGeometry — QQuick3DGeometry subclass, position-only line buffer
                       (primitive = Lines). Used for G0 (red) and G1 (blue).
  * GridGeometry     — QQuick3DGeometry subclass, X/Z grid lines (the "floor")
                       with a soft alpha falloff toward the edges.
  * GcodeToolpath    — QObject: parses G0/G1/G2/G3 (planes G17/G18/G19) and the
                       canned drill cycles G73/G81/G82/G83, owns
                       rapidGeometry/feedGeometry, keeps the tool position.

Machine coords -> scene coords: (X, Y, Z)_machine -> (X, Z, -Y)_scene (Y is up).
"""

import hashlib
import json
import math
import os
import re
import struct
import threading

# OCCT (OCP) отускает GIL внутри тяжёлых вызовов (BRepMesh, HLRBRep), поэтому
# два потока на одном TopoDS_Shape портят кучу (access violation, всплывающий
# в случайном месте: numpy.mean, рендер, импорт). Все OCCT-секции сериализуются
# этим замком: загрузка модели и HLR-прогон не пересекаются никогда.
_occt = threading.RLock()
from array import array
from bisect import bisect_right as _bisect_right

try:
    import numpy as _np
except Exception:                      # numpy is optional: fall back to pure Python
    _np = None
    import sys as _sys
    print("[mock_backend] WARNING: numpy not found — pure-Python fallback is "
          "much slower (edges/geometry). Install numpy on the CNC controller.",
          file=_sys.stderr)

from PySide6.QtCore import Property, QByteArray, QObject, QTimer, Signal, Slot
from PySide6.QtGui import QQuaternion, QVector3D
from PySide6.QtQml import QmlElement, QQmlEngine
from PySide6.QtQuick3D import QQuick3DGeometry

QML_IMPORT_NAME = "MockBackend"
QML_IMPORT_MAJOR_VERSION = 1
QML_IMPORT_MINOR_VERSION = 0

_F32 = QQuick3DGeometry.Attribute.ComponentType.F32Type
_POSITION = QQuick3DGeometry.Attribute.Semantic.PositionSemantic
_COLOR = QQuick3DGeometry.Attribute.Semantic.ColorSemantic
_TEXCOORD0 = QQuick3DGeometry.Attribute.Semantic.TexCoordSemantic
_NORMAL = QQuick3DGeometry.Attribute.Semantic.NormalSemantic
_LINES = QQuick3DGeometry.PrimitiveType.Lines
_TRIANGLES = QQuick3DGeometry.PrimitiveType.Triangles


def _to_3d(p):
    """Machine (X, Y, Z) -> scene (X, Z, -Y); Y is up."""
    return (p[0], p[2], -p[1])


def _pack(points):
    """points: list[(x, y, z)] (scene coords, already pairs for Lines).

    Packs via a C float array (much faster than per-vertex struct.pack)."""
    flat = array('f')
    lo = [math.inf, math.inf, math.inf]
    hi = [-math.inf, -math.inf, -math.inf]
    for x, y, z in points:
        flat.append(x)
        flat.append(y)
        flat.append(z)
        if x < lo[0]:
            lo[0] = x
        if y < lo[1]:
            lo[1] = y
        if z < lo[2]:
            lo[2] = z
        if x > hi[0]:
            hi[0] = x
        if y > hi[1]:
            hi[1] = y
        if z > hi[2]:
            hi[2] = z
    return QByteArray(flat.tobytes()), lo, hi


# ==========================================================================
#  G-code parsing  (G0/G1/G2/G3, all planes G17/G18/G19, canned drill cycles,
#  G90 absolute / G91 incremental)
# ==========================================================================
_WORD_RE = re.compile(r"([A-Za-z])\s*([-+]?\d*\.?\d+)")
_COMMENT_RE = re.compile(r"\([^)]*\)")

# Active plane -> (axis_u, axis_v, axis_w); w is the helical/normal axis.
_PLANES = {"XY": (0, 1, 2), "XZ": (0, 2, 1), "YZ": (1, 2, 0)}
# Active plane -> (offset letter for u, offset letter for v).
_PLANE_OFFSETS = {"XY": ("I", "J"), "XZ": ("I", "K"), "YZ": ("J", "K")}

# G1 points closer than this (machine mm) are dropped: for a 3D preview a
# sub-half-millimetre step is invisible, but it can cut the line count ~10x.
# Override from the environment (useful to coarsen the preview on weak CPUs).
_DECIMATE_TOL = float(os.environ.get("CNC_DECIMATE_TOL", "0.5"))
_DECIMATE_TOL2 = _DECIMATE_TOL * _DECIMATE_TOL
# Arcs/cycles carry a much smaller tolerance: it only removes the near-duplicate
# points a fine tessellation can produce, while keeping circles round.
_ARC_DECIMATE_TOL = float(os.environ.get("CNC_ARC_DECIMATE_TOL", "0.1"))
_ARC_DECIMATE_TOL2 = _ARC_DECIMATE_TOL * _ARC_DECIMATE_TOL

# Playback speed (1:1 with the real machine): feed (G1/G2/G3) segments move at
# their programmed F (mm/min -> scene mm/s, scene units == machine mm); G0
# rapids move at a fixed machine rapid rate. Both are overridable via env.
_RAPID_SPEED_MMS = float(os.environ.get("CNC_RAPID_SPEED_MMS", "500"))
_DEFAULT_FEED_MMS = float(os.environ.get("CNC_DEFAULT_FEED_MMS", "100"))

# Holder (ER20 collet nut + spindle body) above the tool stick-out; used by the
# collision scan and drawn by the viewer. Env-overridable per machine.
_HOLDER_NUT_D = float(os.environ.get("CNC_HOLDER_NUT_D", "33.0"))    # ER20 nut OD
_HOLDER_NUT_H = float(os.environ.get("CNC_HOLDER_NUT_H", "22.0"))    # ER20 nut length
_HOLDER_BODY_D = float(os.environ.get("CNC_HOLDER_BODY_D", "46.0"))  # holder body OD
_HOLDER_BODY_H = float(os.environ.get("CNC_HOLDER_BODY_H", "50.0"))  # holder body length
# Collision scan sampling step along the path (mm).
_COLLISION_STEP = float(os.environ.get("CNC_COLLISION_STEP", "1.0"))


# ---- prepared-data disk cache -------------------------------------------
# Re-opening the same program / model must not re-parse the G-code or recompute
# the feature-edge outline. Cached blobs live in CACHE_DIR, keyed by the source
# file (path + size + mtime) and the decimation tolerances. CNC_NO_CACHE=1
# disables it entirely; it is used only when numpy is available.
CACHE_VER = 9
CACHE_DIR = os.path.join(os.path.expanduser("~"), ".cache", "gcode_viewer")
CACHE_MAX_BYTES = 200 * 1024 * 1024


def _cache_enabled():
    return _np is not None and os.environ.get("CNC_NO_CACHE") != "1"


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
    if _np is None or data.get("pts") is None:
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
    if _np is None:
        return
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


def _to_mms(mm_per_min):
    """Programmed feed F (mm/min, G94 default) -> scene mm/s (1:1)."""
    if mm_per_min and mm_per_min > 0.0:
        return mm_per_min / 60.0
    return _DEFAULT_FEED_MMS


def parse_gcode(text):
    """Return (ordered, last_machine_point, segment_count, stages).

    `ordered` is the full tool path in program order as (point, rapid) pairs;
    rapid and feed are split from it later. Handles line moves, arcs in every
    plane (G17/G18/G19) and the canned drilling cycles G73/G81/G82/G83.

    `stages` is a list of (first_point_index, label, tool) operation blocks in
    program order. Operations come from the `(UI_OP: ...)` tags when present,
    else from the per-section operation-comment lines, else from the `UI_TOOL`
    tags (tool changes), else a single stage.
    """
    ordered = []                 # (machine point, is_rapid) in program order
    ui_tool_marks = []           # (idx, label, info) from UI_TOOL tags
    ui_op_marks = []             # (idx, name) from explicit UI_OP tags
    op_marks = []                # (idx, name) from operation-comment lines
    tool_marks = []              # (idx, label, {}) from tool changes (fallback)
    seen_code = False            # a motion/code line has been seen
    prev_code = ""               # last code line (to skip tool comments)
    pos = [0.0, 0.0, 0.0]
    motion = None                # modal linear/arc motion: None | G0 | G1 | ARC
    plane = "XY"                 # active plane (G17)
    absolute = True
    arc_cw = False               # modal arc direction (G2 = True)
    cycle = None                 # active canned cycle parameters, or None
    cycle_retract = False        # G99 -> return to R plane (True); G98 -> clearance
    feed = None                  # modal programmed feed F (mm/min), if seen

    def emit(a, b, is_rapid, decimate=False, tol2=_DECIMATE_TOL2):
        # Every point carries the flag of its outgoing segment; a shared start
        # point is re-flagged so classification by the start index is correct.
        # The 4th element is the scene speed (mm/s) of the outgoing segment:
        # feeds use the programmed F, rapids use the machine rapid rate.
        pb = tuple(b)
        seg_speed = _RAPID_SPEED_MMS if is_rapid else _to_mms(feed)
        if not ordered:
            ordered.append((tuple(a), is_rapid, seg_speed))
            ordered.append((pb, is_rapid, seg_speed))
            return
        if decimate:
            # Only keep points at least `tol2`^0.5 away from the last kept point;
            # the polyline simply connects to it.
            last = ordered[-1][0]
            dx = pb[0] - last[0]
            dy = pb[1] - last[1]
            dz = pb[2] - last[2]
            if dx * dx + dy * dy + dz * dz < tol2:
                return
            ordered[-1] = (last, is_rapid, seg_speed)
            ordered.append((pb, is_rapid, seg_speed))
            return
        pa = tuple(a)
        if ordered[-1][0] == pa:
            ordered[-1] = (pa, is_rapid, seg_speed)
        else:
            ordered.append((pa, is_rapid, seg_speed))
        ordered.append((pb, is_rapid, seg_speed))

    for raw in text.splitlines():
        up = raw.upper()
        code_only = _COMMENT_RE.sub("", raw.split(";", 1)[0]).strip()
        standalone_comment = (code_only == "") and ("(" in raw)
        if "UI_TOOL" in up:
            # Tool change: gives the cutter geometry for the following section.
            info = parse_ui_tool(raw)
            ui_tool_marks.append((max(0, len(ordered) - 1), stage_label(info), info))
        elif "UI_OP" in up:
            # Explicit operation marker (newer post): unambiguous section start.
            inner = re.sub(r"(?i)^\s*UI_OP\s*:\s*", "", _comment_text(raw)).strip()
            ui_op_marks.append((max(0, len(ordered) - 1), inner or "Операция"))
        elif standalone_comment:
            # A bare comment after the program has started is the section's
            # operation-comment (one per operation). Skip UI tags and the tool
            # comment that directly follows a tool change.
            if seen_code:
                inner = _comment_text(raw)
                if inner and not inner.upper().startswith("UI_") \
                        and not _TOOLCHANGE_RE.search(prev_code):
                    op_marks.append((max(0, len(ordered) - 1), inner))
        else:
            mt = _TOOLCHANGE_RE.search(up)
            if mt:
                tool_marks.append((max(0, len(ordered) - 1), "T" + mt.group(1), {}))
        line = raw.split(";", 1)[0]
        line = _COMMENT_RE.sub("", line).strip().upper()
        if not line:
            continue
        prev_code = line
        words = _WORD_RE.findall(line)
        if not words:
            continue
        if not seen_code and any(w[0] in "XYZ" for w in words):
            seen_code = True          # first axis move -> program body has begun

        target = list(pos)
        has_coord = False
        saw_motion = False       # explicit G0/G1/G2/G3 on this line
        new_cycle = None         # G73/G81/G82/G83 seen on this line
        ijkr = {}
        z_given = r_given = q_given = f_given = False

        for letter, value in words:
            try:
                val = float(value)
            except ValueError:
                continue
            if letter == "G":
                # Exact comparisons: G91.1 must not be read as G91.
                if val == 0:
                    motion = "G0"; saw_motion = True; cycle = None
                elif val == 1:
                    motion = "G1"; saw_motion = True; cycle = None
                elif val == 2:
                    motion = "ARC"; arc_cw = True; saw_motion = True; cycle = None
                elif val == 3:
                    motion = "ARC"; arc_cw = False; saw_motion = True; cycle = None
                elif val == 17:
                    plane = "XY"
                elif val == 18:
                    plane = "XZ"
                elif val == 19:
                    plane = "YZ"
                elif val == 80:
                    cycle = None; motion = None
                elif val in (73, 81, 82, 83):
                    new_cycle = val
                elif val == 98:
                    cycle_retract = False          # return to initial (clearance) level
                elif val == 99:
                    cycle_retract = True           # return to the R plane
                elif val == 90:
                    absolute = True
                elif val == 91:
                    absolute = False
                # G90.1/G91.1 (arc distance mode) and others: ignored
            elif letter in ("X", "Y", "Z"):
                idx = "XYZ".index(letter)
                target[idx] = val if absolute else target[idx] + val
                has_coord = True
                if letter == "Z":
                    z_given = True
            elif letter in ("I", "J", "K"):
                ijkr[letter] = val
            elif letter == "R":
                ijkr["R"] = val
                r_given = True
            elif letter == "Q":
                q_given = True
                ijkr["Q"] = val
            elif letter == "F":
                f_given = True
                ijkr["F"] = val
                if val > 0.0:
                    feed = val

        # ---- canned drilling cycle -------------------------------------
        if new_cycle is not None or (cycle is not None and has_coord):
            if new_cycle is not None:
                cycle = cycle or {}
                cycle["code"] = new_cycle
            if z_given:
                cycle["depth"] = target[2]
            if r_given:
                cycle["r"] = ijkr["R"]
            if q_given:
                cycle["q"] = ijkr["Q"]
            if f_given:
                cycle["f"] = ijkr["F"]
            if "depth" in cycle:
                _run_cycle(cycle, pos, target[0], target[1], emit, cycle_retract)
            continue

        # ---- arcs ------------------------------------------------------
        if motion == "ARC" and (has_coord or ijkr):
            pts = _arc_points(pos, target, ijkr, arc_cw, plane)
            for i in range(len(pts) - 1):
                emit(pts[i], pts[i + 1], False, True, _ARC_DECIMATE_TOL2)
            pos = target
        elif has_coord and motion in ("G0", "G1"):
            is_rapid = motion == "G0"
            emit(pos, target, is_rapid, not is_rapid)   # decimate G1 only
            pos = target
        else:
            pos = target

    # Always keep the exact program end point (it may have been decimated away).
    if ordered and ordered[-1][0] != tuple(pos):
        ordered.append((tuple(pos), ordered[-1][1], ordered[-1][2]))

    # Operation stages, most specific source first: explicit UI_OP tags, then
    # the per-section operation-comment lines, then UI_TOOL tool changes, else
    # a single stage. Every stage carries the cutter active in that operation.
    if ui_op_marks:
        stages = _ops_with_tools(ui_op_marks, ui_tool_marks)
    elif op_marks:
        stages = _ops_with_tools(op_marks, ui_tool_marks)
    elif ui_tool_marks:
        stages = ui_tool_marks
    elif tool_marks:
        stages = tool_marks
    else:
        stages = [(0, "УП")]
    # The first stage always starts at the very beginning of the program (any
    # leading approach rapids belong to it).
    stages[0] = (0,) + tuple(stages[0][1:])
    return ordered, tuple(pos), max(0, len(ordered) - 1), stages


def _run_cycle(cycle, pos, hx, hy, emit, retract_to_r=False):
    """Emit one canned drilling cycle at (hx, hy) and advance `pos`.

    G81/G82 drill straight to depth; G83 pecks with a FULL retract between
    pecks; G73 pecks with a small chip-breaking retract. `retract_to_r` is the
    G99 return mode (True -> R plane, False -> initial clearance level, G98)."""
    z = cycle.get("depth", pos[2])
    r = cycle.get("r", pos[2])
    code = int(cycle.get("code", 81))
    q = abs(float(cycle.get("q", 0.0) or 0.0))
    start = list(pos)
    clear = start[2] if start[2] >= r else r      # initial (clearance) level
    if start[2] < clear:
        emit(start, (start[0], start[1], clear), True)
        start[2] = clear
    emit(start, (hx, hy, start[2]), True)                     # position in XY
    if start[2] > r:
        emit((hx, hy, start[2]), (hx, hy, r), True)           # rapid to R plane

    retract_z = r if retract_to_r else clear

    if code in (73, 83) and q > 1e-6 and (r - z) > q + 1e-6:
        clearance = min(1.0, max(0.2, q * 0.5))               # gap above the peck
        bottom = r                                            # current peck bottom
        cur = r                                               # where the feed starts
        while True:
            bottom = max(z, bottom - q)                       # next peck bottom (-Q)
            emit((hx, hy, cur), (hx, hy, bottom), False)      # feed one peck
            if bottom <= z + 1e-6:                            # reached depth
                break
            if code == 83:                                    # full retract (G83)
                emit((hx, hy, bottom), (hx, hy, retract_z), True)
                cur = min(bottom + clearance, r)
                emit((hx, hy, retract_z), (hx, hy, cur), True)
            else:                                             # chip break (G73)
                up = min(bottom + clearance, r)
                emit((hx, hy, bottom), (hx, hy, up), True)
                emit((hx, hy, up), (hx, hy, bottom), True)
                cur = bottom
        emit((hx, hy, z), (hx, hy, retract_z), True)          # final retract
    else:
        emit((hx, hy, r), (hx, hy, z), False)                 # drill to depth
        emit((hx, hy, z), (hx, hy, retract_z), True)          # retract

    pos[:] = [hx, hy, retract_z]


def _arc_points(start, end, ijkr, cw, plane="XY", max_step_deg=6.0):
    """Tessellate a G2/G3 arc in the active plane (helical on the normal axis)."""
    u, v, w = _PLANES.get(plane, _PLANES["XY"])
    sx, sy = start[u], start[v]
    ex, ey = end[u], end[v]
    sw, ew = start[w], end[w]
    dx, dy = ex - sx, ey - sy
    d = math.hypot(dx, dy)
    lu, lv = _PLANE_OFFSETS.get(plane, ("I", "J"))

    has_offset = lu in ijkr or lv in ijkr
    if "R" in ijkr and not has_offset and d > 1e-9:
        r = abs(ijkr["R"])
        if r < d / 2.0 - 1e-6:
            return [tuple(start), tuple(end)]
        mx, my = (sx + ex) / 2.0, (sy + ey) / 2.0
        h = math.sqrt(max(0.0, r * r - (d / 2.0) ** 2))
        ux, uy = -dy / d, dx / d
        best = None
        for sign in (1.0, -1.0):
            cx, cy = mx + sign * h * ux, my + sign * h * uy
            sweep = _arc_sweep(sx, sy, ex, ey, cx, cy, cw)
            minor = abs(sweep) <= math.pi + 1e-6
            if (ijkr["R"] > 0) == minor:
                best = (cx, cy, sweep)
                break
        if best is None:
            return [tuple(start), tuple(end)]
        cx, cy, sweep = best
    elif has_offset:
        cx = sx + ijkr.get(lu, 0.0)
        cy = sy + ijkr.get(lv, 0.0)
        sweep = _arc_sweep(sx, sy, ex, ey, cx, cy, cw)
    else:
        return [tuple(start), tuple(end)]        # no centre info: straight chord

    r = math.hypot(sx - cx, sy - cy)
    if r < 1e-9:
        return [tuple(start), tuple(end)]
    a0 = math.atan2(sy - cy, sx - cx)
    steps = max(2, int(abs(math.degrees(sweep)) / max_step_deg) + 1)
    pts = []
    for i in range(steps + 1):
        t = i / float(steps)
        a = a0 + sweep * t
        p = [0.0, 0.0, 0.0]
        p[u] = cx + r * math.cos(a)
        p[v] = cy + r * math.sin(a)
        p[w] = sw + (ew - sw) * t
        pts.append(tuple(p))
    pts[0] = tuple(start)
    pts[-1] = tuple(end)
    return pts


def _arc_sweep(sx, sy, ex, ey, cx, cy, cw):
    a0 = math.atan2(sy - cy, sx - cx)
    a1 = math.atan2(ey - cy, ex - cx)
    if cw:
        while a1 >= a0 - 1e-9:
            a1 -= 2.0 * math.pi
    else:
        while a1 <= a0 + 1e-9:
            a1 += 2.0 * math.pi
    return a1 - a0


_UI_ROT_RE = re.compile(
    r"UI_ROTATION:\s*RX\s*(-?\d+(?:\.\d+)?)\.?\s+RY\s*(-?\d+(?:\.\d+)?)\.?\s+RZ\s*(-?\d+(?:\.\d+)?)\.?",
    re.IGNORECASE,
)
_UI_ORG_RE = re.compile(
    r"UI_ORIGIN:\s*X\s*(-?\d+(?:\.\d+)?)\.?\s+Y\s*(-?\d+(?:\.\d+)?)\.?\s+Z\s*(-?\d+(?:\.\d+)?)\.?",
    re.IGNORECASE,
)


def parse_ui_rotation(text):
    """Return (rx, ry, rz) degrees from 'UI_ROTATION: RX.. RY.. RZ..',
    or (0.0, 0.0, 0.0) if the tag is absent."""
    m = _UI_ROT_RE.search(text)
    if not m:
        return 0.0, 0.0, 0.0
    return float(m.group(1)), float(m.group(2)), float(m.group(3))


def parse_ui_origin(text):
    """Return (x, y, z) from 'UI_ORIGIN: X.. Y.. Z..', or (0.0, 0.0, 0.0)."""
    m = _UI_ORG_RE.search(text)
    if not m:
        return 0.0, 0.0, 0.0
    return float(m.group(1)), float(m.group(2)), float(m.group(3))


_UI_TOOL_RE = re.compile(r"UI_TOOL:\s*([^)\n]*)", re.IGNORECASE)
# Tool change (T.. M6 / T.. M06) — used as a stage marker when UI_TOOL is absent.
_TOOLCHANGE_RE = re.compile(r"\bT\s*(\d+)\s*M0?6\b")

# Short Russian names for the tool types used in stage labels.
_TOOL_TYPE_RU = {
    "FLAT": "фреза", "BULL": "фреза R", "BALL": "сфер. фреза",
    "CHAMFER": "фаска", "TAPER": "конус", "DOVETAIL": "ласточка",
    "LOLLIPOP": "грибок", "THREAD": "резьба", "DRILL": "сверло",
    "TAP": "метчик", "PROBE": "щуп",
}


def stage_label(info):
    """Short label of an operation stage from UI_TOOL info, e.g. 'T2 сверло D3'."""
    if not info:
        return "Этап"
    parts = ["T%d" % int(info.get("number", 0))]
    typ = _TOOL_TYPE_RU.get(info.get("type", ""), info.get("type", ""))
    if typ:
        parts.append(typ)
    d = float(info.get("diameter", 0.0) or 0.0)
    if d:
        parts.append("D%g" % d)
    return " ".join(parts)


_COMMENT_TEXT_RE = re.compile(r"\(([^)]*)\)")


def _comment_text(raw):
    """Text inside the first (...) comment of a line, stripped ('' if none)."""
    m = _COMMENT_TEXT_RE.search(raw)
    return m.group(1).strip() if m else ""


def _ops_with_tools(op_marks, tool_marks):
    """Attach the cutter active in each operation to the operation marker.

    The post writes the operation comment (one per section) just before that
    section's tool change, so the cutter of an operation is the first UI_TOOL
    mark inside its range; operations that reuse the previous tool inherit it.
    `op_marks`/`tool_marks` are (point_index, name[, info]) tuples."""
    tmarks = sorted(tool_marks, key=lambda m: m[0])
    n = len(op_marks)
    out = []
    last_info = {}
    for i, mark in enumerate(op_marks):
        idx = mark[0]
        name = mark[1]
        end = op_marks[i + 1][0] if i + 1 < n else float("inf")
        info = {}
        for tm in tmarks:
            if idx <= tm[0] < end:
                info = tm[2] if len(tm) > 2 else {}
                break
        if info:
            last_info = info
        else:
            info = last_info                       # same cutter as the previous op
        label = name + (" · " + stage_label(info) if info else "")
        out.append((idx, label, info))
    return out


def parse_ui_tool(text):
    """Parse 'UI_TOOL: T=.. TYPE=.. D=.. CR=.. TAPER=.. ...' into a dict.

    Returns {} when the tag is absent. Absent numeric fields default to 0."""
    m = _UI_TOOL_RE.search(text)
    if not m:
        return {}
    kv = {}
    for tok in m.group(1).split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            kv[k.strip().upper()] = v.strip()

    def num(key, d=0.0):
        try:
            return float(kv[key])
        except (KeyError, ValueError):
            return d

    return {
        "number": int(num("T")),
        "type": kv.get("TYPE", "FLAT").upper(),
        "diameter": num("D"),
        "corner_radius": num("CR"),
        "taper_deg": num("TAPER"),
        "tip_angle_deg": num("TIPA"),
        "tip_diameter": num("TIPD"),
        "flute_length": num("FL"),
        "shoulder_length": num("SL"),
        "body_length": num("BL"),
        "shank_diameter": num("SD"),
        "overall_length": num("OL"),
        "flutes": int(num("NF")),
    }


_UI_CAD_MAT_RE = re.compile(r"UI_CAD_MATRIX:\s*([^)\n]*)", re.IGNORECASE)
_UI_CAD_MAT_ORG_RE = re.compile(
    r"UI_CAD_MATRIX_ORIGIN:\s*X\s*(-?\d+(?:\.\d+)?)\.?\s*Y\s*(-?\d+(?:\.\d+)?)\.?\s*Z\s*(-?\d+(?:\.\d+)?)\.?",
    re.IGNORECASE,
)
_NUM_RE = re.compile(r"-?\d+(?:\.\d+)?")

_UI_STOCK_RE = re.compile(r"UI_STOCK:\s*([^)\n]*)", re.IGNORECASE)
_UI_HOLDER_RE = re.compile(r"UI_HOLDER:\s*([^)\n]*)", re.IGNORECASE)


def parse_ui_holder(text):
    """Parse 'UI_HOLDER: GAUGE=.. NAME=..' -> {'gauge': float, 'name': str}.

    GAUGE is the holder gauge length (distance from the holder gauge line to
    the tool tip = the true stick-out) as exported by HSMWorks/Fusion."""
    m = _UI_HOLDER_RE.search(text)
    if not m:
        return {}
    body = m.group(1)
    out = {}
    g = re.search(r"GAUGE\s*=\s*([-+]?[0-9.]+)", body, re.IGNORECASE)
    if g:
        try:
            out["gauge"] = float(g.group(1))
        except ValueError:
            pass
    n = re.search(r"NAME\s*=\s*([^=]+?)\s*$", body, re.IGNORECASE)
    if n:
        out["name"] = n.group(1).strip()
    return out


def parse_ui_stock(text):
    """Return (xmin, xmax, ymin, ymax, zmin, zmax) from 'UI_STOCK: XMIN=.. XMAX=.. ...' or None."""
    m = _UI_STOCK_RE.search(text)
    if not m:
        return None
    nums = _NUM_RE.findall(m.group(1))
    if len(nums) < 6:
        return None
    return tuple(float(x) for x in nums[:6])


def parse_ui_cad_matrix(text):
    """Return the 9 numbers of 'UI_CAD_MATRIX: <9 numbers>' (columns X,Y,Z) or None."""
    m = _UI_CAD_MAT_RE.search(text)
    if not m:
        return None
    nums = _NUM_RE.findall(m.group(1))
    if len(nums) < 9:
        return None
    return [float(x) for x in nums[:9]]


def parse_ui_cad_matrix_origin(text):
    """Return (x, y, z) of 'UI_CAD_MATRIX_ORIGIN: X.. Y.. Z..' or None."""
    m = _UI_CAD_MAT_ORG_RE.search(text)
    if not m:
        return None
    return float(m.group(1)), float(m.group(2)), float(m.group(3))


def quat_from_cols(c0, c1, c2):
    """QQuaternion from a rotation matrix given by its three columns (X, Y, Z axes)."""
    m00, m10, m20 = c0
    m01, m11, m21 = c1
    m02, m12, m22 = c2
    tr = m00 + m11 + m22
    if tr > 0.0:
        s = math.sqrt(tr + 1.0) * 2.0
        return QQuaternion(0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s)
    if m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2.0
        return QQuaternion((m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s)
    if m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2.0
        return QQuaternion((m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s)
    s = math.sqrt(1.0 + m22 - m00 - m11) * 2.0
    return QQuaternion((m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s)


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
        return _np.asarray(verts, dtype=float) if _np is not None else verts

    # binary STL
    if len(data) < 84:
        return _np.zeros((0, 3)) if _np is not None else []
    n = struct.unpack_from("<I", data, 80)[0]
    if _np is not None:
        n = min(n, (len(data) - 84) // 50)
        # Each triangle record is 50 bytes: 4 vec3 floats + a 2-byte attribute.
        rec = _np.dtype([("n", "<f4", 3), ("a", "<f4", 3), ("b", "<f4", 3),
                         ("c", "<f4", 3), ("attr", "<u2")])
        tri = _np.frombuffer(data, dtype=rec, count=n, offset=84)
        return _np.stack([tri["a"], tri["b"], tri["c"]], axis=1).reshape(-1, 3) \
            .astype(_np.float64)
    verts = []
    off = 84
    for _ in range(n):
        if off + 50 > len(data):
            break
        v = struct.unpack_from("<12f", data, off)
        verts.append((v[3], v[4], v[5]))
        verts.append((v[6], v[7], v[8]))
        verts.append((v[9], v[10], v[11]))
        off += 50
    return verts


def _verts_bounds(verts):
    """Return (lo, hi) as Python lists for a list or numpy (N, 3) vertex array."""
    if _np is not None and isinstance(verts, _np.ndarray):
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


def _step_shape(path):
    """Read a STEP file once; the TopoDS_Shape feeds both the skin extraction
    and the analytic surface list. A second STEPControl_Reader pass over the
    same file costs ~0.8 s (bench_viewer.py step.surfaces) for nothing."""
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IFSelect import IFSelect_ReturnStatus
    reader = STEPControl_Reader()
    if reader.ReadFile(path) != IFSelect_ReturnStatus.IFSelect_RetDone:
        raise ValueError("STEP read failed: %s" % path)
    reader.TransferRoots()
    return reader.OneShape()


def _orient_shell_outward(verts):
    """Развернуть оболочку наружу, если её треугольники смотрят внутрь.

    У замкнутого тела площадь-взвешенные нормали должны смотреть от центроида.
    Если OCP отдал внутреннюю развёртку (или STEP/STL экспортирован вывернутым),
    разворачивается вся оболочка целиком - это единственное место, где решается
    её ориентация. Пофейсовый разворот по центроиду здесь не годится: на
    вогнутых участках (карманы, отверстия) наружная нормаль смотрит к центроиду,
    и такой разворот выворачивает их внутрь. Требует numpy; возвращает
    (F,3,3) float64."""

    tri = _np.asarray(verts, dtype=_np.float64).reshape(-1, 3, 3)
    cr = _np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ctr = tri.mean(axis=1)
    cent = tri.reshape(-1, 3).mean(axis=0)
    if float((cr * (ctr - cent)).sum()) < 0.0:
        tri = _np.ascontiguousarray(tri[:, ::-1, :])
    return tri


def load_step_model(path, deflection=0.05, shape=None):
    """Full model source from a STEP file: triangle-soup vertices, outline
    segments, per-vertex smooth normals.

    The B-rep is read once and used for both: OpenCascade tessellates the
    faces into a triangle soup (the part skin, same flat layout as
    `load_stl_vertices` returns), and every TopoDS edge is sampled to polyline
    segments ((p0, p1) tuples in raw model coordinates) minus the UV-seam
    lines of single curved faces, which are form lines, not model edges.
    Skin and edges share one 0.05 mm deflection: at 146 mm part extent that
    is a fraction of a pixel of chord sagitta, and it cuts the soup in half
    versus the earlier 0.03 (meshing time, normals pass and GPU fill all
    scale with it; the edge polylines have used 0.05 all along).
    Requires `cadquery-ocp` (pip); raises ImportError when it is absent."""
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.BRep import BRep_Tool
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopAbs import TopAbs_FACE, TopAbs_EDGE, TopAbs_REVERSED
    from OCP.TopoDS import TopoDS
    from OCP.TopLoc import TopLoc_Location
    from OCP.BRepAdaptor import BRepAdaptor_Curve
    from OCP.GCPnts import GCPnts_UniformDeflection

    if shape is None:
        shape = _step_shape(path)
    mesher = BRepMesh_IncrementalMesh(shape, deflection, False, deflection * 2.0, True)
    mesher.Perform()

    pts = []
    soup = []
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        try:
            face = TopoDS.Face(exp.Current())
            loc = TopLoc_Location()
            tri = BRep_Tool.Triangulation_s(face, loc)
            if tri is not None and tri.NbTriangles() > 0:
                trsf = loc.Transformation()
                rev = face.Orientation() == TopAbs_REVERSED
                base = len(pts)
                for i in range(1, tri.NbNodes() + 1):
                    p = tri.Node(i).Transformed(trsf)
                    pts.append((p.X(), p.Y(), p.Z()))
                for i in range(1, tri.NbTriangles() + 1):
                    t = tri.Triangle(i)
                    a, b, c = t.Value(1), t.Value(2), t.Value(3)
                    if rev:
                        b, c = c, b
                    soup.append(pts[base + a - 1])
                    soup.append(pts[base + b - 1])
                    soup.append(pts[base + c - 1])
        except Exception:
            pass                       # skip faces that refuse to mesh
        exp.Next()
    segments = []
    from OCP.BRepAdaptor import BRepAdaptor_Surface
    from OCP.GeomAbs import GeomAbs_Line, GeomAbs_Plane

    def _line_key(crv):
        # Endpoint pair (sorted, rounded) for a LINE curve, else None.
        if crv.GetType() != GeomAbs_Line:
            return None
        p0 = crv.Value(crv.FirstParameter())
        p1 = crv.Value(crv.LastParameter())
        a = (round(p0.X(), 2), round(p0.Y(), 2), round(p0.Z(), 2))
        b = (round(p1.X(), 2), round(p1.Y(), 2), round(p1.Z(), 2))
        return (a, b) if a <= b else (b, a)

    # Straight edges whose every adjacent face is curved (cylinder seams and
    # generatrices, including seams between arc segments of a bore) are form
    # lines, not model edges - SolidWorks does not draw them either. A
    # straight edge touching a plane at all (where a flat wall blends into a
    # fillet, or a 90 deg corner) stays as a real model edge.
    face_of_line = {}
    curved_faces = set()
    fid = 0
    expf = TopExp_Explorer(shape, TopAbs_FACE)
    while expf.More():
        try:
            fad = BRepAdaptor_Surface(TopoDS.Face(expf.Current()))
            curved = fad.GetType() != GeomAbs_Plane
            if curved:
                curved_faces.add(fid)
            expe = TopExp_Explorer(expf.Current(), TopAbs_EDGE)
            while expe.More():
                try:
                    k = _line_key(BRepAdaptor_Curve(TopoDS.Edge(expe.Current())))
                    if k is not None:
                        face_of_line.setdefault(k, set()).add(fid)
                except Exception:
                    pass
                expe.Next()
        except Exception:
            pass
        fid += 1
        expf.Next()
    seam_keys = {k for k, fs in face_of_line.items()
                 if fs.issubset(curved_faces)}

    exp = TopExp_Explorer(shape, TopAbs_EDGE)
    while exp.More():
        try:
            crv = BRepAdaptor_Curve(TopoDS.Edge(exp.Current()))
            k = _line_key(crv)
            if k is not None and k in seam_keys:
                exp.Next()
                continue                   # face seam line: not a visible edge
            sampler = GCPnts_UniformDeflection(crv, 0.05)   # edge polylines: coarse is enough
            if sampler.IsDone() and sampler.NbPoints() >= 2:
                prev = sampler.Value(1)
                for i in range(2, sampler.NbPoints() + 1):
                    p = sampler.Value(i)
                    a = (prev.X(), prev.Y(), prev.Z())
                    b = (p.X(), p.Y(), p.Z())
                    if abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) > 1e-9:
                        segments.append((a, b))
                    prev = p
        except Exception:
            pass                       # skip degenerate/unresolvable edges
        exp.Next()

    segments = _coalesce_segments(segments)
    if _np is not None and soup:
        tri = _orient_shell_outward(soup)
        return tri.reshape(-1, 3), segments, _smooth_face_normals(tri)
    return soup, segments, None


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
    slant = abs(float(cone["s1"]) - float(cone["s0"]))
    if slant < _CONE_MIN_SLANT_MM:
        return False
    if skin_tris is None:
        return True
    sktri, skcen, skirm = skin_tris
    d = _np.array(cone["d"], dtype=float)
    d = d / max(float(_np.linalg.norm(d)), 1e-12)
    apex = _np.array(cone["a"], dtype=float)
    psi = float(cone["psi"])
    # любые два ортогональных к оси орта: проба идёт по обороту конуса
    ref = _np.array([0.0, 0.0, 1.0]) if abs(d[2]) < 0.9 else _np.array([1.0, 0.0, 0.0])
    u1 = ref - d * float(d @ ref)
    u1 /= max(float(_np.linalg.norm(u1)), 1e-12)
    u2 = _np.cross(d, u1)
    s_mid = (float(cone["s0"]) + float(cone["s1"])) / 2.0
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
        d = _np.array(c["d"], dtype=float)
        d = d / max(float(_np.linalg.norm(d)), 1e-12)
        sig = [j for j in range(3) if abs(float(d[j])) > 1e-4]
        if sig and float(d[sig[0]]) < 0.0:
            d = -d
            s0, s1 = -float(c["s1"]), -float(c["s0"])
        else:
            s0, s1 = float(c["s0"]), float(c["s1"])
        key = (tuple(round(float(v), 3) for v in c["a"]),
               tuple(round(float(v), 4) for v in d),
               round(float(c["psi"]), 4),
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
    if skin is not None and _np is not None and skin[0] is not None \
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
        if s["k"] != "cyl":
            cones.append(s)
            continue
        d = _np.array(s["d"], dtype=float)
        dn = float(_np.linalg.norm(d))
        if dn <= 1e-12:
            continue
        d = d / dn
        o = _np.array(s["o"], dtype=float)
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
               round(float(s["r"]), 2))
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
        feet = [_np.array(s["o"], dtype=float)
                - d * float(d @ _np.array(s["o"], dtype=float)) for s in faces]
        o = sum(feet) / len(feet)
        e10 = _np.array(faces[0]["e1"], dtype=float)
        e20 = _np.array(faces[0]["e2"], dtype=float)
        full = False
        for s in faces:
            a0, a1 = s.get("arc", (None, None))
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
            lo, hi = float(s["t0"]), float(s["t1"])
            # t0/t1 are measured along the face's own axis: a member whose
            # axis opposes the canonical direction has its span mirrored
            # (tangents floated past the bore - the "shifted duplicates")
            if float(d @ _np.array(s["d"], dtype=float)) < 0.0:
                lo, hi = -hi, -lo
            c = float(d @ (_np.array(s["o"], dtype=float) - o))
            tbs.append((c + lo, c + hi))
        for tb in sorted(tbs):
            if spans and tb[0] <= spans[-1][1] + 1e-6:
                spans[-1][1] = max(spans[-1][1], tb[1])
            else:
                spans.append([tb[0], tb[1]])
        rrep = float(sum(float(s["r"]) for s in faces) / len(faces))
        # Политика силуэта: view-зависимая образующая - силуэт тела вращения,
        # её получают и вогнутые стенки (отверстия), и выпуклые наружные
        # галтели (скругления). Порог arc >= 85 гр и пролёт >= 2.5 мм оставляют
        # силуэт только у настоящих тел вращения (у 45-гр фасок и мелких лунок
        # образующей нет). Тангенциальные кромки галтелей рисуются статикой
        # (это вертикальные рёбра детали), поэтому у скругления могут стоять
        # рядом образующая и стыковая кромка - это силуэт и кромка, не дубль.
        wmax = 0.0
        for s in faces:
            a0b, a1b = s.get("arc", (None, None))
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
            a0b, a1b = s.get("arc", (None, None))
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
        raw = packed_edges[0]
        nv = packed_edges[1]
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
    arr = {"O": _np.array(O, dtype=float).reshape(-1, 3),
           "D": _np.array(D, dtype=float).reshape(-1, 3),
           "E1": _np.array(E1, dtype=float).reshape(-1, 3),
           "E2": _np.array(E2, dtype=float).reshape(-1, 3),
           "FULL": _np.array(FULL, dtype=bool),
           "DROP": DROP,
           "R": _np.array(R, dtype=float),
           "T0": _np.array(T0, dtype=float),
           "T1": _np.array(T1, dtype=float)}
    # coaxial sections (stepped bores) must share one tangent azimuth: each
    # cylinder's own eye-tangent angle differs by ~0.2 deg, so a step like
    # R=3.42 over R=2.75 drew its contour as two offset stubs - the "doubled,
    # shifted lines that rotate with the camera". Reference = largest radius
    # on the same axis line; sections render placed on their own radius but
    # at the group azimuth, making the outline one straight line per side.
    Rr = arr["R"].copy()
    Oa2, Da2, Ra2 = arr["O"], arr["D"], arr["R"]
    if Oa2.shape[0]:
        PD = _np.abs(Da2 @ Da2.T)
        wo = Oa2[None, :, :] - Oa2[:, None, :]
        alo = _np.einsum("ijk,jk->ij", wo, Da2)
        pfo = wo - Da2[:, None, :] * alo[:, :, None]
        gap = _np.linalg.norm(pfo, axis=2)
        coax = (PD > 0.999) & (gap < 0.4)
        coax |= _np.eye(Oa2.shape[0], dtype=bool)
        Rr = _np.where(coax, Ra2[None, :], -1.0).max(axis=1)
    arr["RREF"] = Rr
    # Азимутальное покрытие цилиндра. Меш кожи даёт честные 360 гр у
    # сквозного отверстия, но у скругления рядом с отверстием ловит лишние
    # корзины (118 гр вместо 90 гр) - образующая вылезает за дугу на плоскость
    # («фантом»). Поэтому маску по мешу ОГРАНИЧИВАЕМ аналитической дугой грани
    # (ARCS), но только у частичных дуг: полное отверстие (меш ~64 корзины)
    # оставляем как есть - его полу-грани дают одинаковые 180-гр дуги.
    ARC = _np.zeros((arr["O"].shape[0], 64), dtype=bool)
    if sktri is not None and arr["O"].shape[0]:
        two_pi = 2.0 * math.pi
        for i in range(arr["O"].shape[0]):
            if not arr["FULL"][i]:
                continue
            u = arr["D"][i]
            o = arr["O"][i]
            e1 = arr["E1"][i]
            e2 = arr["E2"][i]
            Ri = arr["R"][i]
            lo, hi = arr["T0"][i], arr["T1"][i]
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
        for i in range(arr["O"].shape[0]):
            if not arr["FULL"][i]:
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
    arr["ARC"] = ARC
    # непрерывная дуга (a0, a0+span) для точной обрезки на скользящих углах;
    # полные отверстия (маска ~360 гр) помечаем NaN - их не режем
    a0 = _np.array(A0, dtype=float); a1 = _np.array(A1, dtype=float)
    fullm = ARC.sum(axis=1) >= 56
    a0[fullm] = _np.nan; a1[fullm] = _np.nan
    arr["A0"] = a0; arr["A1"] = a1
    # Примечание: полный генератор НЕ запрещается обрезанным дугам -
    # гейт по покрытию убирал и законный контур скруглений. Рельсовый
    # culling по корзинам ARC ниже в рантайме: линия живёт, пока её
    # азимут внутри дуги, и сходит за её край в стыковое ребро.
    return arr, _dedupe_cones([c for c in cones
                               if _cone_keeps_silhouette(c, skin_tris)])


def _tangent_from_families(fam, E):
    """Vectorized per-frame pass over precomputed families (see
    _tangent_families): both tangent sides of every logical cylinder/cone are
    emitted for any camera angle - hole and bore silhouettes are always
    present, never censored per arc (that made them flicker/appear
    intermittently), and they are drawn in the occlusion-free edgeView
    layer. Cones keep the scalar path."""
    cyls, cones = fam
    segments = []
    O, D, R = cyls["O"], cyls["D"], cyls["R"]
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
            Rref = cyls["RREF"]
            Rp_ = _np.maximum(Rp, 1e-9)
            Rang = _np.where(Rp > Rref, Rref, R)
            cg = _np.clip(Rang / Rp_, -1.0, 1.0)                # cos(gamma)
            sg = _np.sqrt(_np.maximum(1.0 - cg * cg, 0.0))
            mh = perp / _np.maximum(Rp, 1e-9)[:, None]
            rot = _np.cross(D, mh)
            e1, e2 = cyls["E1"], cyls["E2"]
            full = cyls["FULL"]
            drop = cyls["DROP"]
            anydrop = _np.isfinite(drop).any(axis=1)
            T0, T1 = cyls["T0"], cyls["T1"]
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
                aa0 = cyls.get("A0") if hasattr(cyls, "get") else None
                arcok = _np.ones(len(phi), dtype=bool)
                if aa0 is not None:
                    part = _np.isfinite(aa0)
                    rel = (phi - aa0) % (2.0 * math.pi)
                    span = (_np.asarray(cyls["A1"]) - aa0) % (2.0 * math.pi)
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
        sd = _np.array(s["d"], dtype=float)
        dn = float(_np.linalg.norm(sd))
        if dn <= 1e-12:
            continue
        sd = sd / dn
        A = _np.array(s["a"], dtype=float)
        psi = float(s["psi"])
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
            a = A + gdir * float(s["s0"])
            b = A + gdir * float(s["s1"])
            segments.append((tuple(a), tuple(b)))
    return segments


_SILHOUETTE_WELD_MM = 1e-4    # допуск сварки вершин меша (округление позиции)
# Меш-силуэт рисует ТОЛЬКО горизонт гладких поверхностей (стенки отверстий,
# скругления, фаски). Острые переходы исключены: их уже точно рисует слой
# статических B-rep рёбер (partEdges), и раньше силуэт дублировал острые
# круглые кромки вторым, тесселированным кольцом рядом с точным («удвоение
# круглых линий»). Вырожденные/сляйверные грани (площадь ~0) исключены: их
# нормаль - численный мусор, знак скачет и рождал короткие ложные «усы».
_SILHOUETTE_MAX_EDGE_DEG = 20.0     # круче = острая кромка (её слой - статические рёбра)
_SILHOUETTE_MIN_FACE_AREA = 1e-6    # мм^2: ниже - вырожденная грань


def _build_silhouette_topology(verts, weld=_SILHOUETTE_WELD_MM):
    """Топология меша для силуэта - строится один раз на модель.

    Меш - тот же triangle soup, что отдают load_step_model / load_stl_vertices:
    позиции вершин склеиваются округлением до weld (1e-4 мм: соседние грани
    одной кромки B-rep имеют общие точки сэмплирования, расхождение много
    меньше допуска), затем строится карта 'ребро (a,b) -> смежные треугольники'
    и геометрические нормали/центроиды треугольников. Генератор силуэта
    (_mesh_silhouette_segments) берёт рёбра ровно с двумя смежными
    треугольниками: силуэт - там, где две грани лежат по разные стороны от
    глаза. Топология берётся с меша, а не с аналитических поверхностей STEP:
    силуэт получает любая гладкая поверхность (стенка отверстия, скругление,
    фаска). Острые переходы и вырожденные (сляйверные) грани отсеяны - см.
    константы _SILHOUETTE_MAX_EDGE_DEG / _SILHOUETTE_MIN_FACE_AREA.

    Возвращает dict с полями ea/eb (K,3) - концы силуэтных кандидатов,
    f0/f1 (K,) - индексы смежных граней, n (F,3) - нормали граней,
    d (F,) - предвычисленное n·cen (центроиды граней) для скоростного
    знакового теста s = n·E - d на кадре; None без numpy."""
    if _np is None or verts is None:
        return None
    tri = _np.asarray(verts, dtype=_np.float64).reshape(-1, 3, 3)
    F = tri.shape[0]
    if F == 0:
        return None
    # сварка вершин: одна позиция -> один индекс. Без неё соседние грани дают
    # различающиеся индексы и карта смежности рёбер не собирается. Позиция -
    # представитель кластера на сетке weld: расхождение <= weld/2 (5e-5 мм),
    # много меньше грани меша; зато детерминированно и без return_index.
    flat = tri.reshape(-1, 3)
    key = _np.round(flat / weld)
    uniq, inv = _np.unique(key, axis=0, return_inverse=True)
    pos = uniq * weld
    fv = inv.reshape(F, 3)                  # треугольник -> индексы вершин
    # три ребра каждого треугольника, канонически отсортированные (a <= b)
    e = _np.stack([fv[:, [0, 1]], fv[:, [1, 2]], fv[:, [2, 0]]], axis=1)
    er = _np.sort(e, axis=2).reshape(-1, 2)
    uid, uinv = _np.unique(er, axis=0, return_inverse=True)
    uinv = uinv.reshape(-1)
    face_of = _np.repeat(_np.arange(F), 3)
    order = _np.argsort(uinv, kind="stable")
    su = uinv[order]
    sf = face_of[order]
    # границы групп по явному скану (return_index в numpy 2.5.3 на массиве с
    # повторами врёт, см. _smooth_face_normals)
    starts = _np.concatenate(([0], _np.flatnonzero(_np.diff(su)) + 1))
    counts = _np.diff(_np.concatenate((starts, [su.size])))
    keep = _np.flatnonzero(counts == 2)     # только рёбра с двумя гранями
    p1 = starts[keep]
    ends = uid[keep]
    f0 = sf[p1]
    f1 = sf[p1 + 1]
    # нормали - геометрические, по самим треугольникам: горизонт силуэта
    # задаёт переворот нормали грани, сглаживание его размывало бы
    cr = _np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ln = _np.linalg.norm(cr, axis=1, keepdims=True)
    area = 0.5 * ln[:, 0]
    n = cr / _np.where(ln == 0.0, 1.0, ln)
    # фильтр кандидатов (см. константы выше): только гладкие горизонты, без
    # острых кромок (их рисует слой статических рёбер) и без вырожденных граней
    e_dot = _np.einsum("ij,ij->i", n[f0], n[f1])
    e_ang = _np.arccos(_np.clip(e_dot, -1.0, 1.0))
    face_ok = area > _SILHOUETTE_MIN_FACE_AREA
    keep_edge = (face_ok[f0] & face_ok[f1] &
                 (e_ang < math.radians(_SILHOUETTE_MAX_EDGE_DEG)))
    f0 = f0[keep_edge]
    f1 = f1[keep_edge]
    ends = ends[keep_edge]
    cen = tri.mean(axis=1).astype(_np.float32)
    return {
        "ea": pos[ends[:, 0]].astype(_np.float32),
        "eb": pos[ends[:, 1]].astype(_np.float32),
        "f0": f0,
        "f1": f1,
        "n": n.astype(_np.float32),
        # n·cen предвычислено здесь, один раз на модель: на кадре остаётся
        # s = n·E - d (BLAS-матвектор) вместо einsum с материализацией
        # (E - cen) на все F граней (замер на Test_model: этап s подешевел
        # ~9.1 -> ~0.6 мс, всё событие камеры - см. _mesh_silhouette_segments).
        "d": _np.einsum("ij,ij->i", n.astype(_np.float32), cen),
    }


def _mesh_silhouette_segments(topo, E):
    """Силуэтные рёбра меша для глаза E (raw-кадр модели).

    Ребро силуэтное, когда два смежных треугольника лежат по разные стороны
    от глаза (n1.(eye-c1) и n2.(eye-c2) разных знаков): ребро разделяет
    видимую и невидимую грань, т.е. лежит на горизонте поверхности. Никаких
    аналитических образующих и фильтров - знак перехода нормали и есть силуэт.
    Возвращает (K,2,3) float32 (пустой массив без силуэта) или None."""
    if topo is None or _np is None:
        return None
    ev = _np.asarray(E, dtype=_np.float32)
    # Знаковый тест остался «произведение < 0» (грань с s == 0 - глаз в
    # её плоскости - не силуэтная, как и раньше), но знаки собраны в int8:
    # сбор sg[f0]/sg[f1] гоняет 1 байт на ребро вместо 4 у float32
    # (замер на Test_model: ~9.1 -> ~6.2 мс на событие камеры).
    s = topo["n"] @ ev - topo["d"]
    sg = _np.sign(s).astype(_np.int8)
    sel = sg[topo["f0"]] * sg[topo["f1"]] < 0
    if not sel.any():
        return _np.empty((0, 2, 3), dtype=_np.float32)
    k = _np.flatnonzero(sel)
    return _np.stack([topo["ea"][k], topo["eb"][k]], axis=1)


def load_step_surfaces(path, shape=None):
    """Analytic surfaces of revolution from a STEP file.

    Returns descriptors for every face whose underlying surface is a cylinder
    or a cone - the shapes that make up machined bores, rounds and chamfers:
      cylinder: {"k":"cyl","o":base,"d":axis unit,"r":radius,"t0","t1": face
                 extent along the axis, from its vertices}
      cone:     {"k":"cone","a":apex,"d":axis unit,"psi":half angle,
                 "s0","s1": generatrix parameter range}
    Feeds GcodeToolpath.refreshTangents: view-dependent tangent generatrices
    computed from the exact CAD geometry (analytic silhouette of bores and
    rounds) - no post-processing shaders, plain Lines geometry like the
    B-rep edges. Requires cadquery-ocp; [] when absent/unparsable."""
    try:
        from OCP.TopExp import TopExp_Explorer
        from OCP.TopAbs import TopAbs_FACE, TopAbs_VERTEX
        from OCP.TopoDS import TopoDS
        from OCP.BRepAdaptor import BRepAdaptor_Surface
        from OCP.BRep import BRep_Tool
        from OCP.BRepMesh import BRepMesh_IncrementalMesh
        from OCP.TopLoc import TopLoc_Location
        from OCP.GeomAbs import GeomAbs_Cylinder, GeomAbs_Cone
        from OCP.gp import gp_Pnt
    except Exception:
        return []
    try:
        if shape is None:
            shape = _step_shape(path)
        BRepMesh_IncrementalMesh(shape, 0.05, False, 0.10, True).Perform()
    except Exception:
        return []
    out = []
    try:
        exp = TopExp_Explorer(shape, TopAbs_FACE)
        while exp.More():
            try:
                face = TopoDS.Face(exp.Current())
                surf = BRepAdaptor_Surface(face)
                st = surf.GetType()
                if st not in (GeomAbs_Cylinder, GeomAbs_Cone):
                    exp.Next()
                    continue
                # BRepAdaptor_Surface и BRep_Tool.Pnt_s уже отдают геометрию в
                # координатах сборки (с учётом TopLoc_Location грани): второе
                # применение трансформации удваивало смещение (ось цилиндра
                # на x=200 мм вместо 100 мм у грани, сдвинутой на 100 мм).
                def xp(t):
                    return (float(t[0]), float(t[1]), float(t[2]))

                pts = []
                ftri = None
                try:
                    floc = TopLoc_Location()
                    ftri = BRep_Tool.Triangulation_s(face, floc)
                except Exception:
                    ftri = None
                if ftri is not None and not getattr(ftri, "IsNull", lambda: True)() \
                        and ftri.NbNodes() > 0:
                    ftrsf = floc.Transformation()
                    for i in range(1, ftri.NbNodes() + 1):
                        q = ftri.Node(i).Transformed(ftrsf)
                        pts.append((q.X(), q.Y(), q.Z()))
                if not pts:
                    vx = TopExp_Explorer(face, TopAbs_VERTEX)
                    while vx.More():
                        try:
                            p = BRep_Tool.Pnt_s(TopoDS.Vertex(vx.Current()))
                            pts.append(xp((p.X(), p.Y(), p.Z())))
                        except Exception:
                            pass
                        vx.Next()
                if not pts:
                    exp.Next()
                    continue
                if st == GeomAbs_Cylinder:
                    cyl = surf.Cylinder()
                    lo_, la = cyl.Axis().Location(), cyl.Axis().Direction()
                    origin = xp((0.0, 0.0, 0.0))
                    o = xp((lo_.X(), lo_.Y(), lo_.Z()))
                    dv = xp((la.X(), la.Y(), la.Z()))
                    dv = (dv[0] - origin[0], dv[1] - origin[1], dv[2] - origin[2])
                    L = max((dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]) ** 0.5, 1e-12)
                    dn = (dv[0] / L, dv[1] / L, dv[2] / L)
                    tl = [(q[0] - o[0]) * dn[0] + (q[1] - o[1]) * dn[1]
                          + (q[2] - o[2]) * dn[2] for q in pts]
                    # Angular footprint of the face on the cylinder: most
                    # "cylinders" in machined STEPs are arcs (fillets, cut
                    # bores); a tangent whose azimuth falls outside the arc
                    # would float over empty space (static ghost lines).
                    e1 = None
                    for q in pts:
                        vq = (q[0] - o[0], q[1] - o[1], q[2] - o[2])
                        al = vq[0] * dn[0] + vq[1] * dn[1] + vq[2] * dn[2]
                        pv = (vq[0] - dn[0] * al, vq[1] - dn[1] * al,
                              vq[2] - dn[2] * al)
                        Rp = (pv[0] ** 2 + pv[1] ** 2 + pv[2] ** 2) ** 0.5
                        if Rp > 1e-6:
                            e1 = (pv[0] / Rp, pv[1] / Rp, pv[2] / Rp)
                            break
                    e2 = (dn[1] * e1[2] - dn[2] * e1[1],
                          dn[2] * e1[0] - dn[0] * e1[2],
                          dn[0] * e1[1] - dn[1] * e1[0]) if e1 else None
                    raw_ang = []
                    if e1:
                        for q in pts:
                            vq = (q[0] - o[0], q[1] - o[1], q[2] - o[2])
                            al = vq[0] * dn[0] + vq[1] * dn[1] + vq[2] * dn[2]
                            pv = (vq[0] - dn[0] * al, vq[1] - dn[1] * al,
                                  vq[2] - dn[2] * al)
                            if (pv[0] ** 2 + pv[1] ** 2 + pv[2] ** 2) ** 0.5 > 1e-6:
                                raw_ang.append(math.atan2(
                                    pv[0] * e2[0] + pv[1] * e2[1] + pv[2] * e2[2],
                                    pv[0] * e1[0] + pv[1] * e1[1] + pv[2] * e1[2]) % (2 * math.pi))
                    arc0 = arc1 = None
                    if len(raw_ang) >= 2:
                        sa = sorted(set(round(a, 6) for a in raw_ang))
                        gaps = [sa[i + 1] - sa[i] for i in range(len(sa) - 1)]
                        gaps.append(sa[0] + 2 * math.pi - sa[-1])
                        gi = gaps.index(max(gaps))
                        start = sa[(gi + 1) % len(sa)]
                        arc0 = start
                        arc1 = (start + 2 * math.pi - max(gaps)) % (2 * math.pi)
                    out.append({"k": "cyl",
                                "o": o, "d": dn,
                                "e1": e1 or (1.0, 0.0, 0.0),
                                "e2": e2 or (0.0, 0.0, 1.0),
                                "arc": (arc0, arc1),
                                "r": float(cyl.Radius()),
                                "t0": min(tl), "t1": max(tl)})
                else:
                    cn = surf.Cone()
                    ap = cn.Apex(); ad = cn.Axis().Direction()
                    origin = xp((0.0, 0.0, 0.0))
                    a2 = xp((ap.X(), ap.Y(), ap.Z()))
                    dv = xp((ad.X(), ad.Y(), ad.Z()))
                    dv = (dv[0] - origin[0], dv[1] - origin[1], dv[2] - origin[2])
                    L = max((dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]) ** 0.5, 1e-12)
                    dn = (dv[0] / L, dv[1] / L, dv[2] / L)
                    # gp_Cone.SemiAngle: метода HalfAngle в OCP нет, а
                    # исключение ниже глотается - все конические грани
                    # (фаски, зенковки) молча выпадали из контура.
                    psi = abs(float(cn.SemiAngle()))
                    ca = max(math.cos(psi), 1e-6)
                    sl = [((q[0] - a2[0]) * dn[0] + (q[1] - a2[1]) * dn[1]
                           + (q[2] - a2[2]) * dn[2]) / ca for q in pts]
                    out.append({"k": "cone",
                                "a": a2, "d": dn,
                                "psi": psi, "s0": min(sl), "s1": max(sl)})
            except Exception:
                pass
            exp.Next()
    except Exception:
        pass
    return out


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
    """Return a packed Lines buffer (bytes, vertex_count, bounds) for the
    feature-edge outline of a triangle soup.

    Keeps boundary edges and edges whose dihedral angle exceeds `angle_deg`,
    so smooth surfaces don't get a dense wireframe. Отобранные рёбра склеиваются
    в цепочки и упрощаются (_simplify_edge_chains): у мелкого меша контур - это
    звенья в доли миллиметра, на которых лента обводки вырождается. Uses a
    vectorised numpy path when available, with the pure-Python implementation as
    fallback (упрощение цепочек делает только numpy-путь). The buffer is built
    directly (no intermediate tuple list)."""
    if _np is not None:
        try:
            return _feature_edges_np(scene, angle_deg)
        except Exception:
            pass
    return _feature_edges_py(scene, angle_deg)


def _feature_edges_np(scene, angle_deg):
    np = _np
    V = np.asarray(scene, dtype=float).reshape(-1, 3)
    F = V.shape[0] // 3
    if F == 0:
        return (b'', 0, None)
    T = V.reshape(F, 3, 3)

    # Unique vertex ids via a quantised grid (0.001 units), matching the
    # Python fallback's rounding convention. Computed once (also reused for
    # the output positions).
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
        return (b'', 0, None)
    u = uq.astype(float) / 1000.0
    lo_ids, hi_ids = _simplify_edge_chains(np.concatenate(keep_lo),
                                           np.concatenate(keep_hi), u,
                                           _FEATURE_SIMPLIFY_MM)
    if lo_ids.size == 0:
        return (b'', 0, None)
    return _pack_edges_np(u[lo_ids], u[hi_ids])


def _feature_edges_py(scene, angle_deg):
    if _np is not None and isinstance(scene, _np.ndarray):
        scene = scene.tolist()              # fast C-level convert for tuple indexing
    keys = {}
    def k(p):
        key = (round(p[0], 3), round(p[1], 3), round(p[2], 3))
        keys.setdefault(key, p)
        return key

    edges = {}
    faces = len(scene) // 3
    for f in range(faces):
        a, b, c = scene[3 * f], scene[3 * f + 1], scene[3 * f + 2]
        ka, kb, kc = k(a), k(b), k(c)
        ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        wx, wy, wz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
        nx, ny, nz = uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx
        ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        n = (nx / ln, ny / ln, nz / ln)
        for e1, e2 in ((ka, kb), (kb, kc), (kc, ka)):
            e = (e1, e2) if e1 <= e2 else (e2, e1)
            edges.setdefault(e, []).append(n)

    thr = math.cos(math.radians(angle_deg))
    segs = []
    for e, ns in edges.items():
        if len(ns) != 2:
            segs.append((keys[e[0]], keys[e[1]]))
            continue
        n0, n1 = ns
        d = abs(n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2])
        if d < thr:
            segs.append((keys[e[0]], keys[e[1]]))
    return _pack_edge_segments(segs)


# ==========================================================================
#  Tool-path preparation (pure, thread-safe: usable from a worker thread)
# ==========================================================================
def _scene_bounds_lo_hi(lo, hi):
    """Machine (X, Y, Z) bounds -> scene (X, Z, -Y) bounds (x0,y0,z0,x1,y1,z1)."""
    return (lo[0], lo[2], -hi[1], hi[0], hi[2], -lo[1])


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


def _pack_stream_py(ordered, cum, total, want_rapid, machine=True):
    """Pure-Python fallback for _pack_stream_np (no numpy available)."""
    flat = array('f')
    ap = flat.append
    inv = (1.0 / total) if total > 0.0 else 0.0
    n = len(ordered)
    for i in range(n - 1):
        p0, flag, spd = ordered[i]
        if bool(flag) != want_rapid:
            continue
        p1 = ordered[i + 1][0]
        if machine:
            ap(p0[0]); ap(p0[2]); ap(-p0[1]); ap(cum[i] * inv); ap(0.0)
            ap(p1[0]); ap(p1[2]); ap(-p1[1]); ap(cum[i + 1] * inv); ap(0.0)
        else:
            ap(p0[0]); ap(p0[1]); ap(p0[2]); ap(cum[i] * inv); ap(0.0)
            ap(p1[0]); ap(p1[1]); ap(p1[2]); ap(cum[i + 1] * inv); ap(0.0)
    return flat.tobytes(), len(flat) // 5


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
        idx = st[0]
        label = st[1]
        info = st[2] if len(st) > 2 else {}
        s = at(idx)
        e = at(stages[i + 1][0]) if i + 1 < n else 1.0
        s = max(0.0, min(1.0, s))
        e = max(s, min(1.0, e))
        out.append({"start": s, "end": e, "label": label, "tool": info})
    return out


def _prepare_ordered(ordered, stages=None):
    """Turn a parsed ordered path into everything needed to display it.

    Pure and thread-safe (no Qt objects): returns a dict with the packed line
    buffers for both streams, the animation polyline and its metrics. This is
    the heavy part of a load, so it may run on a worker thread."""
    n = len(ordered)
    if n == 0:
        return {
            "seg_count": 0, "last": (0.0, 0.0, 0.0),
            "pts": None, "cum": None, "rapid": None, "speeds": None, "total": 0.0,
            "rapid_bytes": b"", "rapid_nv": 0,
            "feed_bytes": b"", "feed_nv": 0,
            "bounds": None, "center": [0.0, 0.0, 0.0],
            "radius": 50.0, "min_y": 0.0, "stages": [],
        }
    last = tuple(ordered[-1][0])

    if _np is not None:
        np = _np
        pts = np.array([p for p, _, _ in ordered], dtype=float)          # (N, 3)
        flags = np.fromiter((bool(f) for _, f, _ in ordered), dtype=bool, count=n)
        spds = np.fromiter((float(spd) for _, _, spd in ordered), dtype=float, count=n)
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
        return {
            "seg_count": n - 1, "last": last,
            "pts": scene, "cum": cum_np, "rapid": flags, "speeds": spds,
            "total": total,
            "rapid_bytes": rapid_bytes, "rapid_nv": rapid_nv,
            "feed_bytes": feed_bytes, "feed_nv": feed_nv,
            "bounds": _scene_bounds_lo_hi(lo, hi),
            "center": list(_to_3d(cm)),
            "radius": 0.5 * math.sqrt(sum((hi[i] - lo[i]) ** 2 for i in range(3))),
            "min_y": float(pts[:, 2].min()),       # scene Y = machine Z
            "stages": _stages_norm(stages, cum_np, total),
        }

    # ---- pure-Python fallback (no numpy) ---------------------------------
    pts = [(p[0], p[2], -p[1]) for p, _, _ in ordered]
    rapid = [bool(f) for _, f, _ in ordered]
    spds = [float(spd) for _, _, spd in ordered]
    cum = []
    total = 0.0
    px = py = pz = None
    for x, y, z in pts:
        if px is not None:
            total += math.sqrt((x - px) ** 2 + (y - py) ** 2 + (z - pz) ** 2)
        cum.append(total)
        px, py, pz = x, y, z
    mlo = [min(p[i] for p, _, _ in ordered) for i in range(3)]
    mhi = [max(p[i] for p, _, _ in ordered) for i in range(3)]
    rapid_bytes, rapid_nv = _pack_stream_py(ordered, cum, total, True)
    feed_bytes, feed_nv = _pack_stream_py(ordered, cum, total, False)
    cm = [(mlo[i] + mhi[i]) / 2.0 for i in range(3)]
    return {
        "seg_count": n - 1, "last": last,
        "pts": pts, "cum": cum, "rapid": rapid, "speeds": spds, "total": total,
        "rapid_bytes": rapid_bytes, "rapid_nv": rapid_nv,
        "feed_bytes": feed_bytes, "feed_nv": feed_nv,
        "bounds": _scene_bounds_lo_hi(mlo, mhi),
        "center": list(_to_3d(cm)),
        "radius": 0.5 * math.sqrt(sum((mhi[i] - mlo[i]) ** 2 for i in range(3))),
        "min_y": min(_to_3d(p)[1] for p, _, _ in ordered),
        "stages": _stages_norm(stages, cum, total),
    }


def _tool_profile(info):
    """Build a 2D profile [(r, y)] for a tool-info dict, tip at y=0, axis +Y.

    Supports the shapes the panel must draw: flat/bull end mills, ball nose,
    chamfer/taper (V-bit), and drill/tap points. Returns None if no diameter."""
    d = float(info.get("diameter", 0.0) or 0.0)
    r = d / 2.0
    if r <= 0.0:
        return None
    typ = str(info.get("type", "FLAT")).upper()
    cr = float(info.get("corner_radius", 0.0) or 0.0)
    taper = float(info.get("taper_deg", 0.0) or 0.0)
    tipa = float(info.get("tip_angle_deg", 0.0) or 0.0)
    tipd = float(info.get("tip_diameter", 0.0) or 0.0)
    fl = float(info.get("flute_length", 0.0) or 0.0)
    sl = float(info.get("shoulder_length", 0.0) or 0.0)
    bl = float(info.get("body_length", 0.0) or 0.0)
    sd = float(info.get("shank_diameter", 0.0) or 0.0)
    ol = float(info.get("overall_length", 0.0) or 0.0)
    if typ == "FLAT" and taper > 0.0 and cr <= 0.0:
        typ = "DRILL"                      # conical tip: post could not classify it
    if fl <= 0.0:
        fl = max(2.0 * r, 16.0)           # no flute length in old posts: keep it visible

    prof = [(0.0, 0.0)]                    # axis at the tip
    if typ == "BALL":
        k_n = 12                           # hemispherical nose
        for k in range(1, k_n + 1):
            phi = -math.pi / 2.0 + (math.pi / 2.0) * k / k_n
            prof.append((r * math.cos(phi), r + r * math.sin(phi)))
        y_cut = r
        if fl > r:
            prof.append((r, fl))
            y_cut = fl
    elif typ in ("DRILL", "TAP"):
        ang = taper if taper > 0.0 else (tipa if tipa > 0.0 else 118.0)
        tr = math.tan(math.radians(max(1.0, min(89.0, ang)) / 2.0))
        if tipd > 0.0:
            prof.append((tipd / 2.0, 0.0))
            tip_h = (r - tipd / 2.0) / tr if tr > 1e-9 else r
        else:
            tip_h = r / tr if tr > 1e-9 else r
        tip_h = max(0.0, tip_h)
        prof.append((r, tip_h))
        y_cut = tip_h
        if fl > tip_h:
            prof.append((r, fl))
            y_cut = fl
    elif typ in ("CHAMFER", "TAPER"):
        tr = math.tan(math.radians(max(1.0, min(89.0, taper if taper > 0.0 else 45.0))))
        if tipd > 0.0:
            prof.append((tipd / 2.0, 0.0))
            tip_h = (r - tipd / 2.0) / tr if tr > 1e-9 else r
        else:
            tip_h = r / tr if tr > 1e-9 else r
        tip_h = max(0.0, tip_h)
        prof.append((r, tip_h))
        y_cut = tip_h
        if fl > tip_h:
            prof.append((r, fl))
            y_cut = fl
    else:                                  # FLAT / BULL / LOLLIPOP / DOVETAIL / PROBE
        if 0.0 < cr < r:                   # rounded bottom corner (bull nose)
            prof.append((r - cr, 0.0))
            k_n = 8
            for k in range(1, k_n + 1):
                phi = -math.pi / 2.0 + (math.pi / 2.0) * k / k_n
                prof.append(((r - cr) + cr * math.cos(phi), cr + cr * math.sin(phi)))
            y_cut = cr
            if fl > cr:
                prof.append((r, fl))
                y_cut = fl
        else:
            prof.append((r, 0.0))
            prof.append((r, fl))
            y_cut = fl

    # ---- flutes -> shoulder -> body (exposed stick-out = body length) ----
    # The holder grips the shank at the body end, so the visible cutter runs
    # from the tip to BL (body length), as HSMWorks shows it; OL only caps it
    # (the body cannot exceed the tool's overall length). Radii: cutter `r`
    # over the flutes, shank `rb` (SD) over the body.
    rb = sd / 2.0 if sd > 0.0 else r
    fl_v = max(fl, y_cut)
    sl_v = max(sl if sl > 0.0 else fl_v, fl_v)
    bl_v = bl if bl > 0.0 else (ol if ol > 0.0 else sl_v)
    bl_v = max(bl_v, sl_v)
    if ol > 0.0:
        bl_v = min(bl_v, ol)                       # never longer than the tool
    if fl_v > y_cut:
        prof.append((r, fl_v))                     # straight flute section
    if sl_v > fl_v:
        prof.append((r, sl_v))                     # shoulder keeps cutter radius
    if abs(rb - r) > 1e-9:
        prof.append((rb, sl_v))                    # flat shoulder step to the shank
    if bl_v > sl_v:
        prof.append((rb, bl_v))                    # body up to the stick-out
    prof.append((0.0, bl_v))
    return prof


def _tool_outline_profile(profile, pad):
    """Dilate a (r, y) cutter profile outward by `pad` along its own normals.

    A radial-only offset leaves the tip apex and the top on the axis in place,
    so the outline collapses / z-fights with the mesh there (the "many
    converging lines" at a drill point). Offsetting every vertex along the
    profile normal (a proper outline/dilation) gives a uniform-thickness
    silhouette all the way around, including the tip and the top faces.

    Returns [] for an empty profile."""
    pts = [(float(r), float(y)) for r, y in profile]
    if len(pts) < 2:
        return pts
    # Outward normal of each segment in the (r, y) plane: outward = (ty, -tr)/L.
    seg = []
    for i in range(len(pts) - 1):
        tr = pts[i + 1][0] - pts[i][0]
        ty = pts[i + 1][1] - pts[i][1]
        L = math.hypot(tr, ty) or 1.0
        seg.append((ty / L, -tr / L))
    out = []
    for i, (r, y) in enumerate(pts):
        if i == 0:
            nr, ny = seg[0]
        elif i == len(pts) - 1:
            nr, ny = seg[-1]
        else:                                  # miter of the two adjacent edges
            nr = seg[i - 1][0] + seg[i][0]
            ny = seg[i - 1][1] + seg[i][1]
            L = math.hypot(nr, ny)
            nr, ny = (nr / L, ny / L) if L > 1e-9 else seg[i]
        out.append((max(0.0, r + nr * pad), y + ny * pad))
    return out


def _tool_exposed_length(info):
    """Visible cutter stick-out (mm) = profile top (body length, capped by OL)."""
    prof = _tool_profile(info) if info else None
    return max((p[1] for p in prof), default=0.0) if prof else 0.0


def _holder_profile(tool_len):
    """Holder as (radius, z0, z1) cylinders relative to the tool tip, in machine
    Z (up positive): the ER20 collet nut, then the holder body above it. Its
    bottom sits at the stick-out."""
    rn = _HOLDER_NUT_D / 2.0
    rb = _HOLDER_BODY_D / 2.0
    return [
        (rn, tool_len, tool_len + _HOLDER_NUT_H),
        (rb, tool_len + _HOLDER_NUT_H,
         tool_len + _HOLDER_NUT_H + _HOLDER_BODY_H),
    ]


def _holder_solid_profile():
    """(r, y) profile of a primitive ER20 collet nut + holder body for display.
    y = 0 at the holder bottom (sits at the tool stick-out), up positive."""
    rn = _HOLDER_NUT_D / 2.0
    rb = _HOLDER_BODY_D / 2.0
    hn = _HOLDER_NUT_H
    hb = _HOLDER_BODY_H
    return [
        (0.0, 0.0),
        (rn - 3.0, 0.0),          # bottom face
        (rn, 3.0),                # bottom chamfer
        (rn, max(3.0, hn - 6.0)), # knurled nut band
        (rn - 1.0, max(3.0, hn - 2.0)),   # top chamfer
        (rn - 5.0, hn),           # nut top face
        (rn - 5.0, hn + 2.0),
        (rb, hn + 6.0),           # widen into the holder body
        (rb, hn + hb),            # holder body (spindle side)
        (0.0, hn + hb),
    ]


def _holder_outline_profile():
    """Outer-surface profile of the holder for the inverted-hull outline.

    The flat bottom/top disks are dropped (axis points removed) so the enlarged
    copy does not show them as dark caps — only the side silhouette remains."""
    return [(r, y) for (r, y) in _holder_solid_profile() if r > 1e-6]


def _rings_segments(profile, segments=48):
    """Line segments for circles (rings) at every (r>0) profile vertex."""
    if not profile or segments < 3:
        return []
    two_pi = 2.0 * math.pi
    segs = []
    for r, y in profile:
        r = float(r)
        y = float(y)
        if r <= 1e-6:
            continue
        for s in range(segments):
            a0 = two_pi * s / segments
            a1 = two_pi * (s + 1) / segments
            segs.append(((r * math.cos(a0), y, r * math.sin(a0)),
                         (r * math.cos(a1), y, r * math.sin(a1))))
    return segs


def _profile_ring_points(profile, min_deg=20.0):
    """Profile vertices that are real edges (direction change > min_deg) plus the
    endpoints — the horizontal rings of the outline. Smooth sections (e.g. a ball
    nose) get no rings, so the wireframe does not turn into a cage."""
    pts = [(float(r), float(y)) for r, y in profile]
    n = len(pts)
    if n < 2:
        return []
    out = []
    cos_t = math.cos(math.radians(min_deg))
    for i in range(n):
        r, y = pts[i]
        if r <= 1e-6:
            continue
        if i == 0 or i == n - 1:
            out.append((r, y))
            continue
        d0 = (pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1])
        d1 = (pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
        L0 = math.hypot(d0[0], d0[1]) or 1.0
        L1 = math.hypot(d1[0], d1[1]) or 1.0
        c = (d0[0] * d1[0] + d0[1] * d1[1]) / (L0 * L1)
        if c < cos_t:
            out.append((r, y))
    return out


def _circle_rect_hit(cx, cy, r, xmin, xmax, ymin, ymax):
    """True when the circle (cx, cy, r) overlaps the AABB rectangle in XY."""
    dx = cx - min(max(cx, xmin), xmax)
    dy = cy - min(max(cy, ymin), ymax)
    return dx * dx + dy * dy <= r * r


def _scan_collisions(scene_pts, cum, total, stage_starts, stage_len, stock, step):
    """Holder-vs-stock collision scan along the tool path.

    `scene_pts`/`cum` are the sampled polyline (scene == machine mm);
    `stage_starts` are the per-stage start distances; `stage_len[i]` is the
    cutter stick-out of stage i; `stock` is (xmin, xmax, ymin, ymax, zmin, zmax)
    in machine coords. Returns merged collision ranges, each with a
    representative point in SCENE coords and the max penetration depth (mm)."""
    if scene_pts is None or cum is None or stock is None or total <= 0.0:
        return []
    xmin, xmax, ymin, ymax, zmin, zmax = stock
    n = len(cum)
    if n < 2 or not stage_starts:
        return []
    holders = [_holder_profile(L) for L in stage_len]

    def stage_at(d):
        lo, hi = 0, len(stage_starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if stage_starts[mid] <= d:
                lo = mid
            else:
                hi = mid - 1
        return lo

    out = []
    cur = None
    idx = 1                              # moving pointer into cum (d increases)
    d = 0.0
    while d <= total:
        while idx < n - 1 and float(cum[idx]) < d:
            idx += 1
        d0 = float(cum[idx - 1])
        d1 = float(cum[idx])
        t = 0.0 if d1 <= d0 else (d - d0) / (d1 - d0)
        p0 = scene_pts[idx - 1]
        p1 = scene_pts[idx]
        sx = float(p0[0]) + (float(p1[0]) - float(p0[0])) * t
        sy = float(p0[1]) + (float(p1[1]) - float(p0[1])) * t
        sz = float(p0[2]) + (float(p1[2]) - float(p0[2])) * t
        x = sx                            # machine X
        y = -sz                           # machine Y (scene z = -machine Y)
        z = sy                            # machine Z (scene y = machine Z)
        si = stage_at(d)
        L = stage_len[si] if si < len(stage_len) else 0.0
        worst = 0.0
        for (r, hz0, hz1) in holders[si]:
            hb = z + hz0                  # holder bottom (machine Z)
            if hb >= zmax:                # entirely above the stock top
                continue
            ht = z + hz1
            if ht <= zmin:                # entirely below the stock
                continue
            if not _circle_rect_hit(x, y, r, xmin, xmax, ymin, ymax):
                continue
            pen = min(ht, zmax) - max(hb, zmin)
            if pen > worst:
                worst = pen
        if worst > 0.05:
            if cur is None:
                cur = {"d": d, "depth": worst, "scene": [sx, sy, sz],
                       "scene_hb": [sx, sy + L, sz]}
            else:
                cur["depth"] = max(cur["depth"], worst)
        elif cur is not None:
            out.append(cur)
            cur = None
        d += step
    if cur is not None:
        out.append(cur)
    return out


def _tool_check_report(stage_tools, stages, scene_pts, cum, total,
                       machine_tools, limits):
    """Compare each NC tool with the machine's measured tool table (skeleton).

    For every tool the program uses: (a) does its number exist in the
    controller table, and (b) is the measured stick-out (length offset) at
    least the value the program assumes? A longer tool is allowed unless it
    would push the spindle past the Z axis travel limit. `machine_tools` maps
    number -> {"length": mm, "name": str, "enabled": bool}; `limits` may hold
    {"zMax": mm}. Returns a list of report rows."""
    if not stages or scene_pts is None or cum is None or total <= 0.0:
        return []
    if not machine_tools:
        return []                         # controller not wired yet -> nothing to check
    n = len(cum)
    starts = [float(s.get("start", 0.0)) * total for s in stages]
    zmin_stage = [None] * len(stages)
    zmax_nc = None
    j = 0
    for i in range(n):
        d = float(cum[i])
        while j + 1 < len(starts) and starts[j + 1] <= d:
            j += 1
        z = float(scene_pts[i][1])            # scene y == machine Z
        if zmin_stage[j] is None or z < zmin_stage[j]:
            zmin_stage[j] = z
        if zmax_nc is None or z > zmax_nc:
            zmax_nc = z

    by_tool = {}
    for k, s in enumerate(stages):
        info = stage_tools[k] if k < len(stage_tools) else {}
        num = int(info.get("number", 0)) if info else 0
        nc_len = float(info.get("overall_length", 0.0) or 0.0) if info else 0.0
        name = stage_label(info) if info else str(s.get("label", ""))
        z = zmin_stage[k]
        e = by_tool.get(num)
        if e is None:
            by_tool[num] = {"number": num, "name": name, "ncLength": nc_len, "zmin": z}
        else:
            if z is not None and (e["zmin"] is None or z < e["zmin"]):
                e["zmin"] = z
            if not e["ncLength"] and nc_len:
                e["ncLength"] = nc_len

    def mget(num, mt):
        if not mt:
            return None
        if num in mt:
            return mt[num]
        return mt.get(str(num))

    zmax_lim = float((limits or {}).get("zMax", 0.0) or 0.0) if limits else 0.0
    report = []
    for num in sorted(by_tool.keys()):
        e = by_tool[num]
        depth = abs(e["zmin"]) if e["zmin"] is not None else 0.0
        row = {"number": num, "name": e["name"],
               "ncLength": round(e["ncLength"], 2), "depth": round(depth, 2),
               "machineLength": None, "delta": None,
               "status": "unknown", "message": ""}
        mt = mget(num, machine_tools)
        if mt is None:
            row["status"] = "missing"
            row["message"] = "T%d нет в таблице стойки" % num
        else:
            ml = float(mt.get("length", 0.0) or 0.0)
            row["machineLength"] = round(ml, 2)
            row["delta"] = round(ml - e["ncLength"], 2)
            if not mt.get("enabled", True):
                row["status"] = "missing"
                row["message"] = "T%d отключён в стойке" % num
            elif ml + 1e-6 < e["ncLength"]:
                row["status"] = "short"
                row["message"] = ("Вылет T%d (%.1f) меньше заданного в УП (%.1f)"
                                  % (num, ml, e["ncLength"]))
            elif depth > 0.0 and ml + 1e-6 < depth:
                row["status"] = "short"
                row["message"] = ("Вылет T%d (%.1f) меньше глубины обработки (%.1f)"
                                  % (num, ml, depth))
            else:
                row["status"] = "ok"
                row["message"] = "OK"
                delta = ml - e["ncLength"]
                if limits and delta > 1e-6 and zmax_nc is not None \
                        and zmax_nc + delta > zmax_lim + 1e-6:
                    row["status"] = "limit"
                    row["message"] = ("T%d длиннее на %.1f мм — выход за лимит оси Z"
                                      % (num, delta))
        report.append(row)
    return report


# ==========================================================================
#  Geometry types
# ==========================================================================
@QmlElement
class ToolpathGeometry(QQuick3DGeometry):
    """Line geometry (primitive = Lines) with a per-vertex path parameter in
    TexCoord0.x (0..1). A custom material discards segments whose parameter is
    below the current simulation progress, hiding the already-cut path.

    The vertex buffer is stored as packed bytes (``_raw``) so a worker thread
    can build it and the GUI thread only has to install it (``set_packed``)."""

    STRIDE = 5 * 4                    # position (3f) + texcoord (2f)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._raw = b''                      # interleaved position(3f) + texcoord(2f)
        self._nv = 0                         # vertex count
        self._bounds = None                  # (x0,y0,z0,x1,y1,z1) scene space
        self._rebuild()

    # ---- API -------------------------------------------------------------
    @property
    def vertexCount(self):
        return self._nv

    def set_packed(self, raw, bounds, nv):
        """Install an already-packed line buffer (built off the GUI thread)."""
        self._raw = raw or b''
        self._nv = int(nv)
        self._bounds = bounds
        self._rebuild()


    # ---- build -----------------------------------------------------------
    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_LINES)
        if not self._raw:
            self.setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0))
            self.update()
            return
        self.setVertexData(QByteArray(self._raw))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_TEXCOORD0, 3 * 4, _F32)
        if self._bounds is not None:
            x0, y0, z0, x1, y1, z1 = self._bounds
        else:
            x0 = y0 = z0 = x1 = y1 = z1 = 0.0
        pad = 1.0
        self.setBounds(QVector3D(x0 - pad, y0 - pad, z0 - pad),
                       QVector3D(x1 + pad, y1 + pad, z1 + pad))
        self.update()


@QmlElement
class HLRLineGeometry(QQuick3DGeometry):
    """Classic-CAD outline renderer with correct visibility: each model
    edge becomes a camera-facing screen-space ribbon (two triangles),
    extruded to a constant pixel width in the vertex shader
    (stepledges.vert) and depth-tested against the opaque skin, so line
    parts behind the body are cut by the rasterizer live during orbiting.
    True GL_LINES exist at exactly 1 px and MSAA coverage thins grazing
    side-wall outlines to a hatch that vanishes on zoom-in; a solid 1.5 px
    ribbon takes antialiasing predictably and keeps the same crisp weight
    at any zoom. A geometric outward float about the model center
    (FLOAT_SCALE, ~0.1-0.2 mm) keeps a line off its own grazing surface -
    zoom-independent, unlike clip-space bias tricks.
    Vertex layout (12 floats, vec4-read safe): POSITION vec4 @0,
    NORMAL vec4 = unit segment direction @16, TEXCOORD0 vec4 = side
    (+-1) @32."""

    STRIDE = 12 * 4
    FLOAT_SCALE = 1.0012
    # cam-float (сдвиг к глазу вдоль луча зрения) переехал в stepledges.vert:
    # uniform camFloatMm, значение задаётся свойством материала в QML
    # (0.0 - видимые линии держит клип-сдвиг polyOffset, скрытые честно
    # режет буфер глубины).

    def __init__(self, parent=None):
        super().__init__(parent)
        self._raw = b''
        self._nv = 0
        self._bounds = None
        self._ref = None
        self._rebuild()

    def set_reference(self, center):
        """Model-frame point that endpoints float away from."""
        if _np is not None and center is not None:
            self._ref = _np.asarray(center, dtype=_np.float32).reshape(3)
        else:
            self._ref = None

    @property
    def vertexCount(self):
        return self._nv

    def set_segments(self, segs):
        """segs: (M,2,3) float array of endpoints, model raw frame.
        Radial FLOAT_SCALE float about _ref is baked here (static per
        upload); the view-ray cam-float lives in stepledges.vert (uniform
        eyeRaw + camFloatMm), so a camera move costs no repack of this
        buffer."""
        if _np is None or segs is None or len(segs) == 0:
            self._raw = b''
            self._nv = 0
            self._bounds = None
            self._rebuild()
            return
        a_ = _np.ascontiguousarray(_np.asarray(segs, dtype=_np.float32))
        if a_.ndim == 2:
            a_ = a_.reshape(-1, 2, 3)
        if self._ref is not None:
            a_ = self._ref[None, None, :] + \
                (a_ - self._ref[None, None, :]) * _np.float32(
                    self.FLOAT_SCALE)
        pa = a_[:, 0, :]
        pb = a_[:, 1, :]
        d = pb - pa
        ln = _np.linalg.norm(d, axis=1, keepdims=True)
        d = d / _np.where(ln == 0.0, 1.0, ln)
        # triangles (pa+, pb+, pb-) and (pa+, pb-, pa-)
        pick = _np.array([0, 1, 1, 0, 1, 0])
        sgn = _np.array([1.0, 1.0, -1.0, 1.0, -1.0, -1.0],
                        dtype=_np.float32)
        ends = _np.stack([pa, pb], axis=1)
        pts = ends[_np.arange(a_.shape[0])[:, None], pick]
        vert = _np.zeros((a_.shape[0], 6, 12), dtype=_np.float32)
        vert[:, :, 0:3] = pts
        vert[:, :, 4:7] = _np.broadcast_to(d[:, None, :],
                                          (a_.shape[0], 6, 3))
        vert[:, :, 8] = sgn[None, :]
        self._raw = vert.reshape(-1, 12).tobytes()
        self._nv = int(a_.shape[0] * 6)
        lo = _np.minimum(pa.min(axis=0), pb.min(axis=0))
        hi = _np.maximum(pa.max(axis=0), pb.max(axis=0))
        self._bounds = (float(lo[0]), float(lo[1]), float(lo[2]),
                        float(hi[0]), float(hi[1]), float(hi[2]))
        self._rebuild()

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_TRIANGLES)
        if not self._raw:
            self.setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0))
            self.update()
            return
        self.setVertexData(QByteArray(self._raw))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_NORMAL, 4 * 4, _F32)
        self.addAttribute(_TEXCOORD0, 8 * 4, _F32)
        if self._bounds is not None:
            x0, y0, z0, x1, y1, z1 = self._bounds
            pad = 1.0
            self.setBounds(QVector3D(x0 - pad, y0 - pad, z0 - pad),
                           QVector3D(x1 + pad, y1 + pad, z1 + pad))
        self.update()


@QmlElement
class GridGeometry(QQuick3DGeometry):
    """N x N grid of lines in the X/Z plane at height `level` (the floor).

    Each line is subdivided and carries a per-vertex RGBA color whose alpha
    falls smoothly to zero over the outer ``feather`` band, so the grid
    dissolves (soft-blurs) into the background at its edges.
    """

    STRIDE = 7 * 4                    # position (3f) + RGBA color (4f)
    sizeChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._size = 200.0
        self._step = 10.0
        self._level = 0.0
        self._cx = 0.0
        self._cz = 0.0
        self._feather = 0.45          # fraction of the half-size faded at the rim
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def size(self):
        return self._size

    @size.setter
    def size(self, v):
        self._size = float(v)
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def step(self):
        return self._step

    @step.setter
    def step(self, v):
        self._step = max(1.0, float(v))
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def level(self):
        return self._level

    @level.setter
    def level(self, v):
        self._level = float(v)
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def centerX(self):
        return self._cx

    @centerX.setter
    def centerX(self, v):
        self._cx = float(v)
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def centerZ(self):
        return self._cz

    @centerZ.setter
    def centerZ(self, v):
        self._cz = float(v)
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def feather(self):
        return self._feather

    @feather.setter
    def feather(self, v):
        self._feather = max(0.0, min(0.95, float(v)))
        self._rebuild()

    def _alpha_at(self, dist):
        """Soft edge falloff: 1.0 in the core, 0.0 at the rim (smoothstep)."""
        f = self._feather
        if f <= 1e-6:
            return 1.0
        t = (dist - (1.0 - f)) / f
        if t <= 0.0:
            return 1.0
        if t >= 1.0:
            return 0.0
        return 1.0 - (t * t * (3.0 - 2.0 * t))

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_LINES)
        half = self._size / 2.0
        step = self._step
        n = int(half // step)
        y = self._level
        cx = self._cx
        cz = self._cz
        segs = max(8, 2 * n)          # subdivisions along each line (smooth fade)
        buf = bytearray()
        lo = [math.inf, math.inf, math.inf]
        hi = [-math.inf, -math.inf, -math.inf]

        def vertex(x, z):
            d = (max(abs(x - cx), abs(z - cz)) / half) if half > 0.0 else 0.0
            a = self._alpha_at(d)
            buf.extend(struct.pack("<7f", x, y, z, 1.0, 1.0, 1.0, a))
            if x < lo[0]:
                lo[0] = x
            if y < lo[1]:
                lo[1] = y
            if z < lo[2]:
                lo[2] = z
            if x > hi[0]:
                hi[0] = x
            if y > hi[1]:
                hi[1] = y
            if z > hi[2]:
                hi[2] = z

        for i in range(-n, n + 1):
            c = i * step
            for k in range(segs):                       # line parallel to Z
                z0 = cz - half + (2.0 * half) * k / segs
                z1 = cz - half + (2.0 * half) * (k + 1) / segs
                vertex(cx + c, z0)
                vertex(cx + c, z1)
            for k in range(segs):                       # line parallel to X
                x0 = cx - half + (2.0 * half) * k / segs
                x1 = cx - half + (2.0 * half) * (k + 1) / segs
                vertex(x0, cz + c)
                vertex(x1, cz + c)

        self.setVertexData(QByteArray(bytes(buf)))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_COLOR, 3 * 4, _F32)
        self.setBounds(QVector3D(lo[0] - 1, lo[1] - 1, lo[2] - 1),
                       QVector3D(hi[0] + 1, hi[1] + 1, hi[2] + 1))
        self.update()
        self.sizeChanged.emit()


@QmlElement
class AxisArrowGeometry(QQuick3DGeometry):
    """Flat filled axis glyph (double-sided) in the XY plane:
    a rectangular shaft from x=0 plus a triangular head with apex at x=`length`."""

    STRIDE = 3 * 4
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._length = 12.0
        self._head_len = 3.5
        self._head_w = 2.0
        self._shaft_w = 0.7
        self.setPrimitiveType(_TRIANGLES)
        self._rebuild()

    @Property(float, notify=changed)
    def length(self):
        return self._length

    @length.setter
    def length(self, v):
        self._length = float(v)
        self._rebuild()

    @Property(float, notify=changed)
    def headLength(self):
        return self._head_len

    @headLength.setter
    def headLength(self, v):
        self._head_len = float(v)
        self._rebuild()

    @Property(float, notify=changed)
    def headWidth(self):
        return self._head_w

    @headWidth.setter
    def headWidth(self, v):
        self._head_w = float(v)
        self._rebuild()

    @Property(float, notify=changed)
    def shaftWidth(self):
        return self._shaft_w

    @shaftWidth.setter
    def shaftWidth(self, v):
        self._shaft_w = float(v)
        self._rebuild()

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_TRIANGLES)
        L = self._length
        s = L - self._head_len            # shaft ends where the head begins
        if s <= 0.0:
            s = L * 0.5
        sw = self._shaft_w
        hw = self._head_w
        front = [
            (0.0, -sw, 0.0), (0.0, sw, 0.0), (s, sw, 0.0),
            (0.0, -sw, 0.0), (s, sw, 0.0), (s, -sw, 0.0),
            (s, -hw, 0.0), (s, hw, 0.0), (L, 0.0, 0.0),
        ]
        back = []
        for i in range(0, len(front), 3):
            back += [front[i + 2], front[i + 1], front[i]]
        pts = front + back
        data, lo, hi = _pack(pts)
        self.setVertexData(data)
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.setBounds(QVector3D(lo[0] - 1, lo[1] - 1, lo[2] - 1),
                       QVector3D(hi[0] + 1, hi[1] + 1, hi[2] + 1))
        self.changed.emit()


@QmlElement
class MeshGeometry(QQuick3DGeometry):
    """Triangle mesh (position + flat normals) in scene coordinates."""

    STRIDE = 6 * 4
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data = b''                     # packed float32 position + normal
        self._nv = 0                         # vertex count
        self._stride = self.STRIDE           # 24 = position (3f) + normal (3f)
        self._lo = [0.0, 0.0, 0.0]
        self._hi = [0.0, 0.0, 0.0]
        self.setPrimitiveType(_TRIANGLES)
        self._rebuild()

    def set_triangles(self, verts, normals=None):
        if _np is not None and isinstance(verts, _np.ndarray) and verts.size:
            tri = _np.ascontiguousarray(verts, dtype=_np.float32).reshape(-1, 3, 3)
            if normals is not None:
                nrm = _np.ascontiguousarray(normals, dtype=_np.float32).reshape(-1, 3, 3)
            else:
                fn = _np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
                ln = _np.linalg.norm(fn, axis=1, keepdims=True)
                _np.divide(fn, _np.where(ln == 0.0, 1.0, ln), out=fn)
                nrm = _np.broadcast_to(fn[:, None, :], tri.shape)
            inter = _np.concatenate([tri, nrm], axis=2)
            self._data = _np.ascontiguousarray(inter, dtype=_np.float32).tobytes()
            self._nv = tri.shape[0] * 3
            self._lo, self._hi = _verts_bounds(verts)
            self._rebuild()
            return
        flat = array('f')
        lo = [math.inf, math.inf, math.inf]
        hi = [-math.inf, -math.inf, -math.inf]
        tri = list(verts)
        nv = 0
        for i in range(0, len(tri) - 2, 3):
            a, b, c = tri[i], tri[i + 1], tri[i + 2]
            ux = b[0] - a[0]
            uy = b[1] - a[1]
            uz = b[2] - a[2]
            wx = c[0] - a[0]
            wy = c[1] - a[1]
            wz = c[2] - a[2]
            nx = uy * wz - uz * wy
            ny = uz * wx - ux * wz
            nz = ux * wy - uy * wx
            ln = math.sqrt(nx * nx + ny * ny + nz * nz)
            if ln > 0.0:
                nx /= ln
                ny /= ln
                nz /= ln
            for p in (a, b, c):
                flat.extend((p[0], p[1], p[2], nx, ny, nz))
                nv += 1
                for j in range(3):
                    if p[j] < lo[j]:
                        lo[j] = p[j]
                    if p[j] > hi[j]:
                        hi[j] = p[j]
        self._data = flat.tobytes()
        self._nv = nv
        self._stride = self.STRIDE
        self._lo = lo if nv else [0.0, 0.0, 0.0]
        self._hi = hi if nv else [0.0, 0.0, 0.0]
        self._rebuild()

    def reset(self):
        self._data = b''
        self._nv = 0
        self._stride = self.STRIDE
        self._rebuild()

    @Property(int, notify=changed)
    def triangleCount(self):
        return self._nv // 3

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_TRIANGLES)
        if not self._data:
            self.setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0))
            self.update()
            self.changed.emit()
            return
        self.setVertexData(QByteArray(self._data))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_NORMAL, 12, _F32)
        lo, hi = self._lo, self._hi
        self.setBounds(QVector3D(lo[0], lo[1], lo[2]), QVector3D(hi[0], hi[1], hi[2]))
        self.update()
        self.changed.emit()


@QmlElement
class ToolGeometry(QQuick3DGeometry):
    """Solid of revolution (triangle mesh) from a 2D profile [(r, y), ...].

    Built from a tool-info dict so the panel draws the actual cutter shape
    (flat/ball/bull/chamfer/drill) instead of a fixed placeholder.

    Vertices carry analytic normals: each profile segment contributes its
    outward 2D normal (dy, -dr), rotated into place by the revolution angle
    for the side bands; horizontal segments (caps) land exactly on the axis,
    so the spindle cylinder reads as a continuous gradient (and the
    screen-space edge detector does not outline every facet)."""

    STRIDE = 6 * 4
    changed = Signal()
    _SEGMENTS = 48

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data = b''
        self._nv = 0
        self._lo = [0.0, 0.0, 0.0]
        self._hi = [0.0, 0.0, 0.0]
        self.setPrimitiveType(_TRIANGLES)
        self._rebuild()

    def set_profile(self, profile, segments=None):
        seg = int(segments) if segments else self._SEGMENTS
        prof = [(max(0.0, float(r)), float(y)) for r, y in (profile or [])]
        cleaned = []
        for p in prof:                       # drop consecutive duplicate points
            if not cleaned or abs(p[0] - cleaned[-1][0]) > 1e-9 \
                    or abs(p[1] - cleaned[-1][1]) > 1e-9:
                cleaned.append(p)
        if len(cleaned) < 2 or seg < 3:
            self._data = b''
            self._nv = 0
            self._rebuild()
            return
        two_pi = 2.0 * math.pi
        cos = [math.cos(two_pi * s / seg) for s in range(seg + 1)]
        sin = [math.sin(two_pi * s / seg) for s in range(seg + 1)]
        flat = array('f')
        lo = [math.inf, math.inf, math.inf]
        hi = [-math.inf, -math.inf, -math.inf]

        def vtx(x, y, z, nx, ny, nz):
            flat.extend((x, y, z, nx, ny, nz))
            if x < lo[0]: lo[0] = x
            if y < lo[1]: lo[1] = y
            if z < lo[2]: lo[2] = z
            if x > hi[0]: hi[0] = x
            if y > hi[1]: hi[1] = y
            if z > hi[2]: hi[2] = z

        for i in range(len(cleaned) - 1):
            r0, y0 = cleaned[i]
            r1, y1 = cleaned[i + 1]
            if r0 <= 1e-9 and r1 <= 1e-9:
                continue
            ur = r1 - r0
            uy = y1 - y0
            nl = math.sqrt(ur * ur + uy * uy) or 1.0
            npr, npy = uy / nl, -ur / nl      # segment outward normal (r, y)
            for s in range(seg):
                c0, s0 = cos[s], sin[s]
                c1, s1 = cos[s + 1], sin[s + 1]
                n0 = (npr * c0, npy, npr * s0)
                n1 = (npr * c1, npy, npr * s1)
                if r0 <= 1e-9:               # fan up from the axis
                    apex = (npr * (c0 + c1) * 0.5, npy, npr * (s0 + s1) * 0.5)
                    vtx(0.0, y0, 0.0, *apex)
                    vtx(r1 * c0, y1, r1 * s0, *n1)
                    vtx(r1 * c1, y1, r1 * s1, *n1)
                elif r1 <= 1e-9:             # fan down to the axis
                    apex = (npr * (c0 + c1) * 0.5, npy, npr * (s0 + s1) * 0.5)
                    vtx(r0 * c0, y0, r0 * s0, *n0)
                    vtx(r0 * c1, y0, r0 * s1, *n1)
                    vtx(0.0, y1, 0.0, *apex)
                else:                        # quad -> two triangles
                    vtx(r0 * c0, y0, r0 * s0, *n0)
                    vtx(r0 * c1, y0, r0 * s1, *n1)
                    vtx(r1 * c1, y1, r1 * s1, *n1)
                    vtx(r0 * c0, y0, r0 * s0, *n0)
                    vtx(r1 * c1, y1, r1 * s1, *n1)
                    vtx(r1 * c0, y1, r1 * s0, *n0)
        self._data = flat.tobytes()
        self._nv = len(flat) // 6
        self._lo = [lo[0], lo[1], lo[2]] if self._nv else [0.0, 0.0, 0.0]
        self._hi = [hi[0], hi[1], hi[2]] if self._nv else [0.0, 0.0, 0.0]
        self._rebuild()

    @Property(int, notify=changed)
    def triangleCount(self):
        return self._nv // 3

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_TRIANGLES)
        if not self._data:
            self.setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0))
            self.update()
            self.changed.emit()
            return
        self.setVertexData(QByteArray(self._data))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_NORMAL, 12, _F32)
        lo, hi = self._lo, self._hi
        self.setBounds(QVector3D(lo[0], lo[1], lo[2]), QVector3D(hi[0], hi[1], hi[2]))
        self.update()
        self.changed.emit()




# ==========================================================================
#  Controller (parser + two geometries + tool position)
# ==========================================================================
@QmlElement
class GcodeToolpath(QObject):
    toolChanged = Signal()
    ready = Signal()
    pathChanged = Signal()
    modelPathChanged = Signal()
    meshChanged = Signal()

    partTransformChanged = Signal()
    stockChanged = Signal()
    edgesReady = Signal(object)
    toolpathLoaded = Signal(int)      # (segment_count) tool path is on screen
    toolpathReady = Signal(object)    # worker -> main: (token, text, prepared)
    meshReady = Signal(object)        # worker -> main: (token, modelPath, verts)
    loadFinished = Signal()           # whole load done (mesh applied)
    loadingChanged = Signal()
    toolInfoChanged = Signal()        # UI_TOOL metadata / cutter shape changed
    collisionsChanged = Signal()      # holder-vs-stock collision scan updated
    toolCheckChanged = Signal()       # NC-vs-machine tool check updated

    def __init__(self, parent=None):
        super().__init__(parent)
        self._loading = False
        self._load_token = 0                # invalidates stale worker results
        # QQuick3DGeometry parent must be a QQuick3DObject, not a plain QObject,
        # so create the geometries unparented and keep Python references.
        self._rapid = ToolpathGeometry()
        self._feed = ToolpathGeometry()
        self._mesh = MeshGeometry()
        self._edges = ToolpathGeometry()
        self._step_edges = None                      # STEP outline, from the model read
        self._model_normals = None                   # STEP crease-smooth normals (F,3,3)
        self._rev_surfaces = None                    # analytic cylinders/cones from STEP
        self._tan_fams = None                        # precomputed tangent families
        self._mesh_topo = None                       # welded mesh topology (mesh silhouette)
        self._hlr_edges = HLRLineGeometry()          # GPU-tested B-rep outline
        self._hlr_tangents = HLRLineGeometry()       # GPU-tested silhouette
        self._eye_raw = QVector3D(0, 0, 0)           # eye in raw model frame
        self._tool_geo = ToolGeometry()
        self._tool_outline_geo = ToolGeometry()    # inverted-hull outline solid
        self._holder_geo = ToolGeometry()          # ER20 nut + spindle body
        self._holder_outline_geo = ToolGeometry()  # silhouette (inverted hull)
        self._tool_rings_geo = ToolpathGeometry()  # horizontal edge rings
        self._holder_rings_geo = ToolpathGeometry()
        QQmlEngine.setObjectOwnership(self._rapid, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._feed, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._mesh, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._edges, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._hlr_edges, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._hlr_tangents,
                                      QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._tool_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._tool_outline_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._holder_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._holder_outline_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._tool_rings_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._holder_rings_geo, QQmlEngine.CppOwnership)
        hprof = _holder_solid_profile()            # static holder geometry
        self._holder_geo.set_profile(hprof)
        self._holder_outline_geo.set_profile(
            _tool_outline_profile(_holder_outline_profile(), 0.1))
        raw, nv, bounds = _pack_edge_segments(_rings_segments(_profile_ring_points(hprof)))
        self._holder_rings_geo.set_packed(raw, bounds, nv)
        self._tool = [0.0, 0.0, 0.0]
        self._tool_info = {}                # parsed UI_TOOL metadata
        self._tool_length = 0.0             # cutter length (tip -> top, scene units)
        self._tool_radius = 0.0             # cutter max radius (for the outline width)
        self._holder_radius = max((r for r, _ in hprof), default=1.0)
        self._center = [0.0, 0.0, 0.0]      # scene-space center of the toolpath
        self._radius = 50.0                 # half diagonal of the toolpath bounds
        self._min_y = 0.0                   # lowest point of the toolpath (scene Y)
        self._sample_pts = None            # scene-space polyline (numpy or list)
        self._sample_cum = None            # cumulative distance per point
        self._sample_rapid = None          # per-point rapid flag
        self._sample_speeds = None         # per-point segment speed (mm/s)
        self._stages = []                  # operation stages [{start,end,label,tool}]
        self._stage_tools = []             # per-stage UI_TOOL info dicts
        self._move_length = 0.0             # total path length
        self._stock_bounds = None           # (xmin,xmax,ymin,ymax,zmin,zmax) machine
        self._holder_info = {}              # UI_HOLDER: {gauge, name} (optional)
        self._collisions = []               # holder-vs-stock collisions
        self._collision_token = 0           # invalidates stale collision scans
        self._machine_tools = {}            # controller tool table (skeleton)
        self._machine_limits = {}           # axis limits (skeleton)
        self._tool_check = []               # NC-vs-machine tool check report
        self._model_path = ""               # auto-detected STL/OBJ model (same name)
        self._rot = [0.0, 0.0, 0.0]         # CAD -> UP rotation (deg) from UI_ROTATION:
        self._model_pos = [0.0, 0.0, 0.0]   # scene-space offset from UI_ORIGIN:
        self._cad_matrix = None             # UI_CAD_MATRIX (9 numbers) if present
        self._cad_matrix_origin = None      # UI_CAD_MATRIX_ORIGIN (x, y, z) if present
        self._part_rot = QQuaternion(1.0, 0.0, 0.0, 0.0)   # final scene rotation
        self._part_pos = [0.0, 0.0, 0.0]                    # final scene position
        self._mesh_center = [0.0, 0.0, 0.0]                 # raw model AABB center
        self._mesh_lo = [0.0, 0.0, 0.0]
        self._mesh_hi = [0.0, 0.0, 0.0]
        self._has_mesh = False
        self._part_auto_center = True                       # rigid registration (HSM plane has no translation)
        self._stock_center = [0.0, 0.0, 0.0]                # stock (billet) center, scene coords
        self._stock_size = [0.0, 0.0, 0.0]                  # stock size, scene axes
        self._stock_lower_m = [0.0, 0.0, 0.0]               # stock min corner, machine coords
        self._has_stock = False
        self._edge_token = 0                                # guards async edge results
        # Feature-edge outline is built on a worker thread to open the view fast.
        self.edgesReady.connect(self._apply_edges)
        # Async load: worker prepares data, the GUI thread applies it.
        self.toolpathReady.connect(self._on_toolpath_ready)
        self.meshReady.connect(self._on_mesh_ready)
        self._part_offset = [                                # manual nudge (machine axes X, Y, Z)
            float(os.environ.get("CNC_OFS_MX", "0")),
            float(os.environ.get("CNC_OFS_MY", "0")),
            float(os.environ.get("CNC_OFS_MZ", "0")),
        ]
        # Deferred: let QML finish binding, then notify so bindings re-resolve.
        QTimer.singleShot(0, self.ready.emit)

    @Property(str, notify=modelPathChanged)
    def modelPath(self):
        return self._model_path

    # ---- CAD/model placement (driven by the UI_* tags) -------------------
    def _set_rot(self, x, y, z):
        self._rot = [x, y, z]

    def _set_model_pos(self, x, y, z):
        self._model_pos = [x, y, z]

    @Property(QQuaternion, notify=partTransformChanged)
    def partRotation(self):
        return self._part_rot

    @Property(QVector3D, notify=partTransformChanged)
    def partPosition(self):
        return QVector3D(self._part_pos[0], self._part_pos[1], self._part_pos[2])

    # ---- tool (from UI_TOOL) ---------------------------------------------
    def _apply_tool_info(self, info):
        """Store UI_TOOL metadata and rebuild the cutter mesh (tip at origin)."""
        self._tool_info = info or {}
        prof = _tool_profile(self._tool_info) if self._tool_info else None
        self._tool_length = max((p[1] for p in prof), default=0.0) if prof else 0.0
        self._tool_geo.set_profile(prof)
        # Silhouette outline (inverted hull): the profile dilated outward by a
        # uniform `pad`, rendered by the QML side with the near faces culled so
        # only the outer silhouette rim remains.
        maxr = max((p[0] for p in prof), default=0.0) if prof else 0.0
        self._tool_radius = maxr
        pad = max(0.12, 0.05 * maxr)
        self._tool_outline_geo.set_profile(
            _tool_outline_profile(prof, pad) if prof else None)
        raw, nv, bounds = _pack_edge_segments(
            _rings_segments(_profile_ring_points(prof)) if prof else [])
        self._tool_rings_geo.set_packed(raw, bounds, nv)
        self.toolInfoChanged.emit()

    @Property("QVariantMap", notify=toolInfoChanged)
    def toolInfo(self):
        return self._tool_info

    @Property(QObject, notify=toolInfoChanged)
    def toolGeometry(self):
        return self._tool_geo

    @Property(QObject, notify=toolInfoChanged)
    def toolOutline(self):
        """Enlarged cutter used for the silhouette outline (same local frame)."""
        return self._tool_outline_geo

    @Property(QObject, constant=True)
    def holderGeometry(self):
        """ER20 collet nut + spindle body solid (bottom at y=0, up positive)."""
        return self._holder_geo

    @Property(QObject, constant=True)
    def holderOutline(self):
        """Inverted-hull outline solid of the holder."""
        return self._holder_outline_geo

    @Property(QObject, notify=toolInfoChanged)
    def toolRings(self):
        """Horizontal edge rings of the cutter (line outline)."""
        return self._tool_rings_geo

    @Property(QObject, constant=True)
    def holderRings(self):
        """Horizontal edge rings of the holder (line outline)."""
        return self._holder_rings_geo

    @Property(float, notify=toolInfoChanged)
    def toolLength(self):
        return self._tool_length

    @Property(float, notify=toolInfoChanged)
    def toolRadius(self):
        """Max cutter radius (for the screen-space outline width)."""
        return self._tool_radius

    @Property(float, constant=True)
    def holderRadius(self):
        """Max holder radius (for the screen-space outline width)."""
        return self._holder_radius

    @Slot(int)
    def setToolStage(self, index):
        """Show the cutter of operation stage `index` (rebuild the mesh).

        Called by the viewer as the described tool changes with the program
        position / selected stage. Empty stages keep the current tool."""
        if not self._stage_tools:
            return
        i = max(0, min(len(self._stage_tools) - 1, int(index)))
        info = self._stage_tools[i]
        if info and info != self._tool_info:
            self._apply_tool_info(info)

    @Property(QVector3D, notify=stockChanged)
    def stockCenter(self):
        return QVector3D(self._stock_center[0], self._stock_center[1], self._stock_center[2])

    @Property(QVector3D, notify=stockChanged)
    def stockSize(self):
        return QVector3D(self._stock_size[0], self._stock_size[1], self._stock_size[2])

    @Property(bool, notify=stockChanged)
    def hasStock(self):
        return self._has_stock

    @Property(float, notify=pathChanged)
    def holderGauge(self):
        """Holder gauge length from UI_HOLDER (true stick-out; 0 if not exported)."""
        return float(self._holder_info.get("gauge", 0.0) or 0.0)

    # ---- collision analysis (holder vs stock), background -----------------
    @Property("QVariantList", notify=collisionsChanged)
    def collisions(self):
        """Holder-vs-stock collision ranges along the path (scene coords)."""
        return self._collisions

    @Property(int, notify=collisionsChanged)
    def collisionCount(self):
        return len(self._collisions)

    @Slot()
    def analyzeCollisions(self):
        """(Re)run the holder-vs-stock collision scan in the background."""
        self._start_collision_scan()

    # ---- NC-vs-machine tool check (skeleton; wire to the controller later) ----
    @Property("QVariantList", notify=toolCheckChanged)
    def toolCheck(self):
        """Per-tool: number match + measured stick-out vs the program value."""
        return self._tool_check

    @Property(int, notify=toolCheckChanged)
    def toolIssueCount(self):
        return sum(1 for r in self._tool_check if r.get("status") != "ok")

    @Slot("QVariantList")
    def setMachineTools(self, tools):
        """Feed the controller's measured tool table:
        [{number, length, name, enabled}] (length = measured stick-out, mm)."""
        self._machine_tools = {}
        for t in (tools or []):
            try:
                num = int(t.get("number"))
            except (TypeError, ValueError, AttributeError):
                continue
            self._machine_tools[num] = {
                "length": float(t.get("length", 0.0) or 0.0),
                "name": str(t.get("name", "")),
                "enabled": bool(t.get("enabled", True)),
            }
        self._rebuild_tool_check()

    @Slot("QVariantMap")
    def setMachineLimits(self, limits):
        """Feed machine axis limits, e.g. {"zMax": 0.0, "zMin": -400.0}."""
        self._machine_limits = dict(limits) if limits else {}
        self._rebuild_tool_check()

    @Slot()
    def runToolCheck(self):
        """Re-run the NC-vs-machine tool comparison."""
        self._rebuild_tool_check()

    def _rebuild_tool_check(self):
        try:
            self._tool_check = _tool_check_report(
                self._stage_tools, self._stages, self._sample_pts,
                self._sample_cum, self._move_length,
                self._machine_tools, self._machine_limits)
        except Exception as exc:
            print("[GcodeToolpath] tool check failed:", exc)
            self._tool_check = []
        self.toolCheckChanged.emit()

    def _start_collision_scan(self):
        """Kick off the holder-vs-stock collision scan on a worker thread."""
        if self._sample_pts is None or not self._stages or self._stock_bounds is None:
            self._collisions = []
            self.collisionsChanged.emit()
            return
        self._collision_token += 1
        total = self._move_length
        starts = [float(s.get("start", 0.0)) * total for s in self._stages]
        lens = [_tool_exposed_length(t) for t in self._stage_tools]
        gauge = float(self._holder_info.get("gauge", 0.0) or 0.0)
        if gauge > 0.0:
            lens = [gauge] * max(1, len(starts))   # true stick-out from UI_HOLDER
        if not lens:
            lens = [0.0]
        while len(lens) < len(starts):
            lens.append(lens[-1])
        threading.Thread(
            target=self._worker_collisions,
            args=(self._collision_token, self._sample_pts, self._sample_cum,
                  total, starts, lens, self._stock_bounds),
            daemon=True).start()

    def _worker_collisions(self, token, pts, cum, total, starts, lens, stock):
        try:
            res = _scan_collisions(pts, cum, total, starts, lens, stock, _COLLISION_STEP)
        except Exception as exc:
            print("[GcodeToolpath] collision scan failed:", exc)
            res = []
        try:
            if token != self._collision_token:
                return
            self._collisions = res
            self.collisionsChanged.emit()
        except RuntimeError:
            pass                       # app shutting down while the scan finishes

    def _rebuild_part_transform(self):
        """Final scene transform of the part: scene <- base <- WCS <- CAD."""
        q_base = QQuaternion.fromEulerAngles(-90.0, 0.0, 0.0)     # machine Z-up -> scene Y-up
        q_wcs = QQuaternion.fromEulerAngles(self._rot[0], self._rot[1], self._rot[2])
        t_wcs = QVector3D(self._model_pos[0], self._model_pos[1], self._model_pos[2])
        if self._cad_matrix:
            c0 = (self._cad_matrix[0], self._cad_matrix[1], self._cad_matrix[2])
            c1 = (self._cad_matrix[3], self._cad_matrix[4], self._cad_matrix[5])
            c2 = (self._cad_matrix[6], self._cad_matrix[7], self._cad_matrix[8])
            q_cad = quat_from_cols(c0, c1, c2)
            o = self._cad_matrix_origin or (0.0, 0.0, 0.0)
            t_cad = QVector3D(o[0], o[1], o[2])
        else:
            q_cad = QQuaternion(1.0, 0.0, 0.0, 0.0)   # no CAD transform available
            t_cad = QVector3D(0.0, 0.0, 0.0)
        q_m = q_wcs * q_cad                       # CAD -> machine rotation
        q = q_base * q_m
        if self._has_stock and self._has_mesh:
            # Asymmetric fit: align the model's rotated AABB min corner to the stock's min corner.
            rmin = [1e30, 1e30, 1e30]
            for cx in (self._mesh_lo[0], self._mesh_hi[0]):
                for cy in (self._mesh_lo[1], self._mesh_hi[1]):
                    for cz in (self._mesh_lo[2], self._mesh_hi[2]):
                        r = q_m.rotatedVector(QVector3D(cx, cy, cz))
                        rmin[0] = min(rmin[0], r.x())
                        rmin[1] = min(rmin[1], r.y())
                        rmin[2] = min(rmin[2], r.z())
            t = q_base.rotatedVector(QVector3D(self._stock_lower_m[0] - rmin[0],
                                               self._stock_lower_m[1] - rmin[1],
                                               self._stock_lower_m[2] - rmin[2]))
        elif self._part_auto_center:
            # Fallback: center on the toolpath.
            rc = q.rotatedVector(QVector3D(self._mesh_center[0], self._mesh_center[1], self._mesh_center[2]))
            t = QVector3D(self._center[0], self._center[1], self._center[2]) - rc
        else:
            t = q_base.rotatedVector(q_wcs.rotatedVector(t_cad) + t_wcs)
        t = t + QVector3D(*_to_3d(self._part_offset))   # machine-axis nudge -> scene
        self._part_rot = q
        self._part_pos = [t.x(), t.y(), t.z()]
        self.partTransformChanged.emit()

    def _set_model_path(self, path):
        if path != self._model_path:
            self._model_path = path
            self.modelPathChanged.emit()

    def _find_model(self, filepath):
        """Model sidecar resolution: STEP wins over STL.

        A STEP carries B-rep topology: the skin is tessellated from it and the
        outline comes from the model tree itself (real face boundaries,
        tangent seams included). Only when no STEP is present does the STL
        mesh serve as the model, with the outline recovered by dihedral-angle
        heuristics."""
        base = os.path.splitext(filepath)[0]
        for ext in (".step", ".stp"):
            p = base + ext
            if os.path.exists(p):
                self._set_model_path(os.path.abspath(p))
                return
        stl = base + ".stl"
        if not os.path.exists(stl):
            for ext in (".STL", ".Stl"):
                if os.path.exists(base + ext):
                    stl = base + ext
                    break
        self._set_model_path(os.path.abspath(stl) if os.path.exists(stl) else "")

    @staticmethod
    def _stl_normals(verts):
        """Crease-smooth vertex normals for an STL triangle soup (position
        list); None when numpy is absent (caller keeps flat normals)."""
        if _np is None or verts is None or len(verts) < 3:
            return None
        try:
            return _smooth_face_normals(_np.asarray(verts, dtype=_np.float64)
                                        .reshape(-1, 3, 3))
        except Exception:
            return None

    def _load_model_verts(self, model_path):
        """Read the model source on a worker thread: STEP returns (verts,
        pre-packed outline segments, smooth normals); STL returns
        (verts, None, smooth normals)."""
        if not model_path:
            return None, None, None
        low = model_path.lower()
        if low.endswith(".stl"):
            try:
                self._rev_surfaces = None
                self._tan_fams = None
                verts = load_stl_vertices(model_path)
                if _np is not None:
                    verts = _orient_shell_outward(verts).reshape(-1, 3)
                self._mesh_topo = _build_silhouette_topology(verts)
                return verts, None, self._stl_normals(verts)
            except Exception as exc:
                print("[GcodeToolpath] STL load failed:", exc)
                return None, None, None
        if low.endswith((".step", ".stp")):
            ck = _cache_key(model_path, "model") if _cache_enabled() else None
            hit = _cache_read_model(ck) if ck else None
            if hit is not None:
                if self._rev_surfaces is None:
                    try:
                        shape = _step_shape(model_path)
                    except Exception:
                        shape = None
                    self._rev_surfaces = load_step_surfaces(model_path,
                                                            shape=shape)
                    self._tan_fams = _tangent_families(self._rev_surfaces,
                                                       hit[1],
                                                       (hit[0], hit[2]))
                    # топология меша для силуэта: строится вместе с моделью
                    # (~137 мс на Test_model, ~45k треугольников) и живёт до
                    # смены модели; на камере силуэт считается по ней
                    self._mesh_topo = _build_silhouette_topology(hit[0])
                return hit
            try:
                shape = _step_shape(model_path)
                verts, segments, normals = load_step_model(model_path,
                                                           shape=shape)
                self._rev_surfaces = load_step_surfaces(model_path, shape=shape)
                packed = _pack_edge_segments(segments)
                self._tan_fams = _tangent_families(self._rev_surfaces, packed,
                                                   (verts, normals))
                self._mesh_topo = _build_silhouette_topology(verts)
                if ck and _np is not None and normals is not None:
                    _cache_write_model(ck, verts, normals, packed)
                return verts, packed, normals
            except Exception as exc:
                print("[GcodeToolpath] STEP load failed:", exc)
                stl = os.path.splitext(model_path)[0] + ".stl"
                if not os.path.exists(stl):
                    for ext in (".STL", ".Stl"):
                        if os.path.exists(os.path.splitext(model_path)[0] + ext):
                            stl = os.path.splitext(model_path)[0] + ext
                            break
                if os.path.exists(stl):
                    self._model_path = os.path.abspath(stl)
                    try:
                        sv = load_stl_vertices(self._model_path)
                        if _np is not None:
                            sv = _orient_shell_outward(sv).reshape(-1, 3)
                        self._mesh_topo = _build_silhouette_topology(sv)
                        return sv, None, None
                    except Exception:
                        pass
        return None, None, None

    @Property(QObject, notify=meshChanged)
    def partGeometry(self):
        return self._mesh

    @Property(QObject, notify=meshChanged)
    def partEdges(self):
        return self._edges

    @Property(QObject, notify=meshChanged)
    def hlrEdges(self):
        """B-rep outline in the depth-tested main-scene form (HLRLineGeometry
        fed by _set_edges_packed; the skin must be opaque for the test)."""
        return self._hlr_edges

    @Property(QObject, notify=meshChanged)
    def hlrTangents(self):
        """Silhouette lines, same GPU visibility treatment, updated
        by refreshTangents on every camera move."""
        return self._hlr_tangents

    @Slot(QVector3D)
    def refreshTangents(self, view_point):
        """Пересчёт силуэта на текущий глаз камеры.

        Глаз приходит в кадре сцены; в raw-кадр модели его переводит обратная
        трансформа детали (та же, что у видимой геометрии). Режим задаётся
        переменной окружения CNC_SILHOUETTE: 'mesh' (по умолчанию) - силуэт по
        топологии меша, 'analytic' - прежние касательные образующие
        цилиндров/конусов (оставлены для сравнения). Сегменты обоих режимов
        публикуются в один слой _hlr_tangents (HLRLineGeometry). Cam-float
        делает stepledges.vert по eyeRaw/camFloatMm, поэтому статические
        B-rep рёбра загружаются один раз на модель и на движение камеры не
        перепаковываются: раньше их репак на GUI-потоке стоил ~22 мс/кадр
        (замер на Test_model, 18986 сегментов)."""
        try:
            eye = QVector3D(view_point.x() - self._part_pos[0],
                            view_point.y() - self._part_pos[1],
                            view_point.z() - self._part_pos[2])
            e = self._part_rot.inverted().rotatedVector(eye)
            eye_raw = (e.x(), e.y(), e.z())
            self._eye_raw = QVector3D(*eye_raw)
            self.eyeChanged.emit()
            mode = os.environ.get("CNC_SILHOUETTE", "mesh").strip().lower()
            if mode == "analytic":
                if not self._rev_surfaces:
                    return
                if self._tan_fams is None:
                    self._tan_fams = _tangent_families(self._rev_surfaces,
                                                       self._step_edges)
                segments = _tangent_from_families(self._tan_fams, eye_raw)
            else:
                if self._mesh_topo is None:
                    return                  # модель без меша: силуэта нет
                segments = _mesh_silhouette_segments(self._mesh_topo, eye_raw)
            has = segments is not None and len(segments) > 0
            if _np is not None and has:
                self._sync_hlr_reference()
                self._hlr_tangents.set_segments(
                    _np.asarray(segments, dtype=_np.float32))
            else:
                self._hlr_tangents.set_segments(None)
        except Exception:
            pass

    eyeChanged = Signal()

    @Property(QVector3D, notify=eyeChanged)
    def eyeRaw(self):
        """Глаз камеры в raw-кадре модели (обновляется в refreshTangents).
        Привязан в QML к материалам рёбер: cam-float в stepledges.vert."""
        return self._eye_raw

    @Property(QVector3D, notify=meshChanged)
    def partMeshCenter(self):
        """AABB center of the raw part mesh (used to scale its outline about it)."""
        return QVector3D(self._mesh_center[0], self._mesh_center[1], self._mesh_center[2])

    @Property(float, notify=meshChanged)
    def partRadius(self):
        """Half-diagonal of the part mesh bounds (for the screen-space outline)."""
        if not self._has_mesh:
            return 1.0
        return 0.5 * math.sqrt(sum((self._mesh_hi[i] - self._mesh_lo[i]) ** 2
                                   for i in range(3)))

    def _load_model_mesh(self):
        """Synchronous path: read the auto-detected STEP/STL, then apply it."""
        with _occt:
            verts, segs, norms = self._load_model_verts(self._model_path)
        self._step_edges = segs
        self._model_normals = norms
        self._apply_mesh_verts(verts)

    def _apply_mesh_verts(self, verts):
        """Build the part mesh + its feature-edge outline (raw model coordinates).
        Vertices may be prepared by a worker thread; the outline is computed in
        the background too, so the view opens before the mesh is ready.
        Must run on the GUI thread (touches QQuick3DGeometry)."""
        self._mesh.reset()
        self._clear_edges()                  # fresh empty outline geometry
        self._hlr_tangents.set_segments(None)   # силуэт прошлой модели убран
        self._mesh_center = [0.0, 0.0, 0.0]
        self._has_mesh = False
        self._edge_token += 1
        token = self._edge_token
        if verts is not None and len(verts):
            try:
                # raw model coords (no scene mapping); STEP supplies crease-
                # grouped smooth normals, STL falls back to flat per-face
                self._mesh.set_triangles(verts, self._model_normals)
                lo, hi = _verts_bounds(verts)
            except Exception:
                lo = hi = [0.0, 0.0, 0.0]
                verts = None
        if verts is not None and len(verts):
            self._mesh_center = [(lo[i] + hi[i]) / 2.0 for i in range(3)]
            self._mesh_lo = lo
            self._mesh_hi = hi
            self._has_mesh = True
            threading.Thread(target=self._compute_edges,
                             args=(verts, token, self._model_path),
                             daemon=True).start()
            self._rebuild_part_transform()
        else:
            self._mesh_topo = None            # у модели без меша силуэта нет
        self.meshChanged.emit()

    def _clear_edges(self):
        """Publish a fresh empty outline geometry (a new object makes the QML
        Model re-upload it even when this runs after the first frame)."""
        g = ToolpathGeometry()
        QQmlEngine.setObjectOwnership(g, QQmlEngine.CppOwnership)
        self._edges = g
        self._hlr_edges.set_segments(None)

    def _set_edges_packed(self, packed):
        """Publish a pre-packed outline buffer (built by the worker thread)
        and mirror it into the depth-tested main-scene geometry."""
        raw, nv, bounds = packed
        g = ToolpathGeometry()
        QQmlEngine.setObjectOwnership(g, QQmlEngine.CppOwnership)
        g.set_packed(raw, bounds, nv)
        self._edges = g
        self._sync_hlr_reference()
        self._hlr_edges.set_segments(packed_positions(packed))

    def _sync_hlr_reference(self):
        """Share the model raw-frame AABB center with both line layers."""
        if _np is None or self._mesh_lo is None or self._mesh_hi is None:
            return
        c = (0.5 * (self._mesh_lo[0] + self._mesh_hi[0]),
             0.5 * (self._mesh_lo[1] + self._mesh_hi[1]),
             0.5 * (self._mesh_lo[2] + self._mesh_hi[2]))
        self._hlr_edges.set_reference(c)
        self._hlr_tangents.set_reference(c)

    def _compute_edges(self, verts, token, model_path=""):
        """Worker thread: build the packed part outline, hand it back.

        STEP model: the outline came with the model read (true B-rep edges,
        already in the skin's coordinate space) - just republish it. STL
        model: dihedral-angle feature edges of the mesh, disk-cached."""
        if self._step_edges is not None:
            packed = self._step_edges
            print("[outline] source=STEP segments=%d" % (packed[1] // 2))
        else:
            ck = _cache_key(model_path, "edges") if (model_path and _cache_enabled()) else None
            packed = _cache_read_edges(ck) if ck else None
            if packed is None:
                try:
                    packed = _feature_edges(verts, 20.0)
                except Exception:
                    packed = (b'', 0, None)
                if ck:
                    _cache_write_edges(ck, packed)
            print("[outline] source=MESH angle=20deg segments=%d" % (packed[1] // 2))
        try:
            self.edgesReady.emit((token, packed))
        except RuntimeError:
            pass                       # app shutting down while the edges finish

    def _apply_edges(self, payload):
        """Main thread: apply the outline if it still matches the current model."""
        token, packed = payload
        if token == self._edge_token:
            self._set_edges_packed(packed)
            self.meshChanged.emit()

    @Property(QObject, notify=ready)
    def rapidGeometry(self):
        return self._rapid

    @Property(QObject, notify=ready)
    def feedGeometry(self):
        return self._feed

    @Property(QVector3D, notify=pathChanged)
    def center(self):
        return QVector3D(self._center[0], self._center[1], self._center[2])

    @Property(float, notify=pathChanged)
    def radius(self):
        return self._radius

    # ---- ordered tool path (for animation) -------------------------------
    @Property(float, notify=pathChanged)
    def moveLength(self):
        return self._move_length

    @Property("QVariantList", notify=pathChanged)
    def stages(self):
        """Operation stages [{start, end, label}] (normalized path ranges)."""
        return self._stages

    def _index_at(self, d):
        """Index of the path segment containing distance `d` (binary search)."""
        cum = self._sample_cum
        n = len(cum) if cum is not None else 0
        if n < 2 or d <= 0.0:
            return 0
        if d >= cum[n - 1]:
            return n - 2
        if _np is not None and isinstance(cum, _np.ndarray):
            i = int(_np.searchsorted(cum, d, side="right")) - 1
        else:
            i = _bisect_right(cum, d) - 1
        if i < 0:
            return 0
        if i > n - 2:
            return n - 2
        return i

    @Slot(float, result=QVector3D)
    def sampleAt(self, d):
        """Interpolated tool position (scene coords) at path distance `d`.

        Sampling lives in C++ so QML never receives the per-point lists."""
        pts = self._sample_pts
        n = len(pts) if pts is not None else 0
        if n == 0:
            return QVector3D(0.0, 0.0, 0.0)
        if n < 2:
            p = pts[0]
            return QVector3D(float(p[0]), float(p[1]), float(p[2]))
        lo = self._index_at(d)
        hi = lo + 1
        cum = self._sample_cum
        span = float(cum[hi] - cum[lo])
        t = (d - float(cum[lo])) / span if span > 1e-9 else 0.0
        a = pts[lo]
        b = pts[hi]
        return QVector3D(float(a[0] + (b[0] - a[0]) * t),
                         float(a[1] + (b[1] - a[1]) * t),
                         float(a[2] + (b[2] - a[2]) * t))

    @Slot(float, result=bool)
    def isRapidAt(self, d):
        """True when the segment containing distance `d` is a rapid move."""
        rapid = self._sample_rapid
        n = len(rapid) if rapid is not None else 0
        if n == 0:
            return False
        return bool(rapid[self._index_at(d)])

    @Slot(float, result=float)
    def speedAt(self, d):
        """Scene speed (mm/s) of the segment containing distance `d`.

        Feed segments (G1/G2/G3) carry their programmed F (mm/min -> mm/s),
        so the tool animation runs 1:1 with the real machine; rapids (G0) use
        the machine rapid rate."""
        spds = self._sample_speeds
        n = len(spds) if spds is not None else 0
        if n == 0:
            return 0.0
        return float(spds[self._index_at(d)])

    @Property(bool, notify=loadingChanged)
    def loading(self):
        return self._loading

    def _set_loading(self, v):
        v = bool(v)
        if v != self._loading:
            self._loading = v
            self.loadingChanged.emit()

    @Slot(str, result=int)
    def load_gcode(self, filepath):
        """Synchronous load (used headless / by tests). Blocks the caller."""
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as fh:
                text = fh.read()
        except OSError as exc:
            print("[GcodeToolpath] cannot read '%s': %s" % (filepath, exc))
            return 0
        count = self.load_string(text)   # clears modelPath
        self._find_model(filepath)       # auto-loads <same name>.stl (if present)
        self._load_model_mesh()          # builds the part mesh
        return count

    @Slot(str, result=bool)
    def loadModel(self, filepath):
        """Загрузка только модели (STEP/STL) без траектории: отладочный путь
        для проверки обводки на голом теле. Синхронный."""
        fp = os.path.abspath(filepath)
        if not os.path.exists(fp):
            return False
        self._load_token += 1        # аннулирует незавершённые async-фазы
        self._set_model_path(fp)
        self._load_model_mesh()
        self.loadFinished.emit()
        return True

    @Slot(str, result=int)
    def load_gcode_async(self, filepath):
        """Load a .ngc on a worker thread so the GUI stays responsive.

        The tool path is applied (and shown) as soon as it is ready; the STL
        mesh is applied afterwards, still off the GUI thread. Progress is
        reported through `loading`, `toolpathLoaded`, `loadFinished`."""
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as fh:
                text = fh.read()
        except OSError as exc:
            print("[GcodeToolpath] cannot read '%s': %s" % (filepath, exc))
            return 0
        self._load_token += 1
        token = self._load_token
        self._set_loading(True)
        threading.Thread(target=self._worker_load,
                         args=(filepath, token, text), daemon=True).start()
        return 1

    def _worker_load(self, filepath, token, text):
        """Worker thread: parse + build buffers (pure), then read the STL.

        The prepared path is restored from the disk cache when possible, so a
        re-open of the same program skips parsing / buffer packing entirely."""
        try:
            ck = _cache_key(filepath, "path") if _cache_enabled() else None
            data = _cache_read_path(ck) if ck else None
            if data is None:
                ordered, _last, _seg, stages = parse_gcode(text)
                data = _prepare_ordered(ordered, stages)
                if ck:
                    _cache_write_path(ck, data)
        except Exception as exc:                       # never take the app down
            print("[GcodeToolpath] parse failed:", exc)
            self.loadFinished.emit()
            return
        if token != self._load_token:
            return
        self.toolpathReady.emit((token, text, data))

        # ---- phase B: model sidecar (STEP preferred, STL fallback). Only read
        # the raw vertices/edges here; the geometry is installed on the GUI
        # thread. ----
        model_path = ""
        verts = None
        segs = None
        norms = None
        base = os.path.splitext(filepath)[0]
        for ext in (".step", ".stp"):
            if os.path.exists(base + ext):
                model_path = os.path.abspath(base + ext)
                break
        if not model_path and os.path.exists(base + ".stl"):
            model_path = os.path.abspath(base + ".stl")
        if not model_path:
            for ext in (".STL", ".Stl"):
                if os.path.exists(base + ext):
                    model_path = os.path.abspath(base + ext)
                    break
        if model_path:
            with _occt:
                verts, segs, norms = self._load_model_verts(model_path)
        else:
            verts = None
        if token != self._load_token:
            return
        self._step_edges = segs
        self._model_normals = norms
        self.meshReady.emit((token, model_path, verts))

    def _on_toolpath_ready(self, payload):
        """GUI thread: apply the tool path prepared by the worker."""
        token, text, data = payload
        if token != self._load_token:
            return
        self._set_model_path("")
        self._apply_toolpath_data(data, text)
        self._mesh.reset()               # replaced by the mesh phase
        self._clear_edges()
        self.meshChanged.emit()
        self.toolpathLoaded.emit(data["seg_count"])

    def _on_mesh_ready(self, payload):
        """GUI thread: apply the STL mesh prepared by the worker."""
        token, model_path, verts = payload
        if token != self._load_token:
            return
        self._set_model_path(model_path)
        self._apply_mesh_verts(verts)
        self._set_loading(False)
        self.loadFinished.emit()

    def _apply_toolpath_data(self, data, text):
        """Apply parsed tags + prepared path data to the live properties."""
        self._set_rot(*parse_ui_rotation(text))              # UI_ROTATION: RX.. RY.. RZ..
        self._set_model_pos(*parse_ui_origin(text))          # UI_ORIGIN: X.. Y.. Z..
        self._cad_matrix = parse_ui_cad_matrix(text)         # UI_CAD_MATRIX (columns) if present
        self._cad_matrix_origin = parse_ui_cad_matrix_origin(text)
        # Per-stage cutter info (one UI_TOOL block per operation) so the drawn
        # tool changes together with the active stage / tool in the program.
        self._stage_tools = [s.get("tool", {}) for s in data.get("stages", [])]
        first_info = next((t for t in self._stage_tools if t), None)
        self._apply_tool_info(first_info if first_info else parse_ui_tool(text))
        self._holder_info = parse_ui_holder(text)            # UI_HOLDER: gauge (optional)
        st = parse_ui_stock(text)                            # UI_STOCK: billet bounds (WCS)
        if st:
            x0, x1, y0, y1, z0, z1 = st
            c = ((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0)
            s = (x1 - x0, y1 - y0, z1 - z0)
            self._stock_center = list(_to_3d(c))             # machine -> scene
            self._stock_size = [s[0], s[2], s[1]]            # (dx, dz, dy) in scene axes
            self._stock_lower_m = [x0, y0, z0]               # min corner, machine coords
            self._has_stock = True
        else:
            self._has_stock = False
        self._stock_bounds = tuple(st) if st else None       # machine AABB for collisions
        self.stockChanged.emit()
        self._rebuild_part_transform()

        self._set_tool(*data["last"])
        self._sample_pts = data["pts"]
        self._sample_cum = data["cum"]
        self._sample_rapid = data["rapid"]
        self._sample_speeds = data["speeds"]
        self._stages = data.get("stages", [])
        self._move_length = data["total"]
        self._rapid.set_packed(data["rapid_bytes"], data["bounds"], data["rapid_nv"])
        self._feed.set_packed(data["feed_bytes"], data["bounds"], data["feed_nv"])
        self._center = list(data["center"])
        self._radius = data["radius"]
        self._min_y = data["min_y"]
        self.pathChanged.emit()
        self._start_collision_scan()      # background holder-vs-stock check
        self._rebuild_tool_check()        # NC-vs-machine tool check (skeleton)

    @Slot(str, result=int)
    def load_string(self, text):
        self._set_model_path("")         # string input has no file to pair with
        ordered, _last, seg_count, stages = parse_gcode(text)
        data = _prepare_ordered(ordered, stages)
        self._apply_toolpath_data(data, text)
        self._mesh.reset()               # string input -> no part model
        self._clear_edges()
        self.meshChanged.emit()
        return seg_count

    @Property(float, notify=pathChanged)
    def minY(self):
        return self._min_y

    # ---- tool position ---------------------------------------------------
    @Property(float, notify=toolChanged)
    def toolX(self):
        return self._tool[0]

    @toolX.setter
    def toolX(self, v):
        self._set_tool(float(v), self._tool[1], self._tool[2])

    @Property(float, notify=toolChanged)
    def toolY(self):
        return self._tool[1]

    @toolY.setter
    def toolY(self, v):
        self._set_tool(self._tool[0], float(v), self._tool[2])

    @Property(float, notify=toolChanged)
    def toolZ(self):
        return self._tool[2]

    @toolZ.setter
    def toolZ(self, v):
        self._set_tool(self._tool[0], self._tool[1], float(v))

    @Property(QVector3D, notify=toolChanged)
    def toolPosition(self):
        p = _to_3d(self._tool)
        return QVector3D(p[0], p[1], p[2])

    def _set_tool(self, x, y, z):
        if (x, y, z) != (self._tool[0], self._tool[1], self._tool[2]):
            self._tool = [x, y, z]
            self.toolChanged.emit()


# ==========================================================================
#  Standalone smoke-test
# ==========================================================================
if __name__ == "__main__":
    import sys

    from PySide6.QtGui import QGuiApplication

    DEMO = "G0 Z10\nG0 X0 Y0\nG1 Z-2 F500\nG1 X50 Y0 F1500\nG1 X50 Y50\nG1 X0 Y50\nG1 X0 Y0\nG0 Z10\n"
    app = QGuiApplication(sys.argv)  # noqa: F841
    tp = GcodeToolpath()
    n = tp.load_string(DEMO)
    print("segments :", n)
    print("rapid vtx:", tp.rapidGeometry.vertexCount)
    print("feed vtx :", tp.feedGeometry.vertexCount)
    print("tool     :", tp.toolPosition)
