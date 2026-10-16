#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless check: контур меша состоит из отрезков, а не из облака швов.

Обводка STL строится по рёбрам меша, а у мелкой триангуляции (медиана ребра
Test_model.STL - 0.07 мм) контур ищется цепочками субмиллиметровых звеньев.
Лента обводки строится по двум концам отрезка, и на субпиксельном звене её
направление - шум: линия вырождается в бледную рябь. Поэтому цепочки рёбер
склеиваются и упрощаются (_simplify_edge_chains), а проверка следит, чтобы
контур остался набором осмысленных отрезков и не потерял покрытие.

До упрощения: 18986 сегментов, из них 18557 (97.7%) короче 0.5 мм.
После: 1435 сегментов, 51 (3.6%) короче 0.5 мм, суммарная длина 3996 мм.

Запуск: python modules/gcode-viewer/test_outline_segments.py (exit 0 = пойдено)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mock_backend import (load_stl_vertices, _feature_edges_np,  # noqa: E402
                          packed_positions, _np)

STL = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.STL"))

MAX_SEGMENTS = 5000         # облако швов давало 18986
MAX_SHORT_SHARE = 0.10      # доля сегментов короче 0.5 мм
MIN_TOTAL_MM = 3000.0       # покрытие контура не должно просесть


def main() -> int:
    if not os.path.exists(STL):
        print("STL: пропущено (нет %s)" % STL)
        return 0

    soup = _np.asarray(load_stl_vertices(STL), dtype=_np.float64)
    packed = _feature_edges_np(soup, 20.0)
    if not packed.raw or packed.nv < 2:
        print("FAIL: контур пуст")
        return 1
    segs = _np.asarray(packed_positions(packed), dtype=_np.float64)
    ln = _np.linalg.norm(segs[:, 1] - segs[:, 0], axis=1)
    short = float((ln < 0.5).mean())
    total = float(ln.sum())
    print("сегментов=%d  коротких(<0.5мм)=%.1f%%  суммарная длина=%.1f мм  медиана=%.2f мм"
          % (len(segs), 100.0 * short, total, float(_np.median(ln))))

    ok = True
    if len(segs) > MAX_SEGMENTS:
        print("FAIL: сегментов %d, предел %d - контур снова стал облаком швов"
              % (len(segs), MAX_SEGMENTS))
        ok = False
    if short > MAX_SHORT_SHARE:
        print("FAIL: короче 0.5 мм - %.1f%% сегментов, предел %.0f%%: звенья не склеены"
              % (100.0 * short, 100.0 * MAX_SHORT_SHARE))
        ok = False
    if total < MIN_TOTAL_MM:
        print("FAIL: суммарная длина %.1f мм меньше %.0f - контур потерял покрытие"
              % (total, MIN_TOTAL_MM))
        ok = False
    print("RESULT: %s" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
