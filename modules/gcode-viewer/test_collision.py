#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless check: коллизии оправки с заготовкой (collision.py).

Закрывает пункт DESIGN.md, раздел 12 («нет теста коллизий»). Особый повод: скан
коллизий однажды МОЛЧА падал — при выносе слоя из mock_backend потерялся импорт
`_holder_profile`, исключение гасилось в `except` воркера, и прогон печатал
«loaded OK», хотя коллизии не считались. Поэтому проверки ниже устроены так:

  * сначала юнит-уровень: `_circle_rect_hit` на попадании, касании границы
    (границы включительно), промахе и углу прямоугольника;
  * затем `_scan_collisions` на СИНТЕТИЧЕСКОЙ сцене из точек, где пересечение
    заведомо есть и где его заведомо нет, с привязкой к конкретной глубине
    врезания и точке пути;
  * отдельно проверяется, что исключение изнутри скана НЕ проглатывается
    (подмена импорта на падающий) — тогда дефект вида «потерян импорт» валит тест;
  * и только потом прогон на реальном УП data/cad-testdata/Test_model.ngc:
    функция обязана вернуть непустой список диапазонов осмысленной формы.

Тест вызывает `_scan_collisions` напрямую (не через воркер с `except`), поэтому
любая ошибка внутри — это падение теста, а не тихий пустой результат.

Запуск: python modules/gcode-viewer/test_collision.py (exit 0 = пойдено)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import collision  # noqa: E402

from mock_backend import (_circle_rect_hit, _scan_collisions,  # noqa: E402
                          _COLLISION_STEP, _tool_exposed_length,
                          parse_gcode, _prepare_ordered)

NGC = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata",
                                   "Test_model.ngc"))

# Заготовка в МАШИННЫХ координатах: (xmin, xmax, ymin, ymax, zmin, zmax), мм.
STOCK = (-20.0, 20.0, -20.0, 20.0, -100.0, 0.0)
STICKOUT = 30.0          # вылет инструмента (низ оправки над острием), мм


def _scene(points):
    """Сцена (X, Z, -Y) из точек: полилиния и накопленная длина."""
    pts = [[float(x), float(y), float(z)] for x, y, z in points]
    cum = [0.0]
    for i in range(1, len(pts)):
        d = [pts[i][k] - pts[i - 1][k] for k in range(3)]
        cum.append(cum[-1] + math.sqrt(sum(v * v for v in d)))
    return pts, cum


