#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless check парсера G-кода из gcode_parser.py (публичная точка parse_gcode).

Покрытие:
  * единицы подачи F (мм/мин -> мм/с), скорость быстрого хода G0 и дефолт;
  * игнор комментариев, неизвестных слов (M/S/N, G54) и модальное G90/G91;
  * прореживание прямых G1 (100 шагов по 0.25 мм -> 52 точки при допуске 0.5 мм);
  * разбор дуг G2/G3: знак CW/CCW в XY и плоскость XZ (G18);
  * координаты: parse_gcode отдаёт машинные (X, Y, Z), а _prepare_ordered
    переводит их в сцену (X, Z, -Y);
  * реальная УП data/cad-testdata/Test_model.ngc.

Запуск: python modules/gcode-viewer/test_gcode_parser.py (exit 0 = пойдено)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mock_backend import (parse_gcode, _arc_points, _arc_sweep,     # noqa: E402
                          _to_mms, _prepare_ordered,
                          _RAPID_SPEED_MMS, _DEFAULT_FEED_MMS)

NGC = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.ngc"))


def segs(ordered):
    """Сегменты пути как (start, end, is_rapid): точка несёт флаг исходящего ребра."""
    return [(ordered[i].point, ordered[i + 1].point, ordered[i].is_rapid)
            for i in range(len(ordered) - 1)]


def close(a, b, tol=1e-6):
    return abs(a - b) <= tol


