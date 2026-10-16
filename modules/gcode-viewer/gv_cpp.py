"""Шим к C-ABI-мосту gvbridge: C++-слои вьювера из Python через ctypes.

Этап 5 плана переноса (PORT-CPP.md). Мост собран из модулей gvcore (нормали кожи,
топология силуэта, касательные, парсер и подготовка траектории) и gvstep (STEP:
суп, рёбра, нормали, поверхности) и наружу отдаёт только C-типы: буферы через
out-параметры и структуры из C-полей, память библиотеки освобождает её же gv_free
(массивы структур - своей функцией gv_free_*). Ни один C++-тип через границу не
проходит.

Умолчание - моста нет: пустая переменная CNC_CPP значит питоновский путь, как
раньше. Значения:

  CNC_CPP=normals     - нормали STL (controller._stl_normals) считает C++;
  CNC_CPP=step        - ветку STEP (_load_model_verts) считает C++ (суп, нормали,
                        рёбра до склейки - склейку и упаковку ведёт Python, чтобы
                        буфер обводки остался бит-в-бит таким же);
  CNC_CPP=surfaces    - аналитические поверхности STEP (cylinders/cones) считает
                        C++; вместе со `step` файл читается один раз на оба слоя
                        (gv_load_step_all), порознь - каждый слой своим проходом;
  CNC_CPP=silhouette  - топологию силуэта меша, силуэт на глазу и аналитические
                        семейства касательных считает C++;
  CNC_CPP=parser      - разбор УП и подготовку траектории (parse_gcode +
                        _prepare_ordered) считает C++;
  CNC_CPP=qtgeom      - буферы геометрий Qt Quick 3D (траектория, меш,
                        инструмент, контур) собирает C++ (gvgeom через шим
                        gvqtshim.dll, оболочка gv_qtgeom.py); типы QML
                        по-прежнему регистрирует Python и контракт не меняется;
  CNC_CPP=all         - всё перечисленное;
  пусто/off/0         - всё на Python (умолчание).

Любой отказ C++-пути виден в журнале: шим пишет logger.warning, а вызывающий
фолбэкает на Python. Молчаливого отказа нет.

Полный путь к библиотеке задаёт CNC_CPP_LIB, иначе берётся <repo>/build/cpp/
gvbridge.dll (туда её кладёт цель gvbridge, см. cpp/CMakeLists.txt).

Запуск проверки: python modules/gcode-viewer/test_cpp_bridge.py
"""
import ctypes
import logging
import os
import threading

from gv_common import _np
from gv_types import (Aabb, ConeSurface, CylinderFamilies, CylinderSurface,
                      MeshTopology, PathData, TangentFamilies)

logger = logging.getLogger(__name__)

MODE_ENV = "CNC_CPP"
LIB_ENV = "CNC_CPP_LIB"

# Версия ABI, которую понимает этот шим (gv_bridge.h: GV_ABI_VERSION).
ABI_VERSION = 1

# Слой, который включает значение флага: ключ -> нужные значения CNC_CPP.
_PART_MODES = {"normals": ("normals", "all"), "step": ("step", "all"),
               "surfaces": ("surfaces", "all"), "silhouette": ("silhouette", "all"),
               "parser": ("parser", "all"), "qtgeom": ("qtgeom", "all")}
_OFF_VALUES = ("", "off", "0", "no", "false")
_MODE_VALUES = ("normals", "step", "surfaces", "silhouette", "parser", "qtgeom", "all")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_LIB = os.path.join(ROOT, "build", "cpp", "gvbridge.dll")

# Каталоги поиска DLL (os.add_dll_directory) держим живыми: закрытая ручка
# выкидывает каталог из путей поиска.
_dll_dirs = []
_lock = threading.Lock()
_lib = None                # загруженная библиотека
_load_error = ""           # почему не загрузилась (для сообщения один раз)
_warned = set()            # о каждом отказе сообщаем один раз, а не построчно


class GvCppError(RuntimeError):
    """Отказ моста: библиотеки нет, ABI не тот или код возврата не 0."""


# Раскладка структур ABI повторяет gv_bridge.h поле в поле: порядок и типы
# обязаны совпасть, иначе ctypes прочитает чужие байты.
class _GvSurfaces(ctypes.Structure):
    """Аналитические поверхности: kinds (0 цилиндр, 1 конус) и два плоских ряда."""
    _fields_ = [("kinds", ctypes.POINTER(ctypes.c_int)),
                ("surface_count", ctypes.c_size_t),
                ("cylinders", ctypes.POINTER(ctypes.c_double)),
                ("cylinder_count", ctypes.c_size_t),
                ("cones", ctypes.POINTER(ctypes.c_double)),
                ("cone_count", ctypes.c_size_t)]


class _GvMeshTopology(ctypes.Structure):
    """Топология силуэта: поля те же, что у MeshTopology в gv_types.py."""
    _fields_ = [("ea", ctypes.POINTER(ctypes.c_float)),
                ("eb", ctypes.POINTER(ctypes.c_float)),
                ("f0", ctypes.POINTER(ctypes.c_int)),
                ("f1", ctypes.POINTER(ctypes.c_int)),
                ("n", ctypes.POINTER(ctypes.c_float)),
                ("d", ctypes.POINTER(ctypes.c_float)),
                ("edge_count", ctypes.c_size_t),
                ("face_count", ctypes.c_size_t)]


