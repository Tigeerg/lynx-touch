# gcode-viewer — 3D-вьювер УП

Отдельный модуль: QML-компонент `GcodeViewer3D` + C++-бэкенд `GcodeToolpath` (QML-модуль `MockBackend`) +
шейдеры. Питоновского слоя в репозитории нет: контроллер, геометрии, разбор УП, STEP/STL и коллизии —
C++.

## Состав
- `GcodeViewer3D.qml` — компонент вьювера (сцена, камера, материалы, панель режимов); корень — `Item`;
- `cpp/qmlbackend/` — QML-модуль `MockBackend` 1.0: тип `GcodeToolpath`, геометрии Qt Quick 3D,
  четыре геометрии рисования остатка (`gv_stockgeom`: верх поля высот, оболочка коробки без верхней
  грани, стенка реза по точному контуру и сама граница полилинией);
- `cpp/core/`, `cpp/occt/` — чистое ядро (парсер УП, теги `UI_*`, нормали, STL, обводка, силуэт,
  касательные, инструмент, коллизии, поле съёма `stock_sim`) и слой STEP на OCCT;
- `cpp/viewer/` — точка входа `gcodeviewer` и пробники живых путей (`keyprobe`, `stock_bind_probe`);
- `cpp/*.cpp` — пробники сверки с эталоном `oracle_manifest.json` (ключ `--manifest`): `gcode_probe`,
  `version_probe`, `geometry_probe`, `step_probe`, `stl_probe`, `silhouette_probe`, `tangents_probe`,
  `stock_sim_probe`, `front_stock_probe`, `bench_probe`, `stepledges_probe`;
- шейдеры: `toolpath.vert`/`.frag` — материал траектории; `stepledges.vert`/`.frag` — ленточная обводка
  (рёбра B-rep, силуэт, рёбра инструмента и оправки); `stocksurface.vert`/`.frag` — верх поля съёма
  (дробление, выборка поля, палитра, лампа слитка; нормаль и граница реза по знаковому расстоянию — во
  фрагменте); `stockshell.vert`/`.frag` — стенки и дно заготовки (нормаль из положения вершины, обрезка
  верха стенки по полю); `stockcutwall.vert`/`.frag` — стенка точного контура реза, `stockcutcontour.vert`/
  `.frag` — полилиния контура у точного вида;
- `preview_3d.qml` — обёртка `Window` 1024x600 вокруг `GcodeViewer3D` (данные, правится руками).

## Запуск (автономно)
```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-viewer.ps1
build\viewer\gcodeviewer.exe "data\cad-testdata\Test_model.ngc"
build\viewer\gcodeviewer.exe --smoke                       # проверка старта, RESULT: OK
build\viewer\gcodeviewer.exe --frame frame.png <УП.ngc>     # кадр в PNG (нужна живая платформа)
```
Qt обязан быть в `PATH` до старта (exe линкуется с `Qt6Core.dll`); каталог плагина `MockBackend` и DLL
OCCT точка входа добавляет себе сама. offscreen-прогон (`QT_QPA_PLATFORM=offscreen`) печатает
`offscreen mode - loaded OK.` и код 0.

## Клавиши
`Пробел`/`P` — пуск/пауза, `Esc` — в начало, `вверх`/`вниз` — скорость; `S` — габарит заготовки,
`M` — вид детали (сплошная -> прозрачная -> скрыта), `K` — кожа детали, `G` — линии УП; `V` — стекло,
`B` — коробка вместо поля, `C` — палитра остатка, `R` — вид остатка (точный -> гладкий -> скруглённый ->
точный контур);
`L` — весь код / ограничить длину, `T` — этапы, `PgUp`/`PgDn` — кадры этапов; `E` — рёбра, `D` —
касательные, `Q` — качество, `F` — FPS. Буквы сверяются по физическому скан-коду, поэтому работают и на
русской раскладке (`keyprobe` гоняет обе).

## Переменные окружения до старта
`CNC_QUALITY` (low/med/high/safe); `CNC_STOCK_VIEW` (exact/smooth/rounded/contour, умолчание smooth),
`CNC_STOCK_ROUND=1` (сокращение rounded), `CNC_STOCK_CONTOUR` (цвет границы реза вида «точный контур»,
`#RRGGBB`), `CNC_STOCK_GHOST` (стекло — умолчание; 0/false/off/no снимает),
`CNC_STOCK_BOX`, `CNC_NO_TOOLPATH`, `CNC_NO_PART_SKIN`, `CNC_PART_MODE` (solid/transparent/hidden),
`CNC_STOCK_RAW`/`CNC_STOCK_CUT` (`#RRGGBB`), `CNC_STOCK_CELL` (шаг сетки поля, мм),
`CNC_FRAME_CAMERA` (`yaw,pitch,dist[,panX,panY]`); `CNC_VIEWER_QML`, `CNC_CPP_QML_DIR`, `CNC_OCC_DIR`,
`CNC_KEY_DEBUG`. Ядро читает `CNC_DECIMATE_TOL`, `CNC_ARC_DECIMATE_TOL`, `CNC_COLLISION_STEP`,
`CNC_HOLDER_*`, `CNC_SILHOUETTE`, `CNC_OFS_M*`.

## Проверки
Шесть пробников манифеста (`gcode_probe`, `geometry_probe`, `step_probe`, `stl_probe`,
`silhouette_probe`, `tangents_probe`) — ключом `--manifest modules/gcode-viewer/oracle_manifest.json`;
`version_probe`, `stock_sim_probe`, `front_stock_probe`, `stock_bind_probe`, `keyprobe`, `--smoke`,
offscreen-прогон, `--frame`. Полный порядок — `VERSIONING.md`, раздел 5.

## Встраивание в Qt-дизайн
1. Добавить каталог `build/qmlbackend` в import-пути QML (`QQmlEngine::addImportPath`), а каталог модуля
   `build/qmlbackend/MockBackend` — в `PATH` (точка входа Qt-плагина ищет зависимости по `PATH`).
2. В Qt-дизайне разместить компонент в layout:
```qml
import MockBackend 1.0
GcodeViewer3D {
    gcode: ""            // или modelPath для STEP; свойства partMode, stockView,
                          // showToolpath, showBrepEdges, showTangentLines,
                          // showStockVisible, camera*
}
```
3. Публичный API — свойства/сигналы `GcodeToolpath` (`load_gcode`/`loadModel`, `pathChanged`,
`loadFinished`, `stockField`, `stockSimState`, `toolInfo`, геометрии) и свойства корня `GcodeViewer3D`
(`partMode`, `stockView`, `stockGhost`, `showToolpath`, `showBrepEdges`, `showTangentLines`,
`showStockVisible`, камера). URI, имена типов, свойства и сигналы совпадают с прежним контрактом.

Журнал изменений — `CHANGELOG.md`; версия — `VERSION`.
