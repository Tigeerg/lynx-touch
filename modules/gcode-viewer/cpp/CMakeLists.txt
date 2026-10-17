# QML-плагин "CNC.Edges" — StepEdges: рёбра B-Rep модели как Lines-геометрия.
# Рядом собирается чистое ядро gvcore (парсер G-кода, теги UI_*, crease-aware
# нормали) и его пробник gcode_probe — они в плагин не входят и Qt не требуют.
# Там же слой STEP и нормалей gvstep (этап 4a плана переноса) с пробником
# step_probe: они читают OCCT напрямую, Qt им не нужен. Импорт STL и обводка меша
# (этап 4b) лежат в ядре gvcore, их пробник - stl_probe. Топология силуэта
# (этап 4c) - тоже в gvcore, её пробник silhouette_probe берёт суп из STEP через
# gvstep. Аналитические поверхности вращения STEP лежат в gvstep
# (occt/step_surfaces), семейства касательных - в gvcore (core/tangents), их
# общий пробник - tangents_probe (этап 4d).
#
# Рядом собирается C-ABI-мост gvbridge.dll (этап 5): SHARED-цель без Qt над
# gvcore и gvstep, наружу только C-типы. Кладётся в <repo>/build/cpp, откуда её
# берёт питоновский шим modules/gcode-viewer/gv_cpp.py; включает C++ один слой
# вьювера по флагу CNC_CPP. Проверка - modules/gcode-viewer/test_cpp_bridge.py.
#
# Сборка (скрипт делает то же самое):
#   powershell -ExecutionPolicy Bypass -File scripts/build-viewer.ps1
# Вручную:
#   call <VS2022>\VC\Auxiliary\Build\vcvars64.bat
#   cmake -S modules/gcode-viewer/cpp -B build/edges -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_PREFIX_PATH="<Qt6>/msvc2022_64;<OCCT>/Library"
#   cmake --build build/edges
#
# Модуль кладётся в build/edges/CNC/Edges/: qmldir, cncedgesplugin.dll (резервная
# библиотека с типами и алгоритмом) и cncedgespluginplugin.dll (точка входа, её
# грузит QML). Импорт-путь для потребителя — build/edges; там же лежит пробник
# stepledges_probe.exe:
#   QML:      import CNC.Edges 1.0
#   PySide6:  engine.addImportPath("<repo>/build/edges")
# Библиотеки OCCT линкуются по импортированным целям TK* из конфиг-пакета, а если их
# в пакете нет — по переменным OpenCASCADE_INCLUDE_DIR/OpenCASCADE_LIBRARY_DIR.
#
# Той же сборкой собирается C++-контроллер вьювера (этап 5.4а): QML-модуль
# MockBackend 1.0 в <repo>/build/qmlbackend/MockBackend (тип GcodeToolpath и шесть
# геометрий этапа 3, цель gvqmlbackend). Модуль включает флаг CNC_CPP=controller, а
# проверка сверяет его буферы и свойства с питоновским эталоном -
# modules/gcode-viewer/test_controller_cpp.py.
#   QML:      import MockBackend 1.0
#   PySide6:  engine.addImportPath("<repo>/build/qmlbackend")
#
# Той же сборкой собирается C++-точка входа вьювера (отказ от Python в рантайме):
# <repo>/build/viewer/gcodeviewer.exe, исходник - viewer/main.cpp. Она грузит
# GcodeViewer3D.qml в QQuickView и работает только с плагином MockBackend:
# питоновского бэкенда в рантайме нет. Запуск - с Qt в PATH (exe линкуется с Qt),
# каталог плагина и DLL OCCT точка входа добавляет себе сама.
#   gcodeviewer.exe <УП.ngc>

cmake_minimum_required(VERSION 3.16)
project(cnc_edges_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Каталог C-ABI-моста gvbridge.dll: путь знает питоновский шим gv_cpp.py
# (по умолчанию <repo>/build/cpp). Отдельной переменной он вынесен потому, что
# не совпадает с каталогом сборки плагина.
set(GV_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/cpp"
    CACHE PATH "Каталог, куда кладётся gvbridge.dll")

# Каталог QML-модуля MockBackend (C++-контроллер вьювера, этап 5.4а плана
# переноса). Путь знает питоновский шим gv_cpp.py (qml_backend_import_path):
# он добавляет его движку как import path и кладёт каталог модуля в PATH.
set(GV_QMLBACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/qmlbackend"
    CACHE PATH "Каталог, куда кладётся QML-модуль MockBackend")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D)
find_package(OpenCASCADE REQUIRED)

