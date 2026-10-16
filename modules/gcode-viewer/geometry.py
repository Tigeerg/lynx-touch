"""Геометрии Qt Quick 3D: траектория, рёбра, сетка, оси, меш, инструмент.

Вынесено из mock_backend.py (этап 0 плана переноса на C++). Буферы собираются вне
GUI-потока и лишь устанавливаются сюда (set_packed), поэтому формат вершин - часть
контракта с шейдерами: toolpath.* - stride 20, stepledges.* - stride 48.
"""
import math
import struct
from array import array

from PySide6.QtCore import Property, QByteArray, QObject, QTimer, Signal, Slot
from PySide6.QtGui import QQuaternion, QVector3D
from PySide6.QtQml import QmlElement
from PySide6.QtQuick3D import QQuick3DGeometry

from gv_common import (_np, QML_IMPORT_NAME, QML_IMPORT_MAJOR_VERSION,
                       QML_IMPORT_MINOR_VERSION)
from stl_import import _verts_bounds

np = _np          # код слоя читает numpy как np


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
        if center is not None:
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
        if segs is None or len(segs) == 0:
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
        if isinstance(verts, _np.ndarray) and verts.size:
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




