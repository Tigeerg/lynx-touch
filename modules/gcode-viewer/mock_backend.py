# -*- coding: utf-8 -*-
"""
mock_backend.py — PySide6 / Qt 6.x backend for a 4-axis CNC (X, Y, Z, A).

Pure QML/Python, no LinuxCNC widgets.

Types (registered into QML module "MockBackend" 1.0 via @QmlElement):
  * ToolpathGeometry — QQuick3DGeometry subclass, position-only line buffer
                       (primitive = Lines). Used for G0 (red) and G1 (blue).
  * GridGeometry     — QQuick3DGeometry subclass, X/Z grid lines (the "floor")
                       with a soft alpha falloff toward the edges.
  * GcodeToolpath    — QObject: parses G0/G1/G2/G3 (planes G17/G18/G19) and the
                       canned drill cycles G73/G81/G82/G83, owns
                       rapidGeometry/feedGeometry, keeps the tool position.

Machine coords -> scene coords: (X, Y, Z)_machine -> (X, Z, -Y)_scene (Y is up).
"""

import hashlib
import json
import math
import os
import re
import struct
import threading

from array import array
from bisect import bisect_right as _bisect_right



from PySide6.QtCore import Property, QByteArray, QObject, QTimer, Signal, Slot
from PySide6.QtGui import QQuaternion, QVector3D
from PySide6.QtQml import QmlElement, QQmlEngine
from PySide6.QtQuick3D import QQuick3DGeometry

QML_IMPORT_NAME = "MockBackend"
QML_IMPORT_MAJOR_VERSION = 1
QML_IMPORT_MINOR_VERSION = 0

_F32 = QQuick3DGeometry.Attribute.ComponentType.F32Type
_POSITION = QQuick3DGeometry.Attribute.Semantic.PositionSemantic
_COLOR = QQuick3DGeometry.Attribute.Semantic.ColorSemantic
_TEXCOORD0 = QQuick3DGeometry.Attribute.Semantic.TexCoordSemantic
_NORMAL = QQuick3DGeometry.Attribute.Semantic.NormalSemantic
_LINES = QQuick3DGeometry.PrimitiveType.Lines
_TRIANGLES = QQuick3DGeometry.PrimitiveType.Triangles



def _pack(points):
    """points: list[(x, y, z)] (scene coords, already pairs for Lines).

    Packs via a C float array (much faster than per-vertex struct.pack)."""
    flat = array('f')
    lo = [math.inf, math.inf, math.inf]
    hi = [-math.inf, -math.inf, -math.inf]
    for x, y, z in points:
        flat.append(x)
        flat.append(y)
        flat.append(z)
        if x < lo[0]:
            lo[0] = x
        if y < lo[1]:
            lo[1] = y
        if z < lo[2]:
            lo[2] = z
        if x > hi[0]:
            hi[0] = x
        if y > hi[1]:
            hi[1] = y
        if z > hi[2]:
            hi[2] = z
    return QByteArray(flat.tobytes()), lo, hi


# ---- вынесенные слои (этап 0 переноса на C++) ------------------------------
# mock_backend остаётся фасадом: имена, которые берут тесты, bench_viewer,
# oracle_dump и QML, реэкспортируются отсюда, поэтому внешний код не меняется.
from gv_common import _np, _occt                                        # noqa: F401
from gcode_parser import (parse_gcode, _run_cycle, _arc_points,          # noqa: F401
                          _arc_sweep, _to_mms, _DECIMATE_TOL, _DECIMATE_TOL2,
                          _ARC_DECIMATE_TOL, _ARC_DECIMATE_TOL2,
                          _RAPID_SPEED_MMS, _DEFAULT_FEED_MMS,
                          _WORD_RE, _COMMENT_RE, _PLANES, _PLANE_OFFSETS)
from ui_tags import (parse_ui_rotation, parse_ui_origin, parse_ui_tool,  # noqa: F401
                     parse_ui_holder, parse_ui_stock, parse_ui_cad_matrix,
                     parse_ui_cad_matrix_origin, stage_label, quat_from_cols,
                     _comment_text, _ops_with_tools, _UI_ROT_RE, _UI_ORG_RE,
                     _UI_TOOL_RE, _TOOLCHANGE_RE, _TOOL_TYPE_RU, _UI_CAD_MAT_RE,
                     _UI_CAD_MAT_ORG_RE, _NUM_RE, _UI_STOCK_RE, _UI_HOLDER_RE,
                     _COMMENT_TEXT_RE)


