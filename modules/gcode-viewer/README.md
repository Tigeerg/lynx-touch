# gcode-viewer — 3D-вьювер УП

Отдельный модуль: QML-компонент `GcodeViewer3D` + C++-бэкенд `GcodeToolpath` (QML-модуль `MockBackend`) +
шейдеры ленты. Питоновского слоя в репозитории нет: контроллер, геометрии, разбор УП, STEP/STL и коллизии —
C++.

## Состав
- `GcodeViewer3D.qml` — компонент вьювера (сцена, камера, материалы, панель режимов);
- `cpp/qmlbackend/` — QML-модуль `MockBackend`: тип `GcodeToolpath`, геометрии Qt Quick 3D, поле съёма;
- `cpp/core/`, `cpp/occt/` — чистое ядро (парсер УП, теги `UI_*`, нормали, STL, обводка, силуэт, касательные,
  инструмент, коллизии, поле съёма) и слой STEP на OCCT;
- `cpp/viewer/` — точка входа `gcodeviewer` и пробники живых путей (`keyprobe`, `stock_bind_probe`);
- `cpp/*.cpp` — пробники сверки с эталоном `oracle_manifest.json` (ключ `--manifest`);
- `stepledges.vert/.frag` — ленточная обводка; `toolpath.vert/.frag` — материал траектории;
- `preview_3d.qml` — обёртка `Window` 1024x600 вокруг `GcodeViewer3D` (данные, правится руками).

## Запуск (автономно)
```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-viewer.ps1
build\viewer\gcodeviewer.exe "data\cad-testdata\Test_model.ngc"
build\viewer\gcodeviewer.exe --smoke                       # проверка старта
build\viewer\gcodeviewer.exe --frame frame.png <УП.ngc>     # кадр в PNG
```

## Встраивание в Qt-дизайн
1. Добавить каталог `build/qmlbackend` в import-пути QML (`QQmlEngine::addImportPath`), а каталог модуля
   `build/qmlbackend/MockBackend` — в `PATH` (точка входа Qt-плагина ищет зависимости по `PATH`).
2. В Qt-дизайне разместить компонент в layout:
```qml
import MockBackend 1.0
GcodeViewer3D {
    gcode: ""            // или modelPath для STEP; свойства xray, showToolpath,
                          // showBrepEdges, showTangentLines, showStockVisible, camera*
}
```
3. Публичный API — свойства/сигналы `GcodeToolpath` (`load_gcode`/`loadModel`, `pathChanged`, `loadFinished`,
`xray` и т.д.). Он совпадает с прежним питоновским контрактом: URI, имена типов, свойства и сигналы те же.

Журнал изменений — `CHANGELOG.md`; версия — `VERSION`.
