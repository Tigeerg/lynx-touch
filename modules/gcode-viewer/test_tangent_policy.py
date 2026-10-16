#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless check: silhouette policy on the STEP model.

Covers the faults found through the 0.10.1 orbit sweeps:
  * static outline drawn twice (duplicate edge TShapes in STEP)
  * tangent lines offset 0.35-1.5 mm from a parallel static edge
  * rotating ghost pairs on coaxial steps of one hole
  * view tangents on convex blends / shallow dishes (spot-drills)
  * tangent endpoints outside the model envelope
and the 0.11.0 mesh silhouette (the default path): edges where the two
adjacent face normals sit on opposite sides of the eye, with no analytic
generators, arc masks or thresholds.
Run:  python modules/gcode-viewer/test_tangent_policy.py  (exit 0 = passed)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

from mock_backend import (load_step_model, load_step_surfaces,  # noqa: E402
                          _tangent_families, _tangent_from_families,
                          _build_silhouette_topology, _mesh_silhouette_segments,
                          _pack_edge_segments)

MODEL = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.STEP"))
# Простая диагностическая модель для приёмки силуэта (одно отверстие + четыре
# скругления R10). Файл может отсутствовать в дереве - тогда проверка
# пропускается, как и приватные образцы cad-testdata.
MODEL2 = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model_2.STEP"))


def _canon_dir(u):
    for c in (u[0], u[1], u[2]):
        if abs(c) > 1e-6:
            return -u if c < 0 else u
    return u