# Holder (ER20 collet nut + spindle body) above the tool stick-out; used by the

# ---- слои данных и геометрии (этап 0 переноса на C++) ---------------
from cache_store import (CACHE_VER, CACHE_DIR, CACHE_MAX_BYTES, _cache_enabled, _cache_key, _cache_file, _cache_prune, _cache_read_path, _cache_write_path, _cache_read_edges, _cache_write_edges, _cache_read_model, _cache_write_model)  # noqa: F401
from stl_import import (load_stl_vertices, _verts_bounds)  # noqa: F401
from packing import (_pack_edges_np, packed_positions, _pack_edge_segments, _coalesce_segments)  # noqa: F401
from step_import import (_step_shape, _orient_shell_outward, load_step_model)  # noqa: F401
from tangents import (_pt_tri_dist2, _CONE_MIN_SLANT_MM, _CONE_PROBE_AZIMUTHS, _cone_keeps_silhouette, _dedupe_cones, _tangent_families, _tangent_from_families)  # noqa: F401
from silhouette import (_SILHOUETTE_WELD_MM, _SILHOUETTE_MAX_EDGE_DEG, _SILHOUETTE_MIN_FACE_AREA, _build_silhouette_topology, _mesh_silhouette_segments)  # noqa: F401
from step_surfaces import (load_step_surfaces)  # noqa: F401
from normals import (_smooth_face_normals)  # noqa: F401
from outline import (_FEATURE_SIMPLIFY_MM, _rdp_indices, _simplify_edge_chains, _feature_edges, _feature_edges_np, _feature_edges_py)  # noqa: F401
# Collision scan sampling step along the path (mm).
_COLLISION_STEP = float(os.environ.get("CNC_COLLISION_STEP", "1.0"))




# ---- слои аналитики и периферии (этап 0 переноса на C++) ----------
from gv_common import _to_3d                                     # noqa: F401
from path_prep import (_scene_bounds_lo_hi, _pack_stream_np, _pack_stream_py, _stages_norm, _prepare_ordered)  # noqa: F401
from tooling import (_HOLDER_NUT_D, _HOLDER_NUT_H, _HOLDER_BODY_D, _HOLDER_BODY_H)  # noqa: F401
from tooling import (_tool_profile, _tool_outline_profile, _tool_exposed_length, _holder_profile, _holder_solid_profile, _holder_outline_profile, _rings_segments, _profile_ring_points)  # noqa: F401
from collision import (_circle_rect_hit, _scan_collisions, _tool_check_report)  # noqa: F401



# ==========================================================================
#  Geometry types
# ==========================================================================
@QmlElement
class ToolpathGeometry(QQuick3DGeometry):
    """Line geometry (primitive = Lines) with a per-vertex path parameter in
    TexCoord0.x (0..1). A custom material discards segments whose parameter is
    below the current simulation progress, hiding the already-cut path.

    The vertex buffer is stored as packed bytes (``_raw``) so a worker thread
    can build it and the GUI thread only has to install it (``set_packed``)."""

    STRIDE = 5 * 4                    # position (3f) + texcoord (2f)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._raw = b''                      # interleaved position(3f) + texcoord(2f)
        self._nv = 0                         # vertex count
        self._bounds = None                  # (x0,y0,z0,x1,y1,z1) scene space
        self._rebuild()

    # ---- API -------------------------------------------------------------
    @property
    def vertexCount(self):
        return self._nv

    def set_packed(self, raw, bounds, nv):
        """Install an already-packed line buffer (built off the GUI thread)."""
        self._raw = raw or b''
        self._nv = int(nv)
        self._bounds = bounds
        self._rebuild()


    # ---- build -----------------------------------------------------------
    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_LINES)
        if not self._raw:
            self.setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0))
            self.update()
            return
        self.setVertexData(QByteArray(self._raw))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_TEXCOORD0, 3 * 4, _F32)
        if self._bounds is not None:
            x0, y0, z0, x1, y1, z1 = self._bounds
        else:
            x0 = y0 = z0 = x1 = y1 = z1 = 0.0
        pad = 1.0
        self.setBounds(QVector3D(x0 - pad, y0 - pad, z0 - pad),
                       QVector3D(x1 + pad, y1 + pad, z1 + pad))
        self.update()