class _GvTangentFamilies(ctypes.Structure):
    """Семейства касательных: плоские массивы CylinderFamilies и ряд конусов."""
    _fields_ = [(name, ctypes.POINTER(ctypes.c_double))
                for name in ("O", "D", "E1", "E2", "R", "T0", "T1", "RREF", "A0", "A1")]
    _fields_ += [("FULL", ctypes.POINTER(ctypes.c_ubyte)),
                 ("DROP", ctypes.POINTER(ctypes.c_double)),
                 ("ARC", ctypes.POINTER(ctypes.c_ubyte)),
                 ("count", ctypes.c_size_t),
                 ("cones", ctypes.POINTER(ctypes.c_double)),
                 ("cone_count", ctypes.c_size_t)]


class _GvPathData(ctypes.Structure):
    """Подготовленная траектория: поля те же, что у PathData в gv_types.py."""
    _fields_ = [("seg_count", ctypes.c_int),
                ("last", ctypes.c_double * 3),
                ("pts", ctypes.POINTER(ctypes.c_double)),
                ("cum", ctypes.POINTER(ctypes.c_double)),
                ("rapid", ctypes.POINTER(ctypes.c_ubyte)),
                ("speeds", ctypes.POINTER(ctypes.c_double)),
                ("point_count", ctypes.c_size_t),
                ("total", ctypes.c_double),
                ("rapid_stream", ctypes.POINTER(ctypes.c_ubyte)),
                ("rapid_bytes", ctypes.c_size_t),
                ("rapid_nv", ctypes.c_int),
                ("feed_stream", ctypes.POINTER(ctypes.c_ubyte)),
                ("feed_bytes", ctypes.c_size_t),
                ("feed_nv", ctypes.c_int),
                ("has_bounds", ctypes.c_int),
                ("bounds", ctypes.c_double * 6),
                ("center", ctypes.c_double * 3),
                ("radius", ctypes.c_double),
                ("min_y", ctypes.c_double),
                ("stages", ctypes.POINTER(ctypes.c_double)),
                ("stage_count", ctypes.c_size_t),
                ("stage_labels", ctypes.POINTER(ctypes.c_char)),
                ("stage_label_at", ctypes.POINTER(ctypes.c_size_t)),
                ("stage_types", ctypes.POINTER(ctypes.c_char)),
                ("stage_type_at", ctypes.POINTER(ctypes.c_size_t))]


# Ширина строк дампов ABI (gv_bridge.h): 17 чисел на цилиндр, 9 на конус,
# 16 на этап пути. Раскладка общая с пробником cpp/tangents_probe.cpp.
_CYL_ROW = 17
_CONE_ROW = 9
_STAGE_ROW = 16
# Порядок числовых полей UI_TOOL в строке этапа - за fillStageTool (gv_bridge.cpp).
_TOOL_FIELDS = ("number", "diameter", "corner_radius", "taper_deg", "tip_angle_deg",
                "tip_diameter", "flute_length", "shoulder_length", "body_length",
                "shank_diameter", "overall_length", "flutes")
_TOOL_INT_FIELDS = ("number", "flutes")


def library_path() -> str:
    """Путь к gvbridge.dll: CNC_CPP_LIB, иначе каталог сборки моста."""
    return os.environ.get(LIB_ENV) or DEFAULT_LIB


def occt_bin_dir() -> str:
    """Каталог DLL OCCT: CNC_OCC_DIR (или окружение conda), подкаталог bin."""
    occ = os.environ.get("CNC_OCC_DIR")
    if not occ and os.environ.get("CONDA_PREFIX"):
        occ = os.path.join(os.environ["CONDA_PREFIX"], "Library")
    if not occ:
        occ = r"C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library"
    return os.path.join(occ, "bin")


def mode() -> str:
    """Значение CNC_CPP: '', 'normals', 'step', 'surfaces', 'silhouette',
    'parser', 'qtgeom' или 'all'. Незнакомое - отказ."""
    raw = os.environ.get(MODE_ENV, "").strip().lower()
    if raw in _OFF_VALUES:
        return ""
    if raw not in _MODE_VALUES:
        _warn_once("unknown",
                   "CNC_CPP=%r не знаком: ожидалось %s; работаем на Python"
                   % (raw, ", ".join(_MODE_VALUES)))
        return ""
    return raw


def available() -> bool:
    """Есть ли мост: библиотека найдена, загружена и ABI совпал."""
    return _ensure() is not None


def load_error() -> str:
    """Почему моста нет (пусто, если он есть)."""
    _ensure()
    return _load_error


def normals_enabled() -> bool:
    """Считать нормали STL на C++ (CNC_CPP=normals|all и живой мост)."""
    return _requested("normals")


def step_enabled() -> bool:
    """Считать ветку STEP на C++ (CNC_CPP=step|all и живой мост)."""
    return _requested("step")


def surfaces_enabled() -> bool:
    """Считать аналитические поверхности STEP на C++ (CNC_CPP=surfaces|all)."""
    return _requested("surfaces")


def silhouette_enabled() -> bool:
    """Считать силуэт и касательные на C++ (CNC_CPP=silhouette|all)."""
    return _requested("silhouette")


def parser_enabled() -> bool:
    """Разбирать УП и готовить траекторию на C++ (CNC_CPP=parser|all)."""
    return _requested("parser")