def main() -> int:
    fails = []

    # -- 1) единицы подачи: F (мм/мин) -> мм/с (1:1), G0 быстрее, дефолт при F<=0
    if not close(_to_mms(300.0), 5.0):
        fails.append("_to_mms(300) != 5.0 мм/с, получено %r" % _to_mms(300.0))
    if not close(_to_mms(0.0), _DEFAULT_FEED_MMS):
        fails.append("_to_mms(0) != дефолт %r" % _DEFAULT_FEED_MMS)
    o, _l, _s, _st = parse_gcode("G90 G1 X10 Y0 F600\n")
    feed_speeds = [p.speed_mm_s for p in o if not p.is_rapid]
    if not feed_speeds or not close(feed_speeds[-1], 10.0):
        fails.append("G1 F600 -> %r мм/с, ожидалось 10.0" % feed_speeds)
    o, _l, _s, _st = parse_gcode("G90 G0 X10 Y0\n")
    rapid_speeds = [p.speed_mm_s for p in o if p.is_rapid]
    if not rapid_speeds or any(not close(v, _RAPID_SPEED_MMS) for v in rapid_speeds):
        fails.append("G0: скорость не %r мм/с: %r" % (_RAPID_SPEED_MMS, rapid_speeds))

    # -- 2) комментарии и неизвестные слова не двигают ось и не плодят точки
    prog = "G90 G0 X5 Y5\nM3 S4000\nG54 M8\nG1 X10 Y5 F600 (подача)\nN100\n"
    o, last, count, _st = parse_gcode(prog)
    if len(o) != 3:
        fails.append("комментарий/неизвестные слова: точек %d, ожидалось 3" % len(o))
    if not close(last[0], 10.0) or not close(last[1], 5.0) or not close(last[2], 0.0):
        fails.append("G54/M/S/N сдвинули конец: %r" % (last,))
    if count != len(o) - 1:
        fails.append("seg_count %d != точек-1 %d" % (count, len(o) - 1))

    # -- 3) инкрементальный режим G91: координаты накапливаются
    o, last, _s, _st = parse_gcode("G91 G1 X10 F600\nX10\n")
    pts = [p.point[0] for p in o]
    if [round(v, 6) for v in pts] != [0.0, 10.0, 20.0]:
        fails.append("G91: X-точки %r, ожидалось [0, 10, 20]" % pts)

    # -- 4) прореживание прямых G1: 100 шагов по 0.25 мм при допуске 0.5 мм
    lines = ["G90 G1 X0 Y0 F600"]
    x = 0.0
    for _ in range(100):
        x += 0.25
        lines.append("X%.4f" % x)
    o, last, _s, _st = parse_gcode("\n".join(lines))
    if len(o) != 52:
        fails.append("прореживание G1: точек %d, ожидалось 52" % len(o))
    if not close(o[0].point[0], 0.0) or not close(last[0], 25.0):
        fails.append("прореживание сдвинуло концы: %r -> %r" % (o[0].point, last))
    total = _prepare_ordered(o).total
    if not close(total, 25.0, 1e-6):
        fails.append("прореживание потеряло длину: total=%.4f, ожидалось 25.0" % total)

    # -- 5) дуги G2/G3 в XY: направление CW/CCW
    o3, _l, _s, _st = parse_gcode("G90 G0 X10 Y0\nG3 X-10 Y0 I-10 J0 F600\n")
    ys3 = [p.point[1] for p in o3]
    if not (max(ys3) > 9.9 and min(ys3) > -1e-9):
        fails.append("G3 (CCW): Y в диапазоне [%.3f, %.3f], ожидался верх (max>9.9)"
                     % (min(ys3), max(ys3)))
    o2, _l, _s, _st = parse_gcode("G90 G0 X10 Y0\nG2 X-10 Y0 I-10 J0 F600\n")
    ys2 = [p.point[1] for p in o2]
    if not (min(ys2) < -9.9 and max(ys2) < 1e-9):
        fails.append("G2 (CW): Y в диапазоне [%.3f, %.3f], ожидался низ (min<-9.9)"
                     % (min(ys2), max(ys2)))
    if not close(_arc_sweep(0, 0, 10, 0, 5, 0, True), -3.141592653589793, 1e-9):
        fails.append("_arc_sweep CW != -pi")
    if not close(_arc_sweep(0, 0, 10, 0, 5, 0, False), 3.141592653589793, 1e-9):
        fails.append("_arc_sweep CCW != +pi")

    # -- 6) плоскость XZ (G18): дуга лежит в XZ, Y не меняется
    pts_xz = _arc_points((0.0, 0.0, 0.0), (0.0, 0.0, 10.0),
                         {"I": 0.0, "K": 5.0}, False, "XZ")
    if any(abs(p[1]) > 1e-9 for p in pts_xz):
        fails.append("_arc_points XZ: Y != 0 (плоскость не соблюдена)")
    if not (max(p[0] for p in pts_xz) > 4.9 and close(min(p[0] for p in pts_xz), 0.0, 5e-2)):
        fails.append("_arc_points XZ CCW: X в [%.3f, %.3f], ожидался горб до +5"
                     % (min(p[0] for p in pts_xz), max(p[0] for p in pts_xz)))
    o18, _l, _s, _st = parse_gcode("G90 G0 X0 Y0 Z0\nG18 G2 X0 Z10 I0 K5 F600\n")
    if any(abs(p.point[1]) > 1e-9 for p in o18):
        fails.append("parse G18 G2: Y != 0")
    xmin18 = min(p.point[0] for p in o18)
    if not (xmin18 < -4.9):
        fails.append("parse G18 G2 CW: Xmin=%.3f, ожидался горб до -5" % xmin18)

    # -- 7) машина -> сцена: parse_gcode даёт машинные (X,Y,Z), сцена (X,Z,-Y)
    o, _l, _s, _st = parse_gcode("G90 G1 X10 Y20 Z30 F600\n")
    pd = _prepare_ordered(o)
    if pd.pts is None or len(pd.pts) < 2:
        fails.append("_prepare_ordered: нет сценовой полилинии")
    else:
        scene_end = tuple(round(float(v), 6) for v in pd.pts[-1])
        if scene_end != (10.0, 30.0, -20.0):
            fails.append("машина->сцена: конец %r, ожидалось (10, 30, -20)" % (scene_end,))
    lo_exp = (0.0, 0.0, -20.0)
    hi_exp = (10.0, 30.0, 0.0)
    if any(not close(pd.bounds.lo[i], lo_exp[i]) for i in range(3)) \
            or any(not close(pd.bounds.hi[i], hi_exp[i]) for i in range(3)):
        fails.append("bounds не в сценовых осях: lo=%r hi=%r, ожидалось %r/%r"
                     % (pd.bounds.lo, pd.bounds.hi, lo_exp, hi_exp))

    # -- 8) реальная УП cad-testdata
    n_points = 0
    n_stages = 0
    if not os.path.exists(NGC):
        print("Test_model.ngc не найден (passed over):", NGC)
    else:
        with open(NGC, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        o, last, count, stages = parse_gcode(text)
        n_points, n_stages = len(o), len(stages)
        if not o or len(o) < 1000:
            fails.append("Test_model.ngc: точек %d, ожидалось > 1000" % len(o))
        if o[0].point != (0.0, 0.0, 0.0):
            fails.append("Test_model.ngc: начало %r, ожидалось (0,0,0)" % (o[0].point,))
        if tuple(last) != o[-1].point:
            fails.append("Test_model.ngc: last %r != последней точке %r"
                         % (tuple(last), o[-1].point))
        if count != len(o) - 1:
            fails.append("Test_model.ngc: seg_count %d != точек-1 %d" % (count, len(o) - 1))
        exp_last = (-28.512, -7.341, 0.0)
        if any(not close(last[i], exp_last[i], 1e-3) for i in range(3)):
            fails.append("Test_model.ngc: конец %r, ожидалось %r" % (tuple(last), exp_last))
        if len(stages) < 10:
            fails.append("Test_model.ngc: этапов %d, ожидалось >= 10" % len(stages))

    print("parser: точек(УП)=%d этапов=%d | прореживание 100x0.25 -> %d точек | "
          "G3 maxy=%.3f G2 miny=%.3f | сцена end=%s"
          % (n_points, n_stages, 52, max(ys3), min(ys2), (10.0, 30.0, -20.0)))
    if fails:
        for f in fails:
            print("FAIL:", f)
        return 1
    print("PASSED: parse_gcode и вспомогательные точки держат контракт")
    return 0


if __name__ == "__main__":
    sys.exit(main())
