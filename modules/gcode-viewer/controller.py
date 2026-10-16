"""Контроллер вьювера: свойства, сигналы и слоты для QML (тип MockBackend).

Вынесено из mock_backend.py (этап 0 плана переноса на C++). Слой верхний: он берёт
имена у всех нижних модулей, обратных зависимостей нет. Модуль регистрирует QML-тип
"MockBackend" 1.0 - QML_IMPORT_* обязан лежать в том же модуле, что и класс.
"""
import logging
import math
import os
import threading
from bisect import bisect_right as _bisect_right

from PySide6.QtCore import Property, QObject, QTimer, Signal, Slot
from PySide6.QtGui import QQuaternion, QVector3D
from PySide6.QtQml import QmlElement, QQmlEngine

from gv_common import (_np, _occt, _to_3d, QML_IMPORT_NAME,
                       QML_IMPORT_MAJOR_VERSION, QML_IMPORT_MINOR_VERSION)
from gv_types import PackedEdges
from gcode_parser import parse_gcode
from ui_tags import (parse_ui_rotation, parse_ui_origin, parse_ui_tool,
                     parse_ui_holder, parse_ui_stock, parse_ui_cad_matrix,
                     parse_ui_cad_matrix_origin, quat_from_cols)
from cache_store import (_cache_enabled, _cache_key, _cache_read_path,
                         _cache_write_path, _cache_read_edges, _cache_write_edges,
                         _cache_read_model, _cache_write_model)
from stl_import import load_stl_vertices, _verts_bounds
from packing import packed_positions, _pack_edge_segments, _coalesce_segments
from step_import import _step_shape, _orient_shell_outward, load_step_model
from step_surfaces import load_step_surfaces
from tangents import _tangent_families, _tangent_from_families
from silhouette import _build_silhouette_topology, _mesh_silhouette_segments
from normals import _smooth_face_normals
import gv_cpp
from outline import _feature_edges
from path_prep import _prepare_ordered
from tooling import (_tool_profile, _tool_outline_profile, _tool_exposed_length,
                     _holder_solid_profile, _holder_outline_profile, _rings_segments,
                     _profile_ring_points)
from collision import _scan_collisions, _tool_check_report, _COLLISION_STEP
from geometry import (ToolpathGeometry, HLRLineGeometry, GridGeometry,
                      AxisArrowGeometry, MeshGeometry, ToolGeometry)
import gv_qtgeom

# Геометрии, которыми владеет бэкенд (траектория, меш, инструмент, лента
# контура), при CNC_CPP=qtgeom|all собирает C++ (gvgeom через шим gvqtshim):
# подмена класса ниже - единственное место выбора пути, API у оболочки тот же,
# поэтому вызовы в контроллере не меняются. Регистрация QML-типов остаётся за
# Python (geometry.py и GcodeToolpath регистрируются как раньше), контракт -
# URI MockBackend 1.0 и имена типов - не меняется. Сетка и указатель оси
# остаются питоновскими: их создаёт сам QML по именам из MockBackend.
if gv_qtgeom.enabled():
    ToolpathGeometry = gv_qtgeom.ToolpathGeometry
    HLRLineGeometry = gv_qtgeom.HLRLineGeometry
    MeshGeometry = gv_qtgeom.MeshGeometry
    ToolGeometry = gv_qtgeom.ToolGeometry

np = _np          # код слоя читает numpy как np

logger = logging.getLogger(__name__)

# Ключи фолбэков, о которых уже сообщили: см. _warn_fallback.
_fallback_warned = set()


def _step_model(path, shape):
    """Модель STEP: суп вершин, рёбра и crease-aware нормали.

    Считает C++, если CNC_CPP=step|all (мост gvbridge, этап 5 плана переноса);
    отказ C++-пути виден в журнале и уходит на питоновский эталон. Рёбра мост
    отдаёт до склейки: склейку ведёт _coalesce_segments, поэтому буфер обводки
    остаётся тем же, что у питоновского пути."""
    if gv_cpp.step_enabled():
        try:
            verts, segments, normals = gv_cpp.load_step_model(path)
            return verts, _coalesce_segments(segments), normals
        except Exception as exc:
            logger.warning("C++-STEP не прочитан (%s): читает Python", exc)
    return load_step_model(path, shape=shape)


def _step_surfaces(path, shape=None):
    """Аналитические поверхности STEP: цилиндры и конусы.

    Считает C++, если CNC_CPP=surfaces|all (мост gvbridge, этап 5 плана переноса);
    отказ C++-пути виден в журнале и уходит на питоновский эталон. Форму для
    питоновского пути даёт _step_shape: вызывающий может передать уже прочитанную,
    тогда STEP читается один раз на модель, иначе она читается здесь."""
    if gv_cpp.surfaces_enabled():
        try:
            return gv_cpp.load_step_surfaces(path)
        except Exception as exc:
            logger.warning("C++-поверхности STEP не посчитаны (%s): считает Python", exc)
    if shape is None:
        try:
            shape = _step_shape(path)
        except Exception:
            shape = None
    return load_step_surfaces(path, shape=shape)


def _read_step_model(path):
    """Модель STEP и поверхности: суп, рёбра до склейки, нормали, поверхности.

    При CNC_CPP=step и surfaces вместе файл читается один раз (мост отдаёт суп и
    поверхности за один проход, gv_load_step_all): у питоновского эталона форму
    читает _step_shape, а второй проход был бы ещё около 0.8 с на модель. Порознь
    слои читают STEP каждый своим проходом - как эталон и как было до переноса
    поверхностей."""
    if gv_cpp.step_enabled() and gv_cpp.surfaces_enabled():
        try:
            verts, segments, normals, surfaces = gv_cpp.load_step_all(path)
            return verts, _coalesce_segments(segments), normals, surfaces
        except Exception as exc:
            logger.warning("C++-STEP не прочитан (%s): читает Python", exc)
    try:
        shape = _step_shape(path)
    except Exception:
        shape = None
    verts, segments, normals = _step_model(path, shape)
    return verts, segments, normals, _step_surfaces(path, shape)


def _warn_fallback(key, text):
    """Предупреждение о фолбэке один раз за прогон.

    refreshTangents вызывается на каждое движение камеры (десятки раз в секунду):
    строка на кадр превратила бы журнал в мусор, а причина отказа одна и та же."""
    if key in _fallback_warned:
        return
    _fallback_warned.add(key)
    logger.warning("%s", text)