def qtgeom_enabled() -> bool:
    """Собирать буферы геометрий Qt Quick 3D на C++ (CNC_CPP=qtgeom|all).

    Мост у этого слоя свой - шим gvqtshim.dll (cpp/exp/gvqtshim), а не gvbridge:
    геометрии Qt живут в процессе PySide6 и на уже загруженной там Qt. Поэтому
    здесь только значение флага; жив ли шим, решает gv_qtgeom (он же пишет
    предупреждение и оставляет питоновский путь).
    """
    return mode() in _PART_MODES["qtgeom"]


def smooth_face_normals(verts, crease_deg: float = 20.0):
    """Нормали кожи (F,3,3) float32 для супа вершин (F*9 double).

    Точный повтор normals._smooth_face_normals: вызывающий может сверить байты.
    Отказ (пустой вход, не кратный девяти, ошибка библиотеки) - GvCppError.
    """
    lib = _require()
    soup = _np.ascontiguousarray(_np.asarray(verts, dtype=_np.float64).reshape(-1))
    if soup.size == 0 or soup.size % 9:
        raise GvCppError("нормали: вершин %d, ожидалось кратно 9" % soup.size)
    out = ctypes.POINTER(ctypes.c_float)()
    count = ctypes.c_size_t(0)
    code = lib.gv_smooth_face_normals(
        soup.ctypes.data_as(ctypes.POINTER(ctypes.c_double)), soup.size,
        ctypes.c_double(crease_deg), ctypes.byref(out), ctypes.byref(count))
    if code != 0:
        raise GvCppError("нормали: код %d, %s" % (code, _last_error(lib)))
    try:
        flat = _take(out, count.value, _np.float32)
    finally:
        lib.gv_free(out)
    return flat.reshape(-1, 3, 3)


def load_step_model(path: str, deflection: float = 0.05):
    """Модель STEP из C++: (verts (N,3) float64, segments, normals).

    segments - список пар точек ((x,y,z),(x,y,z)) в координатах модели, рёбра до
    склейки: склейку (_coalesce_segments) и упаковку буфера ведёт Python, поэтому
    буфер обводки совпадает с эталонным бит-в-бит. Нормали - (F,3,3) float32 или
    None, если супа нет (у эталона то же). Отказ - GvCppError.
    """
    lib = _require()
    verts_p = ctypes.POINTER(ctypes.c_double)()
    normals_p = ctypes.POINTER(ctypes.c_float)()
    segs_p = ctypes.POINTER(ctypes.c_double)()
    nv, nn, ns = ctypes.c_size_t(0), ctypes.c_size_t(0), ctypes.c_size_t(0)
    code = lib.gv_load_step_model(
        str(path).encode("utf-8"), ctypes.c_double(deflection),
        ctypes.byref(verts_p), ctypes.byref(nv),
        ctypes.byref(normals_p), ctypes.byref(nn),
        ctypes.byref(segs_p), ctypes.byref(ns))
    if code != 0:
        raise GvCppError("STEP %s: код %d, %s" % (path, code, _last_error(lib)))
    try:
        verts = _take(verts_p, nv.value, _np.float64).reshape(-1, 3)
        normals = _take(normals_p, nn.value, _np.float32)
        raw = _take(segs_p, ns.value, _np.float64).reshape(-1, 2, 3)
    finally:
        lib.gv_free(verts_p)
        lib.gv_free(normals_p)
        lib.gv_free(segs_p)
    # Список пар, а не массив: ровно так рёбра отдаёт питоновский эталон, и
    # _coalesce_segments принимает их без разбора формы.
    segments = [(tuple(pair[0]), tuple(pair[1])) for pair in raw.tolist()]
    return verts, segments, (None if normals.size == 0 else normals.reshape(-1, 3, 3))


def load_step_surfaces(path: str, deflection: float = 0.05):
    """Аналитические поверхности STEP (CylinderSurface/ConeSurface) из C++.

    Точный повтор step_surfaces.load_step_surfaces: тот же обход граней, тот же
    порядок дескрипторов. Отказ - GvCppError.
    """
    lib = _require()
    surfs = _GvSurfaces()
    code = lib.gv_load_step_surfaces(str(path).encode("utf-8"),
                                     ctypes.c_double(deflection),
                                     ctypes.byref(surfs))
    if code != 0:
        raise GvCppError("поверхности STEP %s: код %d, %s"
                         % (path, code, _last_error(lib)))
    try:
        return _surfaces(surfs)
    finally:
        lib.gv_free_surfaces(ctypes.byref(surfs))


def load_step_all(path: str, deflection: float = 0.05):
    """Модель STEP и поверхности за одно чтение файла.

    Возвращает (verts, segments, normals, surfaces) - то же, что вместе дают
    load_step_model и load_step_surfaces, но STEP читается один раз: второй
    проход Control_Reader стоит около 0.8 с на модель. Отказ - GvCppError.
    """
    lib = _require()
    verts_p = ctypes.POINTER(ctypes.c_double)()
    normals_p = ctypes.POINTER(ctypes.c_float)()
    segs_p = ctypes.POINTER(ctypes.c_double)()
    nv, nn, ns = ctypes.c_size_t(0), ctypes.c_size_t(0), ctypes.c_size_t(0)
    surfs = _GvSurfaces()
    code = lib.gv_load_step_all(
        str(path).encode("utf-8"), ctypes.c_double(deflection),
        ctypes.byref(verts_p), ctypes.byref(nv),
        ctypes.byref(normals_p), ctypes.byref(nn),
        ctypes.byref(segs_p), ctypes.byref(ns), ctypes.byref(surfs))
    if code != 0:
        raise GvCppError("STEP %s: код %d, %s" % (path, code, _last_error(lib)))
    try:
        verts = _take(verts_p, nv.value, _np.float64).reshape(-1, 3)
        normals = _take(normals_p, nn.value, _np.float32)
        raw = _take(segs_p, ns.value, _np.float64).reshape(-1, 2, 3)
        surfaces = _surfaces(surfs)
    finally:
        lib.gv_free(verts_p)
        lib.gv_free(normals_p)
        lib.gv_free(segs_p)
        lib.gv_free_surfaces(ctypes.byref(surfs))
    segments = [(tuple(pair[0]), tuple(pair[1])) for pair in raw.tolist()]
    return (verts, segments,
            (None if normals.size == 0 else normals.reshape(-1, 3, 3)), surfaces)