@QmlElement
class HLRLineGeometry(QQuick3DGeometry):
    """Classic-CAD outline renderer with correct visibility: each model
    edge becomes a camera-facing screen-space ribbon (two triangles),
    extruded to a constant pixel width in the vertex shader
    (stepledges.vert) and depth-tested against the opaque skin, so line
    parts behind the body are cut by the rasterizer live during orbiting.
    True GL_LINES exist at exactly 1 px and MSAA coverage thins grazing
    side-wall outlines to a hatch that vanishes on zoom-in; a solid 1.5 px
    ribbon takes antialiasing predictably and keeps the same crisp weight
    at any zoom. A geometric outward float about the model center
    (FLOAT_SCALE, ~0.1-0.2 mm) keeps a line off its own grazing surface -
    zoom-independent, unlike clip-space bias tricks.
    Vertex layout (12 floats, vec4-read safe): POSITION vec4 @0,
    NORMAL vec4 = unit segment direction @16, TEXCOORD0 vec4 = side
    (+-1) @32."""

    STRIDE = 12 * 4
    FLOAT_SCALE = 1.0012
    # cam-float (сдвиг к глазу вдоль луча зрения) переехал в stepledges.vert:
    # uniform camFloatMm, значение задаётся свойством материала в QML
    # (0.0 - видимые линии держит клип-сдвиг polyOffset, скрытые честно
    # режет буфер глубины).

    def __init__(self, parent=None):
        super().__init__(parent)
        self._raw = b''
        self._nv = 0
        self._bounds = None
        self._ref = None
        self._rebuild()

    def set_reference(self, center):
        """Model-frame point that endpoints float away from."""
        if _np is not None and center is not None:
            self._ref = _np.asarray(center, dtype=_np.float32).reshape(3)
        else:
            self._ref = None

    @property
    def vertexCount(self):
        return self._nv

    def set_segments(self, segs):
        """segs: (M,2,3) float array of endpoints, model raw frame.
        Radial FLOAT_SCALE float about _ref is baked here (static per
        upload); the view-ray cam-float lives in stepledges.vert (uniform
        eyeRaw + camFloatMm), so a camera move costs no repack of this
        buffer."""
        if _np is None or segs is None or len(segs) == 0:
            self._raw = b''
            self._nv = 0
            self._bounds = None
            self._rebuild()
            return
        a_ = _np.ascontiguousarray(_np.asarray(segs, dtype=_np.float32))
        if a_.ndim == 2:
            a_ = a_.reshape(-1, 2, 3)
        if self._ref is not None:
            a_ = self._ref[None, None, :] + \
                (a_ - self._ref[None, None, :]) * _np.float32(
                    self.FLOAT_SCALE)
        pa = a_[:, 0, :]
        pb = a_[:, 1, :]
        d = pb - pa
        ln = _np.linalg.norm(d, axis=1, keepdims=True)
        d = d / _np.where(ln == 0.0, 1.0, ln)
        # triangles (pa+, pb+, pb-) and (pa+, pb-, pa-)
        pick = _np.array([0, 1, 1, 0, 1, 0])
        sgn = _np.array([1.0, 1.0, -1.0, 1.0, -1.0, -1.0],
                        dtype=_np.float32)
        ends = _np.stack([pa, pb], axis=1)
        pts = ends[_np.arange(a_.shape[0])[:, None], pick]
        vert = _np.zeros((a_.shape[0], 6, 12), dtype=_np.float32)
        vert[:, :, 0:3] = pts
        vert[:, :, 4:7] = _np.broadcast_to(d[:, None, :],
                                          (a_.shape[0], 6, 3))
        vert[:, :, 8] = sgn[None, :]
        self._raw = vert.reshape(-1, 12).tobytes()
        self._nv = int(a_.shape[0] * 6)
        lo = _np.minimum(pa.min(axis=0), pb.min(axis=0))
        hi = _np.maximum(pa.max(axis=0), pb.max(axis=0))
        self._bounds = (float(lo[0]), float(lo[1]), float(lo[2]),
                        float(hi[0]), float(hi[1]), float(hi[2]))
        self._rebuild()

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_TRIANGLES)
        if not self._raw:
            self.setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0))
            self.update()
            return
        self.setVertexData(QByteArray(self._raw))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_NORMAL, 4 * 4, _F32)
        self.addAttribute(_TEXCOORD0, 8 * 4, _F32)
        if self._bounds is not None:
            x0, y0, z0, x1, y1, z1 = self._bounds
            pad = 1.0
            self.setBounds(QVector3D(x0 - pad, y0 - pad, z0 - pad),
                           QVector3D(x1 + pad, y1 + pad, z1 + pad))
        self.update()


