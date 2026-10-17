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
# Рядом собирается C-ABI-мост gvbridge.dll (этап 5) и шим Qt-геометрий
# gvqtshim.dll: SHARED-цели над gvcore/gvstep и gvqtgeom. Единственным их
# потребителем был питоновский шим (ctypes), удалённый вместе с питоновским
# слоем: цели остались от этапа переноса и ждут решения (в сборку входят, из
# пробников и вьювера не используются).
#
# Пробники сверяются с эталоном сами: ключ --manifest <oracle_manifest.json>
# считает дайджесты своих массивов и сравнивает с манифестом
# (cpp/manifest_check.h, без интерпретатора). Питоновского слоя в репозитории нет.
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
# Библиотеки OCCT линкуются по импортированным целям TK* из конфиг-пакета, а если их
# в пакете нет — по переменным OpenCASCADE_INCLUDE_DIR/OpenCASCADE_LIBRARY_DIR.
#
# Той же сборкой собирается C++-контроллер вьювера (этап 5.4а): QML-модуль
# MockBackend 1.0 в <repo>/build/qmlbackend/MockBackend (тип GcodeToolpath и шесть
# геометрий этапа 3, цель gvqmlbackend). Это единственный бэкенд вьювера; живьём
# его проверяют пробники viewer/keyprobe и viewer/stock_bind_probe.
#   QML:      import MockBackend 1.0
#
# Той же сборкой собирается C++-точка входа вьювера: <repo>/build/viewer/
# gcodeviewer.exe, исходник - viewer/main.cpp. Она грузит GcodeViewer3D.qml в
# QQuickView и работает только с плагином MockBackend: питоновского бэкенда нет
# ни в рантайме, ни в репозитории. Запуск - с Qt в PATH (exe линкуется с Qt),
# каталог плагина и DLL OCCT точка входа добавляет себе сама.
#   gcodeviewer.exe <УП.ngc>                 offscreen -> "loaded OK", сегменты
#   gcodeviewer.exe --smoke                  проверка старта
#   gcodeviewer.exe --frame f.png <УП.ngc>   кадр в PNG

