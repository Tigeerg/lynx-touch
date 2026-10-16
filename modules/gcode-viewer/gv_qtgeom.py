"""C++-геометрии Qt Quick 3D в живом вьювере (этап 5.3а плана переноса).

Что здесь и зачем. Геометрии этапа 3 на C++ (`cpp/qt/gv_geometry.*`) собирают
те же буферы, что питоновский эталон `geometry.py`, но подключить их к вьюверу
можно было только вместе с регистрацией типов в QML - а её менять нельзя:
`GcodeViewer3D.qml` импортирует `MockBackend` 1.0 и по именам берёт
`GcodeToolpath`, `GridGeometry` и `AxisArrowGeometry`, остальные геометрии
приходят свойствами бэкенда. Поэтому регистрация остаётся за Python (её делает
`@QmlElement` в `geometry.py` и `controller.py`), а C++ отдаёт ровно одно -
готовый буфер.

Как это работает. Классы ниже - тонкие наследники эталонных классов: перекрыт
только вход сборки (`set_packed`, `set_segments`, `set_triangles`, `set_profile`).
Буфер считает `gvgeom` в шиме `gvqtshim.dll` (она же библиотека эксперимента
этапа 5.3: Qt-объекты, собранные против отдельной установки Qt, живут в процессе
PySide6), оболочка забирает байты и габарит и вызывает унаследованный `_rebuild`
эталона, то есть порядок `clear -> setVertexData -> setStride -> addAttribute ->
setBounds -> update` остаётся буквально тем же кодом. Контракт QML и шейдеров
(stride 20 у траектории, 48 у ленты контура) не дублируется.

Чего здесь нет: `GridGeometry` и `AxisArrowGeometry`. Обе создаёт сам QML по
именам из `MockBackend`, их буфер процедурный и малый (PORT-CPP.md, раздел 5:
выигрыш около нуля), а `geometry.py` - замороженный эталон и менять его нельзя.
Их C++-двойники сверяет проверка `test_qt_bridge.py` напрямую через шим.

Отказ C++ виден в журнале: шим не нашёлся, не загрузился или вернул ошибку -
`logger.warning` и питоновский путь эталона. Молчаливого отказа нет.

Путь к библиотеке задаёт CNC_CPP_QT_LIB, иначе берётся <repo>/build/cpp/
gvqtshim.dll (туда её кладёт цель gvqtshim, см. cpp/CMakeLists.txt).

Запуск проверки: python modules/gcode-viewer/test_qt_bridge.py
"""
import ctypes
import logging
import os
import threading

from geometry import (HLRLineGeometry as _PyHLR,
                      MeshGeometry as _PyMesh,
                      ToolGeometry as _PyTool,
                      ToolpathGeometry as _PyToolpath)
from gv_common import _np
import gv_cpp

logger = logging.getLogger(__name__)

LIB_ENV = "CNC_CPP_QT_LIB"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_LIB = os.path.join(ROOT, "build", "cpp", "gvqtshim.dll")
# Каталог QML-модуля CNC/Edges: его точка входа ищет зависимости по PATH.
MODULE_DIR = os.path.join(ROOT, "build", "edges", "CNC", "Edges")

_lock = threading.Lock()
_lib = None                # загруженный шим
_load_error = ""           # почему не загрузился (для сообщения один раз)
_warned = False            # об отказе сообщаем один раз, а не построчно
_call_warned = set()       # то же про отказы отдельных вызовов сборки


class GvQtGeomError(RuntimeError):
    """Отказ шима: библиотеки нет, экспорт не тот или код возврата не 0."""


def library_path() -> str:
    """Путь к gvqtshim.dll: CNC_CPP_QT_LIB, иначе каталог сборки."""
    return os.environ.get(LIB_ENV) or DEFAULT_LIB


def load_error() -> str:
    """Почему шима нет (пусто, если он есть)."""
    _library()
    return _load_error


