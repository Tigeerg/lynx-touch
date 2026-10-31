# Руководство по версионированию и внесению изменений

Этот документ — «гид»: как вносить изменения и вести версии в проекте
**CNC Панель оператора**. Текущая версия — **0.33.0**.

Термины: словарь для человека — [`ROLES.md`](ROLES.md), раздел 9; в тексте термин расшифрован при первом
употреблении (`AI_WORKFLOW.md`, раздел 0.2).

## 1. Проекты

Продукт состоит из пяти независимо версионируемых проектов (манифест —
[`projects.json`](projects.json)):

| id | Проект | Путь | Тип |
|----|--------|------|-----|
| `react-interface` | React интерфейс (панель оператора) | `apps/react-interface` (`app/`, `components/`, `lib/`) | Next.js |
| `qt-interface` | Qt-порт интерфейса | `apps/qt-interface` (`main.qml`, `preview.qml`, …) | QML |
| `gcode-viewer` | 3D-вьювер УП | `modules/gcode-viewer` (`GcodeViewer3D.qml`, `cpp/`, `toolpath.*`, …) | QML + C++ |
| `cad-testdata` | Тестовые УП/модели и постпроцессор | `data/cad-testdata` (`Test_model.ngc`, `Test_model.STL`, `linuxcnc_ui.cps`) | данные |
| `quickprog` | Демо быстрого программирования | `apps/quickprog` (`main.cpp`, `core/`, `tests/`) | C++ (консоль) |

`qt-interface` и `gcode-viewer` — разные проекты с независимыми версиями; лежат они
в разных папках: `apps/qt-interface` и `modules/gcode-viewer`.

## 2. Схема версий

Используется **SemVer** — `MAJOR.MINOR.PATCH`:

- **MAJOR** — несовместимые изменения (ломаются УП/API/форматы).
- **MINOR** — новая функциональность обратно совместимо.
- **PATCH** — исправления без новой функциональности.

Версия продукта — в корневом файле [`VERSION`](VERSION).
Версия каждого проекта — в `projects.json` (`projects[].version`) и, где есть,
в манифесте проекта (`package.json`, `modules/gcode-viewer/VERSION`).

Текущее состояние: **продукт 0.33.0**; `react-interface` — **0.23.0**,
`gcode-viewer` — **0.58.6**, `qt-interface` — **0.5.8**, `cad-testdata` — **0.5.1**, `quickprog` — **0.2.0**.

### Опытные ветки и документация планов

Ветки заводят только под опыты с кодом: альтернативные математические методы, пробы
функциональности. Документы планирования, исследований и оценок в ветке не держат — они
живут в `main`, в каталоге [`docs/`](docs/) (индекс — [`docs/README.md`](docs/README.md)).

Версия опытной ветки — `<база>+<тег>.<n>`, где `<база>` — версия `main`, на которой ветка
построена, `<тег>` называет саму ветку (`sim` — точная огибающая и хроника реза, `mesh` — рез по
сетке, `csg` — экранное CSG заметаемых тел), а `<n>` — номер записи журнала в ветке (1, 2, …).
Так версию ветки не спутать ни с версией `main`, ни с версией другого опыта: две ветки, построенные
на одной версии `main`, различаются тегом, а не только номером записи (без тега обе несли бы одну и
ту же `0.58.1+alt.5`). Вливание так же однозначно: после merge версия `main` остаётся своей, записи
`<база>+<тег>.<n>` идут как есть. Записи прежнего вида `<база>+alt.<n>` остаются как есть -
история не переписывается.

## 3. Где живут версии

| Файл | Что хранит |
|------|-----------|
| `VERSION` | версия продукта (одна строка) |
| `projects.json` | список проектов и версия каждого |
| `package.json` → `version` | версия `react-interface` |
| `modules/gcode-viewer/VERSION` | версия `gcode-viewer` |
| `modules/gcode-viewer/CHANGELOG.md` | журнал изменений вьювера (по версиям) |
| `CHANGELOG.md` (корень) | сводный журнал по продукту |

## 4. Порядок внесения изменения

1. **Определите проект**, к которому относится изменение (`react-interface`,
   `qt-interface`, `gcode-viewer`, `cad-testdata`).
2. **Внесите изменение** в файлы проекта.
3. **Проверьте** (см. раздел 5).
4. **Опишите изменение** в журнале соответствующего проекта:
   - вьювер → `modules/gcode-viewer/CHANGELOG.md` (в разделе текущей версии);
   - остальное → корневой `CHANGELOG.md`.
5. **Поднимите версию** по SemVer (раздел 2) в `projects.json` и в манифесте
   проекта; при значимом изменении — и версию продукта в `VERSION`.
6. Обновите затронутую документацию (`README.md`, `DESIGN.md`/`DESIGN.en.md`, `projects.json`, README
   модулей, индексы `docs/`), а также — если правка добавила компонент, слой или каталог — `ARCHITECTURE.md`
   и его зеркало `ARCHITECTURE.en.md`; техдолг — в `TODO.md`.