@QmlElement
class GridGeometry(QQuick3DGeometry):
    """N x N grid of lines in the X/Z plane at height `level` (the floor).

    Each line is subdivided and carries a per-vertex RGBA color whose alpha
    falls smoothly to zero over the outer ``feather`` band, so the grid
    dissolves (soft-blurs) into the background at its edges.
    """

    STRIDE = 7 * 4                    # position (3f) + RGBA color (4f)
    sizeChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._size = 200.0
        self._step = 10.0
        self._level = 0.0
        self._cx = 0.0
        self._cz = 0.0
        self._feather = 0.45          # fraction of the half-size faded at the rim
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def size(self):
        return self._size

    @size.setter
    def size(self, v):
        self._size = float(v)
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def step(self):
        return self._step

    @step.setter
    def step(self, v):
        self._step = max(1.0, float(v))
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def level(self):
        return self._level

    @level.setter
    def level(self, v):
        self._level = float(v)
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def centerX(self):
        return self._cx

    @centerX.setter
    def centerX(self, v):
        self._cx = float(v)
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def centerZ(self):
        return self._cz

    @centerZ.setter
    def centerZ(self, v):
        self._cz = float(v)
        self._rebuild()

    @Property(float, notify=sizeChanged)
    def feather(self):
        return self._feather

    @feather.setter
    def feather(self, v):
        self._feather = max(0.0, min(0.95, float(v)))
        self._rebuild()

    def _alpha_at(self, dist):
        """Soft edge falloff: 1.0 in the core, 0.0 at the rim (smoothstep)."""
        f = self._feather
        if f <= 1e-6:
            return 1.0
        t = (dist - (1.0 - f)) / f
        if t <= 0.0:
            return 1.0
        if t >= 1.0:
            return 0.0
        return 1.0 - (t * t * (3.0 - 2.0 * t))

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_LINES)
        half = self._size / 2.0
        step = self._step
        n = int(half // step)
        y = self._level
        cx = self._cx
        cz = self._cz
        segs = max(8, 2 * n)          # subdivisions along each line (smooth fade)
        buf = bytearray()
        lo = [math.inf, math.inf, math.inf]
        hi = [-math.inf, -math.inf, -math.inf]

        def vertex(x, z):
            d = (max(abs(x - cx), abs(z - cz)) / half) if half > 0.0 else 0.0
            a = self._alpha_at(d)
            buf.extend(struct.pack("<7f", x, y, z, 1.0, 1.0, 1.0, a))
            if x < lo[0]:
                lo[0] = x
            if y < lo[1]:
                lo[1] = y
            if z < lo[2]:
                lo[2] = z
            if x > hi[0]:
                hi[0] = x
            if y > hi[1]:
                hi[1] = y
            if z > hi[2]:
                hi[2] = z

        for i in range(-n, n + 1):
            c = i * step
            for k in range(segs):                       # line parallel to Z
                z0 = cz - half + (2.0 * half) * k / segs
                z1 = cz - half + (2.0 * half) * (k + 1) / segs
                vertex(cx + c, z0)
                vertex(cx + c, z1)
            for k in range(segs):                       # line parallel to X
                x0 = cx - half + (2.0 * half) * k / segs
                x1 = cx - half + (2.0 * half) * (k + 1) / segs
                vertex(x0, cz + c)
                vertex(x1, cz + c)

        self.setVertexData(QByteArray(bytes(buf)))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_COLOR, 3 * 4, _F32)
        self.setBounds(QVector3D(lo[0] - 1, lo[1] - 1, lo[2] - 1),
                       QVector3D(hi[0] + 1, hi[1] + 1, hi[2] + 1))
        self.update()
        self.sizeChanged.emit()