def enabled() -> bool:
    """Буферы геометрий собирает C++ (CNC_CPP=qtgeom|all и живой шим).

    false - контроллер оставляет питоновские геометрии: поведение вьювера как
    до переключения.
    """
    return gv_cpp.qtgeom_enabled() and _library() is not None


def prepare_dll_path() -> str:
    """Каталог QML-модуля CNC/Edges и DLL OCCT в PATH до создания движка.

    Точка входа Qt-плагина ищет свои зависимости по PATH, а не через
    os.add_dll_directory (разбор - test_qt_bridge.py, cpp/exp/exp_qml_diag.py).
    Каталогов сборки нет - PATH не трогаем: на машине без тулчейна поведение
    прежнее. Возвращает добавленные каталоги (пустая строка - добавлять нечего).
    """
    dirs = [d for d in (MODULE_DIR, gv_cpp.occt_bin_dir()) if d and os.path.isdir(d)]
    if not dirs:
        return ""
    current = os.environ.get("PATH", "")
    have = [p.lower() for p in current.split(os.pathsep) if p]
    add = [d for d in dirs if d.lower() not in have]
    if add:
        os.environ["PATH"] = os.pathsep.join(add + ([current] if current else []))
    return os.pathsep.join(dirs)


def _library():
    """Загруженный шим или None. Загрузка одна на процесс, отказ - в журнале."""
    global _lib, _load_error, _warned
    with _lock:
        if _lib is not None:
            return _lib
        try:
            _lib = _load()
            _load_error = ""
        except Exception as exc:                  # OSError, GvQtGeomError и прочее
            _lib = None
            _load_error = str(exc)
            if not _warned:
                _warned = True
                logger.warning("C++-геометрии Qt недоступны (%s): считаем на Python", exc)
        return _lib


def _load():
    """Загрузка шима с типами C-ABI. Ошибка - GvQtGeomError."""
    path = library_path()
    if not os.path.exists(path):
        raise GvQtGeomError("нет библиотеки %s (соберите scripts/build-viewer.ps1)" % path)
    try:
        lib = ctypes.WinDLL(path) if os.name == "nt" else ctypes.CDLL(path)
    except OSError as exc:
        raise GvQtGeomError("не загрузилась %s: %s" % (path, exc)) from exc
    _declare(lib)
    return lib


def _declare(lib):
    """Прототипы экспортов: без них ctypes молча испортит указатели."""
    dbl_p = ctypes.POINTER(ctypes.c_double)
    flt_p = ctypes.POINTER(ctypes.c_float)
    u8_p = ctypes.POINTER(ctypes.c_ubyte)
    u8_pp = ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte))
    lib.gvq_build_toolpath.restype = ctypes.c_void_p
    lib.gvq_build_toolpath.argtypes = [u8_p, ctypes.c_int, dbl_p, ctypes.c_int, ctypes.c_int]
    lib.gvq_build_hlr.restype = ctypes.c_void_p
    lib.gvq_build_hlr.argtypes = [flt_p, ctypes.c_int, dbl_p]
    lib.gvq_build_mesh.restype = ctypes.c_void_p
    lib.gvq_build_mesh.argtypes = [dbl_p, ctypes.c_int, dbl_p]
    lib.gvq_build_tool.restype = ctypes.c_void_p
    lib.gvq_build_tool.argtypes = [dbl_p, ctypes.c_int, ctypes.c_int]
    lib.gvq_vertex_copy.restype = ctypes.c_int
    lib.gvq_vertex_copy.argtypes = [ctypes.c_void_p, u8_pp, ctypes.POINTER(ctypes.c_int)]
    lib.gvq_buf_free.restype = None
    lib.gvq_buf_free.argtypes = [u8_p]
    lib.gvq_bounds_unpadded.restype = ctypes.c_int
    lib.gvq_bounds_unpadded.argtypes = [ctypes.c_void_p, dbl_p]
    lib.gvq_free_object.restype = None
    lib.gvq_free_object.argtypes = [ctypes.c_void_p]


