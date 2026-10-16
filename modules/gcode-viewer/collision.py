"""Коллизии оправки с заготовкой и проверка инструмента по таблице стоек.

Вынесено из mock_backend.py (этап 0 плана переноса на C++).
"""
import os

from tooling import (_HOLDER_NUT_D, _HOLDER_NUT_H,                        # noqa: F401
                     _HOLDER_BODY_D, _HOLDER_BODY_H)


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


# Шаг выборки вдоль пути при скане коллизий (мм).
# Collision scan sampling step along the path (mm).
_COLLISION_STEP = float(os.environ.get("CNC_COLLISION_STEP", "1.0"))