@QmlElement
class AxisArrowGeometry(QQuick3DGeometry):
    """Flat filled axis glyph (double-sided) in the XY plane:
    a rectangular shaft from x=0 plus a triangular head with apex at x=`length`."""

    STRIDE = 3 * 4
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._length = 12.0
        self._head_len = 3.5
        self._head_w = 2.0
        self._shaft_w = 0.7
        self.setPrimitiveType(_TRIANGLES)
        self._rebuild()

    @Property(float, notify=changed)
    def length(self):
        return self._length

    @length.setter
    def length(self, v):
        self._length = float(v)
        self._rebuild()

    @Property(float, notify=changed)
    def headLength(self):
        return self._head_len

    @headLength.setter
    def headLength(self, v):
        self._head_len = float(v)
        self._rebuild()

    @Property(float, notify=changed)
    def headWidth(self):
        return self._head_w

    @headWidth.setter
    def headWidth(self, v):
        self._head_w = float(v)
        self._rebuild()

    @Property(float, notify=changed)
    def shaftWidth(self):
        return self._shaft_w

    @shaftWidth.setter
    def shaftWidth(self, v):
        self._shaft_w = float(v)
        self._rebuild()

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_TRIANGLES)
        L = self._length
        s = L - self._head_len            # shaft ends where the head begins
        if s <= 0.0:
            s = L * 0.5
        sw = self._shaft_w
        hw = self._head_w
        front = [
            (0.0, -sw, 0.0), (0.0, sw, 0.0), (s, sw, 0.0),
            (0.0, -sw, 0.0), (s, sw, 0.0), (s, -sw, 0.0),
            (s, -hw, 0.0), (s, hw, 0.0), (L, 0.0, 0.0),
        ]
        back = []
        for i in range(0, len(front), 3):
            back += [front[i + 2], front[i + 1], front[i]]
        pts = front + back
        data, lo, hi = _pack(pts)
        self.setVertexData(data)
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.setBounds(QVector3D(lo[0] - 1, lo[1] - 1, lo[2] - 1),
                       QVector3D(hi[0] + 1, hi[1] + 1, hi[2] + 1))
        self.changed.emit()


@QmlElement
class MeshGeometry(QQuick3DGeometry):
    """Triangle mesh (position + flat normals) in scene coordinates."""

    STRIDE = 6 * 4
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data = b''                     # packed float32 position + normal
        self._nv = 0                         # vertex count
        self._stride = self.STRIDE           # 24 = position (3f) + normal (3f)
        self._lo = [0.0, 0.0, 0.0]
        self._hi = [0.0, 0.0, 0.0]
        self.setPrimitiveType(_TRIANGLES)
        self._rebuild()

    def set_triangles(self, verts, normals=None):
        if _np is not None and isinstance(verts, _np.ndarray) and verts.size:
            tri = _np.ascontiguousarray(verts, dtype=_np.float32).reshape(-1, 3, 3)
            if normals is not None:
                nrm = _np.ascontiguousarray(normals, dtype=_np.float32).reshape(-1, 3, 3)
            else:
                fn = _np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
                ln = _np.linalg.norm(fn, axis=1, keepdims=True)
                _np.divide(fn, _np.where(ln == 0.0, 1.0, ln), out=fn)
                nrm = _np.broadcast_to(fn[:, None, :], tri.shape)
            inter = _np.concatenate([tri, nrm], axis=2)
            self._data = _np.ascontiguousarray(inter, dtype=_np.float32).tobytes()
            self._nv = tri.shape[0] * 3
            self._lo, self._hi = _verts_bounds(verts)
            self._rebuild()
            return
        flat = array('f')
        lo = [math.inf, math.inf, math.inf]
        hi = [-math.inf, -math.inf, -math.inf]
        tri = list(verts)
        nv = 0
        for i in range(0, len(tri) - 2, 3):
            a, b, c = tri[i], tri[i + 1], tri[i + 2]
            ux = b[0] - a[0]
            uy = b[1] - a[1]
            uz = b[2] - a[2]
            wx = c[0] - a[0]
            wy = c[1] - a[1]
            wz = c[2] - a[2]
            nx = uy * wz - uz * wy
            ny = uz * wx - ux * wz
            nz = ux * wy - uy * wx
            ln = math.sqrt(nx * nx + ny * ny + nz * nz)
            if ln > 0.0:
                nx /= ln
                ny /= ln
                nz /= ln
            for p in (a, b, c):
                flat.extend((p[0], p[1], p[2], nx, ny, nz))
                nv += 1
                for j in range(3):
                    if p[j] < lo[j]:
                        lo[j] = p[j]
                    if p[j] > hi[j]:
                        hi[j] = p[j]
        self._data = flat.tobytes()
        self._nv = nv
        self._stride = self.STRIDE
        self._lo = lo if nv else [0.0, 0.0, 0.0]
        self._hi = hi if nv else [0.0, 0.0, 0.0]
        self._rebuild()

    def reset(self):
        self._data = b''
        self._nv = 0
        self._stride = self.STRIDE
        self._rebuild()

    @Property(int, notify=changed)
    def triangleCount(self):
        return self._nv // 3

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_TRIANGLES)
        if not self._data:
            self.setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0))
            self.update()
            self.changed.emit()
            return
        self.setVertexData(QByteArray(self._data))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_NORMAL, 12, _F32)
        lo, hi = self._lo, self._hi
        self.setBounds(QVector3D(lo[0], lo[1], lo[2]), QVector3D(hi[0], hi[1], hi[2]))
        self.update()
        self.changed.emit()


