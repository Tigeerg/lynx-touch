#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless check: 2D-профиль инструмента и оправки (tooling.py).

Закрывает пункт DESIGN.md, раздел 12 («нет теста профиля инструмента»): проверка
привязана к конкретным числам, а не к «список непуст». Для каждого типа фрезы из
UI_TOOL разбираются форма и вершины профиля: ось на острие, радиус кромки у
плоской/сферической/угловой, точка острия у сверла/метчика, вылет (BL с потолком
OL) и невозрастание радиуса хвостовика вверх по оси.

Оправка (гайка ER20 + корпус) проверяется как секции (_holder_profile) и как
кольцевые сечения: гайка D33/H22, корпус D46/H50, нижнее сечение стоит на вылете
инструмента. Кольцевые сечения (_rings_segments) и «настоящие рёбра профиля»
(_profile_ring_points) обязаны быть непустыми и содержать ровно ожидаемые кольца;
у гладкой сферы промежуточные витки кольцами НЕ считаются (иначе каркас
превращается в клетку).

Запуск: python modules/gcode-viewer/test_tool_profile.py (exit 0 = пойдено)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mock_backend import (_tool_profile, _tool_exposed_length,  # noqa: E402
                          _tool_outline_profile, _holder_profile,
                          _holder_solid_profile, _holder_outline_profile,
                          _rings_segments, _profile_ring_points,
                          _HOLDER_NUT_D, _HOLDER_NUT_H,
                          _HOLDER_BODY_D, _HOLDER_BODY_H)

# Все типы UI_TOOL, которые панель обязана нарисовать (ui_tags._TOOL_TYPE_RU).
UI_TYPES = ["FLAT", "BULL", "BALL", "CHAMFER", "TAPER", "DRILL", "TAP",
            "THREAD", "LOLLIPOP", "DOVETAIL", "PROBE"]


def _mk(**over):
    """Инструмент D10 с полным набором полей; переопределяется через over."""
    info = {"type": "FLAT", "diameter": 10.0, "corner_radius": 0.0,
            "taper_deg": 0.0, "tip_angle_deg": 0.0, "tip_diameter": 0.0,
            "flute_length": 20.0, "shoulder_length": 25.0,
            "body_length": 40.0, "shank_diameter": 8.0,
            "overall_length": 60.0, "number": 1}
    info.update(over)
    return info


def _pts(profile):
    return [(round(p.radius_mm, 4), round(p.height_mm, 4)) for p in profile]


def _has(profile, r, y, tol=1e-3):
    return any(abs(p.radius_mm - r) <= tol and abs(p.height_mm - y) <= tol
               for p in profile)


def _mono_down_after_max(profile):
    """После вершины максимального радиуса радиус не возрастает (хвостовик)."""
    radii = [p.radius_mm for p in profile]
    top = max(radii)
    i = radii.index(top)
    return all(radii[k + 1] <= radii[k] + 1e-9 for k in range(i, len(radii) - 1))


