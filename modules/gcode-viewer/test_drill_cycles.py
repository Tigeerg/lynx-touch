#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless check кадровых циклов сверления: G81/G82 (сверление), G83 (глубокое
сверление с полным выводом стружки), G73 (прерывистое, короткая чистка).

Проверяется и прямой вызов _run_cycle, и полный разбор программы через
parse_gcode: число и глубина клевков, высота вывода/чистки (R и Q), возврат в
исходную точку при G99 против возврата на плоскость R (G98), координаты X/Y
отверстий, конечное положение инструмента.

Запуск: python modules/gcode-viewer/test_drill_cycles.py (exit 0 = пойдено)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mock_backend import parse_gcode, _run_cycle  # noqa: E402


def collect(cycle, pos, hx, hy, retract_to_r):
    """Прогнать один цикл через _run_cycle, вернуть сегменты и новое pos."""
    segs = []

    def emit(a, b, rapid, decimate=False, tol2=0.0):
        segs.append((tuple(a), tuple(b), bool(rapid)))

    p = list(pos)
    _run_cycle(cycle, p, hx, hy, emit, retract_to_r)
    return segs, p


def feed_bottoms(segs):
    """Глубины (Z) концов подающих сегментов - точки дна клевков."""
    return [b[2] for _a, b, rapid in segs if not rapid]


def up_to_plane(segs, plane_z):
    """Число быстрых подъёмов, дошедших до плоскости возврата plane_z."""
    return sum(1 for a, b, rapid in segs if rapid and b[2] == plane_z and a[2] < plane_z - 1e-9)


def parse_segments(text):
    ordered, last, _count, _stages = parse_gcode(text)
    segs = [(ordered[i].point, ordered[i + 1].point, ordered[i].is_rapid)
            for i in range(len(ordered) - 1)]
    return segs, last