### 4.1. Как вести `TODO.md` и его архив

* Открытая задача живёт в `TODO.md`: пункт, контекст (файл, числа) и зачем он нужен.
* Закрытый пункт помечают `[x]` и **оставляют на месте** до приёмки волны её заказчиком: это запись
  приёмки, по ней видно, что и когда сделали.
* После приёмки волны закрытые пункты переносят в [`docs/TODO-archive.md`](docs/TODO-archive.md) тем же
  коммитом, вместе с заголовком раздела и контекстом. В `TODO.md` остаются открытые задачи и закрытые
  пункты текущей волны.
* Удалять закрытые пункты «чтобы не мешали» нельзя: теряется причина решения и привязка к версии. Если
  пункт стал неактуален — его закрывают с пометкой «отменено» и причиной, а не стирают.
* Архив не редактируют вручную и не пополняют новыми задачами; он только принимает закрытые пункты из
  `TODO.md`.

Шаги 2-6 фиксируются одним коммитом: изменение кода или архитектуры без обновления документации в том же
коммите — незаконченное изменение.

> Версии `qt-interface` и `gcode-viewer` ведите раздельно, даже если правите
> одну папку: правка интерфейса не обязана менять версию вьювера и наоборот.

## 5. Проверки перед фиксацией

**3D-вьювер** (Qt 6, OCCT, CMake + Ninja; питоновского слоя в репозитории нет):
```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-viewer.ps1
set PATH=<Qt6>\bin;<OCCT>\bin;<repo>\build\edges;<repo>\build\edges\CNC\Edges;<repo>\build\qmlbackend;%PATH%
set QT_QPA_PLATFORM=offscreen
build\edges\gcode_probe.exe --manifest modules\gcode-viewer\oracle_manifest.json
build\edges\version_probe.exe              # VERSION, projects.json, viewerVersion в QML
build\edges\geometry_probe.exe --manifest modules\gcode-viewer\oracle_manifest.json
build\edges\step_probe.exe --manifest modules\gcode-viewer\oracle_manifest.json
build\edges\stl_probe.exe --manifest modules\gcode-viewer\oracle_manifest.json
build\edges\silhouette_probe.exe --manifest modules\gcode-viewer\oracle_manifest.json
build\edges\tangents_probe.exe --manifest modules\gcode-viewer\oracle_manifest.json
build\edges\stock_sim_probe.exe            # аналитика поля съёма, RESULT: OK
build\viewer\stock_bind_probe.exe          # привязка поля к траектории, RESULT: OK
build\viewer\gcodeviewer.exe --smoke       # RESULT: OK (parse-level)
build\viewer\gcodeviewer.exe "data\cad-testdata\Test_model.ngc"   # offscreen -> "loaded OK", код 0
build\viewer\keyprobe.exe                  # горячие клавиши ru/en, RESULT: OK
build\viewer\gcodeviewer.exe --frame frame.png "data\cad-testdata\Test_model.ngc"
```
`--manifest` сверяет дайджесты массивов пробника с эталоном
`oracle_manifest.json` без интерпретатора; `--frame` снимает кадр в PNG (сцену
Quick3D рисует живая платформа, offscreen отдаёт кадр без 3D). Живое окно печатает
`loaded g-code from: … segments: N` и ни одной строки `[QML warning]`.

**React интерфейс**:
```
pnpm install
pnpm typecheck   # strict, ошибок типов нет
pnpm test        # тесты редьюсера (lib/cnc/*.test.ts)
pnpm format:check
pnpm guard       # сторож стиля и структуры (tools/check.ps1)
pnpm dev         # открыть в браузере
pnpm build       # сборка без ошибок
```

## 6. Правила журнала изменений

- Записи — кратко, по-русски, с указанием **что** и **зачем**.
- Новые записи — **сверху** (свежие версии первыми).
- Формат раздела:
  ```
  ## <версия> — <дата>
  ### Добавлено / Изменено / Исправлено
  - …
  ```
- В записи фиксируйте влияние на **совместимость УП** (например, изменение
  трактовки `UI_TOOL`/`UI_HOLDER`).

## 7. Совместимость форматов

Теги постпроцессора (`UI_STOCK`, `UI_TOOL`, `UI_OP`, `UI_HOLDER`, …) — это
**контракт** между `cad-testdata` (пост) и `gcode-viewer`. Меняя формат тега:

- обновляйте и пост (`data/cad-testdata/linuxcnc_ui.cps`), и парсер
  (`modules/gcode-viewer/cpp/core/gcode_parser.cpp` + `core/ui_tags.cpp`);
- поднимайте **MINOR**, а при несовместимости — **MAJOR**;
- опишите изменение в журнале вьювера.