def main() -> int:
    fails = []

    def want(cond, msg):
        if not cond:
            fails.append(msg)

    # -- 0) отсутствие диаметра -> профиля нет
    want(_tool_profile({"type": "FLAT", "diameter": 0.0}) is None,
         "нет диаметра: профиль должен быть None")
    want(_tool_exposed_length({"type": "FLAT", "diameter": 0.0}) == 0.0,
         "нет диаметра: вылет должен быть 0")
    want(_tool_exposed_length({}) == 0.0, "пустой info: вылет должен быть 0")

    # -- 1) каждый тип даёт профиль: ось на острие, ось наверху, вылет > 0
    for typ in UI_TYPES:
        prof = _tool_profile(_mk(type=typ))
        tag = "тип %s" % typ
        want(prof is not None and len(prof) >= 3, "%s: профиль пуст" % tag)
        if prof is None:
            continue
        want(prof[0].radius_mm == 0.0 and prof[0].height_mm == 0.0,
             "%s: первая точка не (0, 0)" % tag)
        want(prof[-1].radius_mm == 0.0, "%s: верх профиля не на оси" % tag)
        want(max(p.radius_mm for p in prof) == 5.0,
             "%s: максимальный радиус не 5.0 (D10)" % tag)
        want(_tool_exposed_length(_mk(type=typ)) == 40.0,
             "%s: вылет не 40.0 (BL, потолок OL=60)" % tag)
        want(prof[-1].height_mm == 40.0, "%s: верх профиля не на вылете" % tag)
        want(_mono_down_after_max(prof),
             "%s: радиус хвостовика возрастает вверх" % tag)

    # -- 2) плоская фреза: кромка на полном радиусе в y=0, ступенька на шпиндель
    flat = _tool_profile(_mk(type="FLAT", corner_radius=0.0))
    want(_has(flat, 5.0, 0.0), "FLAT: нет кромки (5.0, 0.0)")
    want(_has(flat, 5.0, 20.0), "FLAT: нет конца винтовой части (5.0, 20.0)")
    want(_has(flat, 5.0, 25.0), "FLAT: нет конца шейки (5.0, 25.0)")
    want(_has(flat, 4.0, 25.0), "FLAT: нет ступеньки на хвостовик SD8 (4.0, 25.0)")
    want(_has(flat, 4.0, 40.0), "FLAT: нет тела на вылете (4.0, 40.0)")
    want(sorted(set(round(p.radius_mm, 4) for p in flat)) == [0.0, 4.0, 5.0],
         "FLAT: ожидались радиусы 0/4/5, получено %s" % (sorted(set(p.radius_mm for p in flat)),))

    # -- 3) угловая фреза (BULL): плоское дно r-cr, дуга угла радиуса cr
    bull = _tool_profile(_mk(type="BULL", corner_radius=2.0))
    want(_has(bull, 3.0, 0.0), "BULL: нет плоского дна (3.0, 0.0)")
    want(_has(bull, 5.0, 2.0), "BULL: дуга не выходит на полный радиус (5.0, 2.0)")
    want(_has(bull, 5.0, 20.0), "BULL: нет конца винтовой части (5.0, 20.0)")

    # -- 4) сферическая фреза: полусфера, радиус r на высоте r
    ball = _tool_profile(_mk(type="BALL"))
    want(_has(ball, 5.0, 5.0), "BALL: нет вершины полусферы (5.0, 5.0)")
    nose = [(p.radius_mm, p.height_mm) for p in ball
            if p.height_mm <= 5.0 and p.radius_mm > 0.0]
    want(all(nose[k + 1][0] >= nose[k][0] - 1e-9 for k in range(len(nose) - 1)),
         "BALL: радиус полусферы не растёт по высоте")
    want(_has(ball, 5.0, 20.0), "BALL: нет конца винтовой части (5.0, 20.0)")

    # -- 5) фаска/конус: tip_h = r / tan(taper)
    cham = _tool_profile(_mk(type="CHAMFER", taper_deg=60.0))
    want(_has(cham, 5.0, 5.0 / math.tan(math.radians(60.0))),
         "CHAMFER 60: основание конуса не на r/tan(60)=2.887")
    want(_has(cham, 5.0, 20.0), "CHAMFER 60: нет конца винтовой части (5.0, 20.0)")
    taper = _tool_profile(_mk(type="TAPER", taper_deg=60.0))
    want(_pts(cham) == _pts(taper), "CHAMFER и TAPER при одном taper разошлись")

    # -- 6) сверло/метчик: острие по углу, с концевым диаметром и без
    drill = _tool_profile(_mk(type="DRILL", taper_deg=60.0))
    want(_has(drill, 5.0, 5.0 / math.tan(math.radians(30.0))),
         "DRILL taper60: вершина не на 8.660")
    drill_t = _tool_profile(_mk(type="DRILL", taper_deg=60.0, tip_diameter=2.0))
    want(_has(drill_t, 1.0, 0.0), "DRILL TIPD2: нет концевой площадки (1.0, 0.0)")
    want(_has(drill_t, 5.0, (5.0 - 1.0) / math.tan(math.radians(30.0))),
         "DRILL TIPD2: вершина не на 6.928")
    tap = _tool_profile(_mk(type="TAP", taper_deg=0.0, tip_angle_deg=0.0))
    want(_has(tap, 5.0, 5.0 / math.tan(math.radians(89.0 / 2.0))),
         "TAP по умолчанию: угол не ограничен 89 град (вершина 5.088)")
    want(_pts(_tool_profile(_mk(type="FLAT", taper_deg=60.0, corner_radius=0.0)))
         == _pts(drill), "FLAT с TAPER должен рисоваться как DRILL")

    # -- 7) вылет: BL, потолок OL и умолчание без полей поста
    capped = _tool_profile(_mk(type="FLAT", body_length=50.0, overall_length=30.0))
    want(_tool_exposed_length(_mk(type="FLAT", body_length=50.0, overall_length=30.0))
         == 30.0, "OL30 не обрезал BL50: вылет должен быть 30")
    want(capped[-1].height_mm == 30.0, "OL30: верх профиля не на 30")
    old = _mk(type="FLAT", diameter=6.0, flute_length=0.0, shoulder_length=0.0,
              body_length=0.0, shank_diameter=0.0, overall_length=0.0)
    want(_tool_exposed_length(old) == 16.0,
         "без FL/SL/BL/OL: вылет-умолчание должен быть max(2r,16)=16")
    old_prof = _tool_profile(old)
    want(sorted(set(round(p.radius_mm, 4) for p in old_prof)) == [0.0, 3.0],
         "без SD: радиусы только 0/3 (ступеньки на хвостовик нет)")
    want(_has(old_prof, 3.0, 0.0) and _has(old_prof, 3.0, 16.0)
         and _has(old_prof, 0.0, 16.0), "без полей поста: контур неверный")

    # -- 8) обводка профиля: дилатация наружу, острие и верх остаются на оси
    outline = _tool_outline_profile(flat, 0.5)
    want(len(outline) == len(flat), "обводка: число вершин изменилось")
    want(outline[0].radius_mm == 0.0 and outline[0].height_mm == -0.5,
         "обводка: острие не сместилось на (0, -pad)")
    want(outline[-1].radius_mm == 0.0 and outline[-1].height_mm == 40.5,
         "обводка: верх не сместился на (0, BL+pad)")
    want(abs(max(p.radius_mm for p in outline) - 5.5) < 1e-9,
         "обводка: максимальный радиус не r+pad=5.5")

    # -- 9) оправка: секции гайки и корпуса, нижнее сечение на вылете
    sec = _holder_profile(40.0)
    want(len(sec) == 2, "оправка: ожидалось 2 секции, получено %d" % len(sec))
    if len(sec) == 2:
        want(sec[0].radius_mm == _HOLDER_NUT_D / 2.0 == 16.5,
             "оправка: радиус гайки не %g" % (_HOLDER_NUT_D / 2.0))
        want(sec[1].radius_mm == _HOLDER_BODY_D / 2.0 == 23.0,
             "оправка: радиус корпуса не %g" % (_HOLDER_BODY_D / 2.0))
        want(sec[0].z0_mm == 40.0, "оправка: низ гайки не на вылете 40")
        want(sec[0].z1_mm == 40.0 + _HOLDER_NUT_H == 62.0,
             "оправка: верх гайки не на 62")
        want(sec[1].z0_mm == sec[0].z1_mm, "оправка: секции не стыкуются")
        want(sec[1].z1_mm == 40.0 + _HOLDER_NUT_H + _HOLDER_BODY_H == 112.0,
             "оправка: верх корпуса не на 112")
        want(sec[1].radius_mm > sec[0].radius_mm,
             "оправка: корпус не шире гайки")

    # -- 10) кольца оправки: жёсткие числа, а не «список непуст»
    solid = _holder_solid_profile()
    outline_h = _holder_outline_profile()
    want(len(solid) == 10, "оправка-контур: ожидалось 10 вершин, %d" % len(solid))
    want(len(outline_h) == 8,
         "оправка-обводка: ожидалось 8 вершин без осей, %d" % len(outline_h))
    want(all(p.radius_mm > 0.0 for p in outline_h),
         "оправка-обводка: ось не выкинута")
    rings = _rings_segments(solid)
    want(len(rings) == 384,
         "кольца оправки: 8 колец x 48 = 384, получено %d" % len(rings))
    radii = set(round(p.radius_mm, 6) for p in solid)
    heights = set(round(p.height_mm, 6) for p in solid)
    for (a, b) in rings:
        for e in (a, b):
            rr = math.hypot(e[0], e[2])
            want(any(abs(rr - r0) < 1e-6 for r0 in radii),
                 "кольцо: точка на радиусе %g, которого нет в профиле" % rr)
            want(any(abs(e[1] - y0) < 1e-6 for y0 in heights),
                 "кольцо: точка на высоте %g, которой нет в профиле" % e[1])
    want(_rings_segments(solid, 2) == [] and _rings_segments([], 48) == [],
         "кольца: при segments < 3 или пустом профиле должен быть []")

    ring_pts = _profile_ring_points(solid)
    want(len(ring_pts) == 7,
         "рёбра оправки: ожидалось 7 колец (без двух осей), %d" % len(ring_pts))
    want(_has(ring_pts, 16.5, 3.0) and _has(ring_pts, 23.0, 72.0),
         "рёбра оправки: нет кольца гайки (16.5, 3.0) или корпуса (23.0, 72.0)")
    want(all(p.radius_mm > 0.0 for p in ring_pts),
         "рёбра оправки: кольцо нулевого радиуса")

    # -- 11) рёбра профиля инструмента: ступеньки и концы, но не гладкая сфера
    flat_rings = [(round(p.radius_mm, 3), round(p.height_mm, 3))
                  for p in _profile_ring_points(flat)]
    want(flat_rings == [(5.0, 0.0), (5.0, 25.0), (4.0, 25.0), (4.0, 40.0)],
         "FLAT: рёбра %s, ожидались кромка/шейка/ступенька/тело" % (flat_rings,))
    ball_rings = [(round(p.radius_mm, 3), round(p.height_mm, 3))
                  for p in _profile_ring_points(ball)]
    want(ball_rings == [(5.0, 25.0), (4.0, 25.0), (4.0, 40.0)],
         "BALL: гладкая полусфера дала лишние кольца: %s" % (ball_rings,))

    for line in fails:
        print("FAIL: %s" % line)
    print("RESULT: %s" % ("OK" if not fails else "FAIL"))
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