def _vertex_copy(lib, ptr) -> bytes:
    """Байты буфера вершин C++-геометрии (копия: объект уже освобождён)."""
    out = ctypes.POINTER(ctypes.c_ubyte)()
    size = ctypes.c_int(0)
    code = lib.gvq_vertex_copy(ptr, ctypes.byref(out), ctypes.byref(size))
    if code < 0:
        raise GvQtGeomError("буфер вершин не скопирован: код %d" % code)
    try:
        return ctypes.string_at(out, size.value) if size.value else b""
    finally:
        if out:
            lib.gvq_buf_free(out)


def _bounds_or_none(lib, ptr):
    """Габарит без покрытия как шесть чисел или None, если его нет."""
    out = (ctypes.c_double * 6)()
    if lib.gvq_bounds_unpadded(ptr, out) != 1:
        return None
    return tuple(float(v) for v in out)


def _bounds6(bounds):
    """Габарит Aabb -> ctypes-массив из шести double (None - габарита нет)."""
    if bounds is None:
        return None
    return (ctypes.c_double * 6)(bounds.lo[0], bounds.lo[1], bounds.lo[2],
                                 bounds.hi[0], bounds.hi[1], bounds.hi[2])


def _u8_pointer(raw):
    """Указатель на байты буфера: у ctypes нет bytes для POINTER(c_ubyte)."""
    if not raw:
        return None
    return _np.frombuffer(raw, dtype=_np.uint8).ctypes.data_as(
        ctypes.POINTER(ctypes.c_ubyte))


def _segment_array(segs):
    """(указатель на float32-звенья, число звеньев) - форма как у эталона."""
    if segs is None or len(segs) == 0:
        return None, 0
    arr = _np.ascontiguousarray(_np.asarray(segs, dtype=_np.float32))
    if arr.ndim == 2:
        arr = arr.reshape(-1, 2, 3)
    return arr.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), int(arr.shape[0])


def _triangle_array(verts):
    """Плоский float64-массив вершин или None, если форму объяснит эталон.

    Эталон вяжет вход через reshape(-1, 3, 3), то есть требует кратности девяти:
    неполный вход остаётся питоновской ветке, чтобы ошибка формы была его же.
    """
    arr = _np.ascontiguousarray(_np.asarray(verts, dtype=_np.float64)).reshape(-1)
    if arr.size % 9:
        return None
    return arr


def _normals_pointer(normals, size):
    """Указатель на float64-нормали или None (тогда нормали считает геометрия)."""
    if normals is None:
        return None
    arr = _np.ascontiguousarray(_np.asarray(normals, dtype=_np.float64)).reshape(-1)
    if arr.size != size:
        return None
    return arr.ctypes.data_as(ctypes.POINTER(ctypes.c_double))


def _profile_array(profile):
    """Профиль инструмента -> ctypes-массив пар (radius_mm, height_mm) float64."""
    pairs = [(max(0.0, float(p.radius_mm)), float(p.height_mm)) for p in profile]
    out = (ctypes.c_double * (2 * len(pairs)))()
    for i, (radius, height) in enumerate(pairs):
        out[2 * i] = radius
        out[2 * i + 1] = height
    return out, len(pairs)


def _warn_call(part, exc):
    """Отказ сборки одного класса: предупреждение один раз и питоновский путь."""
    if part in _call_warned:
        return
    _call_warned.add(part)
    logger.warning("C++-геометрия (%s) не собралась (%s): считаем на Python", part, exc)


def _cpp_or_python(lib, part, cpp, py):
    """Собрать буфер в gvgeom; отказ C++ - предупреждение в журнал и путь эталона.

    Молчаливого отказа нет: и отсутствие шима (lib is None), и ошибка сборки
    видны в журнале, а буфер в обоих случаях собирает питоновский эталон.
    """
    if lib is None:
        return py()
    try:
        return cpp(lib)
    except GvQtGeomError as exc:
        _warn_call(part, exc)
        return py()