cmake_minimum_required(VERSION 3.16)
project(cnc_edges_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Linux: объекты статических gvcore, gvqtgeom и gvstep попадают в SHARED-библиотеки
# (gvbridge, gvqtshim, плагины): без PIC линковка кончается отказом GCC "recompile
# with -fPIC" (у MSVC ключа нет, там свойство ничего не меняет).
#
# Про rpath ключей нет: CMake по умолчанию кладёт в rpath сборки каталоги связанных
# библиотек, поэтому Qt и OCCT, лежащие вне каталогов сборки, находятся без
# LD_LIBRARY_PATH - и точка входа (viewer/main.cpp, prependPath) на Linux PATH не
# трогает. В дерево установки этот rpath не переносится, но установки у целей нет.
if (UNIX AND NOT APPLE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Каталог C-ABI-моста gvbridge.dll. Отдельной переменной он вынесен потому, что
# не совпадает с каталогом сборки плагина.
set(GV_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/cpp"
    CACHE PATH "Каталог, куда кладётся gvbridge.dll")

# Каталог QML-модуля MockBackend (C++-контроллер вьювера, этап 5.4а плана
# переноса). Каталог модуля лежит по предсказуемому пути <repo>/build/qmlbackend/
# MockBackend, импорт-путь - <repo>/build/qmlbackend: тем же путём его находят
# точки входа вьювера и пробники, которые не знают о генераторе CMake.
set(GV_QMLBACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/qmlbackend"
    CACHE PATH "Каталог, куда кладётся QML-модуль MockBackend")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D)
find_package(OpenCASCADE REQUIRED)

# Чистое ядро gvcore (этапы 2 и 4 плана переноса): парсер G-кода, теги UI_*,
# crease-aware нормали кожи (этап 4a), импорт STL и обводка меша (этап 4b),
# топология силуэта (этап 4c), семейства касательных (этап 4d), инструмент с
# оправкой и коллизии (этап 5.4в). Ни Qt, ни OCCT не нужны - только стандартная
# библиотека. Цель отдельная от плагина, чтобы ядро можно было собрать и сверить
# с оракулом без Qt.
#
# /fp:precise обязателен: ядро сверяется с эталонными дайджестами побайтово, а
# /fp:fast разрешает переассоциацию и слияние умножения со сложением (FMA), что
# сдвигает результат.
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

# Пробник ядра: отдельная цель, в плагин не входит. Печатает точки, флаги,
# этапы и sha256 тех же сериализаций, что и оракул, и пишет те же массивы
# дампом. Режим --manifest/--check сверяет дайджесты path.*, pd.* и tag.* с
# oracle_manifest.json без интерпретатора.
add_executable(gcode_probe gcode_probe.cpp)
set_target_properties(gcode_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(gcode_probe PRIVATE /utf-8)
endif()
target_link_libraries(gcode_probe PRIVATE gvcore)

# Пробник версии: сверяет modules/gcode-viewer/VERSION, поле version проекта
# gcode-viewer в projects.json и свойство viewerVersion в GcodeViewer3D.qml -
# забытая правка одного из трёх не видна ни сборке, ни прогону. Ни Qt, ни OCCT
# ему не нужны: только стандартная библиотека.
add_executable(version_probe version_probe.cpp)
set_target_properties(version_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(version_probe PRIVATE /utf-8)
endif()
# Заголовок gv::manifest::repoRoot (общий поиск корня репозитория у пробников)
# лежит в cpp/, а gvcore эту цель не тянет: путь поиска задаётся здесь.
target_include_directories(version_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Геометрии Qt Quick 3D (этап 3 плана переноса) с теми же форматами буферов
# (stride 20 и 48 - контракт QML и шейдеров toolpath.*/stepledges.*). Цель
# отдельная от плагина: геометрии сверяются с дайджестами оракула побайтово, а в
# QML-модуль их подключает этап 5.
#
# /fp:precise обязателен так же, как у gvcore: часть чисел геометрий (нормали
# меша, ленты контура) считалась в float32, и переассоциация с FMA сдвинула бы
# младшие биты буфера.
add_library(gvqtgeom STATIC qt/gv_geometry.cpp)
target_include_directories(gvqtgeom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvqtgeom PUBLIC Qt6::Core Qt6::Gui Qt6::Quick3D)
if (MSVC)
    target_compile_options(gvqtgeom PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvqtgeom PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Пробник геометрий: отдельная цель, в плагин не входит. Строит каждую геометрию
# на входных данных, которые пишет сам, печатает состояние и sha256 буфера и
# пишет буферы дампом.
#
# Режим --manifest/--check строит вход сам (gvcore: parseGcode + prepareOrdered,
# toolProfile) и сверяет дайджесты буферов с oracle_manifest.json без
# интерпретатора, поэтому нужен gvcore: траектория берётся из УП тем же путём,
# что рантайм.
add_executable(geometry_probe geometry_probe.cpp)
set_target_properties(geometry_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(geometry_probe PRIVATE /utf-8)
endif()
target_link_libraries(geometry_probe PRIVATE gvqtgeom gvcore Qt6::Core Qt6::Gui Qt6::Quick3D)

# STEP, crease-aware нормали и аналитические поверхности (этап 4a и 4d плана
# переноса) на OCCT SDK напрямую (нормали и семейства касательных лежат в
# gvcore - они от Qt и OCCT не зависят). Цель отдельная от плагина: слой
# сверяется с дайджестами оракула побитово, а в бэкенд его подключает этап 5.
# Qt сюда не тянется: STEP-слой читает OCCT, больше ему ничего не нужно.
#
# /fp:precise обязателен так же, как у gvcore и gvqtgeom: и суп вершин, и
# нормали сверяются с эталонными дайджестами бит-в-бит, а /fp:fast разрешает
# переассоциацию и слияние умножения со сложением (FMA), что сдвигает младшие
# биты.
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
# сериализациях, что и оракул, и пишет те же массивы дампом; режим --manifest
# сверяет дайджесты step.* с oracle_manifest.json.
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
# sha256 супа и рёбер в тех же сериализациях, что и оракул, и пишет те же массивы
# дампом; режим --manifest сверяет дайджесты stl.* с oracle_manifest.json. Ни Qt,
# ни OCCT: слой живёт в gvcore.
add_executable(stl_probe stl_probe.cpp)
set_target_properties(stl_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stl_probe PRIVATE /utf-8)
endif()
target_link_libraries(stl_probe PRIVATE gvcore)

# Пробник поля высот остатка заготовки (core/stock_sim): отдельная цель, в
# плагин не входит, ни Qt, ни OCCT не требует. Сверяет поле с аналитикой,
# посчитанной в самом пробнике, и меряет цену прохода и всего Test_model.ngc при
# узле 0.5 и 0.2 мм. Печатает "case <имя>", строки "phase <имя> <мс>" и
# "RESULT: OK" / "RESULT: FAIL (n)"; замер кадров и оракул не трогает.
#   stock_sim_probe.exe [файл.ngc]
add_executable(stock_sim_probe stock_sim_probe.cpp)
set_target_properties(stock_sim_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stock_sim_probe PRIVATE /utf-8)
endif()
target_link_libraries(stock_sim_probe PRIVATE gvcore)

# Пробник топологии силуэта и силуэта на фиксированных глазах (этап 4c плана
# переноса): отдельная цель, в плагин не входит. Берёт суп вершин из STEP тем же
# путём gvstep, что и рантайм, строит топологию и считает
# силуэт на пяти глазах. Печатает число вершин, треугольников и кандидатных
# рёбер и sha256 массивов топологии и силуэтов в тех же сериализациях, что и
# оракул, и пишет те же массивы дампом; режим --manifest сверяет дайджесты
# silhouette.* с oracle_manifest.json. Топология живёт в gvcore, поэтому
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
# вращения из STEP тем же путём gvstep, что и рантайм, строит упакованные рёбра
# STEP сам из той же формы тем же путём, что рантайм (occt/step_edges +
# core/packing), и строит семейства касательных (ядро gvcore), затем считает
# отрезки на пяти фиксированных глазах. Печатает числа и sha256 массивов в тех же
# сериализациях, что и оракул, и пишет те же массивы дампом. Семейства живут в
# gvcore, поэтому цели нужен только слой STEP: DLL OCCT пробнику нужны, DLL Qt -
# нет.
#
# Есть и режим --manifest/--check: он сверяет 22 записи tangents.* манифеста со
# своего буфера рёбер. Оговорка: свой буфер не совпадает с дайджестом
# packed_edges.f32 манифеста байт в байт - набор из 2426 отрезков один, но 64
# отрезка стоят на чужой позиции, а 16 расходятся в младших битах (максимум
# 1.8e-15 мм при значениях порядка 5.7e-14); sha256 4eff01c2... против 926b3836...
# Разбор чисел идёт в cpp/tangents_probe.cpp.
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
# грузил питоновский шим (ctypes), который удалён вместе с питоновским слоем;
# цель осталась от этапа переноса и ждёт решения (в сборку входит, никем не
# используется). Кладётся не в каталог сборки, а по предсказуемому пути
# <repo>/build/cpp - так её искал шим, не зная о генераторе CMake.
#
# /fp:precise обязателен: нормали и суп сверяются с эталонными дайджестами
# бит-в-бит.
add_library(gvbridge SHARED bridge/gv_bridge.cpp)
target_include_directories(gvbridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvbridge PRIVATE gvstep)
# Linux: gvbridge.so - LIBRARY-артефакт, поэтому каталог задан и ему: мост лежит
# в <repo>/build/cpp/libgvbridge.so.
set_target_properties(gvbridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${GV_BRIDGE_DIR}
)
if (MSVC)
    target_compile_options(gvbridge PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvbridge PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Эксперимент этапа 5.3 плана переноса: обычная SHARED-библиотека с Qt-объектами
# (настоящая gvgeom::ToolpathGeometry этапа 3), которую грузил интерпретатор
# через ctypes. Цель собрана против Qt из CMAKE_PREFIX_PATH, а процесс нёс свою
# Qt в site-packages: проверялось, разрешит ли загрузчик Windows импорт
# Qt6Core.dll на уже загруженную DLL и заведётся ли объект. Интерпретатор удалён
# вместе с питоновским слоем: цель осталась от этапа переноса и ждёт решения (в
# сборку входит, никем не используется). Кладётся рядом с мостом
# (GV_BRIDGE_DIR), исходники - в exp/.
#
# /fp:precise обязателен: буфер ToolpathGeometry сверялся с эталоном побайтово.
add_library(gvqtshim SHARED exp/gvqtshim.cpp)
target_include_directories(gvqtshim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gvqtshim PRIVATE gvqtgeom Qt6::Core Qt6::Gui Qt6::Quick3D)
# Linux: тот же LIBRARY-каталог, что у моста:
# <repo>/build/cpp/libgvqtshim.so.
set_target_properties(gvqtshim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_BRIDGE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${GV_BRIDGE_DIR}
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${GV_BRIDGE_DIR}
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
    # Linux: .so - это LIBRARY-артефакт, поэтому тот же каталог задан и ему;
    # точку входа плагина (cncedgespluginplugin) кладёт в CNC/Edges сам
    # qt_add_qml_module (OUTPUT_DIRECTORY).
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
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

# Пробник рёбер STEP: отдельная цель, в плагин не входит.
# Читает геометрию через сам плагин (setSource -> setShape), печатает число
# отрезков и sha256 буфера; ключ --qml дополнительно проверяет, что движок QML
# открывает модуль CNC.Edges и создаёт элемент StepEdges.
add_executable(stepledges_probe stepledges_probe.cpp)
set_target_properties(stepledges_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stepledges_probe PRIVATE /utf-8 /MP)
endif()
target_link_libraries(stepledges_probe PRIVATE cncedgesplugin Qt6::Core Qt6::Qml Qt6::Quick3D)
# Пробник включает заголовок плагина stepledgesgeometry.h, а его инлайны зовут
# Standard::Free из TKernel. OCCT плагин линкует PRIVATE и эти символы наружу не
# отдаёт: на Windows линковка проходила, на Linux ld отвечал "DSO missing from
# command line" и требовал OCCT прямо в пробнике. Пробник берёт тот же набор
# OCCT_NEEDED, что и плагин выше.
foreach(lib IN LISTS OCC_NEEDED)
    if (TARGET OpenCASCADE::${lib})
        target_link_libraries(stepledges_probe PRIVATE OpenCASCADE::${lib})
    else()
        target_link_libraries(stepledges_probe PRIVATE ${lib})
    endif()
endforeach()
target_include_directories(stepledges_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})

# C++-контроллер вьювера (этап 5.4а плана переноса): QML-модуль MockBackend 1.0 с
# типом GcodeToolpath и шестью геометриями этапа 3. Там же две геометрии
# рисования остатка заготовки (qmlbackend/gv_stockgeom): верх поля высот и
# оболочка коробки без верхней грани; в набор сверки шести они не входят.
# Это единственный бэкенд вьювера: точки входа грузят только его, питоновской
# регистрации тех же типов нет.
#
# Модуль, как и CNC.Edges, разделён по образцу Qt: резервная библиотека
# gvqmlbackend.dll несёт типы и логику (её грузит точка входа), точка входа
# gvqmlbackendplugin.dll - то, что QML открывает по строке "plugin ..." в qmldir.
# Каталог модуля лежит не в каталоге сборки, а по предсказуемому пути
# <repo>/build/qmlbackend/MockBackend: тем же путём его находят точки входа
# вьювера, которые не знают о генераторе CMake и конфигурации.
#
# /fp:precise обязателен так же, как у gvcore и gvqtgeom: буферы траектории
# сверяются с эталонными дайджестами побайтово, а /fp:fast разрешает
# переассоциацию и слияние умножения со сложением (FMA), что сдвигает младшие биты.
qt_add_library(gvqmlbackend SHARED)
set_target_properties(gvqmlbackend PROPERTIES
    QT_QML_GENERATE_QMLLS_INI OFF
    # Резервная библиотека лежит рядом с точкой входа: иначе QML её не найдёт.
    # Linux: тот же каталог задан и LIBRARY-артефакту (.so), см. cncedgesplugin.
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
    # Каталог модуля повторяет URI: <build>/MockBackend. Импорт-путь - <build>.
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

# C++-точка входа вьювера: <repo>/build/viewer/gcodeviewer.exe грузит
# GcodeViewer3D.qml в QQuickView и работает только с плагином MockBackend -
# питоновского бэкенда нет ни в пути исполнения, ни в репозитории. Каталог
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
# gvcore нужен: gv::presetFromEnv из core/quality.h зовёт gv::strtodC, а тот
# живёт в core/text_util.cpp. Без линковки точка входа не собирается (LNK2019).
target_link_libraries(gcodeviewer PRIVATE gvcore Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
# core/quality.h читает уровень тем же разбором, что ядро (gv::presetFromEnv),
# поэтому каталог cpp/ обязан быть в путях поиска заголовков.
target_include_directories(gcodeviewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(gcodeviewer PRIVATE
    GV_REPO_ROOT="${GV_REPO_ROOT}"
    GV_VIEWER_QML="${CMAKE_CURRENT_SOURCE_DIR}/../GcodeViewer3D.qml")
if (MSVC)
    target_compile_options(gcodeviewer PRIVATE /utf-8)
endif()

# Пробник горячих клавиш (viewer/keyprobe.cpp): грузит тот же GcodeViewer3D.qml в
# QQuickView и подаёт клавиши с признаками настоящего события (скан-код, VK и символ
# по раскладке), проверяя, что каждая горячая клавиша переключила своё свойство. Им
# измеряют дефект «горячие клавиши требовательны к языку»: на русской раскладке
# буквенные ветки обязаны срабатывать по скан-коду. Платформа - offscreen, окно не
# показывается, поэтому пробник годится и для сторожа.
#   keyprobe.exe <УП.ngc> [ru,en]
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

# Проба привязки съёма материала к траектории (gv_toolpath): создаёт настоящий
# GcodeToolpath из QML-модуля MockBackend и грузит реальную УП. Печатает время
# фонового прохода при пресетах med и high, сверяет текстуру рисования с полем и
# задержку отмены. Платформа offscreen ставится сама. CMake-цель
# нужна Qt (движок QML), сам контроллер приходит из собранного плагина, как у
# точки входа вьювера; Qt6::Quick3D нужен на тип QQuick3DTextureData - им
# проверяется опубликованное поле.
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

