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
from outline import (_FEATURE_SIMPLIFY_MM, _rdp_indices, _simplify_edge_chains, _feature_edges, _feature_edges_np)  # noqa: F401
# ---- геометрии и контроллер (этап 0 переноса на C++) ----------------------
# QML-тип "MockBackend" регистрирует controller.py (QML_IMPORT_* там же, где класс);
# импорт этой строки и запускает регистрацию.
from collision import (_COLLISION_STEP)                                  # noqa: F401
from geometry import (ToolpathGeometry, HLRLineGeometry, GridGeometry,   # noqa: F401
                      AxisArrowGeometry, MeshGeometry, ToolGeometry, _pack)
from controller import GcodeToolpath                                     # noqa: F401






# ---- слои аналитики и периферии (этап 0 переноса на C++) ----------
from gv_common import _to_3d                                     # noqa: F401
from path_prep import (_scene_bounds_lo_hi, _pack_stream_np, _stages_norm, _prepare_ordered)  # noqa: F401
from tooling import (_HOLDER_NUT_D, _HOLDER_NUT_H, _HOLDER_BODY_D, _HOLDER_BODY_H)  # noqa: F401
from tooling import (_tool_profile, _tool_outline_profile, _tool_exposed_length, _holder_profile, _holder_solid_profile, _holder_outline_profile, _rings_segments, _profile_ring_points)  # noqa: F401
from collision import (_circle_rect_hit, _scan_collisions, _tool_check_report)  # noqa: F401

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
