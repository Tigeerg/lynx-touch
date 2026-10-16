"""Общий низ бэкенда вьювера: numpy, замок OCCT, преобразование координат и URI
QML-модуля.

Вынесено из mock_backend.py (этап 0 плана переноса на C++): модуль не зависит от
остальных и импортируется снизу вверх. numpy пока опционален - его обязательность
отдельный шаг этапа 0.
"""
import threading

# URI и версия QML-модуля: одни на все @QmlElement-классы вьювера. Декоратор берёт
# эти имена из модуля, где объявлен класс, поэтому модули импортируют их отсюда -
# источник один.
QML_IMPORT_NAME = "MockBackend"
QML_IMPORT_MAJOR_VERSION = 1
QML_IMPORT_MINOR_VERSION = 0

# OCCT (OCP) отпускает GIL внутри тяжёлых вызовов (BRepMesh, HLRBRep), поэтому два
# потока на одном TopoDS_Shape портят кучу (access violation, всплывающий в
# случайном месте: numpy.mean, рендер, импорт). Все OCCT-секции сериализуются этим
# замком: загрузка модели и HLR-прогон не пересекаются никогда.
_occt = threading.RLock()

try:
    import numpy as _np
except Exception:                      # numpy is optional: fall back to pure Python
    _np = None
    import sys as _sys
    print("[gv_common] WARNING: numpy not found - pure-Python fallback is much "
          "slower (edges/geometry). Install numpy on the CNC controller.",
          file=_sys.stderr)


def _to_3d(p):
    """Machine (X, Y, Z) -> scene (X, Z, -Y); Y is up."""
    return (p[0], p[2], -p[1])
