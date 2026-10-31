# QML plugin "CNC.Edges" - StepEdges: B-Rep model edges as Lines geometry.
# Next to it a pure gvcore core is built (G-code parser, UI_* tags, crease-aware
# normals) and its probe gcode_probe - they are not part of the plugin and need no Qt.
# There also lies the STEP and normals layer gvstep (stage 4a of the port plan) with its probe
# step_probe: they read OCCT directly, Qt is not needed for them. STL import and mesh outline
# (stage 4b) live in the gvcore core, their probe is stl_probe. Silhouette topology
# (stage 4c) is in gvcore too, its probe silhouette_probe takes the soup from STEP through
# gvstep. Analytical surfaces of revolution for STEP live in gvstep
# (occt/step_surfaces), tangent families - in gvcore (core/tangents), their
# common probe is tangents_probe (stage 4d).
# The probes verify against the oracle themselves: the key --manifest <oracle_manifest.json>
# computes digests of their arrays and compares them with the manifest
# (cpp/manifest_check.h, no interpreter). There is no Python layer in the repository.
#
# Build (the script does the same):
#   powershell -ExecutionPolicy Bypass -File scripts/build-viewer.ps1
# Manually:
#   call <VS2022>\VC\Auxiliary\Build\vcvars64.bat
#   cmake -S modules/gcode-viewer/cpp -B build/edges -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_PREFIX_PATH="<Qt6>/msvc2022_64;<OCCT>/Library"
#   cmake --build build/edges
#
# The module is placed into build/edges/CNC/Edges/: qmldir, cncedgesplugin.dll (the backing
# library with the types and the algorithm) and cncedgespluginplugin.dll (the entry point,
# QML loads it). The import path for the consumer is build/edges; the probe
# stepledges_probe.exe lies there too:
#   QML:      import CNC.Edges 1.0
# OCCT libraries are linked by the imported targets TK* from the config package, and if they
# are absent in the package - by the variables OpenCASCADE_INCLUDE_DIR/OpenCASCADE_LIBRARY_DIR.
#
# The same build also builds the C++ viewer controller (stage 5.4a): the QML module
# MockBackend 1.0 in <repo>/build/qmlbackend/MockBackend (the type GcodeToolpath and the six
# geometries of stage 3, target gvqmlbackend). This is the only viewer backend; live
# it is checked by the probes viewer/keyprobe and viewer/stock_bind_probe.
#   QML:      import MockBackend 1.0
#
# The same build builds the C++ viewer entry point: <repo>/build/viewer/
# gcodeviewer.exe, the source is viewer/main.cpp. It loads GcodeViewer3D.qml into
# QQuickView and works only with the MockBackend plugin: there is no Python backend
# either at runtime or in the repository. Launch - with Qt in PATH (the exe links with Qt),
# the plugin directory and the OCCT DLLs the entry point adds to itself.
#   gcodeviewer.exe <NC program.ngc>                 offscreen -> "loaded OK", segments
#   gcodeviewer.exe --smoke                          start check
#   gcodeviewer.exe --frame f.png <NC program.ngc>   frame to PNG

