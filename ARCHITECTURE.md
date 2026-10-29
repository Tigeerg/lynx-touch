# ARCHITECTURE.md — карта проекта

Карта для агента: где лежат слои, кто с кем говорит и куда кладут новое. Источник истины — код и его
запуск (`CLAUDE.md`), карта может отставать: правь её вместе с поведением. Английское зеркало —
[ARCHITECTURE.en.md](ARCHITECTURE.en.md); оба варианта — один источник, их правят одним заходом.

Глубокий разбор устройства вьювера (слои, потоки, контракт QML) — в [DESIGN.md](DESIGN.md). Здесь — карта
всего продукта; при новом каталоге, слое или компоненте обоснование дают в этом файле.

Термины: словарь для человека — [ROLES.md](ROLES.md), раздел 9; термин расшифрован там или здесь при первом
употреблении (`AI_WORKFLOW.md`, раздел 0.2).

## 1. Продукт

Продукт — пять независимо версионируемых проектов (манифест — [projects.json](projects.json)):

| id | Путь | Тип | Точка входа |
|---|---|---|---|
| `react-interface` | `apps/react-interface` | Next.js + TS | `app/page.tsx` |
| `qt-interface` | `apps/qt-interface` | QML | `main.qml` |
| `gcode-viewer` | `modules/gcode-viewer` | QML + C++ | `GcodeViewer3D.qml` |
| `cad-testdata` | `data/cad-testdata` | данные | `Test_model.ngc` |
| `quickprog` | `apps/quickprog` | C++ (консоль) | `main.cpp` |

Связь проектов: `cad-testdata` отдаёт УП и постпроцессор, `gcode-viewer` их читает по контракту тегов
`UI_*`; `qt-interface` повторяет интерфейс `react-interface` дословно. Версии сверяет `version_probe`.

## 2. `react-interface` (панель оператора)

Слои, сверху вниз:

* `app/` — маршруты и точка входа: `page.tsx` (панель), `classic/` (маршрут `/classic` — тот же редьюсер,
  всегда панельный вариант), `layout.tsx`, `globals.css`.
* `components/cnc/` — слой представления: экраны (`screens/`), физический пульт (`hardware/`), диалоги,
  оверлеи, `primitives.tsx`. Боковая колонка состояния — `side-status.tsx` — фасад над каталогом
  `side-status/` (`sections.tsx` — секции и раскладка, `tool.tsx` — инструмент и корректоры, `load.tsx` —
  нагрузка, `jog.tsx` — ручные режимы, `operator-pad.tsx` — пульт, `status-column.tsx` — сборка колонки).
  Экран «Сервис» — `screens/service-screen.tsx` — фасад над каталогом `screens/service-screen/`
  (`service-screen.tsx` — сам экран, `params-table.tsx` — таблица параметров, `hal-monitor.tsx` — монитор
  HAL, `mesa-io.tsx` — ввод-вывод Mesa, `system-info.tsx` — сведения о системе, `password-dialog.tsx` — ввод
  пароля). Монитор ПЛК — `screens/ladder-view.tsx` — фасад над `screens/ladder-view/` (`ladder-view.tsx` — сам
  экран, `rung.tsx` — контакт, катушка и цепь, `form-panel.tsx` — форма элемента, `scheme-manager.tsx` —
  менеджер схем); схема обмера щупа — `probe/probe-schematic.tsx` — фасад над `probe/probe-schematic/`
  (`probe-schematic.tsx` — сборка схемы, `parts.tsx` — сетка, заготовка и бейдж, `probe.tsx` — щуп,
  `gizmo.tsx` — указатель стороны). Экран «Программа» — `screens/program-screen.tsx` — фасад над
  `screens/program-screen/` (`program-screen.tsx` — сам экран, `program-list.tsx` — список кадров и ход
  прогона, `search-bar.tsx` — поиск и замена, `dro.tsx` — полоса координат, `line-editor.tsx` — ввод строки,
  `highlight.tsx` — подсветка кадра); мастер обмера щупа — `probe/probe-wizard.tsx` — фасад над
  `probe/probe-wizard/` (`probe-wizard.tsx` — сам мастер, `stepper.tsx` — шаги, `cycle-list.tsx` — выбор
  цикла и готовность щупа, `readout.tsx` — показания, `step-card.tsx` — карточка шага). Физический пульт —
  `hardware/hardware-panel.tsx` — фасад над `hardware/hardware-panel/` (`program-mode-block.tsx` — режим
  программы, `spindle-block.tsx` — шпиндель, `jog-pad.tsx` — маховик осей); сенсорная клавиатура —
  `touch/touch-deck.tsx` — фасад над `touch/touch-deck/` (`hold-button.tsx` — клавиша с автоповтором,
  `cycle-row.tsx` — строка цикла, `workplace-column.tsx` — рабочие места, `jog-panel.tsx` — ручной пульт,
  `functions-panel.tsx` — функции). Диалог обмера инструмента — `tool-measure-dialog.tsx` — фасад над
  `tool-measure-dialog/` (`measure-scene.tsx` — визуализация датчика, `scene-defs.tsx` — его градиенты и
  метки, `measure-readout.tsx` — результат, `param-fields.tsx` — параметры подвода, `phase-label.ts` — слова
  стадий). Наборы софт-кеев по экранам — `softkeys/screens/*.ts` под фасадом `softkey-screens.ts`. Сенсорный
  вариант — `components/cnc/touch/`; переключатель — `ui-mode.tsx`,
  контекст — `ui-mode-context.ts`.