def build_silhouette_topology(verts):
    """Топология меша для силуэта (MeshTopology из gv_types) из C++.

    Точный повтор silhouette._build_silhouette_topology на том же супе вершин:
    сварка, карта рёбер по двум граням, нормали и предвычисленное n·cen. Отказ
    (пустой суп, не кратный девяти, ошибка библиотеки) - GvCppError.
    """
    lib = _require()
    soup = _np.ascontiguousarray(_np.asarray(verts, dtype=_np.float64).reshape(-1))
    if soup.size == 0 or soup.size % 9:
        raise GvCppError("топология силуэта: вершин %d, ожидалось кратно 9"
                         % soup.size)
    topo = _GvMeshTopology()
    code = lib.gv_build_silhouette_topology(
        soup.ctypes.data_as(ctypes.POINTER(ctypes.c_double)), soup.size,
        ctypes.byref(topo))
    if code != 0:
        raise GvCppError("топология силуэта: код %d, %s" % (code, _last_error(lib)))
    try:
        ne, nf = int(topo.edge_count), int(topo.face_count)
        return MeshTopology(
            ea=_take(topo.ea, ne * 3, _np.float32).reshape(-1, 3),
            eb=_take(topo.eb, ne * 3, _np.float32).reshape(-1, 3),
            # f0/f1 у эталона - машинный int (np.arange): на этой сборке int32,
            # и тип виден в сверке побайтово, поэтому он повторён, а не угадан
            f0=_take(topo.f0, ne, _np.int_),
            f1=_take(topo.f1, ne, _np.int_),
            n=_take(topo.n, nf * 3, _np.float32).reshape(-1, 3),
            d=_take(topo.d, nf, _np.float32))
    finally:
        lib.gv_free_mesh_topology(ctypes.byref(topo))


def mesh_silhouette_segments(topo, eye):
    """Силуэтные рёбра меша для глаза eye (raw-кадр модели), (K,2,3) float32.

    Точный повтор silhouette._mesh_silhouette_segments: знаковый тест n·E - d по
    смежным граням. Топология - MeshTopology (питоновский объект: массивы только
    передаются указателями). Отказ - GvCppError.
    """
    if topo is None:
        return None
    lib = _require()
    keep = _topology_arrays(topo)
    struct = _GvMeshTopology()
    _fill_topology(struct, keep)
    eye3 = (ctypes.c_double * 3)(*[float(v) for v in eye])
    out = ctypes.POINTER(ctypes.c_float)()
    count = ctypes.c_size_t(0)
    code = lib.gv_mesh_silhouette_segments(ctypes.byref(struct), eye3,
                                           ctypes.byref(out), ctypes.byref(count))
    if code != 0:
        raise GvCppError("силуэт: код %d, %s" % (code, _last_error(lib)))
    try:
        flat = _take(out, count.value, _np.float32)
    finally:
        lib.gv_free(out)
    return flat.reshape(-1, 2, 3)


def tangent_families(surfaces, packed=None, skin=None):
    """Предвычисленные семейства касательных (TangentFamilies из gv_types) из C++.

    Точный повтор tangents._tangent_families: та же группировка коаксиальных
    цилиндров, тот же базис, покрытие дуги и политика конусов. `packed` -
    PackedEdges обводки STEP (пометки стыковых азимутов) или None; `skin` -
    (verts, normals) кожи для политики конусов или None. Отказ - GvCppError.
    """
    lib = _require()
    kinds, cyls, cones = _flat_surfaces(surfaces)
    surf = _GvSurfaces(kinds.ctypes.data_as(ctypes.POINTER(ctypes.c_int)), len(kinds),
                       cyls.ctypes.data_as(ctypes.POINTER(ctypes.c_double)), cyls.shape[0],
                       cones.ctypes.data_as(ctypes.POINTER(ctypes.c_double)), cones.shape[0])
    # Ссылки на массивы живут до конца вызова: структура держит только указатели.
    keep = [kinds, cyls, cones]
    raw_ptr, raw_bytes, raw_nv = None, 0, 0
    if packed is not None and getattr(packed, "raw", None):
        raw = _np.frombuffer(packed.raw, dtype=_np.uint8)
        keep.append(raw)
        raw_ptr = raw.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte))
        raw_bytes, raw_nv = int(raw.size), int(packed.nv)
    soup_ptr, soup_count, norm_ptr, norm_count = None, 0, None, 0
    if skin is not None and skin[0] is not None and skin[1] is not None:
        # Приведение как в эталоне: суп - float64, нормали - float32 (у кожи STEP
        # они уже такие; расширение до float64 точное).
        soup = _np.ascontiguousarray(_np.asarray(skin[0], dtype=_np.float64).reshape(-1))
        normals = _np.ascontiguousarray(_np.asarray(skin[1], dtype=_np.float32).reshape(-1))
        keep += [soup, normals]
        soup_ptr = soup.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
        soup_count = int(soup.size)
        norm_ptr = normals.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        norm_count = int(normals.size)
    struct = _GvTangentFamilies()
    code = lib.gv_tangent_families(
        ctypes.byref(surf), raw_ptr, ctypes.c_size_t(raw_bytes), ctypes.c_int(raw_nv),
        soup_ptr, ctypes.c_size_t(soup_count), norm_ptr, ctypes.c_size_t(norm_count),
        ctypes.byref(struct))
    if code != 0:
        raise GvCppError("семейства касательных: код %d, %s" % (code, _last_error(lib)))
    try:
        return _families(struct)
    finally:
        lib.gv_free_tangent_families(ctypes.byref(struct))