def main() -> int:
    fails = []

    def want(cond, msg):
        if not cond:
            fails.append(msg)

    # -- 1) _circle_rect_hit: попадание, касание, промах, углы
    want(_circle_rect_hit(5.0, 5.0, 2.0, 0.0, 10.0, 0.0, 10.0),
         "центр внутри должен попадать")
    want(_circle_rect_hit(0.0, 5.0, 2.0, 0.0, 10.0, 0.0, 10.0),
         "центр на ребре должен попадать")
    want(_circle_rect_hit(11.0, 11.0, 2.0, 0.0, 10.0, 0.0, 10.0),
         "угловой центр на дистанции < r должен попадать")
    want(_circle_rect_hit(12.0, 5.0, 2.0, 0.0, 10.0, 0.0, 10.0),
         "касание справа (dx == r) должно попадать: границы включительно")
    want(not _circle_rect_hit(12.001, 5.0, 2.0, 0.0, 10.0, 0.0, 10.0),
         "чуть правее касания (dx > r) - промах")
    want(not _circle_rect_hit(15.0, 5.0, 2.0, 0.0, 10.0, 0.0, 10.0),
         "далеко вправо - промах")
    want(not _circle_rect_hit(12.0, 12.0, 2.0, 0.0, 10.0, 0.0, 10.0),
         "диагональ угла (2, 2) при r=2 - промах: круг не доходит до угла")
    want(not _circle_rect_hit(5.0, 5.0, 0.0, 6.0, 10.0, 6.0, 10.0),
         "точка вне прямоугольника при r=0 - промах")
    want(_circle_rect_hit(6.0, 6.0, 0.0, 6.0, 10.0, 6.0, 10.0),
         "точка на углу при r=0 - попадание")

    # -- 2) синтетика: пересечение заведомо есть, глубина 22 мм на всём пути
    hit_pts, hit_cum = _scene([(0.0, -60.0, 0.0), (10.0, -60.0, 0.0),
                               (20.0, -60.0, 0.0)])
    hit = _scan_collisions(hit_pts, hit_cum, hit_cum[-1], [0.0], [STICKOUT],
                           STOCK, _COLLISION_STEP)
    want(isinstance(hit, list) and len(hit) == 1,
         "синтетика-врез: ожидался 1 диапазон, получено %r" % (hit,))
    if len(hit) == 1:
        row = hit[0]
        want(set(row.keys()) >= {"d", "depth", "scene", "scene_hb"},
             "синтетика-врез: нет ключей контракта QML, есть %s"
             % (sorted(row.keys()),))
        want(abs(row["depth"] - 22.0) < 1e-9,
             "синтетика-врез: глубина %r, ожидалась 22.0 (гайка ER20 D33)"
             % row["depth"])
        want(row["d"] == 0.0, "синтетика-врез: диапазон не с начала пути")
        want(row["scene"] == [0.0, -60.0, 0.0],
             "синтетика-врез: точка сцены %r, ожидалась [0, -60, 0]"
             % (row["scene"],))
        want(row["scene_hb"] == [0.0, -30.0, 0.0],
             "синтетика-врез: низ оправки %r, ожидался [0, -30, 0]"
             % (row["scene_hb"],))

    # -- 3) синтетика: пересечения заведомо нет (по XY и по вертикали)
    far_pts, far_cum = _scene([(200.0, -60.0, 0.0), (210.0, -60.0, 0.0),
                               (220.0, -60.0, 0.0)])
    want(_scan_collisions(far_pts, far_cum, far_cum[-1], [0.0], [STICKOUT],
                          STOCK, _COLLISION_STEP) == [],
         "синтетика-далеко: оправка в 200 мм по X не должна пересекать заготовку")
    up_pts, up_cum = _scene([(0.0, 50.0, 0.0), (10.0, 50.0, 0.0),
                             (20.0, 50.0, 0.0)])
    want(_scan_collisions(up_pts, up_cum, up_cum[-1], [0.0], [STICKOUT],
                          STOCK, _COLLISION_STEP) == [],
         "синтетика-выше: оправка целиком над заготовкой - пересечения нет")
    low_pts, low_cum = _scene([(0.0, -300.0, 0.0), (10.0, -300.0, 0.0),
                               (20.0, -300.0, 0.0)])
    want(_scan_collisions(low_pts, low_cum, low_cum[-1], [0.0], [STICKOUT],
                          STOCK, _COLLISION_STEP) == [],
         "синтетика-ниже: оправка целиком под заготовкой - пересечения нет")

    # -- 4) синтетика: врез только в середине пути (скан реально шагает по d)
    mid_pts, mid_cum = _scene([(0.0, 50.0, 0.0), (0.0, -60.0, 0.0),
                               (0.0, 50.0, 0.0)])
    mid = _scan_collisions(mid_pts, mid_cum, mid_cum[-1], [0.0], [STICKOUT],
                           STOCK, _COLLISION_STEP)
    want(len(mid) == 1, "синтетика-середина: ожидался 1 диапазон, %r" % (mid,))
    if len(mid) == 1:
        want(0.0 < mid[0]["d"] < mid_cum[-1],
             "синтетика-середина: диапазон не в середине пути (d=%r)"
             % mid[0]["d"])
        want(mid[0]["depth"] > 0.05, "синтетика-середина: глубина не осмысленна")

    # -- 5) защитные ветки: пустой вход -> пустой список, без исключений
    want(_scan_collisions(None, hit_cum, 20.0, [0.0], [STICKOUT], STOCK,
                          _COLLISION_STEP) == [], "нет сцены -> []")
    want(_scan_collisions(hit_pts, None, 20.0, [0.0], [STICKOUT], STOCK,
                          _COLLISION_STEP) == [], "нет cum -> []")
    want(_scan_collisions(hit_pts, hit_cum, 0.0, [0.0], [STICKOUT], STOCK,
                          _COLLISION_STEP) == [], "total <= 0 -> []")
    want(_scan_collisions(hit_pts, hit_cum, 20.0, [], [STICKOUT], STOCK,
                          _COLLISION_STEP) == [], "нет этапов -> []")
    want(_scan_collisions(hit_pts, hit_cum, 20.0, [0.0], [STICKOUT], None,
                          _COLLISION_STEP) == [], "нет заготовки -> []")
    want(_scan_collisions([[0.0, -60.0, 0.0]], [0.0], 0.0, [0.0], [STICKOUT],
                          STOCK, _COLLISION_STEP) == [],
         "одна точка (n < 2) -> []")

    # -- 6) импорт, без которого скан падал молча, на месте и вызывается
    want(hasattr(collision, "_holder_profile")
         and callable(collision._holder_profile),
         "collision потерял импорт _holder_profile - это тот самый тихий отказ")

    # -- 7) исключение изнутри скана НЕ проглатывается: падающий импорт должен
    #       вылететь наружу, иначе тест обязан упасть
    orig_holder = collision._holder_profile

    def _boom(_length):
        raise RuntimeError("внедрённый отказ построения оправки")

    collision._holder_profile = _boom
    leaked = False
    try:
        _scan_collisions(hit_pts, hit_cum, hit_cum[-1], [0.0], [STICKOUT],
                         STOCK, _COLLISION_STEP)
    except RuntimeError:
        leaked = True
    finally:
        collision._holder_profile = orig_holder
    want(leaked,
         "скан проглотил исключение внутри себя - дефект «потерян импорт» не поймается")

    # -- 8) реальный УП: скан обязан вернуть непустой список осмысленной формы
    if not os.path.exists(NGC):
        print("Test_model.ngc: пропущено (нет %s)" % NGC)
    else:
        text = open(NGC, "r", errors="replace").read()
        ordered, _last, _seg, stages = parse_gcode(text)
        data = _prepare_ordered(ordered, stages)
        want(data.pts is not None and len(data.pts) > 1000,
             "УП: после подготовки нет полилинии сцены")
        if data.pts is not None and len(data.pts) > 1000:
            lo = [min(p.point[k] for p in ordered) for k in range(3)]
            hi = [max(p.point[k] for p in ordered) for k in range(3)]
            stock = (lo[0], hi[0], lo[1], hi[1], lo[2], hi[2])
            starts = [float(s.get("start", 0.0)) * data.total
                      for s in data.stages]
            lens = [_tool_exposed_length(s.get("tool", {}))
                    for s in data.stages]
            if not lens:
                lens = [0.0]
            while len(lens) < len(starts):
                lens.append(lens[-1])
            res = _scan_collisions(data.pts, data.cum, data.total, starts, lens,
                                   stock, _COLLISION_STEP)
            want(isinstance(res, list) and len(res) >= 1,
                 "УП: скан вернул %r - пересечений на реальном пути не найдено"
                 % (res if not isinstance(res, list) else len(res),))
            depths = [r["depth"] for r in res if isinstance(r, dict)]
            for r in res:
                want(set(r.keys()) >= {"d", "depth", "scene", "scene_hb"},
                     "УП: диапазон без ключей контракта QML: %s"
                     % (sorted(r.keys()),))
                want(r["depth"] > 0.05, "УП: глубина %r не больше порога 0.05"
                     % r["depth"])
            print("УП: точек сцены=%d  этапов=%d  диапазонов=%d  макс. глубина=%.2f мм"
                  % (len(data.pts), len(starts), len(res),
                     max(depths) if depths else 0.0))

    for line in fails:
        print("FAIL: %s" % line)
    print("RESULT: %s" % ("OK" if not fails else "FAIL"))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