def main() -> int:
    if not os.path.exists(MODEL):
        print("MODEL NOT FOUND (passed over):", MODEL)
        return 0
    verts, segments, normals = load_step_model(MODEL)
    surfs = load_step_surfaces(MODEL)
    fams = _tangent_families(surfs, _pack_edge_segments(segments), (verts, normals))
    arr, cones = fams
    fails = []

    # -- 1) static outline holds no near-coincident parallel duplicates
    S = np.array([[a, b] for a, b in segments], dtype=float)
    A, B = S[:, 0], S[:, 1]
    D = B - A
    L = np.linalg.norm(D, axis=1)
    keep = L > 1e-6
    A, B, D, L = A[keep], B[keep], D[keep], L[keep]
    U = _np_canon = np.array([_canon_dir(u) for u in D / L[:, None]])
    buckets = {}
    for i, u in enumerate(U):
        buckets.setdefault(tuple(np.round(u, 3)), []).append(i)
    static_dupes = 0
    for idxs in buckets.values():
        idxs = np.array(idxs)
        for pi in range(len(idxs)):
            i = idxs[pi]
            w = A[idxs[pi + 1:]] - A[i]
            dist = np.linalg.norm(w - U[i] * (w @ U[i])[:, None], axis=1)
            t0 = (A[idxs[pi + 1:]] - A[i]) @ U[i]
            t1 = (B[idxs[pi + 1:]] - A[i]) @ U[i]
            ov = np.minimum(np.maximum(t0, t1), L[i]) - np.maximum(np.minimum(t0, t1), 0.0)
            static_dupes += int(np.count_nonzero((dist < 0.15) & (ov > 1.0)))
    if static_dupes:
        fails.append(f"static outline duplicates: {static_dupes}")

    # -- 2/3) orbit sweep: stable count, no ghosts near parallel static
    # Число цилиндрических касательных постоянно; конические образующие
    # существуют только в окне направлений (проверка 6) - в общий дрейф
    # не входят.
    ctr = np.array([s["o"] for s in surfs if s["k"] == "cyl"], dtype=float).mean(axis=0)
    ext = 200.0
    counts = set()
    ghosts = 0
    cyl_only = (arr, [])
    for i in range(24):
        az = i * math.pi / 12
        for el in (0.05, 0.3, 0.6, 1.0):
            E = (ctr[0] + ext * math.cos(el) * math.cos(az),
                 ctr[1] + ext * math.cos(el) * math.sin(az),
                 ctr[2] + ext * math.sin(el))
            segs = _tangent_from_families(fams, E)
            counts.add(len(_tangent_from_families(cyl_only, E)))
            for p0, q0 in segs:
                p = np.asarray(p0, dtype=float)
                q = np.asarray(q0, dtype=float)
                d = q - p
                ll = float(np.linalg.norm(d))
                if ll < 1e-6:
                    continue
                u = _canon_dir(d / ll)
                w = A - p
                dist = np.linalg.norm(w - (w @ u)[:, None] * u, axis=1)
                par = (np.abs(U @ u) > 0.999) & (dist < 0.45)
                if not par.any():
                    continue
                t0 = (A[par] - p) @ u
                t1 = (B[par] - p) @ u
                ov = np.minimum(np.maximum(t0, t1), ll) - np.maximum(np.minimum(t0, t1), 0.0)
                if np.any(ov > 2.0):
                    ghosts += 1
    # holes (26 concave families x 2) are never censored by the junction
    # band: their walls have no axial static edge. The convex outer rounds
    # may retract their generators into the junction edges per view, so the
    # total may sit between the hole floor 52 and 52 + 2*4 = 60.
    if min(counts) < 52:
        fails.append(f"hole tangents censored somewhere: {sorted(counts)}")
    if max(counts) > 60 or max(counts) - min(counts) > 8:
        fails.append(f"tangent drift beyond convex censoring: {sorted(counts)}")
    if ghosts:
        fails.append(f"ghost tangents within 1 mm of a static edge: {ghosts}")
    if not counts or min(counts) == 0:
        fails.append("hole tangents missing entirely")

    # -- 4) classification: convex blends and shallow dishes not tangented,
    #       the six counterbored bolt holes are.
    # bolt-hole axis lines (raw mm, from the STEP): axis Y, 6 positions
    bolt_axes = [(-40.0, 60.0), (40.0, 60.0), (44.0, 0.0),
                 (-44.0, 0.0), (40.0, -60.0), (-40.0, -60.0)]
    covered = 0
    O, Dv, R, F = arr["O"], arr["D"], arr["R"], arr["FULL"]
    yv = np.array([0.0, 1.0, 0.0])
    for bx, bz in bolt_axes:
        hit = False
        for i in range(O.shape[0]):
            if abs(float(Dv[i] @ yv)) < 0.999:
                continue
            # O already is the foot of the axis line (x, any y, z): the
            # through-wall section is the long one (span > 30 mm), R=2.75
            if abs(O[i][0] - bx) < 0.6 and abs(O[i][2] - bz) < 0.6:
                span = float(arr["T1"][i] - arr["T0"][i])
                if bool(F[i]) and span > 30.0 and abs(float(R[i]) - 2.75) < 0.1:
                    hit = True
        covered += int(hit)
    if covered < 6:
        fails.append(f"bolt-hole walls tangented: {covered}/6")
    # counterbore sections (the openings the user reads as 'the hole'):
    # depth 5.2 mm >= the 2.5 mm floor -> must be tangented
    cb = 0
    for i in range(O.shape[0]):
        if abs(float(Dv[i] @ yv)) > 0.999 and abs(float(R[i]) - 3.42) < 0.1 \
                and bool(F[i]):
            cb += 1
    if cb < 6:
        fails.append(f"bolt counterbores tangented: {cb}/6")
    for i in range(O.shape[0]):     # 45-deg edge blends and 1-2 mm dishes
        span = float(arr["T1"][i] - arr["T0"][i])
        if bool(F[i]) and span < 2.5:
            fails.append(f"shallow dish tangented at fam {i} "
                         f"(span {span:.1f} R {float(R[i]):.2f})")
    # Fillet tangent edges stay (the user reads them as the vertical edges of
    # the part); the round contour is carried by the analytic silhouette on
    # top of them.

    # -- 5) every tangent lies within the model envelope
    tris = np.asarray(verts, dtype=float).reshape(-1, 3, 3)
    lo = tris.min(axis=(0, 1)) - 1.0
    hi = tris.max(axis=(0, 1)) + 1.0
    E = (ctr[0] + ext, ctr[1], ctr[2] + 0.4 * ext)
    for p0, q0 in _tangent_from_families(fams, E):
        for c in (p0, q0):
            v = np.asarray(c, dtype=float)
            if np.any(v < lo) or np.any(v > hi):
                fails.append(f"tangent endpoint outside envelope: {np.round(v, 1)}")
                break

    # -- 6) cones: faces of the STEP must reach the descriptor list (14 cone
    #       faces in Test_model; a wrong OCP method name used to drop them all
    #       silently), and the policy keeps only deep concave ones: 45-deg
    #       edge chamfers of 0.7 mm slant would orbit as ghost pairs.
    n_cone_surfs = sum(1 for s in surfs if s["k"] == "cone")
    if n_cone_surfs == 0:
        fails.append("no cone descriptors loaded from STEP (cone faces dropped)")
    for c in cones:
        slant = float(c["s1"]) - float(c["s0"])
        if slant < 2.5:
            fails.append(f"shallow cone tangented (slant {slant:.2f} mm, "
                         f"half-angle {math.degrees(float(c['psi'])):.0f} deg)")
    if n_cone_surfs and not cones:
        fails.append("every cone dropped: drill-tip cones of the bolt holes "
                     "must keep their silhouette")
    # 6a) дедуп: одна конусная поверхность в двух face не должна рисовать
    #     две ленты в одной точке (z-fighting).
    # 6b) эмиссия ровно в геометрическом окне: образующая конуса существует,
    #     только если |cos(направление на глаз, ось)| <= cos(psi); тогда их
    #     ровно две, иначе ноль - «исчезновение у оси» здесь корректно.
    empty_arr = {k: v[:0] for k, v in arr.items()}
    dup_pairs = 0
    window_bad = 0
    cone_seen = 0
    for i in range(24):
        az = i * math.pi / 12
        for el in (0.3, 0.6):
            E = (ctr[0] + ext * math.cos(el) * math.cos(az),
                 ctr[1] + ext * math.cos(el) * math.sin(az),
                 ctr[2] + ext * math.sin(el))
            segs_k = _tangent_from_families((empty_arr, cones), E)
            cone_seen += int(bool(segs_k))
            if len(segs_k):
                Sk = np.array(segs_k, dtype=float)
                for a in range(len(Sk)):
                    for b in range(a + 1, len(Sk)):
                        same = (np.linalg.norm(Sk[a] - Sk[b], axis=1).max() < 0.01
                                or np.linalg.norm(Sk[a] - Sk[b][::-1],
                                                  axis=1).max() < 0.01)
                        dup_pairs += int(same)
            Ev = np.array(E, dtype=float)
            for c in cones:
                d = np.array(c["d"], dtype=float)
                w = Ev - np.array(c["a"], dtype=float)
                ct = abs(float(d @ w) / float(np.linalg.norm(w)))
                want = 2 if ct <= abs(math.cos(float(c["psi"]))) + 1e-9 else 0
                got = len(_tangent_from_families((empty_arr, [c]), E))
                window_bad += int(got != want)
    if dup_pairs:
        fails.append(f"duplicate cone silhouette drawn at one place: {dup_pairs}")
    if window_bad:
        fails.append(f"cone emission breaks the geometric window: {window_bad} views")
    if cones and cone_seen == 0:
        fails.append("cone silhouettes never emitted over the orbit")

    # -- 7) the analytic surface of a moved face must keep its true position:
    #       BRepAdaptor_Surface already applies the face TopLoc_Location, a
    #       second transform doubled the shift (axis 200 mm instead of 100 mm).
    from OCP.BRepPrimAPI import BRepPrimAPI_MakeCylinder
    from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Trsf, gp_Vec
    from OCP.TopLoc import TopLoc_Location
    cyl = BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 5.0, 20.0).Shape()
    shift = gp_Trsf()
    shift.SetTranslation(gp_Vec(100.0, 0.0, 0.0))
    moved = cyl.Moved(TopLoc_Location(shift))
    got = [s for s in load_step_surfaces("", shape=moved) if s["k"] == "cyl"]
    if not got:
        fails.append("moved cylinder produced no descriptor")
    else:
        ox = float(got[0]["o"][0])
        if abs(ox - 100.0) > 1e-6:
            fails.append(f"moved face transformed twice: axis x={ox:.1f} mm, "
                         f"expected 100.0")

    # -- 8) mesh silhouette (the default path). Topology from the same
    #       triangle soup; a silhouette edge is where the two adjacent face
    #       normals sit on opposite sides of the eye. Checks: the contour of a
    #       closed solid is never empty over the orbit and stays bounded, every
    #       endpoint lies inside the mesh envelope (no floating phantom line),
    #       and the six bolt-hole walls get a silhouette without any analytic
    #       generator.
    mtopo = _build_silhouette_topology(verts)
    mesh_counts = []
    mesh_segs = []
    covered_mesh = 0
    if mtopo is None:
        fails.append("mesh silhouette topology did not build")
    else:
        for i in range(24):
            az = i * math.pi / 12
            for el in (0.05, 0.3, 0.6, 1.0):
                E = (ctr[0] + ext * math.cos(el) * math.cos(az),
                     ctr[1] + ext * math.cos(el) * math.sin(az),
                     ctr[2] + ext * math.sin(el))
                ms = _mesh_silhouette_segments(mtopo, E)
                if ms is None:
                    fails.append("mesh silhouette returned None")
                    continue
                mesh_counts.append(int(len(ms)))
                mesh_segs.append(ms)
        if not mesh_counts or min(mesh_counts) == 0:
            fails.append("mesh silhouette empty at some orbit view")
        elif max(mesh_counts) > 5000:
            fails.append(f"mesh silhouette count out of range: "
                         f"{min(mesh_counts)}..{max(mesh_counts)}")
        mv = np.asarray(verts, float).reshape(-1, 3)
        lo = mv.min(axis=0) - 1.0
        hi = mv.max(axis=0) + 1.0
        out = 0
        for ms in mesh_segs:
            if len(ms):
                pts = ms.reshape(-1, 3)
                out += int(np.any((pts < lo) | (pts > hi), axis=1).sum())
        if out:
            fails.append(f"mesh silhouette endpoints outside envelope: {out}")
        yv = np.array([0.0, 1.0, 0.0])
        for bx, bz in bolt_axes:
            for ms in mesh_segs:
                if not len(ms):
                    continue
                dd = ms[:, 1] - ms[:, 0]
                ll = np.linalg.norm(dd, axis=1)
                u = dd / np.maximum(ll[:, None], 1e-9)
                mid = ms.mean(axis=1)
                rr = np.sqrt((mid[:, 0] - bx) ** 2 + (mid[:, 2] - bz) ** 2)
                if np.any((np.abs(u @ yv) > 0.9) & (np.abs(rr - 2.75) < 0.35)):
                    covered_mesh += 1
                    break
        if covered_mesh < 6:
            fails.append(f"mesh silhouette bolt-hole walls covered: "
                         f"{covered_mesh}/6")

    # -- 9) acceptance model Test_model_2 (one hole + four R10 rounds, loaded
    #       only if present). On the orbit the mesh silhouette must carry the
    #       outer corner round (no early vanishing) and must never place a line
    #       on the flat plane beside the round (the analytic-generator phantom
    #       went exactly there, ~28 deg past the real arc).
    if not os.path.exists(MODEL2):
        print("Test_model_2 not found (passed over):", MODEL2)
    else:
        v2, _s2, _n2 = load_step_model(MODEL2)
        t2 = _build_silhouette_topology(v2)
        tv = np.asarray(v2, float).reshape(-1, 3)
        c2 = tv.mean(axis=0)
        rounds = ((65.0, 40.0), (65.0, -40.0), (-65.0, 40.0), (-65.0, -40.0))
        counts2 = []
        plane_hits = 0
        round_hits = 0
        for i in range(36):
            az = i * math.pi / 18
            for el in (0.05, 0.25, 0.5, 0.8, 1.1):
                E = (c2[0] + ext * math.cos(el) * math.cos(az),
                     c2[1] + ext * math.cos(el) * math.sin(az),
                     c2[2] + ext * math.sin(el))
                ms = _mesh_silhouette_segments(t2, E)
                counts2.append(int(len(ms)))
                if not len(ms):
                    continue
                dd = ms[:, 1] - ms[:, 0]
                ll = np.linalg.norm(dd, axis=1)
                u = dd / np.maximum(ll[:, None], 1e-9)
                vert = np.abs(u[:, 1]) > 0.9
                mid = ms.mean(axis=1)
                # плоские стенки рядом с углом: +X-плоскость x=75, +Z-плоскость
                # z=50. Полоса +-2 мм у точки стыка исключена (там силуэт -
                # это кромка перехода, а не «фантом»)
                plane_hits += int((vert & (np.abs(mid[:, 0] - 75.0) < 0.15)
                                   & (mid[:, 2] > 25) & (mid[:, 2] < 50)
                                   & (np.abs(mid[:, 2] - 40.0) > 2.0)).sum())
                plane_hits += int((vert & (np.abs(mid[:, 2] - 50.0) < 0.15)
                                   & (mid[:, 0] > 50) & (mid[:, 0] < 75)
                                   & (np.abs(mid[:, 0] - 65.0) > 2.0)).sum())
                for ax, azc in rounds:
                    r = np.sqrt((mid[:, 0] - ax) ** 2 + (mid[:, 2] - azc) ** 2)
                    round_hits += int((vert & (np.abs(r - 10.0) < 0.25)).sum())
        if not counts2 or min(counts2) == 0:
            fails.append("mesh silhouette empty on Test_model_2")
        if plane_hits:
            fails.append(f"mesh silhouette phantom on the plane beside a "
                         f"round: {plane_hits}")
        if round_hits == 0:
            fails.append("mesh silhouette lost the R10 corner round")

    print(f"segments(static)={len(segments)} families={O.shape[0]} "
          f"tangented={int(F.sum())} cones={len(cones)} "
          f"orbit_counts={sorted(counts)} "
          f"bolt_walls_covered={covered}")
    if mesh_counts:
        print(f"mesh silhouette orbit counts="
              f"{min(mesh_counts)}..{max(mesh_counts)} "
              f"bolt_walls_covered={covered_mesh}")
    if fails:
        for f in fails:
            print("FAIL:", f)
        return 1
    print("PASSED: silhouette policy holds on Test_model")
    return 0


if __name__ == "__main__":
    sys.exit(main())
