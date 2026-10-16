# gcode-viewer — 3D-вьювер УП

Отдельный модуль: QML-компонент `GcodeViewer3D` + Python-бэкенд `GcodeToolpath` (QmlElement) + шейдеры ленты.

## Состав
- `GcodeViewer3D.qml` — компонент вьювера;
- `mock_backend.py` — бэкенд `GcodeToolpath` (парсер УП, геометрии, силуэт);
- `stepledges.vert/.frag` — ленточная обводка; `toolpath.vert/.frag` — материал траектории;
- `preview_3d.qml/.py` — автономная обёртка (dev-harness);
- `test_*.py`, `smoke_test_qml.py`, `render_probe.py`, `bench_viewer.py` — проверки/диагностика.

## Запуск (автономно)
```
python modules/gcode-viewer/preview_3d.py "data/cad-testdata/Test_model.ngc"
```

## Встраивание в Qt-дизайн
1. Добавить каталог `modules/gcode-viewer` в import-пути QML (`QQmlEngine::addImportPath`) и, при переносе бэкенда в C++, зарегистрировать тип `GcodeToolpath` в том же URI.
2. В Qt-дизайне разместить компонент в layout:
```qml
import GcodeViewer
GcodeViewer3D {
    gcode: ""            // или modelPath для STEP; свойства xray, showToolpath,
                          // showBrepEdges, showTangentLines, showStockVisible, camera*
}
```
3. Публичный API — свойства/сигналы `GcodeToolpath` (`load_gcode`/`loadModel`, `pathChanged`, `loadFinished`, `xray` и т.д.). При C++-порте API сохраняется — замена drop-in.

Журнал изменений — `CHANGELOG.md`; версия — `VERSION`.