class ToolpathGeometry(_PyToolpath):
    """Линии траектории (stride 20): буфер собирает gvgeom, ставит _rebuild эталона."""

    def set_packed(self, raw, bounds, nv):
        def py():
            return _PyToolpath.set_packed(self, raw, bounds, nv)

        if not raw:
            return py()
        return _cpp_or_python(_library(), "траектория",
                              lambda lib: self._cpp_packed(lib, raw, bounds, nv), py)

    def _cpp_packed(self, lib, raw, bounds, nv):
        ptr = lib.gvq_build_toolpath(_u8_pointer(raw), len(raw), _bounds6(bounds),
                                     int(bounds is not None), int(nv))
        try:
            self._raw = _vertex_copy(lib, ptr)
            self._nv = int(nv)
        finally:
            lib.gvq_free_object(ptr)
        self._bounds = bounds
        self._rebuild()


class HLRLineGeometry(_PyHLR):
    """Лента контура (stride 48): отжим FLOAT_SCALE и упаковка рёбер - в gvgeom."""

    def set_segments(self, segs):
        def py():
            return _PyHLR.set_segments(self, segs)

        if segs is None or len(segs) == 0:
            return py()
        return _cpp_or_python(_library(), "лента контура",
                              lambda lib: self._cpp_segments(lib, segs), py)

    def _cpp_segments(self, lib, segs):
        points, count = _segment_array(segs)
        ref = None if self._ref is None else \
            (ctypes.c_double * 3)(*[float(v) for v in self._ref])
        ptr = lib.gvq_build_hlr(points, count, ref)
        try:
            self._raw = _vertex_copy(lib, ptr)
            self._nv = len(self._raw) // self.STRIDE
            box = _bounds_or_none(lib, ptr)
        finally:
            lib.gvq_free_object(ptr)
        self._bounds = box
        self._rebuild()


class MeshGeometry(_PyMesh):
    """Сетка (stride 24): плоские нормали и укладку позиций считает gvgeom."""

    def set_triangles(self, verts, normals=None):
        def py():
            return _PyMesh.set_triangles(self, verts, normals)

        if not (isinstance(verts, _np.ndarray) and verts.size):
            return py()
        return _cpp_or_python(_library(), "меш",
                              lambda lib: self._cpp_triangles(lib, verts, normals), py)

    def _cpp_triangles(self, lib, verts, normals):
        flat = _triangle_array(verts)
        if flat is None:
            return _PyMesh.set_triangles(self, verts, normals)
        nrm = _normals_pointer(normals, flat.size)
        if normals is not None and nrm is None:
            return _PyMesh.set_triangles(self, verts, normals)
        ptr = lib.gvq_build_mesh(flat.ctypes.data_as(ctypes.POINTER(ctypes.c_double)),
                                 int(flat.size), nrm)
        try:
            self._data = _vertex_copy(lib, ptr)
            self._nv = len(self._data) // self.STRIDE
            box = _bounds_or_none(lib, ptr)
        finally:
            lib.gvq_free_object(ptr)
        self._lo = [box[0], box[1], box[2]]
        self._hi = [box[3], box[4], box[5]]
        self._rebuild()


class ToolGeometry(_PyTool):
    """Тело вращения по профилю (stride 24): циклы профиля считает gvgeom."""

    def set_profile(self, profile, segments=None):
        def py():
            return _PyTool.set_profile(self, profile, segments)

        if not profile:
            return py()
        return _cpp_or_python(_library(), "инструмент",
                              lambda lib: self._cpp_profile(lib, profile, segments), py)

    def _cpp_profile(self, lib, profile, segments):
        values, count = _profile_array(profile)
        ptr = lib.gvq_build_tool(values, count, int(segments) if segments else 0)
        try:
            self._data = _vertex_copy(lib, ptr)
            self._nv = len(self._data) // self.STRIDE
            box = _bounds_or_none(lib, ptr)
        finally:
            lib.gvq_free_object(ptr)
        self._lo = [box[0], box[1], box[2]]
        self._hi = [box[3], box[4], box[5]]
        self._rebuild()