@QmlElement
class ToolGeometry(QQuick3DGeometry):
    """Solid of revolution (triangle mesh) from a 2D profile [(r, y), ...].

    Built from a tool-info dict so the panel draws the actual cutter shape
    (flat/ball/bull/chamfer/drill) instead of a fixed placeholder.

    Vertices carry analytic normals: each profile segment contributes its
    outward 2D normal (dy, -dr), rotated into place by the revolution angle
    for the side bands; horizontal segments (caps) land exactly on the axis,
    so the spindle cylinder reads as a continuous gradient (and the
    screen-space edge detector does not outline every facet)."""

    STRIDE = 6 * 4
    changed = Signal()
    _SEGMENTS = 48

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data = b''
        self._nv = 0
        self._lo = [0.0, 0.0, 0.0]
        self._hi = [0.0, 0.0, 0.0]
        self.setPrimitiveType(_TRIANGLES)
        self._rebuild()

    def set_profile(self, profile, segments=None):
        seg = int(segments) if segments else self._SEGMENTS
        prof = [(max(0.0, float(r)), float(y)) for r, y in (profile or [])]
        cleaned = []
        for p in prof:                       # drop consecutive duplicate points
            if not cleaned or abs(p[0] - cleaned[-1][0]) > 1e-9 \
                    or abs(p[1] - cleaned[-1][1]) > 1e-9:
                cleaned.append(p)
        if len(cleaned) < 2 or seg < 3:
            self._data = b''
            self._nv = 0
            self._rebuild()
            return
        two_pi = 2.0 * math.pi
        cos = [math.cos(two_pi * s / seg) for s in range(seg + 1)]
        sin = [math.sin(two_pi * s / seg) for s in range(seg + 1)]
        flat = array('f')
        lo = [math.inf, math.inf, math.inf]
        hi = [-math.inf, -math.inf, -math.inf]

        def vtx(x, y, z, nx, ny, nz):
            flat.extend((x, y, z, nx, ny, nz))
            if x < lo[0]: lo[0] = x
            if y < lo[1]: lo[1] = y
            if z < lo[2]: lo[2] = z
            if x > hi[0]: hi[0] = x
            if y > hi[1]: hi[1] = y
            if z > hi[2]: hi[2] = z

        for i in range(len(cleaned) - 1):
            r0, y0 = cleaned[i]
            r1, y1 = cleaned[i + 1]
            if r0 <= 1e-9 and r1 <= 1e-9:
                continue
            ur = r1 - r0
            uy = y1 - y0
            nl = math.sqrt(ur * ur + uy * uy) or 1.0
            npr, npy = uy / nl, -ur / nl      # segment outward normal (r, y)
            for s in range(seg):
                c0, s0 = cos[s], sin[s]
                c1, s1 = cos[s + 1], sin[s + 1]
                n0 = (npr * c0, npy, npr * s0)
                n1 = (npr * c1, npy, npr * s1)
                if r0 <= 1e-9:               # fan up from the axis
                    apex = (npr * (c0 + c1) * 0.5, npy, npr * (s0 + s1) * 0.5)
                    vtx(0.0, y0, 0.0, *apex)
                    vtx(r1 * c0, y1, r1 * s0, *n1)
                    vtx(r1 * c1, y1, r1 * s1, *n1)
                elif r1 <= 1e-9:             # fan down to the axis
                    apex = (npr * (c0 + c1) * 0.5, npy, npr * (s0 + s1) * 0.5)
                    vtx(r0 * c0, y0, r0 * s0, *n0)
                    vtx(r0 * c1, y0, r0 * s1, *n1)
                    vtx(0.0, y1, 0.0, *apex)
                else:                        # quad -> two triangles
                    vtx(r0 * c0, y0, r0 * s0, *n0)
                    vtx(r0 * c1, y0, r0 * s1, *n1)
                    vtx(r1 * c1, y1, r1 * s1, *n1)
                    vtx(r0 * c0, y0, r0 * s0, *n0)
                    vtx(r1 * c1, y1, r1 * s1, *n1)
                    vtx(r1 * c0, y1, r1 * s0, *n0)
        self._data = flat.tobytes()
        self._nv = len(flat) // 6
        self._lo = [lo[0], lo[1], lo[2]] if self._nv else [0.0, 0.0, 0.0]
        self._hi = [hi[0], hi[1], hi[2]] if self._nv else [0.0, 0.0, 0.0]
        self._rebuild()

    @Property(int, notify=changed)
    def triangleCount(self):
        return self._nv // 3

    def _rebuild(self):
        self.clear()
        self.setPrimitiveType(_TRIANGLES)
        if not self._data:
            self.setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0))
            self.update()
            self.changed.emit()
            return
        self.setVertexData(QByteArray(self._data))
        self.setStride(self.STRIDE)
        self.addAttribute(_POSITION, 0, _F32)
        self.addAttribute(_NORMAL, 12, _F32)
        lo, hi = self._lo, self._hi
        self.setBounds(QVector3D(lo[0], lo[1], lo[2]), QVector3D(hi[0], hi[1], hi[2]))
        self.update()
        self.changed.emit()




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
        raw, nv, bounds = _pack_edge_segments(_rings_segments(_profile_ring_points(hprof)))
        self._holder_rings_geo.set_packed(raw, bounds, nv)
        self._tool = [0.0, 0.0, 0.0]
        self._tool_info = {}                # parsed UI_TOOL metadata
        self._tool_length = 0.0             # cutter length (tip -> top, scene units)
        self._tool_radius = 0.0             # cutter max radius (for the outline width)
        self._holder_radius = max((r for r, _ in hprof), default=1.0)
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
        self._tool_length = max((p[1] for p in prof), default=0.0) if prof else 0.0
        self._tool_geo.set_profile(prof)
        # Silhouette outline (inverted hull): the profile dilated outward by a
        # uniform `pad`, rendered by the QML side with the near faces culled so
        # only the outer silhouette rim remains.
        maxr = max((p[0] for p in prof), default=0.0) if prof else 0.0
        self._tool_radius = maxr
        pad = max(0.12, 0.05 * maxr)
        self._tool_outline_geo.set_profile(
            _tool_outline_profile(prof, pad) if prof else None)
        raw, nv, bounds = _pack_edge_segments(
            _rings_segments(_profile_ring_points(prof)) if prof else [])
        self._tool_rings_geo.set_packed(raw, bounds, nv)
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
            print("[GcodeToolpath] tool check failed:", exc)
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
            print("[GcodeToolpath] collision scan failed:", exc)
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
        list); None when numpy is absent (caller keeps flat normals)."""
        if _np is None or verts is None or len(verts) < 3:
            return None
        try:
            return _smooth_face_normals(_np.asarray(verts, dtype=_np.float64)
                                        .reshape(-1, 3, 3))
        except Exception:
            return None

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
                if _np is not None:
                    verts = _orient_shell_outward(verts).reshape(-1, 3)
                self._mesh_topo = _build_silhouette_topology(verts)
                return verts, None, self._stl_normals(verts)
            except Exception as exc:
                print("[GcodeToolpath] STL load failed:", exc)
                return None, None, None
        if low.endswith((".step", ".stp")):
            ck = _cache_key(model_path, "model") if _cache_enabled() else None
            hit = _cache_read_model(ck) if ck else None
            if hit is not None:
                if self._rev_surfaces is None:
                    try:
                        shape = _step_shape(model_path)
                    except Exception:
                        shape = None
                    self._rev_surfaces = load_step_surfaces(model_path,
                                                            shape=shape)
                    self._tan_fams = _tangent_families(self._rev_surfaces,
                                                       hit[1],
                                                       (hit[0], hit[2]))
                    # топология меша для силуэта: строится вместе с моделью
                    # (~137 мс на Test_model, ~45k треугольников) и живёт до
                    # смены модели; на камере силуэт считается по ней
                    self._mesh_topo = _build_silhouette_topology(hit[0])
                return hit
            try:
                shape = _step_shape(model_path)
                verts, segments, normals = load_step_model(model_path,
                                                           shape=shape)
                self._rev_surfaces = load_step_surfaces(model_path, shape=shape)
                packed = _pack_edge_segments(segments)
                self._tan_fams = _tangent_families(self._rev_surfaces, packed,
                                                   (verts, normals))
                self._mesh_topo = _build_silhouette_topology(verts)
                if ck and _np is not None and normals is not None:
                    _cache_write_model(ck, verts, normals, packed)
                return verts, packed, normals
            except Exception as exc:
                print("[GcodeToolpath] STEP load failed:", exc)
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
                        if _np is not None:
                            sv = _orient_shell_outward(sv).reshape(-1, 3)
                        self._mesh_topo = _build_silhouette_topology(sv)
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
                    self._tan_fams = _tangent_families(self._rev_surfaces,
                                                       self._step_edges)
                segments = _tangent_from_families(self._tan_fams, eye_raw)
            else:
                if self._mesh_topo is None:
                    return                  # модель без меша: силуэта нет
                segments = _mesh_silhouette_segments(self._mesh_topo, eye_raw)
            has = segments is not None and len(segments) > 0
            if _np is not None and has:
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
                lo, hi = _verts_bounds(verts)
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
        raw, nv, bounds = packed
        g = ToolpathGeometry()
        QQmlEngine.setObjectOwnership(g, QQmlEngine.CppOwnership)
        g.set_packed(raw, bounds, nv)
        self._edges = g
        self._sync_hlr_reference()
        self._hlr_edges.set_segments(packed_positions(packed))

    def _sync_hlr_reference(self):
        """Share the model raw-frame AABB center with both line layers."""
        if _np is None or self._mesh_lo is None or self._mesh_hi is None:
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
            print("[outline] source=STEP segments=%d" % (packed[1] // 2))
        else:
            ck = _cache_key(model_path, "edges") if (model_path and _cache_enabled()) else None
            packed = _cache_read_edges(ck) if ck else None
            if packed is None:
                try:
                    packed = _feature_edges(verts, 20.0)
                except Exception:
                    packed = (b'', 0, None)
                if ck:
                    _cache_write_edges(ck, packed)
            print("[outline] source=MESH angle=20deg segments=%d" % (packed[1] // 2))
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
        if _np is not None and isinstance(cum, _np.ndarray):
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
            print("[GcodeToolpath] cannot read '%s': %s" % (filepath, exc))
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
            print("[GcodeToolpath] cannot read '%s': %s" % (filepath, exc))
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
                ordered, _last, _seg, stages = parse_gcode(text)
                data = _prepare_ordered(ordered, stages)
                if ck:
                    _cache_write_path(ck, data)
        except Exception as exc:                       # never take the app down
            print("[GcodeToolpath] parse failed:", exc)
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
        self.toolpathLoaded.emit(data["seg_count"])

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
        self._stage_tools = [s.get("tool", {}) for s in data.get("stages", [])]
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

        self._set_tool(*data["last"])
        self._sample_pts = data["pts"]
        self._sample_cum = data["cum"]
        self._sample_rapid = data["rapid"]
        self._sample_speeds = data["speeds"]
        self._stages = data.get("stages", [])
        self._move_length = data["total"]
        self._rapid.set_packed(data["rapid_bytes"], data["bounds"], data["rapid_nv"])
        self._feed.set_packed(data["feed_bytes"], data["bounds"], data["feed_nv"])
        self._center = list(data["center"])
        self._radius = data["radius"]
        self._min_y = data["min_y"]
        self.pathChanged.emit()
        self._start_collision_scan()      # background holder-vs-stock check
        self._rebuild_tool_check()        # NC-vs-machine tool check (skeleton)

    @Slot(str, result=int)
    def load_string(self, text):
        self._set_model_path("")         # string input has no file to pair with
        ordered, _last, seg_count, stages = parse_gcode(text)
        data = _prepare_ordered(ordered, stages)
        self._apply_toolpath_data(data, text)
        self._mesh.reset()               # string input -> no part model
        self._clear_edges()
        self.meshChanged.emit()
        return seg_count

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


# ==========================================================================
#  Standalone smoke-test
# ==========================================================================
if __name__ == "__main__":
    import sys

    from PySide6.QtGui import QGuiApplication

    DEMO = "G0 Z10\nG0 X0 Y0\nG1 Z-2 F500\nG1 X50 Y0 F1500\nG1 X50 Y50\nG1 X0 Y50\nG1 X0 Y0\nG0 Z10\n"
    app = QGuiApplication(sys.argv)  # noqa: F841
    tp = GcodeToolpath()
    n = tp.load_string(DEMO)
    print("segments :", n)
    print("rapid vtx:", tp.rapidGeometry.vertexCount)
    print("feed vtx :", tp.feedGeometry.vertexCount)
    print("tool     :", tp.toolPosition)