* `lib/cnc/` — **чистая логика станка, без JSX, React и DOM**: `machine.ts` (редьюсер, сводит подсистемы
  через реестр), `subsystem.ts` (листовой слой действий: `Action`, `Result`, `Dispatch`, запись реестра)
  и подсистемы рядом (`ladder.ts` — образец: фасад над каталогом `ladder/` — `ladder-data.ts` (данные и
  предикаты), `ladder-parse.ts` (разбор сравнения), `ladder-reducer.ts` (редьюсер редактора), `ladder-forms.ts`
  (форма элемента), `ladder-schemes.ts` (менеджер схем), `ladder-nav.ts` (курсор и навигация по цепям),
  `ladder-rung.ts` (построение цепи: свободный адрес, новая цепь, копия) — листья графа, на них опираются
  форма, схемы и редьюсер; `ladder/types.ts` (типы ПЛК); `types.ts` держит
  `MachineState` и реэкспортирует типы ПЛК как прежняя точка входа; `ladder-scan.ts` — скан ПЛК, `nav.ts`,
  `cycle.ts` — фасад над каталогом `cycle/` (`cycle-exec.ts` — исполнение кадра, `cycle-actions.ts` —
  объединение действий, `cycle-reducer.ts` — редьюсер), `control.ts`, `service.ts`), `types.ts`, `coords.ts`,
  `clamp.ts` (зажим индексов списков), `keys.ts` (раскладка пульта: `keyToAction`),
  `gcode.ts` (разбор кадра, модальные коды, инструменты программы), `spindle.ts` (обороты, резание,
  нагрузка), `calc.ts` (расчёт режимов), `guards.ts` (проверки входа), `data.ts` и
  `data.ts` (тексты) и `softkeys.ts` — фасад над каталогом `softkeys/`: `softkey-slots.ts` (слоты, потолок
  подписи, `softkeyAction`), `softkey-icons.ts` (иконки), `softkey-dialogs.ts` (наборы диалогов),
  `softkey-workplaces.ts` (НАЛАДКА, ПРОГОН, АВТО), `softkey-screens.ts` (наборы экранов); `service-data.ts`
  (API каталога: типы `ParamDef` и разделы),
  `params/` (таблицы разделов, по файлу на раздел: `def.ts`, `display.ts`, `system.ts`, `motion.ts`,
  `linuxcnc.ts`, `mesa.ts`, `joints.ts`, `pid.ts`; отдельный каталог — чтобы таблицы не смешивались с
  логикой), `service-sections.ts` (разделы вкладки «Сервис»), `keys-route.ts` (маршрут клавиши к действию;
  берёт `keyToAction` из `keys.ts`, обратного импорта нет),
  `hal.ts` (справочник пинов HAL), `messages.ts`, `time.ts` (отметки времени из экшена), `toolpath.ts`,
  `tool-check.ts`, `probe.ts` (замер щупом; таблица циклов и её геометрия — `probe-shapes.ts`),
  `mpg.ts`, `test.ts` (ПРОГОН: четыре способа прогона — симуляция, холостой, по шагам, по маховику),
  `workplace.ts`, `files.ts`, `list.ts`, `prog.ts`, `tool-measure.ts` (замер инструмента по
  датчику), `machine-branches.ts` (собственные ветки машины: аварии, справка, тема), `theme`-логика.
