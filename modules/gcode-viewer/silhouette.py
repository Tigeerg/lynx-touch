"""Силуэт по топологии меша: сварка вершин, горизонт поверхности.

Вынесено из mock_backend.py (этап 0 плана переноса на C++).
"""
import math

from gv_common import _np

np = _np          # код слоя читает numpy как np

_SILHOUETTE_WELD_MM = 1e-4    # допуск сварки вершин меша (округление позиции)
# Меш-силуэт рисует ТОЛЬКО горизонт гладких поверхностей (стенки отверстий,
# скругления, фаски). Острые переходы исключены: их уже точно рисует слой
# статических B-rep рёбер (partEdges), и раньше силуэт дублировал острые
# круглые кромки вторым, тесселированным кольцом рядом с точным («удвоение
# круглых линий»). Вырожденные/сляйверные грани (площадь ~0) исключены: их
# нормаль - численный мусор, знак скачет и рождал короткие ложные «усы».
_SILHOUETTE_MAX_EDGE_DEG = 20.0     # круче = острая кромка (её слой - статические рёбра)
_SILHOUETTE_MIN_FACE_AREA = 1e-6    # мм^2: ниже - вырожденная грань


def _build_silhouette_topology(verts, weld=_SILHOUETTE_WELD_MM):
    """Топология меша для силуэта - строится один раз на модель.

    Меш - тот же triangle soup, что отдают load_step_model / load_stl_vertices:
    позиции вершин склеиваются округлением до weld (1e-4 мм: соседние грани
    одной кромки B-rep имеют общие точки сэмплирования, расхождение много
    меньше допуска), затем строится карта 'ребро (a,b) -> смежные треугольники'
    и геометрические нормали/центроиды треугольников. Генератор силуэта
    (_mesh_silhouette_segments) берёт рёбра ровно с двумя смежными
    треугольниками: силуэт - там, где две грани лежат по разные стороны от
    глаза. Топология берётся с меша, а не с аналитических поверхностей STEP:
    силуэт получает любая гладкая поверхность (стенка отверстия, скругление,
    фаска). Острые переходы и вырожденные (сляйверные) грани отсеяны - см.
    константы _SILHOUETTE_MAX_EDGE_DEG / _SILHOUETTE_MIN_FACE_AREA.

    Возвращает dict с полями ea/eb (K,3) - концы силуэтных кандидатов,
    f0/f1 (K,) - индексы смежных граней, n (F,3) - нормали граней,
    d (F,) - предвычисленное n·cen (центроиды граней) для скоростного
    знакового теста s = n·E - d на кадре; None без numpy."""
    if _np is None or verts is None:
        return None
    tri = _np.asarray(verts, dtype=_np.float64).reshape(-1, 3, 3)
    F = tri.shape[0]
    if F == 0:
        return None
    # сварка вершин: одна позиция -> один индекс. Без неё соседние грани дают
    # различающиеся индексы и карта смежности рёбер не собирается. Позиция -
    # представитель кластера на сетке weld: расхождение <= weld/2 (5e-5 мм),
    # много меньше грани меша; зато детерминированно и без return_index.
    flat = tri.reshape(-1, 3)
    key = _np.round(flat / weld)
    uniq, inv = _np.unique(key, axis=0, return_inverse=True)
    pos = uniq * weld
    fv = inv.reshape(F, 3)                  # треугольник -> индексы вершин
    # три ребра каждого треугольника, канонически отсортированные (a <= b)
    e = _np.stack([fv[:, [0, 1]], fv[:, [1, 2]], fv[:, [2, 0]]], axis=1)
    er = _np.sort(e, axis=2).reshape(-1, 2)
    uid, uinv = _np.unique(er, axis=0, return_inverse=True)
    uinv = uinv.reshape(-1)
    face_of = _np.repeat(_np.arange(F), 3)
    order = _np.argsort(uinv, kind="stable")
    su = uinv[order]
    sf = face_of[order]
    # границы групп по явному скану (return_index в numpy 2.5.3 на массиве с
    # повторами врёт, см. _smooth_face_normals)
    starts = _np.concatenate(([0], _np.flatnonzero(_np.diff(su)) + 1))
    counts = _np.diff(_np.concatenate((starts, [su.size])))
    keep = _np.flatnonzero(counts == 2)     # только рёбра с двумя гранями
    p1 = starts[keep]
    ends = uid[keep]
    f0 = sf[p1]
    f1 = sf[p1 + 1]
    # нормали - геометрические, по самим треугольникам: горизонт силуэта
    # задаёт переворот нормали грани, сглаживание его размывало бы
    cr = _np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ln = _np.linalg.norm(cr, axis=1, keepdims=True)
    area = 0.5 * ln[:, 0]
    n = cr / _np.where(ln == 0.0, 1.0, ln)
    # фильтр кандидатов (см. константы выше): только гладкие горизонты, без
    # острых кромок (их рисует слой статических рёбер) и без вырожденных граней
    e_dot = _np.einsum("ij,ij->i", n[f0], n[f1])
    e_ang = _np.arccos(_np.clip(e_dot, -1.0, 1.0))
    face_ok = area > _SILHOUETTE_MIN_FACE_AREA
    keep_edge = (face_ok[f0] & face_ok[f1] &
                 (e_ang < math.radians(_SILHOUETTE_MAX_EDGE_DEG)))
    f0 = f0[keep_edge]
    f1 = f1[keep_edge]
    ends = ends[keep_edge]
    cen = tri.mean(axis=1).astype(_np.float32)
    return {
        "ea": pos[ends[:, 0]].astype(_np.float32),
        "eb": pos[ends[:, 1]].astype(_np.float32),
        "f0": f0,
        "f1": f1,
        "n": n.astype(_np.float32),
        # n·cen предвычислено здесь, один раз на модель: на кадре остаётся
        # s = n·E - d (BLAS-матвектор) вместо einsum с материализацией
        # (E - cen) на все F граней (замер на Test_model: этап s подешевел
        # ~9.1 -> ~0.6 мс, всё событие камеры - см. _mesh_silhouette_segments).
        "d": _np.einsum("ij,ij->i", n.astype(_np.float32), cen),
    }


def _mesh_silhouette_segments(topo, E):
    """Силуэтные рёбра меша для глаза E (raw-кадр модели).

    Ребро силуэтное, когда два смежных треугольника лежат по разные стороны
    от глаза (n1.(eye-c1) и n2.(eye-c2) разных знаков): ребро разделяет
    видимую и невидимую грань, т.е. лежит на горизонте поверхности. Никаких
    аналитических образующих и фильтров - знак перехода нормали и есть силуэт.
    Возвращает (K,2,3) float32 (пустой массив без силуэта) или None."""
    if topo is None or _np is None:
        return None
    ev = _np.asarray(E, dtype=_np.float32)
    # Знаковый тест остался «произведение < 0» (грань с s == 0 - глаз в
    # её плоскости - не силуэтная, как и раньше), но знаки собраны в int8:
    # сбор sg[f0]/sg[f1] гоняет 1 байт на ребро вместо 4 у float32
    # (замер на Test_model: ~9.1 -> ~6.2 мс на событие камеры).
    s = topo["n"] @ ev - topo["d"]
    sg = _np.sign(s).astype(_np.int8)
    sel = sg[topo["f0"]] * sg[topo["f1"]] < 0
    if not sel.any():
        return _np.empty((0, 2, 3), dtype=_np.float32)
    k = _np.flatnonzero(sel)
    return _np.stack([topo["ea"][k], topo["eb"][k]], axis=1)