def main() -> int:
    fails = []

    # -- 1) G81: один клевок до Z=-10, вывод на плоскость R=2, позиция после цикла
    segs81, p = collect({"code": 81, "depth": -10.0, "r": 2.0, "f": 300.0},
                        (0.0, 0.0, 5.0), 10.0, 20.0, True)
    if feed_bottoms(segs81) != [-10.0]:
        fails.append("G81: клевки %r, ожидался один до -10" % feed_bottoms(segs81))
    if up_to_plane(segs81, 2.0) != 1:
        fails.append("G81: подъёмов на R=2 %d, ожидался 1" % up_to_plane(segs81, 2.0))
    if p != [10.0, 20.0, 2.0]:
        fails.append("G81: позиция после цикла %r, ожидалось [10, 20, 2]" % (p,))

    # -- 2) G82 ведёт себя как G81 (выдержка на дне в контуре не моделируется)
    segs82, _p = collect({"code": 82, "depth": -10.0, "r": 2.0, "f": 300.0},
                         (0.0, 0.0, 5.0), 10.0, 20.0, True)
    if feed_bottoms(segs82) != [-10.0]:
        fails.append("G82: клевки %r, ожидался один до -10" % feed_bottoms(segs82))

    # -- 3) G83 глубокое сверление: клевки по Q=3 от дна R=2, полный вывод стружки
    segs83, p83 = collect({"code": 83, "depth": -10.0, "r": 2.0, "q": 3.0},
                          (0.0, 0.0, 5.0), 10.0, 20.0, True)
    bottoms83 = feed_bottoms(segs83)
    if bottoms83 != [-1.0, -4.0, -7.0, -10.0]:
        fails.append("G83: клевки %r, ожидалось [-1, -4, -7, -10]" % bottoms83)
    if up_to_plane(segs83, 2.0) != 4:
        fails.append("G83: подъёмов на R=2 %d, ожидалось 4 (полный вывод)"
                     % up_to_plane(segs83, 2.0))
    if p83 != [10.0, 20.0, 2.0]:
        fails.append("G83 G99: позиция %r, ожидалось [10, 20, 2]" % (p83,))

    # -- 4) G83 при G98: полный вывод идёт на исходный уровень (clear=5), а не на R
    segs83b, p83b = collect({"code": 83, "depth": -10.0, "r": 2.0, "q": 3.0},
                            (0.0, 0.0, 5.0), 10.0, 20.0, False)
    if up_to_plane(segs83b, 5.0) != 4:
        fails.append("G83 G98: подъёмов на 5 %d, ожидалось 4" % up_to_plane(segs83b, 5.0))
    if up_to_plane(segs83b, 2.0) != 0:
        fails.append("G83 G98: подъёмов на R=2 %d, ожидалось 0 (вывод идёт на 5)"
                     % up_to_plane(segs83b, 2.0))
    if p83b != [10.0, 20.0, 5.0]:
        fails.append("G83 G98: позиция %r, ожидалось [10, 20, 5]" % (p83b,))

    # -- 5) G73 прерывистое: те же клевки по Q, но чистка короткая, не на R
    segs73, p73 = collect({"code": 73, "depth": -10.0, "r": 2.0, "q": 3.0},
                          (0.0, 0.0, 5.0), 10.0, 20.0, True)
    if feed_bottoms(segs73) != [-1.0, -4.0, -7.0, -10.0]:
        fails.append("G73: клевки %r, ожидалось [-1, -4, -7, -10]" % feed_bottoms(segs73))
    if up_to_plane(segs73, 2.0) != 1:
        fails.append("G73: подъёмов на R=2 %d, ожидался 1 (только финальный)"
                     % up_to_plane(segs73, 2.0))
    if p73 != [10.0, 20.0, 2.0]:
        fails.append("G73: позиция %r, ожидалось [10, 20, 2]" % (p73,))

    # -- 6) Q больше глубины: дробление не нужно, один клевок до дна
    segs_big, _p = collect({"code": 83, "depth": -10.0, "r": 2.0, "q": 20.0},
                           (0.0, 0.0, 5.0), 10.0, 20.0, True)
    if feed_bottoms(segs_big) != [-10.0]:
        fails.append("G83 Q=20: клевки %r, ожидался один до -10" % feed_bottoms(segs_big))

    # -- 7) программа G81 на два отверстия: X/Y и возврат (G98 -> Z=5, G99 -> Z=2)
    prog98 = ("G90 G0 X0 Y0 Z5\nG98 G81 X10 Y20 Z-10 R2 F300\nX30 Y20\nG80\n")
    segs, last = parse_segments(prog98)
    feeds = [(a, b) for a, b, rapid in segs if not rapid]
    if len(feeds) != 2:
        fails.append("G81 два отверстия: подач %d, ожидалось 2" % len(feeds))
    else:
        holes = [(a[0], a[1]) for a, _b in feeds]
        if holes != [(10.0, 20.0), (30.0, 20.0)]:
            fails.append("G81 два отверстия: X/Y %r, ожидалось [(10,20),(30,20)]" % holes)
        if [b[2] for _a, b in feeds] != [-10.0, -10.0]:
            fails.append("G81 два отверстия: глубины %r" % [b[2] for _a, b in feeds])
    if last != (30.0, 20.0, 5.0):
        fails.append("G81 G98: конец %r, ожидалось (30, 20, 5)" % (last,))

    prog99 = ("G90 G0 X0 Y0 Z5\nG99 G81 X10 Y20 Z-10 R2 F300\nX30 Y20\nG80\n")
    _segs, last99 = parse_segments(prog99)
    if last99 != (30.0, 20.0, 2.0):
        fails.append("G81 G99: конец %r, ожидалось (30, 20, 2)" % (last99,))

    # -- 8) программа G83 через parse_gcode: те же клевки, что и в прямом вызове
    prog83 = ("G90 G0 X10 Y20 Z5\nG98 G83 X10 Y20 Z-10 R2 Q3 F300\nG80\n")
    segs, last = parse_segments(prog83)
    if feed_bottoms(segs) != [-1.0, -4.0, -7.0, -10.0]:
        fails.append("G83 через parse: клевки %r, ожидалось [-1, -4, -7, -10]"
                     % feed_bottoms(segs))
    if last != (10.0, 20.0, 5.0):
        fails.append("G83 G98 через parse: конец %r, ожидалось (10, 20, 5)" % (last,))

    print("drill: G81 клевки=%r | G83 клевки=%r вывод_R=%d | G73 вывод_R=%d | "
          "программа G81 отверстия=%s"
          % (feed_bottoms(segs81), bottoms83, up_to_plane(segs83, 2.0),
             up_to_plane(segs73, 2.0), [(10.0, 20.0), (30.0, 20.0)]))
    if fails:
        for f in fails:
            print("FAIL:", f)
        return 1
    print("PASSED: кадровые циклы сверления держат контракт")
    return 0


if __name__ == "__main__":
    sys.exit(main())