# Чистое ядро gvcore (этапы 2 и 4 плана переноса): парсер G-кода, теги UI_*,
# crease-aware нормали кожи (этап 4a), импорт STL и обводка меша (этап 4b),
# топология силуэта (этап 4c), семейства касательных (этап 4d), инструмент с
# оправкой и коллизии (этап 5.4в). Ни Qt, ни OCCT не нужны - только стандартная
# библиотека. Цель отдельная от плагина, чтобы ядро можно было собрать и сверить
# с Python без Qt.
#
# /fp:precise обязателен: ядро сверяется с Python побайтово, а /fp:fast разрешает
# переассоциацию и слияние умножения со сложением (FMA), что сдвигает результат.
add_library(gvcore STATIC
    core/collision.cpp
    core/gcode_parser.cpp
    core/normals.cpp
    core/outline.cpp
    core/packing.cpp
    core/path_prep.cpp
    core/silhouette.cpp
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

# Пробник ядра: отдельная цель, в плагин не входит. Печатает точки, флаги,
# этапы и sha256 тех же сериализаций, что считает Python, и пишет те же массивы
# дампом; проверка - modules/gcode-viewer/test_gcode_core.py.
add_executable(gcode_probe gcode_probe.cpp)
set_target_properties(gcode_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(gcode_probe PRIVATE /utf-8)
endif()
target_link_libraries(gcode_probe PRIVATE gvcore)

# Геометрии Qt Quick 3D (этап 3 плана переноса): зеркало modules/gcode-viewer/geometry.py
# с теми же форматами буферов (stride 20 и 48 - контракт QML и шейдеров
# toolpath.*/stepledges.*). Цель отдельная от плагина: геометрии сверяются с
# питоновским эталоном побайтово, а в QML-модуль их подключает этап 5.
#
# /fp:precise обязателен так же, как у gvcore: часть чисел геометрий (нормали
# меша, ленты контура) Python считает в float32, и переассоциация с FMA сдвинула
# бы младшие биты буфера.
add_library(gvqtgeom STATIC qt/gv_geometry.cpp)
target_include_directories(gvqtgeom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvqtgeom PUBLIC Qt6::Core Qt6::Gui Qt6::Quick3D)
if (MSVC)
    target_compile_options(gvqtgeom PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvqtgeom PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Пробник геометрий: отдельная цель, в плагин не входит. Строит каждую геометрию
# на входных данных, которые пишет питоновская проверка, печатает состояние и
# sha256 буфера и пишет буферы дампом; сверку ведёт
# modules/gcode-viewer/test_geometry_core.py.
add_executable(geometry_probe geometry_probe.cpp)
set_target_properties(geometry_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(geometry_probe PRIVATE /utf-8)
endif()
target_link_libraries(geometry_probe PRIVATE gvqtgeom Qt6::Core Qt6::Gui Qt6::Quick3D)

# STEP, crease-aware нормали и аналитические поверхности (этап 4a и 4d плана
# переноса): зеркало modules/gcode-viewer/step_import.py, normals.py и
# step_surfaces.py на OCCT SDK напрямую (нормали и семейства касательных лежат в
# gvcore - они от Qt и OCCT не зависят). Цель отдельная от плагина: слой
# сверяется с питоновским эталоном побитово, а в бэкенд его подключит этап 5.
# Qt сюда не тянется: STEP-слой читает OCCT, больше ему ничего не нужно.
#
# /fp:precise обязателен так же, как у gvcore и gvqtgeom: и суп вершин, и
# нормали сверяются с Python бит-в-бит, а /fp:fast разрешает переассоциацию и
# слияние умножения со сложением (FMA), что сдвигает младшие биты.
add_library(gvstep STATIC occt/step_import.cpp occt/step_surfaces.cpp
                            occt/step_edges.cpp occt/step_bundle.cpp)
target_include_directories(gvstep PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvstep PUBLIC gvcore)
if (MSVC)
    target_compile_options(gvstep PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvstep PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# OpenCASCADE: TKMesh - тесселяция (BRepMesh_IncrementalMesh, Poly_Triangulation),
# TKDESTEP/TKXSBase - чтение STEP, TKBRep/TKTopAlgo - обход топологии, TKG3d -
# GCPnts_UniformDeflection (полилинии рёбер в occt/step_edges).
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

# Пробник STEP и нормалей: отдельная цель, в плагин не входит. Печатает число
# вершин и треугольников и sha256 вершин, треугольников и нормалей в тех же
# сериализациях, что Python, и пишет те же массивы дампом; сверку ведёт
# modules/gcode-viewer/test_step_core.py.
add_executable(step_probe step_probe.cpp)
set_target_properties(step_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(step_probe PRIVATE /utf-8)
endif()
target_link_libraries(step_probe PRIVATE gvstep)
# Заголовок слоя включает TopoDS_Shape.hxx: инклюды OCCT у gvstep приватные.
target_include_directories(step_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# Пробник STL и обводки меша (этап 4b плана переноса): отдельная цель, в плагин
# не входит. Читает STL (бинарный или ASCII) в суп треугольников, считает рёбра
# по двугранному углу со склейкой цепочек, печатает число вершин и отрезков и
# sha256 супа и рёбер в тех же сериализациях, что Python, и пишет те же массивы
# дампом; сверку ведёт modules/gcode-viewer/test_stl_core.py. Ни Qt, ни OCCT:
# слой живёт в gvcore.
add_executable(stl_probe stl_probe.cpp)
set_target_properties(stl_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stl_probe PRIVATE /utf-8)
endif()
target_link_libraries(stl_probe PRIVATE gvcore)

# Пробник топологии силуэта и силуэта на фиксированных глазах (этап 4c плана
# переноса): отдельная цель, в плагин не входит. Берёт суп вершин из STEP тем же
# путём gvstep, что питоновский load_step_model, строит топологию и считает
# силуэт на пяти глазах. Печатает число вершин, треугольников и кандидатных
# рёбер и sha256 массивов топологии и силуэтов в тех же сериализациях, что
# Python, и пишет те же массивы дампом; сверку ведёт
# modules/gcode-viewer/test_silhouette_core.py. Топология живёт в gvcore, поэтому
# цели нужен только слой STEP: DLL OCCT пробнику нужны, DLL Qt - нет.
add_executable(silhouette_probe silhouette_probe.cpp)
set_target_properties(silhouette_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(silhouette_probe PRIVATE /utf-8)
endif()
target_link_libraries(silhouette_probe PRIVATE gvstep)
# Заголовок слоя включает TopoDS_Shape.hxx: инклюды OCCT у gvstep приватные.
target_include_directories(silhouette_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# Пробник аналитических поверхностей STEP и семейств касательных (этап 4d плана
# переноса): отдельная цель, в плагин не входит. Берёт суп вершин и поверхности
# вращения из STEP тем же путём gvstep, что питоновские load_step_model и
# load_step_surfaces, читает упакованные рёбра STEP из файла проверки и строит
# семейства касательных (ядро gvcore), затем считает отрезки на пяти
# фиксированных глазах. Печатает числа и sha256 массивов в тех же
# сериализациях, что Python, и пишет те же массивы дампом; сверку ведёт
# modules/gcode-viewer/test_tangents_core.py. Семейства живут в gvcore, поэтому
# цели нужен только слой STEP: DLL OCCT пробнику нужны, DLL Qt - нет.
add_executable(tangents_probe tangents_probe.cpp)
set_target_properties(tangents_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(tangents_probe PRIVATE /utf-8)
endif()
target_link_libraries(tangents_probe PRIVATE gvstep)
# Заголовки слоёв включают TopoDS_Shape.hxx: инклюды OCCT у gvstep приватные.
target_include_directories(tangents_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# Пробник замеров (не входит ни в плагин, ни в ядро): фазы холодной загрузки
# модели STEP и УП по шагам и цена кадра (силуэт и установка буферов HLR).
# Печатает строки "phase <имя> <мс>" и "frame <имя> <мс>". Ему нужны OCCT (фазы
# STEP) и Qt (установка буферов HLRLineGeometry - цель gvqtgeom). Запуск - с DLL
# OCCT и Qt в PATH:
#   bench_probe.exe <файл.step> <файл.ngc> [повторы фаз] [повторы кадров]
add_executable(bench_probe bench_probe.cpp)
set_target_properties(bench_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(bench_probe PRIVATE /utf-8 /fp:precise)
endif()
target_link_libraries(bench_probe PRIVATE gvstep gvqtgeom Qt6::Core Qt6::Gui Qt6::Quick3D)
target_include_directories(bench_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# C-ABI-мост над Qt-free слоями (этап 5 плана переноса): нормали кожи и модель
# STEP одним вызовом, наружу - только C-типы и буферы, выделенные библиотекой.
# Qt в цели нет; OCCT есть, потому что STEP без него не читается. Библиотеку
# грузит питоновский шим modules/gcode-viewer/gv_cpp.py (ctypes), поэтому она
# кладётся не в каталог сборки, а по предсказуемому пути <repo>/build/cpp:
# шим ищет её без знания о генераторе CMake и конфигурации.
#
# /fp:precise обязателен: нормали и суп сверяются с Python бит-в-бит.
add_library(gvbridge SHARED bridge/gv_bridge.cpp)
target_include_directories(gvbridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvbridge PRIVATE gvstep)
set_target_properties(gvbridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${GV_BRIDGE_DIR}
)
if (MSVC)
    target_compile_options(gvbridge PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvbridge PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Эксперимент этапа 5.3 плана переноса: обычная SHARED-библиотека с Qt-объектами
# (настоящая gvgeom::ToolpathGeometry этапа 3), которую грузит интерпретатор
# PySide6 через ctypes. Цель собрана против Qt из CMAKE_PREFIX_PATH, а процесс
# несёт свою Qt в site-packages/PySide6: проверяется, разрешит ли загрузчик
# Windows импорт Qt6Core.dll на уже загруженную DLL и заведётся ли объект.
# Кладётся рядом с мостом (GV_BRIDGE_DIR), исходники - в exp/, проверка -
# modules/gcode-viewer/test_qt_bridge.py.
#
# /fp:precise обязателен: буфер ToolpathGeometry сверяется с Python побайтово.
add_library(gvqtshim SHARED exp/gvqtshim.cpp)
target_include_directories(gvqtshim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvqtshim PRIVATE gvqtgeom Qt6::Core Qt6::Gui Qt6::Quick3D)
set_target_properties(gvqtshim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${GV_BRIDGE_DIR}
)
if (MSVC)
    target_compile_options(gvqtshim PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvqtshim PRIVATE -ffp-contract=off -fno-fast-math)
endif()

qt_standard_project_setup()
if(COMMAND qt_policy)
    # Префикс ресурсов QML-модуля ':/qt/qml/' — значение по умолчанию для новых проектов.
    qt_policy(SET QTP0001 NEW)
endif()

# Модуль разделён по образцу Qt: резервная библиотека cncedgesplugin (SHARED) несёт
# типы и алгоритм, плагин cncedgespluginplugin (MODULE) — точка входа, которую QML
# грузит по строке "plugin cncedgespluginplugin" в qmldir. Резервная библиотека
# линкуется в пробник; MODULE-библиотеку CMake линковать не разрешает.
qt_add_library(cncedgesplugin SHARED)

set_target_properties(cncedgesplugin PROPERTIES
    QT_QML_GENERATE_QMLLS_INI OFF
    # Резервная библиотека лежит рядом с плагином: иначе QML не найдёт её при загрузке.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    # Windows: класс StepEdgesGeometry нужен пробнику, а по умолчанию DLL ничего не экспортирует.
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

qt_add_qml_module(cncedgesplugin
    URI CNC.Edges
    VERSION 1.0
    # Каталог модуля повторяет URI: <build>/CNC/Edges/. Импорт-путь потребителя — <build>.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    SOURCES
        stepledgesgeometry.h
        stepledgesgeometry.cpp
)

target_compile_definitions(cncedgesplugin PRIVATE OCCT_HANDLE_NOCAST)
if (MSVC)
    target_compile_options(cncedgesplugin PRIVATE /utf-8 /MP)
endif()

# OpenCASCADE: сначала импортированные цели (TKxxx::), иначе классические переменные
# из старого конфиг-пакета.
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
# Отбор и склейка рёбер лежат в gvstep (cpp/occt/step_edges): одна редакция на
# плагин и на мост gvbridge.
target_link_libraries(cncedgesplugin PRIVATE gvstep)
target_include_directories(cncedgesplugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Пробник сверки с питоновским эталоном: отдельная цель, в плагин не входит.
# Читает геометрию через сам плагин (setSource -> setShape), печатает число
# отрезков и sha256 буфера; сравнение с эталоном ведёт
# modules/gcode-viewer/test_stepledges_plugin.py.
add_executable(stepledges_probe stepledges_probe.cpp)
set_target_properties(stepledges_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stepledges_probe PRIVATE /utf-8 /MP)
endif()
target_link_libraries(stepledges_probe PRIVATE cncedgesplugin Qt6::Core Qt6::Qml Qt6::Quick3D)
target_include_directories(stepledges_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# C++-контроллер вьювера (этап 5.4а плана переноса): QML-модуль MockBackend 1.0 с
# типом GcodeToolpath и шестью геометриями этапа 3. Заменяет питоновскую
# регистрацию тех же типов (controller.py, geometry.py): контракт QML - URI, имена
# типов, свойства, слоты и сигналы - тот же, поэтому GcodeViewer3D.qml не меняется.
# Это единственный бэкенд вьювера: точки входа грузят только его.
#
# Модуль, как и CNC.Edges, разделён по образцу Qt: резервная библиотека
# gvqmlbackend.dll несёт типы и логику (её грузит точка входа), точка входа
# gvqmlbackendplugin.dll - то, что QML открывает по строке "plugin ..." в qmldir.
# Каталог модуля лежит не в каталоге сборки, а по предсказуемому пути
# <repo>/build/qmlbackend/MockBackend: тем же путём его находят точки входа
# вьювера, которые не знают о генераторе CMake и конфигурации.
#
# /fp:precise обязателен так же, как у gvcore и gvqtgeom: буферы траектории
# сверяются с питоновским эталоном побайтово, а /fp:fast разрешает
# переассоциацию и слияние умножения со сложением (FMA), что сдвигает младшие биты.
qt_add_library(gvqmlbackend SHARED)
set_target_properties(gvqmlbackend PROPERTIES
    QT_QML_GENERATE_QMLLS_INI OFF
    # Резервная библиотека лежит рядом с точкой входа: иначе QML её не найдёт.
    RUNTIME_OUTPUT_DIRECTORY ${GV_QMLBACKEND_DIR}/MockBackend
)
qt_add_qml_module(gvqmlbackend
    URI MockBackend
    VERSION 1.0
    # Каталог модуля повторяет URI: <build>/MockBackend. Импорт-путь - <build>.
    OUTPUT_DIRECTORY ${GV_QMLBACKEND_DIR}/MockBackend
    SOURCES
        qmlbackend/gv_qmlgeom.h
        qmlbackend/gv_qmlgeom.cpp
        qmlbackend/gv_toolpath.h
        qmlbackend/gv_toolpath.cpp
)
target_include_directories(gvqmlbackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Сгенерированный qt_add_qml_module файл регистрации типов включает заголовки
# по имени файла (<gv_qmlgeom.h>, <gv_toolpath.h>): их каталог обязан быть в
# путях поиска, иначе регистрация молча пропускается (__has_include).
target_include_directories(gvqmlbackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qmlbackend)
# Ядро (разбор УП, подготовка траектории, STL, обводка, силуэт и касательные),
# геометрии этапа 3 (буферы линий) и слой STEP (чтение модели вместе с обводкой
# B-Rep и поверхностями: этап 5.4б). OCCT-библиотеки тянет gvstep.
target_link_libraries(gvqmlbackend PRIVATE
    gvcore gvstep gvqtgeom Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D)
# Заголовки слоя STEP включают TopoDS_Shape.hxx (orientShellOutward): у gvstep
# инклюды OCCT приватные, поэтому путь поиска задаётся здесь (как у step_probe).
target_include_directories(gvqmlbackend PRIVATE ${OpenCASCADE_INCLUDE_DIR})
if (MSVC)
    target_compile_options(gvqmlbackend PRIVATE /utf-8 /MP /fp:precise)
else()
    target_compile_options(gvqmlbackend PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# C++-точка входа вьювера (отказ от Python в рантайме): <repo>/build/viewer/
# gcodeviewer.exe грузит GcodeViewer3D.qml в QQuickView и работает только с
# плагином MockBackend - питоновского бэкенда в пути исполнения нет. Каталог
# вынесен отдельной переменной по той же причине, что у моста: путь знает
# сторож и сборка, а не генератор CMake.
#
# Корень репозитория и файл вьювера записаны в цель на случай запуска exe из
# другого каталога: по умолчанию точка входа выводит их из своего пути
# (<репо>/build/viewer -> <репо>).
get_filename_component(GV_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(GV_VIEWER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/viewer"
    CACHE PATH "Каталог, куда кладётся gcodeviewer.exe")

add_executable(gcodeviewer viewer/main.cpp)
set_target_properties(gcodeviewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_VIEWER_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${GV_VIEWER_DIR}
    # Q_OBJECT-класс живёт в main.cpp: moc собирается автоматически.
    AUTOMOC ON
)
# Qt6::Quick нужен QQuickView (окно для Item-корня вьювера); QtQuick3D движок
# находит как QML-модуль Qt, отдельной линковки не требует.
target_link_libraries(gcodeviewer PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(gcodeviewer PRIVATE
    GV_REPO_ROOT="${GV_REPO_ROOT}"
    GV_VIEWER_QML="${CMAKE_CURRENT_SOURCE_DIR}/../GcodeViewer3D.qml")
if (MSVC)
    target_compile_options(gcodeviewer PRIVATE /utf-8)
endif()

