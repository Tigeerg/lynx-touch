"""Шим к C-ABI-мосту gvbridge: C++-слои вьювера из Python через ctypes.

Этап 5 плана переноса (PORT-CPP.md). Мост собран из модулей gvcore (нормали кожи)
и gvstep (STEP: суп, рёбра, нормали) и наружу отдаёт только C-типы: буферы через
out-параметры, память библиотеки освобождает её же gv_free. Ни один C++-тип через
границу не проходит.

Умолчание - моста нет: пустая переменная CNC_CPP значит питоновский путь, как
раньше. Значения:

  CNC_CPP=normals  - нормали STL (controller._stl_normals) считает C++;
  CNC_CPP=step     - ветку STEP (_load_model_verts) считает C++ (суп, нормали,
                     рёбра до склейки - склейку и упаковку ведёт Python, чтобы
                     буфер обводки остался бит-в-бит таким же);
  CNC_CPP=all      - оба слоя;
  пусто/off/0      - всё на Python (умолчание).

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

logger = logging.getLogger(__name__)

MODE_ENV = "CNC_CPP"
LIB_ENV = "CNC_CPP_LIB"

# Версия ABI, которую понимает этот шим (gv_bridge.h: GV_ABI_VERSION).
ABI_VERSION = 1

# Слой, который включает значение флага: ключ -> нужные значения CNC_CPP.
_PART_MODES = {"normals": ("normals", "all"), "step": ("step", "all")}
_OFF_VALUES = ("", "off", "0", "no", "false")

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
    """Значение CNC_CPP: '', 'normals', 'step' или 'all'. Незнакомое - отказ."""
    raw = os.environ.get(MODE_ENV, "").strip().lower()
    if raw in _OFF_VALUES:
        return ""
    if raw not in ("normals", "step", "all"):
        _warn_once("unknown",
                   "CNC_CPP=%r не знаком: ожидалось normals, step или all; "
                   "работаем на Python" % raw)
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
    lib.gv_abi_version.restype = ctypes.c_int
    lib.gv_last_error.restype = ctypes.c_char_p
    lib.gv_last_error.argtypes = []
    lib.gv_free.restype = None
    lib.gv_free.argtypes = [ctypes.c_void_p]
    lib.gv_smooth_face_normals.restype = ctypes.c_int
    lib.gv_smooth_face_normals.argtypes = [
        ctypes.POINTER(ctypes.c_double), ctypes.c_size_t, ctypes.c_double,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_float)), ctypes.POINTER(ctypes.c_size_t)]
    lib.gv_load_step_model.restype = ctypes.c_int
    lib.gv_load_step_model.argtypes = [
        ctypes.c_char_p, ctypes.c_double,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_double)), ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(ctypes.POINTER(ctypes.c_float)), ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(ctypes.POINTER(ctypes.c_double)), ctypes.POINTER(ctypes.c_size_t)]


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


def _warn_once(key: str, text: str):
    """Предупреждение о фолбэке один раз за прогон: строка на кадр недопустима."""
    with _lock:
        if key in _warned:
            return
        _warned.add(key)
    logger.warning("%s", text)