def tangent_segments(families, eye):
    """Отрезки касательных на глазу eye (raw-кадр модели), (M,2,3) float64.

    Точный повтор tangents._tangent_from_families на семействах из
    tangent_families. Отказ - GvCppError.
    """
    lib = _require()
    keep = _families_arrays(families)
    struct = _GvTangentFamilies()
    _fill_families(struct, keep)
    eye3 = (ctypes.c_double * 3)(*[float(v) for v in eye])
    out = ctypes.POINTER(ctypes.c_double)()
    count = ctypes.c_size_t(0)
    code = lib.gv_tangent_segments(ctypes.byref(struct), eye3,
                                   ctypes.byref(out), ctypes.byref(count))
    if code != 0:
        raise GvCppError("касательные: код %d, %s" % (code, _last_error(lib)))
    try:
        flat = _take(out, count.value, _np.float64)
    finally:
        lib.gv_free(out)
    return flat.reshape(-1, 2, 3)


def prepare_path(text: str) -> PathData:
    """Разбор УП и подготовка траектории (PathData из gv_types) из C++.

    Точный повтор пары gcode_parser.parse_gcode + path_prep._prepare_ordered:
    те же точки, флаги, скорость, потоки вершин, габарит и этапы. Отказ -
    GvCppError.
    """
    lib = _require()
    data = _GvPathData()
    code = lib.gv_prepare_path(str(text).encode("utf-8"), ctypes.byref(data))
    if code != 0:
        raise GvCppError("разбор УП: код %d, %s" % (code, _last_error(lib)))
    try:
        return _path_data(data)
    finally:
        lib.gv_free_path_data(ctypes.byref(data))


def _surfaces(struct):
    """Плоские массивы ABI -> список CylinderSurface/ConeSurface в порядке граней."""
    kinds = _take(struct.kinds, struct.surface_count, _np.int32).tolist()
    cyls = _take(struct.cylinders, struct.cylinder_count * _CYL_ROW,
                 _np.float64).reshape(-1, _CYL_ROW)
    cones = _take(struct.cones, struct.cone_count * _CONE_ROW,
                  _np.float64).reshape(-1, _CONE_ROW)
    out = []
    ci = ni = 0
    for kind in kinds:
        if kind == 1:
            row = cones[ni]
            ni += 1
            out.append(ConeSurface(a=_row(row[0:3]), d=_row(row[3:6]),
                                   psi=float(row[6]), s0=float(row[7]),
                                   s1=float(row[8])))
        else:
            row = cyls[ci]
            ci += 1
            a0, a1 = float(row[15]), float(row[16])
            # полный круг вместо дуги едет как NaN в a0 и a1 (эталон: (None, None))
            arc = (None, None) if (_np.isnan(a0) and _np.isnan(a1)) else (a0, a1)
            out.append(CylinderSurface(o=_row(row[0:3]), d=_row(row[3:6]),
                                       e1=_row(row[6:9]), e2=_row(row[9:12]),
                                       arc=arc, r=float(row[12]),
                                       t0=float(row[13]), t1=float(row[14])))
    return out


def _flat_surfaces(surfaces):
    """Список поверхностей -> плоские массивы ABI (обратная к _surfaces)."""
    count = len(surfaces)
    kinds = _np.empty(count, dtype=_np.int32)
    cyls = []
    cones = []
    for i, s in enumerate(surfaces):
        if isinstance(s, ConeSurface):
            kinds[i] = 1
            cones.append(list(s.a) + list(s.d) + [s.psi, s.s0, s.s1])
        else:
            kinds[i] = 0
            arc = s.arc if s.arc else (None, None)
            nan = float("nan")
            cyls.append(list(s.o) + list(s.d) + list(s.e1) + list(s.e2)
                        + [s.r, s.t0, s.t1,
                           nan if arc[0] is None else arc[0],
                           nan if arc[1] is None else arc[1]])
    return (_np.ascontiguousarray(kinds),
            _np.ascontiguousarray(cyls, dtype=_np.float64).reshape(-1, _CYL_ROW),
            _np.ascontiguousarray(cones, dtype=_np.float64).reshape(-1, _CONE_ROW))


