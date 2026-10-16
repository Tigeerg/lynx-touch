#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Performance baseline for the gcode-viewer pipeline (QtQuick3D backend).

Measures the CPU stages the GUI thread and load workers spend, on the real
project data (CAD/Test_model.ngc + .step + .stl). Run before and after an
optimization to get a comparable delta:

    python modules/gcode-viewer/bench_viewer.py            # all stages
    python modules/gcode-viewer/bench_viewer.py hot        # interactive path only

Stages and their interactive meaning:
  gcode.parse          program load (worker thread)
  gcode.prepare        program load (worker thread)
  step.read            first open of a STEP (B-rep, tessellation, normals,
                       edge sampling, coalescing) - worker thread
  step.smooth_normals  the same load, crease clustering alone - worker thread
  step.pack            outline buffer pack - GUI install
  step.surfaces        analytic surface list from the B-rep - worker thread
  families.build       once per load: tangent family precompute (worker B)
  frame.refresh        per camera event: tangent recompute (GUI thread!)
  frame.pack           per camera event: tangent buffer bytes (GUI thread!)
  stl.read/stl.feature_edges  the STEP-less fallback path (worker thread)

Interactive budget: the orbit drives camera.position -> refreshTangents ->
frame.refresh + frame.pack on the GUI thread; keep that pair comfortably
under one 60 fps frame (16.6 ms) so the orbit never drops frames. The
numbers below are the machine this baseline was recorded on; a regression
check fails only the interactive stages (they gate the UI), load stages
print for information.
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import mock_backend as mb  # noqa: E402

NGC = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.ngc"))
STEP = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.STEP"))
STL = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.stl"))

REFRESH_BUDGET_MS = 4.0          # hard ceiling: refresh+pack per camera event
REFRESH_TARGET_MS = 1.0          # optimization goal

N_FRAMES = 300


def ms(sec):
    return sec * 1000.0


def timed(label, fn, repeat=1):
    best = None
    for _ in range(max(1, repeat)):
        t0 = time.perf_counter()
        out = fn()
        dt = time.perf_counter() - t0
        best = dt if best is None else min(best, dt)
    print("%-22s %10.2f ms" % (label, ms(best)))
    return best, out


def main():
    only_hot = len(sys.argv) > 1 and sys.argv[1] == "hot"
    if not os.path.exists(NGC):
        print("SKIPPED: no", NGC)
        return 0
    results = {}

    # ---- gcode load path (worker thread in the app) --------------------
    with open(NGC, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    t, parsed = timed("gcode.parse", lambda: mb.parse_gcode(text))
    results["gcode.parse"] = ms(t)
    ordered, _last, nmove, stages = parsed
    t, prep = timed("gcode.prepare", lambda: mb._prepare_ordered(ordered, stages))
    results["gcode.prepare"] = ms(t)
    print("  moves=%d feed nv=%d rapid nv=%d" %
          (nmove, prep.feed_nv, prep.rapid_nv))

    if os.path.exists(STEP):
        # ---- cold STEP open: one B-rep read shared by skin and surfaces ----
        # (mirrors _load_model_verts: _step_shape once, then load_step_model
        # and load_step_surfaces on the same shape)
        t, shape = timed("step.shape_read", lambda: mb._step_shape(STEP))
        results["step.shape_read"] = ms(t)
        t, soup = timed("step.read",
                        lambda: mb.load_step_model(STEP, shape=shape), repeat=1)
        results["step.read"] = ms(t)
        verts, segments, normals = soup
        print("  tris=%d edges=%d" % (len(verts) // 3, len(segments)))

        def _smooth():
            T = mb._np.asarray(verts, dtype=mb._np.float64).reshape(-1, 3, 3)
            return mb._smooth_face_normals(T)
        t, _ = timed("step.smooth_normals", _smooth, repeat=1)
        results["step.smooth_normals"] = ms(t)

        t, packed = timed("step.pack", lambda: mb._pack_edge_segments(segments))
        results["step.pack"] = ms(t)

        if only_hot:
            surfs = mb.load_step_surfaces(STEP, shape=shape)
            fams = mb._tangent_families(surfs, packed, (verts, normals))
        else:
            t, surfs = timed("step.surfaces",
                             lambda: mb.load_step_surfaces(STEP, shape=shape))
            results["step.surfaces"] = ms(t)
            t, fams = timed("families.build",
                            lambda: mb._tangent_families(surfs, packed,
                                                          (verts, normals)),
                            repeat=3)
            results["families.build"] = ms(t)

    # ---- interactive orbit path (GUI thread) ----------------------------
    arr, cones = fams.cylinders, fams.cones
    O = arr.O
    cx = float(O[:, 0].mean()) if O.shape[0] else 0.0
    cy = float(O[:, 1].mean()) if O.shape[0] else 0.0
    cz = float(O[:, 2].mean()) if O.shape[0] else 0.0
    ext = 200.0

    def eye_at(k):
        a = k * 2 * math.pi / 97.0
        return (cx + ext * math.cos(a), cy + ext * math.sin(a) * 0.6,
                cz + ext * math.cos(a * 0.7) * 0.8)

    def _refresh():
        segs = mb._tangent_from_families(fams, eye_at(0))
        return mb._pack_edge_segments(segs)

    t0 = time.perf_counter()
    for k in range(N_FRAMES):
        segs = mb._tangent_from_families(fams, eye_at(k))
        mb._pack_edge_segments(segs)
    per = ms((time.perf_counter() - t0) / N_FRAMES)
    results["frame.refresh+pack"] = per
    t, _ = timed("frame.refresh", lambda: mb._tangent_from_families(fams, eye_at(3)),
                 repeat=200)
    results["frame.refresh"] = ms(t)
    segs = mb._tangent_from_families(fams, eye_at(3))
    t, _ = timed("frame.pack", lambda: mb._pack_edge_segments(segs), repeat=200)
    results["frame.pack"] = ms(t)
    print("  sustained: ~%.0f camera-move events/s in the GUI thread" %
          (1000.0 / max(per, 1e-6)))

    # ---- STL feature-edge path (no STEP models) -------------------------
    if os.path.exists(STL) and not only_hot:
        t, stlv = timed("stl.read", lambda: mb.load_stl_vertices(STL))
        results["stl.read"] = ms(t)
        t, _ = timed("stl.feature_edges", lambda: mb._feature_edges_np(stlv, 25.0))
        results["stl.feature_edges"] = ms(t)

    # ---- verdict ---------------------------------------------------------
    bad = []
    if results["frame.refresh"] + results["frame.pack"] > REFRESH_BUDGET_MS:
        bad.append("frame refresh+pack %.2f ms > budget %.2f ms" %
                   (results["frame.refresh"] + results["frame.pack"], REFRESH_BUDGET_MS))
    if len(segs) == 0:
        bad.append("frame.refresh produced no tangents")
    for k in bad:
        print("FAIL:", k)
    if bad:
        return 1
    print("PASSED: interactive stages within %.1f ms budget "
          "(target %.1f ms for the next optimization round)"
          % (REFRESH_BUDGET_MS, REFRESH_TARGET_MS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
