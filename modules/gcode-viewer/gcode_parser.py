"""Парсер управляющих программ: перемещения, дуги, циклы сверления, плоскости.

Вынесено из mock_backend.py (этап 0 плана переноса на C++): слой не зависит ни от
Qt, ни от numpy, поэтому переносится первым (этап 2 плана). parse_gcode возвращает
(ordered, последняя точка, число сегментов, этапы); ordered - точки пути в машинных
координатах.
"""
import math
import os
import re

from gv_types import PathPoint, Stage
from ui_tags import (parse_ui_tool, stage_label, _comment_text,          # noqa: F401
                     _TOOLCHANGE_RE, _ops_with_tools)

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


def _to_mms(mm_per_min):
    """Programmed feed F (mm/min, G94 default) -> scene mm/s (1:1)."""
    if mm_per_min and mm_per_min > 0.0:
        return mm_per_min / 60.0
    return _DEFAULT_FEED_MMS


def parse_gcode(text):
    """Return (ordered, last_machine_point, segment_count, stages).

    `ordered` is the full tool path in program order as PathPoint entries (each
    carries the scene speed of its outgoing segment); rapid and feed are split
    from it later. Handles line moves, arcs in every plane (G17/G18/G19) and the
    canned drilling cycles G73/G81/G82/G83.

    `stages` is a list of Stage (first_point_index, label, tool) operation blocks
    in program order. Operations come from the `(UI_OP: ...)` tags when present,
    else from the per-section operation-comment lines, else from the `UI_TOOL`
    tags (tool changes), else a single stage.
    """
    ordered = []                 # PathPoint entries in program order
    ui_tool_marks = []           # Stage from UI_TOOL tags (label + info)
    ui_op_marks = []             # Stage from explicit UI_OP tags
    op_marks = []                # Stage from operation-comment lines
    tool_marks = []              # Stage from tool changes (fallback)
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
            ordered.append(PathPoint(tuple(a), is_rapid, seg_speed))
            ordered.append(PathPoint(pb, is_rapid, seg_speed))
            return
        if decimate:
            # Only keep points at least `tol2`^0.5 away from the last kept point;
            # the polyline simply connects to it.
            last = ordered[-1].point
            dx = pb[0] - last[0]
            dy = pb[1] - last[1]
            dz = pb[2] - last[2]
            if dx * dx + dy * dy + dz * dz < tol2:
                return
            ordered[-1] = PathPoint(last, is_rapid, seg_speed)
            ordered.append(PathPoint(pb, is_rapid, seg_speed))
            return
        pa = tuple(a)
        if ordered[-1].point == pa:
            ordered[-1] = PathPoint(pa, is_rapid, seg_speed)
        else:
            ordered.append(PathPoint(pa, is_rapid, seg_speed))
        ordered.append(PathPoint(pb, is_rapid, seg_speed))

    for raw in text.splitlines():
        up = raw.upper()
        code_only = _COMMENT_RE.sub("", raw.split(";", 1)[0]).strip()
        standalone_comment = (code_only == "") and ("(" in raw)
        if "UI_TOOL" in up:
            # Tool change: gives the cutter geometry for the following section.
            info = parse_ui_tool(raw)
            ui_tool_marks.append(Stage(max(0, len(ordered) - 1), stage_label(info), info))
        elif "UI_OP" in up:
            # Explicit operation marker (newer post): unambiguous section start.
            inner = re.sub(r"(?i)^\s*UI_OP\s*:\s*", "", _comment_text(raw)).strip()
            ui_op_marks.append(Stage(max(0, len(ordered) - 1), inner or "Операция"))
        elif standalone_comment:
            # A bare comment after the program has started is the section's
            # operation-comment (one per operation). Skip UI tags and the tool
            # comment that directly follows a tool change.
            if seen_code:
                inner = _comment_text(raw)
                if inner and not inner.upper().startswith("UI_") \
                        and not _TOOLCHANGE_RE.search(prev_code):
                    op_marks.append(Stage(max(0, len(ordered) - 1), inner))
        else:
            mt = _TOOLCHANGE_RE.search(up)
            if mt:
                tool_marks.append(Stage(max(0, len(ordered) - 1), "T" + mt.group(1)))
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
    if ordered and ordered[-1].point != tuple(pos):
        ordered.append(PathPoint(tuple(pos), ordered[-1].is_rapid,
                                 ordered[-1].speed_mm_s))

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
        stages = [Stage(0, "УП")]
    # The first stage always starts at the very beginning of the program (any
    # leading approach rapids belong to it).
    stages[0] = Stage(0, stages[0].label, stages[0].info)
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