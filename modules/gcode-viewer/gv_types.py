"""Именованные структуры, которые ходят между слоями бэкенда вьювера.

Этап 0 переноса на C++: данные между модулями ходили кортежами и dict, и поля
приходилось угадывать. Здесь лежат типы границ модулей. Структуры, видимые QML
(QVariantMap/QVariantList - этапы пути, коллизии, toolCheck, toolInfo), остаются
dict/list: QML читает поля по именам, и подменять их объектами Python нельзя.

Тип Aabb хранит габарит (lo/hi) и переживает границу модуля; точка (x, y, z)
остаётся кортежем - она сама говорит, что хранит (CLAUDE.md).
"""
from dataclasses import dataclass, field
from typing import List, Optional, Tuple, Union

import numpy as np


@dataclass(frozen=True)
class PathPoint:
    """Точка траектории в машинных координатах, мм.

    `speed_mm_s` - скорость ИСХОДЯЩЕГО сегмента: запрограммированная подача F
    (мм/мин -> мм/с) у G1/G2/G3 либо фиксированная машинная скорость быстрого
    хода у G0 (см. gcode_parser._RAPID_SPEED_MMS).
    """
    point: Tuple[float, float, float]
    is_rapid: bool
    speed_mm_s: float


@dataclass(frozen=True)
class Stage:
    """Этап УП из parse_gcode: индекс первой точки, подпись и метаданные UI_TOOL.

    `info` - разобранный тег UI_TOOL ({}, когда тега нет): его читают построение
    профиля инструмента и подпись этапа.
    """
    index: int
    label: str
    info: dict = field(default_factory=dict)


@dataclass(frozen=True)
class Aabb:
    """Осевой габарит (AABB): минимум и максимум по трём осям, мм."""
    lo: Tuple[float, float, float]
    hi: Tuple[float, float, float]

    def flatten(self) -> Tuple[float, float, float, float, float, float]:
        """Габарит одним кортежем (x0, y0, z0, x1, y1, z1) - формат буферов и кэша."""
        return (self.lo[0], self.lo[1], self.lo[2],
                self.hi[0], self.hi[1], self.hi[2])


@dataclass(frozen=True, eq=False)
class PackedEdges:
    """Упакованный Lines-буфер отрезков: байты вершин, их число и габарит.

    Проходит границу packing -> controller/geometry/cache_store. `bounds` бывает
    None у пустого буфера.
    """
    raw: bytes
    nv: int
    bounds: Optional[Aabb]


@dataclass(eq=False)
class PathData:
    """Подготовленная траектория из _prepare_ordered.

    Переживает границу path_prep <-> cache_store <-> controller. `stages` -
    нормализованные диапазоны пути (список dict {start, end, label, tool}):
    их читает QML, поэтому это dict, а не объекты. `pts/cum/rapid/speeds` -
    массивы numpy (None у пустого пути).
    """
    seg_count: int
    last: Tuple[float, float, float]
    pts: Optional[np.ndarray]
    cum: Optional[np.ndarray]
    rapid: Optional[np.ndarray]
    speeds: Optional[np.ndarray]
    total: float
    rapid_bytes: bytes
    rapid_nv: int
    feed_bytes: bytes
    feed_nv: int
    bounds: Optional[Aabb]
    center: List[float]
    radius: float
    min_y: float
    stages: list


@dataclass(frozen=True)
class CylinderSurface:
    """Аналитическая цилиндрическая грань STEP.

    `o` - точка на оси, `d` - единичное направление оси; `e1`/`e2` - базис
    поперечного сечения, `arc` - угловая дуга грани (a0, a1) в радианах либо
    (None, None) у полного круга; `r` - радиус; `t0`/`t1` - пределы вдоль оси по
    вершинам грани, мм.
    """
    o: Tuple[float, float, float]
    d: Tuple[float, float, float]
    e1: Tuple[float, float, float]
    e2: Tuple[float, float, float]
    arc: Tuple[Optional[float], Optional[float]]
    r: float
    t0: float
    t1: float


@dataclass(frozen=True)
class ConeSurface:
    """Аналитическая коническая грань STEP.

    `a` - вершина конуса, `d` - единичное направление оси, `psi` - полуугол,
    рад; `s0`/`s1` - пределы параметра образующей (длина вдоль оси / cos psi), мм.
    """
    a: Tuple[float, float, float]
    d: Tuple[float, float, float]
    psi: float
    s0: float
    s1: float


StepSurface = Union[CylinderSurface, ConeSurface]


@dataclass(eq=False)
class MeshTopology:
    """Топология меша для силуэта (строит _build_silhouette_topology).

    `ea`/`eb` (K,3) - концы кандидатных силуэтных рёбер, `f0`/`f1` (K,) - индексы
    смежных граней, `n` (F,3) - нормали граней, `d` (F,) - предвычисленное n·cen.
    Переживает границу silhouette <-> controller.
    """
    ea: np.ndarray
    eb: np.ndarray
    f0: np.ndarray
    f1: np.ndarray
    n: np.ndarray
    d: np.ndarray


@dataclass(eq=False)
class CylinderFamilies:
    """Предвычисленные цилиндрические семейства касательных (плоские массивы).

    `O`/`D` - точка и направление оси, `E1`/`E2` - базис сечения, `R` - радиус
    секции, `T0`/`T1` - пределы вдоль оси, `FULL` - полный ли круг, `DROP` -
    азимуты стыковых рёбер, `RREF` - опорный радиус коаксиальной группы,
    `ARC`/`A0`/`A1` - покрытие дуги.
    """
    O: np.ndarray
    D: np.ndarray
    E1: np.ndarray
    E2: np.ndarray
    FULL: np.ndarray
    DROP: np.ndarray
    R: np.ndarray
    T0: np.ndarray
    T1: np.ndarray
    RREF: np.ndarray
    ARC: np.ndarray
    A0: np.ndarray
    A1: np.ndarray


@dataclass(frozen=True, eq=False)
class TangentFamilies:
    """Семейства касательных STEP: цилиндры (плоские массивы) и конусы."""
    cylinders: CylinderFamilies
    cones: List[ConeSurface]


@dataclass(frozen=True)
class ProfilePoint:
    """Вершина 2D-профиля инструмента/оправки в плоскости (радиус, высота), мм.

    Профиль вращается вокруг оси Y; высота отсчитывается от острия инструмента
    либо от основания оправки.
    """
    radius_mm: float
    height_mm: float


@dataclass(frozen=True)
class HolderSection:
    """Цилиндрическая секция оправки: радиус и пределы по высоте от острия, мм.

    Секции читает скан коллизий: круг радиуса `radius_mm` между `z0_mm` и
    `z1_mm` над остриём инструмента.
    """
    radius_mm: float
    z0_mm: float
    z1_mm: float
