"""Инструмент и оправка: профили, кольца, вылет.

Вынесено из mock_backend.py (этап 0 плана переноса на C++). Константы оправки
живут здесь: их берут и коллизии.
"""
import math
import os

# Оправка (гайка ER20 и корпус шпинделя) над вылетом инструмента; на конкретном
# станке переопределяется окружением. Те же константы берут коллизии.
_HOLDER_NUT_D = float(os.environ.get("CNC_HOLDER_NUT_D", "33.0"))    # ER20 nut OD
_HOLDER_NUT_H = float(os.environ.get("CNC_HOLDER_NUT_H", "22.0"))    # ER20 nut length
_HOLDER_BODY_D = float(os.environ.get("CNC_HOLDER_BODY_D", "46.0"))  # holder body OD
_HOLDER_BODY_H = float(os.environ.get("CNC_HOLDER_BODY_H", "50.0"))  # holder body length

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