* `public/` — картинки и статика проекта.
* Тесты — рядом с логикой (`lib/cnc/*.test.ts`), слой тот же, что и дефект. Свод проверок редьюсера держат
  `lib/cnc/machine.<подсистема>.test.ts` (общие помощники — `machine.test-helpers.ts`), чтобы файл не рос
  вместе с машиной.

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

## 6. `quickprog` (демо быстрого программирования, C++)

Опытный проект: отдельное консольное приложение, чтобы обкатать конвейер быстрого программирования до
панели (`docs/PLAN-QUICKPROG.md`, раздел 7.2).

* `apps/quickprog/main.cpp` — тонкий диспетчер (22 строки); консольный сценарий — `cli/` (`help`, `app`,
  `operation_program`, `demo_program`, `run`): `--demo`, `--op face|pocket|circle-pocket|drill`, `--qprog`,
  `--save-qprog`, `--version`, `--help`.
* `apps/quickprog/core/` — чистая логика без Qt, OCCT и ввода-вывода экрана: модель (`ops.h`), геометрия
  (`geom.h`), режимы (`feeds`), проверки (`validate*`), планировщик (`path*` по операции), вывод УП
  (`emit*`), сериализация `.qprog` (`serialize*`), сводка (`summary`). Пределы станка — `limits.h`.
* `apps/quickprog/tests/` — своя таблица случаев без внешнего фреймворка (`quickprog_check`, общий список в
  `checks.h`); цель `quickprog_check` дополнительно сверяет УП с ядром вьювера (`gv::parseGcode`,
  `tests/checks_roundtrip.cpp`), для этого собирает `gvcore_quickprog` — только три файла парсера
  `modules/gcode-viewer/cpp/core`, без Qt и OCCT.
* Сборка — `scripts/build-quickprog.sh` (Linux) и `.ps1` (Windows), результат в `build/quickprog/`.
* Контракт с вьювером — теги `UI_STOCK`/`UI_OP`/`UI_TOOL`/`UI_ORIGIN`, как у постпроцессора `cad-testdata`.

## 7. Куда кладут новое

* Новая подсистема панели — свой модуль в `lib/cnc/` рядом (`ladder.ts` — образец), подключённый к
  `machine.ts` через него; `machine.ts` сводит, а не впитывает.
* Новый слой вьювера — в соответствующий каталог `cpp/` (`core` — чистое, `occt` — STEP, `qt`/`qmlbackend` —
  мост, `viewer` — точка входа); проверку — пробником рядом.
* Новая операция быстрого программирования — свой вариант в `Operation` (`apps/quickprog/core/ops.h`),
  планировщик в `core/path*`, правило проверки в `core/validate`: `std::variant` не даёт молча пропустить
  операцию, а вьюверу она обязана отдать те же теги, что и постпроцессор.
* Новый каталог обосновывают в этом файле; размер файла держат в ориентире 250 строк (`CLAUDE.md`, раздел 2),
  превышение — с записью в [TODO.md](TODO.md).