def _families(struct):
    """Плоские массивы ABI -> TangentFamilies (типы полей как у эталона)."""
    n = int(struct.count)
    cyl = CylinderFamilies(
        O=_take(struct.O, n * 3, _np.float64).reshape(-1, 3),
        D=_take(struct.D, n * 3, _np.float64).reshape(-1, 3),
        E1=_take(struct.E1, n * 3, _np.float64).reshape(-1, 3),
        E2=_take(struct.E2, n * 3, _np.float64).reshape(-1, 3),
        R=_take(struct.R, n, _np.float64),
        T0=_take(struct.T0, n, _np.float64),
        T1=_take(struct.T1, n, _np.float64),
        RREF=_take(struct.RREF, n, _np.float64),
        A0=_take(struct.A0, n, _np.float64),
        A1=_take(struct.A1, n, _np.float64),
        FULL=_take(struct.FULL, n, _np.bool_),
        DROP=_take(struct.DROP, n * 8, _np.float64).reshape(-1, 8),
        ARC=_take(struct.ARC, n * 64, _np.bool_).reshape(-1, 64))
    rows = _take(struct.cones, int(struct.cone_count) * _CONE_ROW,
                 _np.float64).reshape(-1, _CONE_ROW)
    cones = [ConeSurface(a=_row(r[0:3]), d=_row(r[3:6]), psi=float(r[6]),
                         s0=float(r[7]), s1=float(r[8])) for r in rows]
    return TangentFamilies(cylinders=cyl, cones=cones)


def _families_arrays(fam):
    """Массивы семейств в раскладке ABI: (N,3)/(N,) float64, FULL и ARC - байты."""
    cyl = fam.cylinders
    keep = {
        "O": _np.ascontiguousarray(cyl.O, dtype=_np.float64).reshape(-1, 3),
        "D": _np.ascontiguousarray(cyl.D, dtype=_np.float64).reshape(-1, 3),
        "E1": _np.ascontiguousarray(cyl.E1, dtype=_np.float64).reshape(-1, 3),
        "E2": _np.ascontiguousarray(cyl.E2, dtype=_np.float64).reshape(-1, 3),
        "R": _np.ascontiguousarray(cyl.R, dtype=_np.float64).reshape(-1),
        "T0": _np.ascontiguousarray(cyl.T0, dtype=_np.float64).reshape(-1),
        "T1": _np.ascontiguousarray(cyl.T1, dtype=_np.float64).reshape(-1),
        "RREF": _np.ascontiguousarray(cyl.RREF, dtype=_np.float64).reshape(-1),
        "A0": _np.ascontiguousarray(cyl.A0, dtype=_np.float64).reshape(-1),
        "A1": _np.ascontiguousarray(cyl.A1, dtype=_np.float64).reshape(-1),
        "FULL": _np.ascontiguousarray(cyl.FULL, dtype=_np.uint8).reshape(-1),
        "DROP": _np.ascontiguousarray(cyl.DROP, dtype=_np.float64).reshape(-1, 8),
        "ARC": _np.ascontiguousarray(cyl.ARC, dtype=_np.uint8).reshape(-1, 64),
    }
    keep["count"] = keep["O"].shape[0]
    keep["cones"] = _np.ascontiguousarray(
        [list(c.a) + list(c.d) + [c.psi, c.s0, c.s1] for c in fam.cones],
        dtype=_np.float64).reshape(-1, _CONE_ROW)
    return keep


def _fill_families(struct, keep):
    """Заполнить структуру ABI указателями массивов семейств (без копии)."""
    for name in ("O", "D", "E1", "E2"):
        setattr(struct, name, keep[name].ctypes.data_as(ctypes.POINTER(ctypes.c_double)))
    for name in ("R", "T0", "T1", "RREF", "A0", "A1"):
        setattr(struct, name, keep[name].ctypes.data_as(ctypes.POINTER(ctypes.c_double)))
    struct.FULL = keep["FULL"].ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte))
    struct.DROP = keep["DROP"].ctypes.data_as(ctypes.POINTER(ctypes.c_double))
    struct.ARC = keep["ARC"].ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte))
    struct.count = keep["count"]
    struct.cones = keep["cones"].ctypes.data_as(ctypes.POINTER(ctypes.c_double))
    struct.cone_count = keep["cones"].shape[0]


def _topology_arrays(topo):
    """Массивы топологии в раскладке ABI: ea/eb/n/d float32, f0/f1 int."""
    return {
        "ea": _np.ascontiguousarray(topo.ea, dtype=_np.float32).reshape(-1),
        "eb": _np.ascontiguousarray(topo.eb, dtype=_np.float32).reshape(-1),
        "f0": _np.ascontiguousarray(topo.f0, dtype=_np.int32).reshape(-1),
        "f1": _np.ascontiguousarray(topo.f1, dtype=_np.int32).reshape(-1),
        "n": _np.ascontiguousarray(topo.n, dtype=_np.float32).reshape(-1),
        "d": _np.ascontiguousarray(topo.d, dtype=_np.float32).reshape(-1),
        "edges": int(_np.asarray(topo.f0).size),
        "faces": int(_np.asarray(topo.n).reshape(-1, 3).shape[0]),
    }


def _fill_topology(struct, keep):
    """Заполнить структуру ABI указателями массивов топологии (без копии)."""
    struct.ea = keep["ea"].ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    struct.eb = keep["eb"].ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    struct.f0 = keep["f0"].ctypes.data_as(ctypes.POINTER(ctypes.c_int))
    struct.f1 = keep["f1"].ctypes.data_as(ctypes.POINTER(ctypes.c_int))
    struct.n = keep["n"].ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    struct.d = keep["d"].ctypes.data_as(ctypes.POINTER(ctypes.c_float))
    struct.edge_count = keep["edges"]
    struct.face_count = keep["faces"]