cmake_minimum_required(VERSION 3.16)
project(cnc_edges_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Linux: objects of the static gvcore, gvqtgeom and gvstep end up in SHARED libraries
# (plugins): without PIC the link ends with the GCC refusal "recompile with -fPIC" (with MSVC
# there is no such flag, there the property changes nothing).
#
# About rpath keys there are none: by default CMake puts the directories of linked
# libraries into the build rpath, therefore Qt and OCCT lying outside the build directories
# are found without LD_LIBRARY_PATH - and the entry point (viewer/main.cpp, prependPath) does
# not touch PATH on Linux. This rpath is not carried into the install tree, but the targets have no install.
if (UNIX AND NOT APPLE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Directory of the QML module MockBackend (C++ viewer controller, stage 5.4a of the port
# plan). The module directory lies at the predictable path <repo>/build/qmlbackend/
# MockBackend, the import path is <repo>/build/qmlbackend: by the same path it is found by the
# viewer entry points and the probes that do not know about the CMake generator.
set(GV_QMLBACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/qmlbackend"
    CACHE PATH "Каталог, куда кладётся QML-модуль MockBackend")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D)
find_package(OpenCASCADE REQUIRED)

# Pure gvcore core (stages 2 and 4 of the port plan): G-code parser, UI_* tags,
# crease-aware skin normals (stage 4a), STL import and mesh outline (stage 4b),
# silhouette topology (stage 4c), tangent families (stage 4d), tool with
# holder and collisions (stage 5.4c). Neither Qt nor OCCT is needed - only the standard
# library. The target is separate from the plugin so that the core can be built and verified
# against the oracle without Qt.
#
# /fp:precise is mandatory: the core is verified against reference digests byte by byte, and
# /fp:fast allows reassociation and contraction of multiplication with addition (FMA), which
# shifts the result.
add_library(gvcore STATIC
    core/collision.cpp
    core/gcode_parser.cpp
    core/normals.cpp
    core/outline.cpp
    core/packing.cpp
    core/path_prep.cpp
    core/silhouette.cpp
    core/stock_sim.cpp
    core/stl_import.cpp
    core/tangents.cpp
    core/text_util.cpp
    core/tooling.cpp
    core/ui_tags.cpp
)
target_include_directories(gvcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (MSVC)
    target_compile_options(gvcore PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvcore PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Core probe: a separate target, not part of the plugin. It prints points, flags,
# stages and sha256 of the same serializations as the oracle, and writes the same arrays
# as a dump. The --manifest/--check mode verifies the digests path.*, pd.* and tag.* against
# oracle_manifest.json without an interpreter.
add_executable(gcode_probe gcode_probe.cpp)
set_target_properties(gcode_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(gcode_probe PRIVATE /utf-8)
endif()
target_link_libraries(gcode_probe PRIVATE gvcore)

# Version probe: verifies modules/gcode-viewer/VERSION, the field version of the project
# gcode-viewer in projects.json and the property viewerVersion in GcodeViewer3D.qml -
# a forgotten edit of one of the three is invisible both to the build and to the run. Neither Qt
# nor OCCT is needed for it: only the standard library.
add_executable(version_probe version_probe.cpp)
set_target_properties(version_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(version_probe PRIVATE /utf-8)
endif()
# The gv::manifest::repoRoot header (the common repository root search of the probes)
# lies in cpp/, and gvcore does not pull this target: the search path is set here.
target_include_directories(version_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Qt Quick 3D geometries (stage 3 of the port plan) with the same buffer formats
# (stride 20 and 48 - the contract of QML and the shaders toolpath.*/stepledges.*). The target
# is separate from the plugin: the geometries are verified against the oracle digests byte by byte,
# and stage 5 connects them into the QML module.
#
# /fp:precise is mandatory just like for gvcore: part of the geometry numbers (mesh
# normals, contour strips) were computed in float32, and reassociation with FMA would shift
# the low bits of the buffer.
add_library(gvqtgeom STATIC qt/gv_geometry.cpp)
target_include_directories(gvqtgeom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvqtgeom PUBLIC Qt6::Core Qt6::Gui Qt6::Quick3D)
if (MSVC)
    target_compile_options(gvqtgeom PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvqtgeom PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Geometry probe: a separate target, not part of the plugin. It builds each geometry
# on input data it writes itself, prints the state and sha256 of the buffer and
# writes the buffers as a dump.
#
# The --manifest/--check mode builds the input itself (gvcore: parseGcode + prepareOrdered,
# toolProfile) and verifies the buffer digests against oracle_manifest.json without an
# interpreter, therefore gvcore is needed: the trajectory is taken from the NC program by the same
# path as at runtime.
add_executable(geometry_probe geometry_probe.cpp)
set_target_properties(geometry_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(geometry_probe PRIVATE /utf-8)
endif()
target_link_libraries(geometry_probe PRIVATE gvqtgeom gvcore Qt6::Core Qt6::Gui Qt6::Quick3D)

# STEP, crease-aware normals and analytical surfaces (stages 4a and 4d of the port
# plan) on the OCCT SDK directly (normals and tangent families lie in
# gvcore - they do not depend on Qt and OCCT). The target is separate from the plugin: the layer
# is verified against the oracle digests bitwise, and stage 5 connects it into the backend.
# Qt is not pulled here: the STEP layer reads OCCT, it needs nothing else.
#
# /fp:precise is mandatory just like for gvcore and gvqtgeom: both the vertex soup and
# the normals are verified against reference digests bit by bit, and /fp:fast allows
# reassociation and contraction of multiplication with addition (FMA), which shifts the low
# bits.
add_library(gvstep STATIC occt/step_import.cpp occt/step_surfaces.cpp
                            occt/step_edges.cpp occt/step_bundle.cpp)
target_include_directories(gvstep PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvstep PUBLIC gvcore)
if (MSVC)
    target_compile_options(gvstep PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvstep PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# OpenCASCADE: TKMesh - tessellation (BRepMesh_IncrementalMesh, Poly_Triangulation),
# TKDESTEP/TKXSBase - STEP reading, TKBRep/TKTopAlgo - topology traversal, TKG3d -
# GCPnts_UniformDeflection (edge polylines in occt/step_edges).
set(OCC_STEP_NEEDED TKernel TKMath TKG3d TKG2d TKGeomBase TKTopAlgo TKBRep
                    TKMesh TKDESTEP TKXSBase)
foreach(lib IN LISTS OCC_STEP_NEEDED)
    if (TARGET OpenCASCADE::${lib})
        target_link_libraries(gvstep PRIVATE OpenCASCADE::${lib})
    else()
        target_link_libraries(gvstep PRIVATE ${lib})
    endif()
endforeach()
target_include_directories(gvstep PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# STEP and normals probe: a separate target, not part of the plugin. It prints the number
# of vertices and triangles and sha256 of vertices, triangles and normals in the same
# serializations as the oracle, and writes the same arrays as a dump; the --manifest mode
# verifies the digests step.* against oracle_manifest.json.
add_executable(step_probe step_probe.cpp)
set_target_properties(step_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(step_probe PRIVATE /utf-8)
endif()
target_link_libraries(step_probe PRIVATE gvstep)
# The layer header includes TopoDS_Shape.hxx: the OCCT includes of gvstep are private.
target_include_directories(step_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# STL and mesh outline probe (stage 4b of the port plan): a separate target, not part of
# the plugin. It reads STL (binary or ASCII) into a triangle soup, computes edges
# by the dihedral angle with chain merging, prints the number of vertices and segments and
# sha256 of the soup and edges in the same serializations as the oracle, and writes the same arrays
# as a dump; the --manifest mode verifies the digests stl.* against oracle_manifest.json. Neither Qt
# nor OCCT: the layer lives in gvcore.
add_executable(stl_probe stl_probe.cpp)
set_target_properties(stl_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stl_probe PRIVATE /utf-8)
endif()
target_link_libraries(stl_probe PRIVATE gvcore)

# Stock remainder height field probe (core/stock_sim): a separate target, it is not part
# of the plugin and needs neither Qt nor OCCT. It verifies the field against the analytics
# computed in the probe itself, and measures the cost of a pass and of the whole Test_model.ngc at
# grid node 0.5 and 0.2 mm. It prints "case <name>", the lines "phase <name> <ms>" and
# "RESULT: OK" / "RESULT: FAIL (n)"; it does not touch frame measurement and the oracle.
#   stock_sim_probe.exe [file.ngc]
add_executable(stock_sim_probe stock_sim_probe.cpp)
set_target_properties(stock_sim_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stock_sim_probe PRIVATE /utf-8)
endif()
target_link_libraries(stock_sim_probe PRIVATE gvcore)

# Silhouette topology and silhouette on fixed eyes probe (stage 4c of the port
# plan): a separate target, not part of the plugin. It takes the vertex soup from STEP by the same
# gvstep path as at runtime, builds the topology and computes
# the silhouette on five eyes. It prints the number of vertices, triangles and candidate
# edges and sha256 of the topology and silhouette arrays in the same serializations as
# the oracle, and writes the same arrays as a dump; the --manifest mode verifies the digests
# silhouette.* against oracle_manifest.json. The topology lives in gvcore, therefore
# the target needs only the STEP layer: the probe needs OCCT DLLs, Qt DLLs - not.
add_executable(silhouette_probe silhouette_probe.cpp)
set_target_properties(silhouette_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(silhouette_probe PRIVATE /utf-8)
endif()
target_link_libraries(silhouette_probe PRIVATE gvstep)
# The layer header includes TopoDS_Shape.hxx: the OCCT includes of gvstep are private.
target_include_directories(silhouette_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# Analytical STEP surfaces and tangent families probe (stage 4d of the port
# plan): a separate target, not part of the plugin. It takes the vertex soup and the surfaces
# of revolution from STEP by the same gvstep path as at runtime, builds the packed STEP edges
# itself from the same shape by the same path as at runtime (occt/step_edges +
# core/packing), and builds the tangent families (the gvcore core), then computes
# segments on five fixed eyes. It prints numbers and sha256 of the arrays in the same
# serializations as the oracle, and writes the same arrays as a dump. The families live in
# gvcore, therefore the target needs only the STEP layer: the probe needs OCCT DLLs, Qt DLLs -
# not.
#
# There is also the --manifest/--check mode: it verifies 22 entries tangents.* of the manifest against
# its own edge buffer. Caveat: its own buffer does not match the digest
# packed_edges.f32 of the manifest byte for byte - the set of 2426 segments is the same, but 64
# segments stand at a foreign position, and 16 diverge in the low bits (maximum
# 1.8e-15 mm at values of the order of 5.7e-14); sha256 4eff01c2... against 926b3836...
# The analysis of the numbers goes in cpp/tangents_probe.cpp.
add_executable(tangents_probe tangents_probe.cpp)
set_target_properties(tangents_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(tangents_probe PRIVATE /utf-8)
endif()
target_link_libraries(tangents_probe PRIVATE gvstep)
# The layer headers include TopoDS_Shape.hxx: the OCCT includes of gvstep are private.
target_include_directories(tangents_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# Benchmark probe (part of neither the plugin nor the core): the phases of cold loading
# of the STEP model and of the NC program step by step and the frame cost (silhouette and setting HLR buffers).
# It prints the lines "phase <name> <ms>" and "frame <name> <ms>". It needs OCCT (the STEP
# phases) and Qt (setting the HLRLineGeometry buffers - target gvqtgeom). Launch - with the DLLs of
# OCCT and Qt in PATH:
#   bench_probe.exe <file.step> <file.ngc> [phase repeats] [frame repeats]
add_executable(bench_probe bench_probe.cpp)
set_target_properties(bench_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(bench_probe PRIVATE /utf-8 /fp:precise)
endif()
target_link_libraries(bench_probe PRIVATE gvstep gvqtgeom Qt6::Core Qt6::Gui Qt6::Quick3D)
target_include_directories(bench_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

qt_standard_project_setup()
if(COMMAND qt_policy)
    # The QML module resource prefix ':/qt/qml/' is the default for new projects.
    qt_policy(SET QTP0001 NEW)
endif()

# The module is split following the Qt pattern: the backing library cncedgesplugin (SHARED) carries
# the types and the algorithm, the plugin cncedgespluginplugin (MODULE) - the entry point that QML
# loads by the line "plugin cncedgespluginplugin" in qmldir. The backing library
# is linked into the probe; CMake does not allow linking a MODULE library.
qt_add_library(cncedgesplugin SHARED)

set_target_properties(cncedgesplugin PROPERTIES
    QT_QML_GENERATE_QMLLS_INI OFF
    # The backing library lies next to the plugin: otherwise QML will not find it at load time.
    # Linux: .so is the LIBRARY artifact, therefore the same directory is set for it too;
    # the plugin entry point (cncedgespluginplugin) is placed into CNC/Edges by
    # qt_add_qml_module (OUTPUT_DIRECTORY) itself.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    # Windows: the class StepEdgesGeometry is needed by the probe, and by default the DLL exports nothing.
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

qt_add_qml_module(cncedgesplugin
    URI CNC.Edges
    VERSION 1.0
    # The module directory repeats the URI: <build>/CNC/Edges/. The consumer import path is <build>.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    SOURCES
        stepledgesgeometry.h
        stepledgesgeometry.cpp
)

target_compile_definitions(cncedgesplugin PRIVATE OCCT_HANDLE_NOCAST)
if (MSVC)
    target_compile_options(cncedgesplugin PRIVATE /utf-8 /MP)
endif()

# OpenCASCADE: first the imported targets (TKxxx::), otherwise the classic variables
# from the old config package.
set(OCC_NEEDED TKernel TKMath TKTopAlgo TKGeomBase TKBRep TKDESTEP TKXSBase)
foreach(lib IN LISTS OCC_NEEDED)
    if (TARGET OpenCASCADE::${lib})
        target_link_libraries(cncedgesplugin PRIVATE OpenCASCADE::${lib})
    else()
        target_link_libraries(cncedgesplugin PRIVATE ${lib})
    endif()
endforeach()
target_include_directories(cncedgesplugin PRIVATE ${OpenCASCADE_INCLUDE_DIR})

target_link_libraries(cncedgesplugin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D)
# Edge selection and merging lie in gvstep (cpp/occt/step_edges): one edition for
# the plugin and the controller.
target_link_libraries(cncedgesplugin PRIVATE gvstep)
target_include_directories(cncedgesplugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# STEP edges probe: a separate target, not part of the plugin.
# It reads the geometry through the plugin itself (setSource -> setShape), prints the number
# of segments and sha256 of the buffer; the key --qml additionally checks that the QML engine
# opens the module CNC.Edges and creates the element StepEdges.
add_executable(stepledges_probe stepledges_probe.cpp)
set_target_properties(stepledges_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stepledges_probe PRIVATE /utf-8 /MP)
endif()
target_link_libraries(stepledges_probe PRIVATE cncedgesplugin Qt6::Core Qt6::Qml Qt6::Quick3D)
# The probe includes the plugin header stepledgesgeometry.h, and its inlines call
# Standard::Free from TKernel. The plugin links OCCT PRIVATE and does not give these symbols
# outwards: on Windows the link passed, on Linux ld answered "DSO missing from
# command line" and required OCCT directly in the probe. The probe takes the same set
# OCCT_NEEDED as the plugin above.
foreach(lib IN LISTS OCC_NEEDED)
    if (TARGET OpenCASCADE::${lib})
        target_link_libraries(stepledges_probe PRIVATE OpenCASCADE::${lib})
    else()
        target_link_libraries(stepledges_probe PRIVATE ${lib})
    endif()
endforeach()
target_include_directories(stepledges_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# C++ viewer controller (stage 5.4a of the port plan): the QML module MockBackend 1.0 with
# the type GcodeToolpath and the six geometries of stage 3. There also lie the two stock
# remainder drawing geometries (qmlbackend/gv_stockgeom): the top of the height field and
# the box shell without the top face; they are not part of the six-item verification set.
# This is the only viewer backend: the entry points load only it, there is no Python
# registration of the same types.
#
# The module, like CNC.Edges, is split following the Qt pattern: the backing library
# gvqmlbackend.dll carries the types and the logic (the entry point loads it), the entry point
# gvqmlbackendplugin.dll - what QML opens by the line "plugin ..." in qmldir.
# The module directory lies not in the build directory, but at the predictable path
# <repo>/build/qmlbackend/MockBackend: by the same path it is found by the viewer
# entry points that do not know about the CMake generator and configuration.
#
# /fp:precise is mandatory just like for gvcore and gvqtgeom: the trajectory buffers
# are verified against reference digests byte by byte, and /fp:fast allows
# reassociation and contraction of multiplication with addition (FMA), which shifts the low bits.
qt_add_library(gvqmlbackend SHARED)
set_target_properties(gvqmlbackend PROPERTIES
    QT_QML_GENERATE_QMLLS_INI OFF
    # The backing library lies next to the entry point: otherwise QML will not find it.
    # Linux: the same directory is set for the LIBRARY artifact (.so) too, see cncedgesplugin.
    RUNTIME_OUTPUT_DIRECTORY ${GV_QMLBACKEND_DIR}/MockBackend
    LIBRARY_OUTPUT_DIRECTORY ${GV_QMLBACKEND_DIR}/MockBackend
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${GV_QMLBACKEND_DIR}/MockBackend
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${GV_QMLBACKEND_DIR}/MockBackend
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_QMLBACKEND_DIR}/MockBackend
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${GV_QMLBACKEND_DIR}/MockBackend
)
qt_add_qml_module(gvqmlbackend
    URI MockBackend
    VERSION 1.0
    # The module directory repeats the URI: <build>/MockBackend. The import path is <build>.
    OUTPUT_DIRECTORY ${GV_QMLBACKEND_DIR}/MockBackend
    SOURCES
        qmlbackend/gv_qmlgeom.h
        qmlbackend/gv_qmlgeom.cpp
        qmlbackend/gv_stockgeom.h
        qmlbackend/gv_stockgeom.cpp
        qmlbackend/gv_toolpath.h
        qmlbackend/gv_toolpath.cpp
)
target_include_directories(gvqmlbackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# The generated qt_add_qml_module file of type registration includes the headers
# by file name (<gv_qmlgeom.h>, <gv_toolpath.h>): their directory must be in the
# search paths, otherwise registration is silently skipped (__has_include).
target_include_directories(gvqmlbackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qmlbackend)
# The core (NC program parsing, trajectory preparation, STL, outline, silhouette and tangents),
# the stage 3 geometries (line buffers) and the STEP layer (model reading together with the B-Rep
# outline and surfaces: stage 5.4b). gvstep pulls the OCCT libraries.
target_link_libraries(gvqmlbackend PRIVATE
    gvcore gvstep gvqtgeom Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D)
# The STEP layer headers include TopoDS_Shape.hxx (orientShellOutward): the OCCT includes
# of gvstep are private, therefore the search path is set here (as for step_probe).
target_include_directories(gvqmlbackend PRIVATE ${OpenCASCADE_INCLUDE_DIR})
if (MSVC)
    target_compile_options(gvqmlbackend PRIVATE /utf-8 /MP /fp:precise)
else()
    target_compile_options(gvqmlbackend PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# C++ viewer entry point: <repo>/build/viewer/gcodeviewer.exe loads
# GcodeViewer3D.qml into QQuickView and works only with the MockBackend plugin -
# there is no Python backend either in the execution path or in the repository. The directory
# is moved into a separate variable for the same reason as with the bridge: the path is known
# by the watcher and the build, not by the CMake generator.
#
# The repository root and the viewer file are written into the target in case the exe is launched from
# another directory: by default the entry point derives them from its own path
# (<repo>/build/viewer -> <repo>).
get_filename_component(GV_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(GV_VIEWER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/viewer"
    CACHE PATH "Каталог, куда кладётся gcodeviewer.exe")

add_executable(gcodeviewer WIN32 viewer/main.cpp)
set_target_properties(gcodeviewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${GV_VIEWER_DIR}
    # The Q_OBJECT class lives in main.cpp: moc is built automatically.
    AUTOMOC ON
)
# Qt6::Quick is needed for QQuickView (the window for the Item root of the viewer); QtQuick3D the engine
# finds as a QML module of Qt, it requires no separate linking.
# gvcore is needed: gv::presetFromEnv from core/quality.h calls gv::strtodC, and it
# lives in core/text_util.cpp. Without linking the entry point does not build (LNK2019).
target_link_libraries(gcodeviewer PRIVATE gvcore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
# core/quality.h reads the level with the same parsing as the core (gv::presetFromEnv),
# therefore the cpp/ directory must be in the header search paths.
target_include_directories(gcodeviewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(gcodeviewer PRIVATE
    GV_REPO_ROOT="${GV_REPO_ROOT}"
    GV_VIEWER_QML="${CMAKE_CURRENT_SOURCE_DIR}/../GcodeViewer3D.qml")
if (MSVC)
    target_compile_options(gcodeviewer PRIVATE /utf-8)
endif()

# Hot key probe (viewer/keyprobe.cpp): it loads the same GcodeViewer3D.qml into
# QQuickView and feeds keys with the signs of a real event (scan code, VK and the character
# by layout), checking that each hot key switched its own property. With it
# they measure the defect "hot keys are language-sensitive": in the Russian layout
# the letter branches must fire by scan code. The platform is offscreen, the window is not
# shown, therefore the probe is suitable for the watcher too.
#   keyprobe.exe <NC program.ngc> [ru,en]
add_executable(keyprobe viewer/keyprobe.cpp)
set_target_properties(keyprobe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${GV_VIEWER_DIR}
)
target_link_libraries(keyprobe PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(keyprobe PRIVATE
    GV_REPO_ROOT="${GV_REPO_ROOT}"
    GV_VIEWER_QML="${CMAKE_CURRENT_SOURCE_DIR}/../GcodeViewer3D.qml"
)
if (MSVC)
    target_compile_options(keyprobe PRIVATE /utf-8)
endif()

# Material removal to trajectory binding probe (gv_toolpath): it creates a real
# GcodeToolpath from the QML module MockBackend and loads a real NC program. It prints the time
# of the background pass at the presets med and high, verifies the drawing texture against the field and
# the cancellation delay. The offscreen platform is set by itself. The CMake target
# needs Qt (the QML engine), the controller itself comes from the built plugin, as for
# the viewer entry point; Qt6::Quick3D is needed for the type QQuick3DTextureData - with it
# the published field is verified.
add_executable(stock_bind_probe viewer/stock_bind_probe.cpp)
set_target_properties(stock_bind_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${GV_VIEWER_DIR}
)
target_link_libraries(stock_bind_probe PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D)
target_compile_definitions(stock_bind_probe PRIVATE GV_REPO_ROOT="${GV_REPO_ROOT}")
if (MSVC)
    target_compile_options(stock_bind_probe PRIVATE /utf-8)
endif()

