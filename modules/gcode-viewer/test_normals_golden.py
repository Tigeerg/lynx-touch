#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Golden test: crease-smooth normals of the STEP skin.

Pins the exact (F,3,3) float32 output of _smooth_face_normals on
CAD/Test_model.STEP so a rewrite (vectorized clustering) can be proven
bit-identical, not merely close. Run before changing normals code;
regenerate the golden only together with a deliberate visual decision.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

from mock_backend import load_step_model  # noqa: E402

STEP = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.STEP"))
GOLDEN = os.path.join(HERE, "normals_test_model.npy")


def current_normals():
    # load_step_model already returns the crease-smoothed (F,3,3) float32
    # normals computed after its global orientation fix
    _verts, _segments, normals = load_step_model(STEP)
    return normals


def main() -> int:
    n = current_normals()
    if "--update" in sys.argv:
        np.save(GOLDEN, n)
        print("golden written:", GOLDEN, n.shape, "checksum",
              float(np.abs(n.astype(np.float64)).sum()))
        return 0
    if not os.path.exists(GOLDEN):
        print("GOLDEN MISSING (passed over; run --update once):", GOLDEN)
        return 0
    g = np.load(GOLDEN)
    if g.shape != n.shape:
        print("FAIL: shape", g.shape, "->", n.shape)
        return 1
    bad = int(np.count_nonzero(g != n))
    worst = float(np.abs(g.astype(np.float64) -
                          n.astype(np.float64)).max()) if bad else 0.0
    print("triangles=%d differing-floats=%d max-delta=%.3e" % (n.shape[0], bad, worst))
    if bad:
        print("FAIL: normals changed from the golden output")
        return 1
    print("PASSED: normals bit-identical to golden")
    return 0


if __name__ == "__main__":
    sys.exit(main())
