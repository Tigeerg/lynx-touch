"""Общий низ бэкенда вьювера: numpy и замок OCCT.

Вынесено из mock_backend.py (этап 0 плана переноса на C++): модуль не зависит от
остальных и импортируется снизу вверх. numpy пока опционален - его обязательность
отдельный шаг этапа 0.
"""
import threading

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
