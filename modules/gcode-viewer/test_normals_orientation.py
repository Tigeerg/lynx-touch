#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless check: нормали кожи смотрят наружу на обеих ветках импорта.

Знак нормали задаёт освещение грани: вывернутая нормаль освещает карман или
отверстие со изнанки. Эталон - геометрическая нормаль обхода треугольника
(векторное произведение его рёбер): у замкнутого тела положительный знаковый
объём означает, что обход направлен наружу, поэтому нормаль вершины обязана
лежать в той же полусфере. Проверка ловит разворот нормалей на вогнутых
участках; пофейсовый разворот по центроиду давал 29.5% наружу на STEP и 12.8%
на STL.

Запуск: python modules/gcode-viewer/test_normals_orientation.py (exit 0 = пойдено)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mock_backend import (load_step_model, load_stl_vertices,  # noqa: E402
                          GcodeToolpath, _np)

STEP = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.STEP"))
STL = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.STL"))

MIN_OUTWARD = 0.99      # доля слотов нормалей в полусфере обхода


def _outward_share(verts, normals):
    """Знаковый объём оболочки и доля нормалей в полусфере обхода."""
    tri = _np.asarray(verts, dtype=_np.float64).reshape(-1, 3, 3)
    gn = _np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ln = _np.linalg.norm(gn, axis=1, keepdims=True)
    gn = gn / _np.where(ln == 0.0, 1.0, ln)
    signed_volume = float(_np.einsum("ij,ij->i", tri[:, 0],
                                     _np.cross(tri[:, 1], tri[:, 2])).sum() / 6.0)
    dots = _np.einsum("tik,tk->ti", _np.asarray(normals, dtype=_np.float64), gn)
    return signed_volume, float((dots > 0.0).mean())


def _check(label, verts, normals):
    if verts is None or normals is None:
        print("%s: пропущено (нет модели либо нет numpy/OCP)" % label)
        return True
    tri = _np.asarray(verts).reshape(-1, 3, 3)
    signed_volume, share = _outward_share(verts, normals)
    print("%s: треугольников=%d  знаковый объём=%.1f  нормалей наружу=%.2f%%"
          % (label, tri.shape[0], signed_volume, 100.0 * share))
    if signed_volume <= 0.0:
        print("FAIL %s: обход оболочки направлен внутрь - эталон неприменим" % label)
        return False
    if share < MIN_OUTWARD:
        print("FAIL %s: наружу смотрят %.2f%% нормалей, нужно не меньше %.0f%%"
              % (label, 100.0 * share, 100.0 * MIN_OUTWARD))
        return False
    return True


def main() -> int:
    if _np is None:
        print("numpy отсутствует - проверка пропущена")
        return 0
    ok = True
    if os.path.exists(STEP):
        verts, _segments, normals = load_step_model(STEP)
        ok = _check("STEP", verts, normals) and ok
    else:
        print("STEP: пропущено (нет %s)" % STEP)
    if os.path.exists(STL):
        soup = load_stl_vertices(STL)
        ok = _check("STL", soup, GcodeToolpath._stl_normals(soup)) and ok
    else:
        print("STL: пропущено (нет %s)" % STL)
    print("RESULT: %s" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
