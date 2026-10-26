# ARCHITECTURE.md — карта проекта

Карта для агента: где лежат слои, кто с кем говорит и куда кладут новое. Источник истины — код и его
запуск (`CLAUDE.md`), карта может отставать: правь её вместе с поведением. Английское зеркало —
[ARCHITECTURE.en.md](ARCHITECTURE.en.md); оба варианта — один источник, их правят одним заходом.

Глубокий разбор устройства вьювера (слои, потоки, контракт QML) — в [DESIGN.md](DESIGN.md). Здесь — карта
всего продукта; при новом каталоге, слое или компоненте обоснование дают в этом файле.

## 1. Продукт

Продукт — четыре независимо версионируемых проекта (манифест — [projects.json](projects.json)):

| id | Путь | Тип | Точка входа |
|---|---|---|---|
| `react-interface` | `apps/react-interface` | Next.js + TS | `app/page.tsx` |
| `qt-interface` | `apps/qt-interface` | QML | `main.qml` |
| `gcode-viewer` | `modules/gcode-viewer` | QML + C++ | `GcodeViewer3D.qml` |
| `cad-testdata` | `data/cad-testdata` | данные | `Test_model.ngc` |

Связь проектов: `cad-testdata` отдаёт УП и постпроцессор, `gcode-viewer` их читает по контракту тегов
`UI_*`; `qt-interface` повторяет интерфейс `react-interface` дословно. Версии сверяет `version_probe`.

## 2. `react-interface` (панель оператора)

Слои, сверху вниз:

* `app/` — маршруты и точка входа: `page.tsx` (панель), `classic/` (маршрут `/classic` — тот же редьюсер,
  всегда панельный вариант), `layout.tsx`, `globals.css`.
* `components/cnc/` — слой представления: экраны (`screens/`), физический пульт (`hardware/`), диалоги,
  оверлеи, `primitives.tsx`. Сенсорный вариант — `components/cnc/touch/`; переключатель — `ui-mode.tsx`,
  контекст — `ui-mode-context.ts`.
* `lib/cnc/` — **чистая логика станка, без JSX, React и DOM**: `machine.ts` (редьюсер, сводит подсистемы
  через реестр), `subsystem.ts` (листовой слой действий: `Action`, `Result`, `Dispatch`, запись реестра)
  и подсистемы рядом (`ladder.ts` — образец, `ladder-scan.ts` — скан ПЛК, `nav.ts`, `cycle.ts`,
  `control.ts`, `service.ts`), `types.ts`, `coords.ts`, `keys.ts` (раскладка пульта: `keyToAction`),
  `guards.ts` (проверки входа), `data.ts` и
  `softkeys.ts` (тексты и иконки), `service-data.ts` (API каталога: типы `ParamDef` и разделы),
  `params/` (таблицы разделов, по файлу на раздел: `def.ts`, `display.ts`, `system.ts`, `motion.ts`,
  `linuxcnc.ts`, `mesa.ts`, `joints.ts`, `pid.ts`; отдельный каталог — чтобы таблицы не смешивались с
  логикой), `hal.ts` (справочник пинов HAL), `messages.ts`, `time.ts` (отметки времени из экшена), `toolpath.ts`, `tool-check.ts`, `probe.ts`,
  `mpg.ts`, `workplace.ts`, `files.ts`, `list.ts`, `prog.ts`, `tool-measure.ts` (замер инструмента по
  датчику), `machine-branches.ts` (собственные ветки машины: аварии, справка, тема), `theme`-логика.
* `public/` — картинки и статика проекта.
* Тесты — рядом с логикой (`lib/cnc/*.test.ts`), слой тот же, что и дефект.

Правила слоя: компонент получает `state + dispatch` и не меняет состояние машины сам; любое действие —
типизированный `Action`; сервисные значения — через `ParamDef`. Внешних сетевых API у панели нет: машина
состояний локальна (появится запрос к API — заведи слой здесь и назови его).

## 3. `qt-interface` (Qt-порт интерфейса)

* `main.qml` — источник интерфейса (правится руками); `preview.qml` — данные, тоже правятся руками
  (генератор удалён вместе с питоновским слоем).
* `assets/` — картинки проекта; `fonts/` — `Inter`, `JetBrains Mono`, `lucide.ttf`.
* Точки запуска в репозитории нет: рантайм `qml` модуль `MockBackend` не грузит.
* Тексты совпадают с `react-interface` дословно; иконки — только lucide из приложенного шрифта.
* Порт отстал от React на несколько заходов (рабочие места, вкладка «Работа», 8 софт-кеев, экран АВТО,
  «Минимал», сенсорный режим); план приведения — [docs/PORT-QML.md](docs/PORT-QML.md).

## 4. `gcode-viewer` (3D-вьювер УП)

Слои (подробно — [DESIGN.md](DESIGN.md)):

* `GcodeViewer3D.qml` — QML-слой: сцена, камера, материалы, панель режимов, обработка клавиш.
* `cpp/core/` — **чистое ядро без Qt**: парсер УП и теги `UI_*` (`gcode_parser`, `ui_tags`), геометрия
  (`outline`, `silhouette`, `tangents`, `normals`), упаковка (`packing`, `path_prep`), импорт STL,
  инструмент и коллизии (`tooling`, `collision`), поле съёма (`stock_sim`), типы (`gv_types.h`),
  настройки (`quality.h`).
* `cpp/occt/` — слой STEP на OpenCASCADE.
* `cpp/qt/`, `cpp/qmlbackend/` — мост к Qt: геометрии `QQuick3DGeometry` и QML-модуль `MockBackend`
  (контроллер `GcodeToolpath`, `gv_stockgeom`).
* `cpp/viewer/` — точка входа `gcodeviewer` и пробники живых путей (`keyprobe`, `stock_bind_probe`).
* `cpp/*_probe.cpp` — пробники сверки с эталоном `oracle_manifest.json` (ключ `--manifest`).
* Шейдеры: `toolpath.*`, `stepledges.*`, `stocksurface.*`, `stockshell.*`.
* `preview_3d.qml` — обёртка `Window` 1024x600 (данные, правится руками).

Поток данных: УП -> чистое ядро в рабочем потоке -> сигналы (`toolpathReady`, `meshReady`, `loadFinished`)
-> контроллер -> QML -> GPU (QtQuick3D). Объекты, видимые QML, трогает только GUI-поток. Контракт с
`cad-testdata` — теги `UI_*`; меняя тег, правь пост и парсер в одном заходе (`VERSIONING.md`, раздел 7).

## 5. `cad-testdata` (данные и постпроцессор)

* `data/cad-testdata/Test_model.ngc` — тестовая УП; `Test_model.STL` — модель детали.
* `data/cad-testdata/linuxcnc_ui.cps` — постпроцессор с тегами `UI_*`; `linuxcnc.cps` — базовый.

## 6. Куда кладут новое

* Новая подсистема панели — свой модуль в `lib/cnc/` рядом (`ladder.ts` — образец), подключённый к
  `machine.ts` через него; `machine.ts` сводит, а не впитывает.
* Новый слой вьювера — в соответствующий каталог `cpp/` (`core` — чистое, `occt` — STEP, `qt`/`qmlbackend` —
  мост, `viewer` — точка входа); проверку — пробником рядом.
* Новый каталог обосновывают в этом файле; размер файла держат в ориентире 250 строк (`CLAUDE.md`, раздел 2),
  превышение — с записью в [TODO.md](TODO.md).