# ==========================================================================
#  Controller (parser + two geometries + tool position)
# ==========================================================================
@QmlElement
class GcodeToolpath(QObject):
    toolChanged = Signal()
    ready = Signal()
    pathChanged = Signal()
    modelPathChanged = Signal()
    meshChanged = Signal()

    partTransformChanged = Signal()
    stockChanged = Signal()
    edgesReady = Signal(object)
    toolpathLoaded = Signal(int)      # (segment_count) tool path is on screen
    toolpathReady = Signal(object)    # worker -> main: (token, text, prepared)
    meshReady = Signal(object)        # worker -> main: (token, modelPath, verts)
    loadFinished = Signal()           # whole load done (mesh applied)
    loadingChanged = Signal()
    toolInfoChanged = Signal()        # UI_TOOL metadata / cutter shape changed
    collisionsChanged = Signal()      # holder-vs-stock collision scan updated
    toolCheckChanged = Signal()       # NC-vs-machine tool check updated

    def __init__(self, parent=None):
        super().__init__(parent)
        self._loading = False
        self._load_token = 0                # invalidates stale worker results
        # QQuick3DGeometry parent must be a QQuick3DObject, not a plain QObject,
        # so create the geometries unparented and keep Python references.
        self._rapid = ToolpathGeometry()
        self._feed = ToolpathGeometry()
        self._mesh = MeshGeometry()
        self._edges = ToolpathGeometry()
        self._step_edges = None                      # STEP outline, from the model read
        self._model_normals = None                   # STEP crease-smooth normals (F,3,3)
        self._rev_surfaces = None                    # analytic cylinders/cones from STEP
        self._tan_fams = None                        # precomputed tangent families
        self._mesh_topo = None                       # welded mesh topology (mesh silhouette)
        self._hlr_edges = HLRLineGeometry()          # GPU-tested B-rep outline
        self._hlr_tangents = HLRLineGeometry()       # GPU-tested silhouette
        self._eye_raw = QVector3D(0, 0, 0)           # eye in raw model frame
        self._tool_geo = ToolGeometry()
        self._tool_outline_geo = ToolGeometry()    # inverted-hull outline solid
        self._holder_geo = ToolGeometry()          # ER20 nut + spindle body
        self._holder_outline_geo = ToolGeometry()  # silhouette (inverted hull)
        self._tool_rings_geo = ToolpathGeometry()  # horizontal edge rings
        self._holder_rings_geo = ToolpathGeometry()
        QQmlEngine.setObjectOwnership(self._rapid, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._feed, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._mesh, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._edges, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._hlr_edges, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._hlr_tangents,
                                      QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._tool_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._tool_outline_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._holder_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._holder_outline_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._tool_rings_geo, QQmlEngine.CppOwnership)
        QQmlEngine.setObjectOwnership(self._holder_rings_geo, QQmlEngine.CppOwnership)
        hprof = _holder_solid_profile()            # static holder geometry
        self._holder_geo.set_profile(hprof)
        self._holder_outline_geo.set_profile(
            _tool_outline_profile(_holder_outline_profile(), 0.1))
        rings = _pack_edge_segments(_rings_segments(_profile_ring_points(hprof)))
        self._holder_rings_geo.set_packed(rings.raw, rings.bounds, rings.nv)
        self._tool = [0.0, 0.0, 0.0]
        self._tool_info = {}                # parsed UI_TOOL metadata
        self._tool_length = 0.0             # cutter length (tip -> top, scene units)
        self._tool_radius = 0.0             # cutter max radius (for the outline width)
        self._holder_radius = max((p.radius_mm for p in hprof), default=1.0)
        self._center = [0.0, 0.0, 0.0]      # scene-space center of the toolpath
        self._radius = 50.0                 # half diagonal of the toolpath bounds
        self._min_y = 0.0                   # lowest point of the toolpath (scene Y)
        self._sample_pts = None            # scene-space polyline (numpy or list)
        self._sample_cum = None            # cumulative distance per point
        self._sample_rapid = None          # per-point rapid flag
        self._sample_speeds = None         # per-point segment speed (mm/s)
        self._stages = []                  # operation stages [{start,end,label,tool}]
        self._stage_tools = []             # per-stage UI_TOOL info dicts
        self._move_length = 0.0             # total path length
        self._stock_bounds = None           # (xmin,xmax,ymin,ymax,zmin,zmax) machine
        self._holder_info = {}              # UI_HOLDER: {gauge, name} (optional)
        self._collisions = []               # holder-vs-stock collisions
        self._collision_token = 0           # invalidates stale collision scans
        self._machine_tools = {}            # controller tool table (skeleton)
        self._machine_limits = {}           # axis limits (skeleton)
        self._tool_check = []               # NC-vs-machine tool check report
        self._model_path = ""               # auto-detected STL/OBJ model (same name)
        self._rot = [0.0, 0.0, 0.0]         # CAD -> UP rotation (deg) from UI_ROTATION:
        self._model_pos = [0.0, 0.0, 0.0]   # scene-space offset from UI_ORIGIN:
        self._cad_matrix = None             # UI_CAD_MATRIX (9 numbers) if present
        self._cad_matrix_origin = None      # UI_CAD_MATRIX_ORIGIN (x, y, z) if present
        self._part_rot = QQuaternion(1.0, 0.0, 0.0, 0.0)   # final scene rotation
        self._part_pos = [0.0, 0.0, 0.0]                    # final scene position
        self._mesh_center = [0.0, 0.0, 0.0]                 # raw model AABB center
        self._mesh_lo = [0.0, 0.0, 0.0]
        self._mesh_hi = [0.0, 0.0, 0.0]
        self._has_mesh = False
        self._part_auto_center = True                       # rigid registration (HSM plane has no translation)
        self._stock_center = [0.0, 0.0, 0.0]                # stock (billet) center, scene coords
        self._stock_size = [0.0, 0.0, 0.0]                  # stock size, scene axes
        self._stock_lower_m = [0.0, 0.0, 0.0]               # stock min corner, machine coords
        self._has_stock = False
        self._edge_token = 0                                # guards async edge results
        # Feature-edge outline is built on a worker thread to open the view fast.
        self.edgesReady.connect(self._apply_edges)
        # Async load: worker prepares data, the GUI thread applies it.
        self.toolpathReady.connect(self._on_toolpath_ready)
        self.meshReady.connect(self._on_mesh_ready)
        self._part_offset = [                                # manual nudge (machine axes X, Y, Z)
            float(os.environ.get("CNC_OFS_MX", "0")),
            float(os.environ.get("CNC_OFS_MY", "0")),
            float(os.environ.get("CNC_OFS_MZ", "0")),
        ]
        # Deferred: let QML finish binding, then notify so bindings re-resolve.
        QTimer.singleShot(0, self.ready.emit)

    @Property(str, notify=modelPathChanged)
    def modelPath(self):
        return self._model_path

    # ---- CAD/model placement (driven by the UI_* tags) -------------------
    def _set_rot(self, x, y, z):
        self._rot = [x, y, z]

    def _set_model_pos(self, x, y, z):
        self._model_pos = [x, y, z]

    @Property(QQuaternion, notify=partTransformChanged)
    def partRotation(self):
        return self._part_rot

    @Property(QVector3D, notify=partTransformChanged)
    def partPosition(self):
        return QVector3D(self._part_pos[0], self._part_pos[1], self._part_pos[2])

    # ---- tool (from UI_TOOL) ---------------------------------------------
    def _apply_tool_info(self, info):
        """Store UI_TOOL metadata and rebuild the cutter mesh (tip at origin)."""
        self._tool_info = info or {}
        prof = _tool_profile(self._tool_info) if self._tool_info else None
        self._tool_length = max((p.height_mm for p in prof), default=0.0) if prof else 0.0
        self._tool_geo.set_profile(prof)
        # Silhouette outline (inverted hull): the profile dilated outward by a
        # uniform `pad`, rendered by the QML side with the near faces culled so
        # only the outer silhouette rim remains.
        maxr = max((p.radius_mm for p in prof), default=0.0) if prof else 0.0
        self._tool_radius = maxr
        pad = max(0.12, 0.05 * maxr)
        self._tool_outline_geo.set_profile(
            _tool_outline_profile(prof, pad) if prof else None)
        rings = _pack_edge_segments(
            _rings_segments(_profile_ring_points(prof)) if prof else [])
        self._tool_rings_geo.set_packed(rings.raw, rings.bounds, rings.nv)
        self.toolInfoChanged.emit()

    @Property("QVariantMap", notify=toolInfoChanged)
    def toolInfo(self):
        return self._tool_info

    @Property(QObject, notify=toolInfoChanged)
    def toolGeometry(self):
        return self._tool_geo

    @Property(QObject, notify=toolInfoChanged)
    def toolOutline(self):
        """Enlarged cutter used for the silhouette outline (same local frame)."""
        return self._tool_outline_geo

    @Property(QObject, constant=True)
    def holderGeometry(self):
        """ER20 collet nut + spindle body solid (bottom at y=0, up positive)."""
        return self._holder_geo

    @Property(QObject, constant=True)
    def holderOutline(self):
        """Inverted-hull outline solid of the holder."""
        return self._holder_outline_geo

    @Property(QObject, notify=toolInfoChanged)
    def toolRings(self):
        """Horizontal edge rings of the cutter (line outline)."""
        return self._tool_rings_geo

    @Property(QObject, constant=True)
    def holderRings(self):
        """Horizontal edge rings of the holder (line outline)."""
        return self._holder_rings_geo

    @Property(float, notify=toolInfoChanged)
    def toolLength(self):
        return self._tool_length

    @Property(float, notify=toolInfoChanged)
    def toolRadius(self):
        """Max cutter radius (for the screen-space outline width)."""
        return self._tool_radius

    @Property(float, constant=True)
    def holderRadius(self):
        """Max holder radius (for the screen-space outline width)."""
        return self._holder_radius

    @Slot(int)
    def setToolStage(self, index):
        """Show the cutter of operation stage `index` (rebuild the mesh).

        Called by the viewer as the described tool changes with the program
        position / selected stage. Empty stages keep the current tool."""
        if not self._stage_tools:
            return
        i = max(0, min(len(self._stage_tools) - 1, int(index)))
        info = self._stage_tools[i]
        if info and info != self._tool_info:
            self._apply_tool_info(info)

    @Property(QVector3D, notify=stockChanged)
    def stockCenter(self):
        return QVector3D(self._stock_center[0], self._stock_center[1], self._stock_center[2])

    @Property(QVector3D, notify=stockChanged)
    def stockSize(self):
        return QVector3D(self._stock_size[0], self._stock_size[1], self._stock_size[2])

    @Property(bool, notify=stockChanged)
    def hasStock(self):
        return self._has_stock

    @Property(float, notify=pathChanged)
    def holderGauge(self):
        """Holder gauge length from UI_HOLDER (true stick-out; 0 if not exported)."""
        return float(self._holder_info.get("gauge", 0.0) or 0.0)

    # ---- collision analysis (holder vs stock), background -----------------
    @Property("QVariantList", notify=collisionsChanged)
    def collisions(self):
        """Holder-vs-stock collision ranges along the path (scene coords)."""
        return self._collisions

    @Property(int, notify=collisionsChanged)
    def collisionCount(self):
        return len(self._collisions)

    @Slot()
    def analyzeCollisions(self):
        """(Re)run the holder-vs-stock collision scan in the background."""
        self._start_collision_scan()

    # ---- NC-vs-machine tool check (skeleton; wire to the controller later) ----
    @Property("QVariantList", notify=toolCheckChanged)
    def toolCheck(self):
        """Per-tool: number match + measured stick-out vs the program value."""
        return self._tool_check

    @Property(int, notify=toolCheckChanged)
    def toolIssueCount(self):
        return sum(1 for r in self._tool_check if r.get("status") != "ok")

    @Slot("QVariantList")
    def setMachineTools(self, tools):
        """Feed the controller's measured tool table:
        [{number, length, name, enabled}] (length = measured stick-out, mm)."""
        self._machine_tools = {}
        for t in (tools or []):
            try:
                num = int(t.get("number"))
            except (TypeError, ValueError, AttributeError):
                continue
            self._machine_tools[num] = {
                "length": float(t.get("length", 0.0) or 0.0),
                "name": str(t.get("name", "")),
                "enabled": bool(t.get("enabled", True)),
            }
        self._rebuild_tool_check()

    @Slot("QVariantMap")
    def setMachineLimits(self, limits):
        """Feed machine axis limits, e.g. {"zMax": 0.0, "zMin": -400.0}."""
        self._machine_limits = dict(limits) if limits else {}
        self._rebuild_tool_check()

    @Slot()
    def runToolCheck(self):
        """Re-run the NC-vs-machine tool comparison."""
        self._rebuild_tool_check()

    def _rebuild_tool_check(self):
        try:
            self._tool_check = _tool_check_report(
                self._stage_tools, self._stages, self._sample_pts,
                self._sample_cum, self._move_length,
                self._machine_tools, self._machine_limits)
        except Exception as exc:
            logger.warning("tool check failed: %s", exc)
            self._tool_check = []
        self.toolCheckChanged.emit()

    def _start_collision_scan(self):
        """Kick off the holder-vs-stock collision scan on a worker thread."""
        if self._sample_pts is None or not self._stages or self._stock_bounds is None:
            self._collisions = []
            self.collisionsChanged.emit()
            return
        self._collision_token += 1
        total = self._move_length
        starts = [float(s.get("start", 0.0)) * total for s in self._stages]
        lens = [_tool_exposed_length(t) for t in self._stage_tools]
        gauge = float(self._holder_info.get("gauge", 0.0) or 0.0)
        if gauge > 0.0:
            lens = [gauge] * max(1, len(starts))   # true stick-out from UI_HOLDER
        if not lens:
            lens = [0.0]
        while len(lens) < len(starts):
            lens.append(lens[-1])
        threading.Thread(
            target=self._worker_collisions,
            args=(self._collision_token, self._sample_pts, self._sample_cum,
                  total, starts, lens, self._stock_bounds),
            daemon=True).start()

    def _worker_collisions(self, token, pts, cum, total, starts, lens, stock):
        try:
            res = _scan_collisions(pts, cum, total, starts, lens, stock, _COLLISION_STEP)
        except Exception as exc:
            logger.warning("collision scan failed: %s", exc)
            res = []
        try:
            if token != self._collision_token:
                return
            self._collisions = res
            self.collisionsChanged.emit()
        except RuntimeError:
            pass                       # app shutting down while the scan finishes

    def _rebuild_part_transform(self):
        """Final scene transform of the part: scene <- base <- WCS <- CAD."""
        q_base = QQuaternion.fromEulerAngles(-90.0, 0.0, 0.0)     # machine Z-up -> scene Y-up
        q_wcs = QQuaternion.fromEulerAngles(self._rot[0], self._rot[1], self._rot[2])
        t_wcs = QVector3D(self._model_pos[0], self._model_pos[1], self._model_pos[2])
        if self._cad_matrix:
            c0 = (self._cad_matrix[0], self._cad_matrix[1], self._cad_matrix[2])
            c1 = (self._cad_matrix[3], self._cad_matrix[4], self._cad_matrix[5])
            c2 = (self._cad_matrix[6], self._cad_matrix[7], self._cad_matrix[8])
            q_cad = quat_from_cols(c0, c1, c2)
            o = self._cad_matrix_origin or (0.0, 0.0, 0.0)
            t_cad = QVector3D(o[0], o[1], o[2])
        else:
            q_cad = QQuaternion(1.0, 0.0, 0.0, 0.0)   # no CAD transform available
            t_cad = QVector3D(0.0, 0.0, 0.0)
        q_m = q_wcs * q_cad                       # CAD -> machine rotation
        q = q_base * q_m
        if self._has_stock and self._has_mesh:
            # Asymmetric fit: align the model's rotated AABB min corner to the stock's min corner.
            rmin = [1e30, 1e30, 1e30]
            for cx in (self._mesh_lo[0], self._mesh_hi[0]):
                for cy in (self._mesh_lo[1], self._mesh_hi[1]):
                    for cz in (self._mesh_lo[2], self._mesh_hi[2]):
                        r = q_m.rotatedVector(QVector3D(cx, cy, cz))
                        rmin[0] = min(rmin[0], r.x())
                        rmin[1] = min(rmin[1], r.y())
                        rmin[2] = min(rmin[2], r.z())
            t = q_base.rotatedVector(QVector3D(self._stock_lower_m[0] - rmin[0],
                                               self._stock_lower_m[1] - rmin[1],
                                               self._stock_lower_m[2] - rmin[2]))
        elif self._part_auto_center:
            # Fallback: center on the toolpath.
            rc = q.rotatedVector(QVector3D(self._mesh_center[0], self._mesh_center[1], self._mesh_center[2]))
            t = QVector3D(self._center[0], self._center[1], self._center[2]) - rc
        else:
            t = q_base.rotatedVector(q_wcs.rotatedVector(t_cad) + t_wcs)
        t = t + QVector3D(*_to_3d(self._part_offset))   # machine-axis nudge -> scene
        self._part_rot = q
        self._part_pos = [t.x(), t.y(), t.z()]
        self.partTransformChanged.emit()

    def _set_model_path(self, path):
        if path != self._model_path:
            self._model_path = path
            self.modelPathChanged.emit()

    def _find_model(self, filepath):
        """Model sidecar resolution: STEP wins over STL.

        A STEP carries B-rep topology: the skin is tessellated from it and the
        outline comes from the model tree itself (real face boundaries,
        tangent seams included). Only when no STEP is present does the STL
        mesh serve as the model, with the outline recovered by dihedral-angle
        heuristics."""
        base = os.path.splitext(filepath)[0]
        for ext in (".step", ".stp"):
            p = base + ext
            if os.path.exists(p):
                self._set_model_path(os.path.abspath(p))
                return
        stl = base + ".stl"
        if not os.path.exists(stl):
            for ext in (".STL", ".Stl"):
                if os.path.exists(base + ext):
                    stl = base + ext
                    break
        self._set_model_path(os.path.abspath(stl) if os.path.exists(stl) else "")

    @staticmethod
    def _stl_normals(verts):
        """Crease-smooth vertex normals for an STL triangle soup (position
        list); None when the soup is too short (caller keeps flat normals).

        Слой считает Python, пока CNC_CPP не просит C++ (мост gvbridge):
        отказ C++-пути виден в журнале и возвращает расчёт на Python, а не
        молчание."""
        if verts is None or len(verts) < 3:
            return None
        if gv_cpp.normals_enabled():
            try:
                return gv_cpp.smooth_face_normals(verts)
            except Exception as exc:
                logger.warning("C++-нормали не посчитаны (%s): считает Python", exc)
        try:
            return _smooth_face_normals(_np.asarray(verts, dtype=_np.float64)
                                        .reshape(-1, 3, 3))
        except Exception:
            return None

    @staticmethod
    def _mesh_topology(verts):
        """Топология меша для силуэта (MeshTopology) или None на пустом супе.

        Считает C++, если CNC_CPP=silhouette|all (мост gvbridge, этап 5 плана
        переноса): отказ C++-пути виден в журнале и уходит на питоновский эталон.
        Строится один раз на модель (~137 мс на Test_model, ~45k треугольников)."""
        if verts is None:
            return None
        if gv_cpp.silhouette_enabled():
            try:
                return gv_cpp.build_silhouette_topology(verts)
            except Exception as exc:
                _warn_fallback("topology",
                               "C++-топология силуэта не построена (%s): строит Python"
                               % exc)
        return _build_silhouette_topology(verts)

    @staticmethod
    def _mesh_segments(topo, eye):
        """Силуэтные рёбра меша на глазу eye: C++ при CNC_CPP=silhouette."""
        if gv_cpp.silhouette_enabled():
            try:
                return gv_cpp.mesh_silhouette_segments(topo, eye)
            except Exception as exc:
                _warn_fallback("silhouette",
                               "C++-силуэт не посчитан (%s): считает Python" % exc)
        return _mesh_silhouette_segments(topo, eye)

    @staticmethod
    def _tangents(surfaces, packed=None, skin=None):
        """Семейства касательных STEP: C++ при CNC_CPP=silhouette.

        Семейства строятся один раз на модель: у эталона это тот же
        _tangent_families, и результат сравнивается с ним побитово."""
        if gv_cpp.silhouette_enabled():
            try:
                return gv_cpp.tangent_families(surfaces, packed, skin)
            except Exception as exc:
                _warn_fallback("tangent-families",
                               "C++-семейства касательных не посчитаны (%s): "
                               "считает Python" % exc)
        return _tangent_families(surfaces, packed, skin)

    @staticmethod
    def _tangent_segments(families, eye):
        """Отрезки касательных на глазу eye: C++ при CNC_CPP=silhouette."""
        if gv_cpp.silhouette_enabled():
            try:
                return gv_cpp.tangent_segments(families, eye)
            except Exception as exc:
                _warn_fallback("tangent-segments",
                               "C++-касательные не посчитаны (%s): считает Python" % exc)
        return _tangent_from_families(families, eye)

    @staticmethod
    def _prepare_toolpath(text):
        """Разбор УП и подготовка траектории (parse_gcode + _prepare_ordered).

        Считает C++, если CNC_CPP=parser|all (мост gvbridge, этап 5 плана
        переноса): отказ C++-пути виден в журнале и уходит на питоновский эталон."""
        if gv_cpp.parser_enabled():
            try:
                return gv_cpp.prepare_path(text)
            except Exception as exc:
                logger.warning("C++-разбор УП не прошёл (%s): разбирает Python", exc)
        ordered, _last, _seg, stages = parse_gcode(text)
        return _prepare_ordered(ordered, stages)

    def _load_model_verts(self, model_path):
        """Read the model source on a worker thread: STEP returns (verts,
        pre-packed outline segments, smooth normals); STL returns
        (verts, None, smooth normals)."""
        if not model_path:
            return None, None, None
        low = model_path.lower()
        if low.endswith(".stl"):
            try:
                self._rev_surfaces = None
                self._tan_fams = None
                verts = load_stl_vertices(model_path)
                verts = _orient_shell_outward(verts).reshape(-1, 3)
                self._mesh_topo = self._mesh_topology(verts)
                return verts, None, self._stl_normals(verts)
            except Exception as exc:
                logger.warning("STL load failed: %s", exc)
                return None, None, None
        if low.endswith((".step", ".stp")):
            ck = _cache_key(model_path, "model") if _cache_enabled() else None
            hit = _cache_read_model(ck) if ck else None
            if hit is not None:
                if self._rev_surfaces is None:
                    self._rev_surfaces = _step_surfaces(model_path)
                    self._tan_fams = self._tangents(self._rev_surfaces,
                                                    hit[1],
                                                    (hit[0], hit[2]))
                    # топология меша для силуэта: строится вместе с моделью
                    # (~137 мс на Test_model, ~45k треугольников) и живёт до
                    # смены модели; на камере силуэт считается по ней
                    self._mesh_topo = self._mesh_topology(hit[0])
                return hit
            try:
                verts, segments, normals, surfaces = _read_step_model(model_path)
                self._rev_surfaces = surfaces
                packed = _pack_edge_segments(segments)
                self._tan_fams = self._tangents(self._rev_surfaces, packed,
                                                (verts, normals))
                self._mesh_topo = self._mesh_topology(verts)
                if ck and normals is not None:
                    _cache_write_model(ck, verts, normals, packed)
                return verts, packed, normals
            except Exception as exc:
                logger.warning("STEP load failed: %s", exc)
                stl = os.path.splitext(model_path)[0] + ".stl"
                if not os.path.exists(stl):
                    for ext in (".STL", ".Stl"):
                        if os.path.exists(os.path.splitext(model_path)[0] + ext):
                            stl = os.path.splitext(model_path)[0] + ext
                            break
                if os.path.exists(stl):
                    self._model_path = os.path.abspath(stl)
                    try:
                        sv = load_stl_vertices(self._model_path)
                        sv = _orient_shell_outward(sv).reshape(-1, 3)
                        self._mesh_topo = self._mesh_topology(sv)
                        return sv, None, None
                    except Exception:
                        pass
        return None, None, None

    @Property(QObject, notify=meshChanged)
    def partGeometry(self):
        return self._mesh

    @Property(QObject, notify=meshChanged)
    def partEdges(self):
        return self._edges

    @Property(QObject, notify=meshChanged)
    def hlrEdges(self):
        """B-rep outline in the depth-tested main-scene form (HLRLineGeometry
        fed by _set_edges_packed; the skin must be opaque for the test)."""
        return self._hlr_edges

    @Property(QObject, notify=meshChanged)
    def hlrTangents(self):
        """Silhouette lines, same GPU visibility treatment, updated
        by refreshTangents on every camera move."""
        return self._hlr_tangents

    @Slot(QVector3D)
    def refreshTangents(self, view_point):
        """Пересчёт силуэта на текущий глаз камеры.

        Глаз приходит в кадре сцены; в raw-кадр модели его переводит обратная
        трансформа детали (та же, что у видимой геометрии). Режим задаётся
        переменной окружения CNC_SILHOUETTE: 'mesh' (по умолчанию) - силуэт по
        топологии меша, 'analytic' - прежние касательные образующие
        цилиндров/конусов (оставлены для сравнения). Сегменты обоих режимов
        публикуются в один слой _hlr_tangents (HLRLineGeometry). Cam-float
        делает stepledges.vert по eyeRaw/camFloatMm, поэтому статические
        B-rep рёбра загружаются один раз на модель и на движение камеры не
        перепаковываются: раньше их репак на GUI-потоке стоил ~22 мс/кадр
        (замер на Test_model, 18986 сегментов)."""
        try:
            eye = QVector3D(view_point.x() - self._part_pos[0],
                            view_point.y() - self._part_pos[1],
                            view_point.z() - self._part_pos[2])
            e = self._part_rot.inverted().rotatedVector(eye)
            eye_raw = (e.x(), e.y(), e.z())
            self._eye_raw = QVector3D(*eye_raw)
            self.eyeChanged.emit()
            mode = os.environ.get("CNC_SILHOUETTE", "mesh").strip().lower()
            if mode == "analytic":
                if not self._rev_surfaces:
                    return
                if self._tan_fams is None:
                    self._tan_fams = self._tangents(self._rev_surfaces,
                                                    self._step_edges)
                segments = self._tangent_segments(self._tan_fams, eye_raw)
            else:
                if self._mesh_topo is None:
                    return                  # модель без меша: силуэта нет
                segments = self._mesh_segments(self._mesh_topo, eye_raw)
            has = segments is not None and len(segments) > 0
            if has:
                self._sync_hlr_reference()
                self._hlr_tangents.set_segments(
                    _np.asarray(segments, dtype=_np.float32))
            else:
                self._hlr_tangents.set_segments(None)
        except Exception:
            pass

    eyeChanged = Signal()

    @Property(QVector3D, notify=eyeChanged)
    def eyeRaw(self):
        """Глаз камеры в raw-кадре модели (обновляется в refreshTangents).
        Привязан в QML к материалам рёбер: cam-float в stepledges.vert."""
        return self._eye_raw

    @Property(QVector3D, notify=meshChanged)
    def partMeshCenter(self):
        """AABB center of the raw part mesh (used to scale its outline about it)."""
        return QVector3D(self._mesh_center[0], self._mesh_center[1], self._mesh_center[2])

    @Property(float, notify=meshChanged)
    def partRadius(self):
        """Half-diagonal of the part mesh bounds (for the screen-space outline)."""
        if not self._has_mesh:
            return 1.0
        return 0.5 * math.sqrt(sum((self._mesh_hi[i] - self._mesh_lo[i]) ** 2
                                   for i in range(3)))

    def _load_model_mesh(self):
        """Synchronous path: read the auto-detected STEP/STL, then apply it."""
        with _occt:
            verts, segs, norms = self._load_model_verts(self._model_path)
        self._step_edges = segs
        self._model_normals = norms
        self._apply_mesh_verts(verts)

    def _apply_mesh_verts(self, verts):
        """Build the part mesh + its feature-edge outline (raw model coordinates).
        Vertices may be prepared by a worker thread; the outline is computed in
        the background too, so the view opens before the mesh is ready.
        Must run on the GUI thread (touches QQuick3DGeometry)."""
        self._mesh.reset()
        self._clear_edges()                  # fresh empty outline geometry
        self._hlr_tangents.set_segments(None)   # силуэт прошлой модели убран
        self._mesh_center = [0.0, 0.0, 0.0]
        self._has_mesh = False
        self._edge_token += 1
        token = self._edge_token
        if verts is not None and len(verts):
            try:
                # raw model coords (no scene mapping); STEP supplies crease-
                # grouped smooth normals, STL falls back to flat per-face
                self._mesh.set_triangles(verts, self._model_normals)
                vb = _verts_bounds(verts)
                lo, hi = vb.lo, vb.hi
            except Exception:
                lo = hi = [0.0, 0.0, 0.0]
                verts = None
        if verts is not None and len(verts):
            self._mesh_center = [(lo[i] + hi[i]) / 2.0 for i in range(3)]
            self._mesh_lo = lo
            self._mesh_hi = hi
            self._has_mesh = True
            threading.Thread(target=self._compute_edges,
                             args=(verts, token, self._model_path),
                             daemon=True).start()
            self._rebuild_part_transform()
        else:
            self._mesh_topo = None            # у модели без меша силуэта нет
        self.meshChanged.emit()

    def _clear_edges(self):
        """Publish a fresh empty outline geometry (a new object makes the QML
        Model re-upload it even when this runs after the first frame)."""
        g = ToolpathGeometry()
        QQmlEngine.setObjectOwnership(g, QQmlEngine.CppOwnership)
        self._edges = g
        self._hlr_edges.set_segments(None)

    def _set_edges_packed(self, packed):
        """Publish a pre-packed outline buffer (built by the worker thread)
        and mirror it into the depth-tested main-scene geometry."""
        g = ToolpathGeometry()
        QQmlEngine.setObjectOwnership(g, QQmlEngine.CppOwnership)
        g.set_packed(packed.raw, packed.bounds, packed.nv)
        self._edges = g
        self._sync_hlr_reference()
        self._hlr_edges.set_segments(packed_positions(packed))

    def _sync_hlr_reference(self):
        """Share the model raw-frame AABB center with both line layers."""
        if self._mesh_lo is None or self._mesh_hi is None:
            return
        c = (0.5 * (self._mesh_lo[0] + self._mesh_hi[0]),
             0.5 * (self._mesh_lo[1] + self._mesh_hi[1]),
             0.5 * (self._mesh_lo[2] + self._mesh_hi[2]))
        self._hlr_edges.set_reference(c)
        self._hlr_tangents.set_reference(c)

    def _compute_edges(self, verts, token, model_path=""):
        """Worker thread: build the packed part outline, hand it back.

        STEP model: the outline came with the model read (true B-rep edges,
        already in the skin's coordinate space) - just republish it. STL
        model: dihedral-angle feature edges of the mesh, disk-cached."""
        if self._step_edges is not None:
            packed = self._step_edges
            logger.debug("outline: source=STEP segments=%d", packed.nv // 2)
        else:
            ck = _cache_key(model_path, "edges") if (model_path and _cache_enabled()) else None
            packed = _cache_read_edges(ck) if ck else None
            if packed is None:
                try:
                    packed = _feature_edges(verts, 20.0)
                except Exception:
                    packed = PackedEdges(b'', 0, None)
                if ck:
                    _cache_write_edges(ck, packed)
            logger.debug("outline: source=MESH angle=20deg segments=%d", packed.nv // 2)
        try:
            self.edgesReady.emit((token, packed))
        except RuntimeError:
            pass                       # app shutting down while the edges finish

    def _apply_edges(self, payload):
        """Main thread: apply the outline if it still matches the current model."""
        token, packed = payload
        if token == self._edge_token:
            self._set_edges_packed(packed)
            self.meshChanged.emit()

    @Property(QObject, notify=ready)
    def rapidGeometry(self):
        return self._rapid

    @Property(QObject, notify=ready)
    def feedGeometry(self):
        return self._feed

    @Property(QVector3D, notify=pathChanged)
    def center(self):
        return QVector3D(self._center[0], self._center[1], self._center[2])

    @Property(float, notify=pathChanged)
    def radius(self):
        return self._radius

    # ---- ordered tool path (for animation) -------------------------------
    @Property(float, notify=pathChanged)
    def moveLength(self):
        return self._move_length

    @Property("QVariantList", notify=pathChanged)
    def stages(self):
        """Operation stages [{start, end, label}] (normalized path ranges)."""
        return self._stages

    def _index_at(self, d):
        """Index of the path segment containing distance `d` (binary search)."""
        cum = self._sample_cum
        n = len(cum) if cum is not None else 0
        if n < 2 or d <= 0.0:
            return 0
        if d >= cum[n - 1]:
            return n - 2
        if isinstance(cum, _np.ndarray):
            i = int(_np.searchsorted(cum, d, side="right")) - 1
        else:
            i = _bisect_right(cum, d) - 1
        if i < 0:
            return 0
        if i > n - 2:
            return n - 2
        return i

    @Slot(float, result=QVector3D)
    def sampleAt(self, d):
        """Interpolated tool position (scene coords) at path distance `d`.

        Sampling lives in C++ so QML never receives the per-point lists."""
        pts = self._sample_pts
        n = len(pts) if pts is not None else 0
        if n == 0:
            return QVector3D(0.0, 0.0, 0.0)
        if n < 2:
            p = pts[0]
            return QVector3D(float(p[0]), float(p[1]), float(p[2]))
        lo = self._index_at(d)
        hi = lo + 1
        cum = self._sample_cum
        span = float(cum[hi] - cum[lo])
        t = (d - float(cum[lo])) / span if span > 1e-9 else 0.0
        a = pts[lo]
        b = pts[hi]
        return QVector3D(float(a[0] + (b[0] - a[0]) * t),
                         float(a[1] + (b[1] - a[1]) * t),
                         float(a[2] + (b[2] - a[2]) * t))

    @Slot(float, result=bool)
    def isRapidAt(self, d):
        """True when the segment containing distance `d` is a rapid move."""
        rapid = self._sample_rapid
        n = len(rapid) if rapid is not None else 0
        if n == 0:
            return False
        return bool(rapid[self._index_at(d)])

    @Slot(float, result=float)
    def speedAt(self, d):
        """Scene speed (mm/s) of the segment containing distance `d`.

        Feed segments (G1/G2/G3) carry their programmed F (mm/min -> mm/s),
        so the tool animation runs 1:1 with the real machine; rapids (G0) use
        the machine rapid rate."""
        spds = self._sample_speeds
        n = len(spds) if spds is not None else 0
        if n == 0:
            return 0.0
        return float(spds[self._index_at(d)])

    @Property(bool, notify=loadingChanged)
    def loading(self):
        return self._loading

    def _set_loading(self, v):
        v = bool(v)
        if v != self._loading:
            self._loading = v
            self.loadingChanged.emit()

    @Slot(str, result=int)
    def load_gcode(self, filepath):
        """Synchronous load (used headless / by tests). Blocks the caller."""
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as fh:
                text = fh.read()
        except OSError as exc:
            logger.error("cannot read '%s': %s", filepath, exc)
            return 0
        count = self.load_string(text)   # clears modelPath
        self._find_model(filepath)       # auto-loads <same name>.stl (if present)
        self._load_model_mesh()          # builds the part mesh
        return count

    @Slot(str, result=bool)
    def loadModel(self, filepath):
        """Загрузка только модели (STEP/STL) без траектории: отладочный путь
        для проверки обводки на голом теле. Синхронный."""
        fp = os.path.abspath(filepath)
        if not os.path.exists(fp):
            return False
        self._load_token += 1        # аннулирует незавершённые async-фазы
        self._set_model_path(fp)
        self._load_model_mesh()
        self.loadFinished.emit()
        return True

    @Slot(str, result=int)
    def load_gcode_async(self, filepath):
        """Load a .ngc on a worker thread so the GUI stays responsive.

        The tool path is applied (and shown) as soon as it is ready; the STL
        mesh is applied afterwards, still off the GUI thread. Progress is
        reported through `loading`, `toolpathLoaded`, `loadFinished`."""
        try:
            with open(filepath, "r", encoding="utf-8", errors="ignore") as fh:
                text = fh.read()
        except OSError as exc:
            logger.error("cannot read '%s': %s", filepath, exc)
            return 0
        self._load_token += 1
        token = self._load_token
        self._set_loading(True)
        threading.Thread(target=self._worker_load,
                         args=(filepath, token, text), daemon=True).start()
        return 1

    def _worker_load(self, filepath, token, text):
        """Worker thread: parse + build buffers (pure), then read the STL.

        The prepared path is restored from the disk cache when possible, so a
        re-open of the same program skips parsing / buffer packing entirely."""
        try:
            ck = _cache_key(filepath, "path") if _cache_enabled() else None
            data = _cache_read_path(ck) if ck else None
            if data is None:
                data = self._prepare_toolpath(text)
                if ck:
                    _cache_write_path(ck, data)
        except Exception as exc:                       # never take the app down
            logger.error("parse failed: %s", exc)
            self.loadFinished.emit()
            return
        if token != self._load_token:
            return
        self.toolpathReady.emit((token, text, data))

        # ---- phase B: model sidecar (STEP preferred, STL fallback). Only read
        # the raw vertices/edges here; the geometry is installed on the GUI
        # thread. ----
        model_path = ""
        verts = None
        segs = None
        norms = None
        base = os.path.splitext(filepath)[0]
        for ext in (".step", ".stp"):
            if os.path.exists(base + ext):
                model_path = os.path.abspath(base + ext)
                break
        if not model_path and os.path.exists(base + ".stl"):
            model_path = os.path.abspath(base + ".stl")
        if not model_path:
            for ext in (".STL", ".Stl"):
                if os.path.exists(base + ext):
                    model_path = os.path.abspath(base + ext)
                    break
        if model_path:
            with _occt:
                verts, segs, norms = self._load_model_verts(model_path)
        else:
            verts = None
        if token != self._load_token:
            return
        self._step_edges = segs
        self._model_normals = norms
        self.meshReady.emit((token, model_path, verts))

    def _on_toolpath_ready(self, payload):
        """GUI thread: apply the tool path prepared by the worker."""
        token, text, data = payload
        if token != self._load_token:
            return
        self._set_model_path("")
        self._apply_toolpath_data(data, text)
        self._mesh.reset()               # replaced by the mesh phase
        self._clear_edges()
        self.meshChanged.emit()
        self.toolpathLoaded.emit(data.seg_count)

    def _on_mesh_ready(self, payload):
        """GUI thread: apply the STL mesh prepared by the worker."""
        token, model_path, verts = payload
        if token != self._load_token:
            return
        self._set_model_path(model_path)
        self._apply_mesh_verts(verts)
        self._set_loading(False)
        self.loadFinished.emit()

    def _apply_toolpath_data(self, data, text):
        """Apply parsed tags + prepared path data to the live properties."""
        self._set_rot(*parse_ui_rotation(text))              # UI_ROTATION: RX.. RY.. RZ..
        self._set_model_pos(*parse_ui_origin(text))          # UI_ORIGIN: X.. Y.. Z..
        self._cad_matrix = parse_ui_cad_matrix(text)         # UI_CAD_MATRIX (columns) if present
        self._cad_matrix_origin = parse_ui_cad_matrix_origin(text)
        # Per-stage cutter info (one UI_TOOL block per operation) so the drawn
        # tool changes together with the active stage / tool in the program.
        self._stage_tools = [s.get("tool", {}) for s in data.stages]
        first_info = next((t for t in self._stage_tools if t), None)
        self._apply_tool_info(first_info if first_info else parse_ui_tool(text))
        self._holder_info = parse_ui_holder(text)            # UI_HOLDER: gauge (optional)
        st = parse_ui_stock(text)                            # UI_STOCK: billet bounds (WCS)
        if st:
            x0, x1, y0, y1, z0, z1 = st
            c = ((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0)
            s = (x1 - x0, y1 - y0, z1 - z0)
            self._stock_center = list(_to_3d(c))             # machine -> scene
            self._stock_size = [s[0], s[2], s[1]]            # (dx, dz, dy) in scene axes
            self._stock_lower_m = [x0, y0, z0]               # min corner, machine coords
            self._has_stock = True
        else:
            self._has_stock = False
        self._stock_bounds = tuple(st) if st else None       # machine AABB for collisions
        self.stockChanged.emit()
        self._rebuild_part_transform()

        self._set_tool(*data.last)
        self._sample_pts = data.pts
        self._sample_cum = data.cum
        self._sample_rapid = data.rapid
        self._sample_speeds = data.speeds
        self._stages = data.stages
        self._move_length = data.total
        self._rapid.set_packed(data.rapid_bytes, data.bounds, data.rapid_nv)
        self._feed.set_packed(data.feed_bytes, data.bounds, data.feed_nv)
        self._center = list(data.center)
        self._radius = data.radius
        self._min_y = data.min_y
        self.pathChanged.emit()
        self._start_collision_scan()      # background holder-vs-stock check
        self._rebuild_tool_check()        # NC-vs-machine tool check (skeleton)

    @Slot(str, result=int)
    def load_string(self, text):
        self._set_model_path("")         # string input has no file to pair with
        data = self._prepare_toolpath(text)
        self._apply_toolpath_data(data, text)
        self._mesh.reset()               # string input -> no part model
        self._clear_edges()
        self.meshChanged.emit()
        return data.seg_count

    @Property(float, notify=pathChanged)
    def minY(self):
        return self._min_y

    # ---- tool position ---------------------------------------------------
    @Property(float, notify=toolChanged)
    def toolX(self):
        return self._tool[0]

    @toolX.setter
    def toolX(self, v):
        self._set_tool(float(v), self._tool[1], self._tool[2])

    @Property(float, notify=toolChanged)
    def toolY(self):
        return self._tool[1]

    @toolY.setter
    def toolY(self, v):
        self._set_tool(self._tool[0], float(v), self._tool[2])

    @Property(float, notify=toolChanged)
    def toolZ(self):
        return self._tool[2]

    @toolZ.setter
    def toolZ(self, v):
        self._set_tool(self._tool[0], self._tool[1], float(v))

    @Property(QVector3D, notify=toolChanged)
    def toolPosition(self):
        p = _to_3d(self._tool)
        return QVector3D(p[0], p[1], p[2])

    def _set_tool(self, x, y, z):
        if (x, y, z) != (self._tool[0], self._tool[1], self._tool[2]):
            self._tool = [x, y, z]
            self.toolChanged.emit()