def _path_data(data) -> PathData:
    """Поля структуры ABI -> PathData (пустой путь - те же None, что у эталона)."""
    if int(data.point_count) == 0:
        return PathData(seg_count=0, last=(0.0, 0.0, 0.0), pts=None, cum=None,
                        rapid=None, speeds=None, total=0.0,
                        rapid_bytes=b"", rapid_nv=0, feed_bytes=b"", feed_nv=0,
                        bounds=None, center=[0.0, 0.0, 0.0], radius=50.0,
                        min_y=0.0, stages=[])
    n = int(data.point_count)
    pts = _take(data.pts, n * 3, _np.float64).reshape(-1, 3)
    cum = _take(data.cum, n, _np.float64)
    # dtype как у эталона (np.fromiter с dtype=bool): с ним сравнивают маски
    rapid = _take(data.rapid, n, _np.bool_)
    speeds = _take(data.speeds, n, _np.float64)
    bounds = None
    if int(data.has_bounds):
        bounds = Aabb((float(data.bounds[0]), float(data.bounds[1]),
                       float(data.bounds[2])),
                      (float(data.bounds[3]), float(data.bounds[4]),
                       float(data.bounds[5])))
    return PathData(
        seg_count=int(data.seg_count),
        last=tuple(float(v) for v in data.last),
        pts=pts, cum=cum, rapid=rapid, speeds=speeds,
        total=float(data.total),
        rapid_bytes=_bytes(data.rapid_stream, data.rapid_bytes),
        rapid_nv=int(data.rapid_nv),
        feed_bytes=_bytes(data.feed_stream, data.feed_bytes),
        feed_nv=int(data.feed_nv),
        bounds=bounds,
        center=[float(v) for v in data.center],
        radius=float(data.radius), min_y=float(data.min_y),
        stages=_path_stages(data))


def _path_stages(data) -> list:
    """Этапы пути: диапазоны, подписи и разобранные теги UI_TOOL."""
    count = int(data.stage_count)
    if count == 0:
        return []
    rows = _take(data.stages, count * _STAGE_ROW, _np.float64).reshape(-1, _STAGE_ROW)
    labels = _stage_text(data.stage_labels, data.stage_label_at, count)
    types = _stage_text(data.stage_types, data.stage_type_at, count)
    out = []
    for i, row in enumerate(rows):
        info = {}
        if row[2]:
            for k, name in enumerate(_TOOL_FIELDS):
                value = row[3 + k]
                info[name] = int(value) if name in _TOOL_INT_FIELDS else float(value)
            info["type"] = types[i]
        out.append({"start": float(row[0]), "end": float(row[1]),
                    "label": labels[i], "tool": info})
    return out


def _stage_text(blob, offsets_ptr, count):
    """Текстовый блок этапов и таблица смещений -> список строк UTF-8."""
    at = _take(offsets_ptr, count + 1, _np.uint64).tolist()
    text = _bytes(blob, at[-1] if at else 0)
    return [text[at[i]:at[i + 1]].decode("utf-8", "replace") for i in range(count)]


def _row(values):
    """Три числа из строки ABI в кортеж (форма точки у дескрипторов поверхностей)."""
    return (float(values[0]), float(values[1]), float(values[2]))


def _requested(part: str) -> bool:
    """Включён ли слой part: флаг просит C++, мост жив, иначе фолбэк на Python."""
    if mode() not in _PART_MODES[part]:
        return False
    if available():
        return True
    _warn_once("bridge:" + part,
               "CNC_CPP=%s просит C++ для слоя %s, но мост не загружен (%s): "
               "работаем на Python" % (mode(), part, _load_error))
    return False


def _ensure():
    """Загруженная библиотека или None. Загрузка одна на процесс."""
    global _lib, _load_error
    with _lock:
        if _lib is not None:
            return _lib
        try:
            _lib = _load()
            _load_error = ""
        except Exception as exc:              # OSError, GvCppError и прочее
            _lib = None
            _load_error = str(exc)
        return _lib


def _require():
    lib = _ensure()
    if lib is None:
        raise GvCppError("мост gvbridge не загружен: %s" % _load_error)
    return lib


def _load():
    """Загрузка gvbridge.dll с типами ABI. Ошибка - GvCppError."""
    path = library_path()
    if not os.path.exists(path):
        raise GvCppError("нет библиотеки %s (соберите scripts/build-viewer.ps1)" % path)
    _add_dll_dir(os.path.dirname(path))
    _add_dll_dir(occt_bin_dir())
    _preload_occt()
    try:
        lib = ctypes.CDLL(path)
    except OSError as exc:
        raise GvCppError("не загрузилась %s: %s" % (path, exc)) from exc
    _declare(lib)
    abi = lib.gv_abi_version()
    if abi != ABI_VERSION:
        raise GvCppError("ABI моста %d, ожидался %d: пересоберите библиотеку"
                         % (abi, ABI_VERSION))
    return lib


