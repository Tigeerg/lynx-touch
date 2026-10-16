"""Теги постпроцессора UI_*: поворот, начало координат, матрица, поворот вокруг
оси, инструмент, оправка, заготовка, ярлыки этапов.

Вынесено из mock_backend.py (этап 0 плана переноса на C++): слой зависит только от
re/math и QQuaternion.
"""
import math
import re

from PySide6.QtGui import QQuaternion

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