def _declare(lib):
    """Прототипы экспортов: без них ctypes молча испортит указатели."""
    dbl_p = ctypes.POINTER(ctypes.c_double)
    flt_p = ctypes.POINTER(ctypes.c_float)
    u8_p = ctypes.POINTER(ctypes.c_ubyte)
    size_p = ctypes.POINTER(ctypes.c_size_t)
    lib.gv_abi_version.restype = ctypes.c_int
    lib.gv_last_error.restype = ctypes.c_char_p
    lib.gv_last_error.argtypes = []
    lib.gv_free.restype = None
    lib.gv_free.argtypes = [ctypes.c_void_p]
    lib.gv_smooth_face_normals.restype = ctypes.c_int
    lib.gv_smooth_face_normals.argtypes = [
        dbl_p, ctypes.c_size_t, ctypes.c_double,
        ctypes.POINTER(flt_p), size_p]
    lib.gv_load_step_model.restype = ctypes.c_int
    lib.gv_load_step_model.argtypes = [
        ctypes.c_char_p, ctypes.c_double,
        ctypes.POINTER(dbl_p), size_p, ctypes.POINTER(flt_p), size_p,
        ctypes.POINTER(dbl_p), size_p]
    lib.gv_load_step_surfaces.restype = ctypes.c_int
    lib.gv_load_step_surfaces.argtypes = [
        ctypes.c_char_p, ctypes.c_double, ctypes.POINTER(_GvSurfaces)]
    lib.gv_load_step_all.restype = ctypes.c_int
    lib.gv_load_step_all.argtypes = [
        ctypes.c_char_p, ctypes.c_double,
        ctypes.POINTER(dbl_p), size_p, ctypes.POINTER(flt_p), size_p,
        ctypes.POINTER(dbl_p), size_p, ctypes.POINTER(_GvSurfaces)]
    lib.gv_free_surfaces.restype = None
    lib.gv_free_surfaces.argtypes = [ctypes.POINTER(_GvSurfaces)]
    lib.gv_build_silhouette_topology.restype = ctypes.c_int
    lib.gv_build_silhouette_topology.argtypes = [
        dbl_p, ctypes.c_size_t, ctypes.POINTER(_GvMeshTopology)]
    lib.gv_free_mesh_topology.restype = None
    lib.gv_free_mesh_topology.argtypes = [ctypes.POINTER(_GvMeshTopology)]
    lib.gv_mesh_silhouette_segments.restype = ctypes.c_int
    lib.gv_mesh_silhouette_segments.argtypes = [
        ctypes.POINTER(_GvMeshTopology), dbl_p, ctypes.POINTER(flt_p), size_p]
    lib.gv_tangent_families.restype = ctypes.c_int
    lib.gv_tangent_families.argtypes = [
        ctypes.POINTER(_GvSurfaces), u8_p, ctypes.c_size_t, ctypes.c_int,
        dbl_p, ctypes.c_size_t, flt_p, ctypes.c_size_t,
        ctypes.POINTER(_GvTangentFamilies)]
    lib.gv_tangent_segments.restype = ctypes.c_int
    lib.gv_tangent_segments.argtypes = [
        ctypes.POINTER(_GvTangentFamilies), dbl_p, ctypes.POINTER(dbl_p), size_p]
    lib.gv_free_tangent_families.restype = None
    lib.gv_free_tangent_families.argtypes = [ctypes.POINTER(_GvTangentFamilies)]
    lib.gv_prepare_path.restype = ctypes.c_int
    lib.gv_prepare_path.argtypes = [ctypes.c_char_p, ctypes.POINTER(_GvPathData)]
    lib.gv_free_path_data.restype = None
    lib.gv_free_path_data.argtypes = [ctypes.POINTER(_GvPathData)]


def _add_dll_dir(directory: str):
    """Каталог DLL в пути поиска. Ручка хранится: закрытая выкидывает каталог."""
    if not directory or not hasattr(os, "add_dll_directory") or not os.path.isdir(directory):
        return
    try:
        _dll_dirs.append(os.add_dll_directory(directory))
    except OSError as exc:
        logger.warning("каталог DLL %s не подключён: %s", directory, exc)


def _preload_occt():
    """Подгрузить DLL OCCT по полному пути до gvbridge.dll.

    Иначе загрузчик Windows отказывает gvbridge.dll кодом 1114 (инициализация
    зависимости): библиотеки OCCT лежат вне каталога исполняемого файла, и
    разрешение зависимостей по одному каталогу поиска проходит неполно. С уже
    загруженным TKernel.dll та же загрузка проходит - проверено на этой сборке.
    """
    occ = occt_bin_dir()
    if not os.path.isdir(occ):
        return
    for name in ("TKernel.dll", "TKMath.dll", "TKG2d.dll", "TKG3d.dll",
                 "TKGeomBase.dll", "TKBRep.dll", "TKTopAlgo.dll", "TKShHealing.dll",
                 "TKXSBase.dll", "TKLCAF.dll", "TKXCAF.dll", "TKDE.dll",
                 "TKMesh.dll", "TKDESTEP.dll"):
        full = os.path.join(occ, name)
        if not os.path.exists(full):
            continue
        try:
            ctypes.WinDLL(full)
        except OSError as exc:                 # не наш случай: мост просто не поднимется
            logger.warning("DLL OCCT %s не подгрузилась: %s", name, exc)


def _last_error(lib) -> str:
    raw = lib.gv_last_error()
    if not raw:
        return "без текста"
    return raw.decode("utf-8", "replace")


def _take(ptr, count: int, dtype):
    """Копия буфера библиотеки в numpy: буфер освобождает вызывающий."""
    if not ptr or count == 0:
        return _np.empty(0, dtype=dtype)
    view = _np.ctypeslib.as_array(ptr, shape=(int(count),))
    return _np.array(view, dtype=dtype, copy=True)


def _bytes(ptr, count: int) -> bytes:
    """Копия байтового буфера библиотеки: строка живёт до следующего вызова."""
    if not ptr or count == 0:
        return b""
    return ctypes.string_at(ptr, int(count))


def _warn_once(key: str, text: str):
    """Предупреждение о фолбэке один раз за прогон: строка на кадр недопустима."""
    with _lock:
        if key in _warned:
            return
        _warned.add(key)
    logger.warning("%s", text)
