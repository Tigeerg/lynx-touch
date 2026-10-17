# Руководство по версионированию и внесению изменений

Этот документ — «гид»: как вносить изменения и вести версии в проекте
**CNC Панель оператора**. Текущая версия — **0.15.0**.

## 1. Проекты

Продукт состоит из четырёх независимо версионируемых проектов (манифест —
[`projects.json`](projects.json)):

| id | Проект | Путь | Тип |
|----|--------|------|-----|
| `react-interface` | React интерфейс (панель оператора) | `apps/react-interface` (`app/`, `components/`, `lib/`) | Next.js |
| `qt-interface` | Qt-порт интерфейса | `apps/qt-interface` (`main.qml`, `preview.qml`, …) | QML |
| `gcode-viewer` | 3D-вьювер УП | `modules/gcode-viewer` (`GcodeViewer3D.qml`, `mock_backend.py`, `toolpath.*`, …) | QML + Python |
| `cad-testdata` | Тестовые УП/модели и постпроцессор | `data/cad-testdata` (`Test_model.ngc`, `Test_model.STL`, `linuxcnc_ui.cps`) | данные |

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

Текущее состояние: **продукт 0.15.0**; `react-interface` — **0.11.0**,
`gcode-viewer` — **0.41.0**, `qt-interface` — **0.5.3**, `cad-testdata` — **0.5.1**.

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
6. Обновите затронутую документацию (`README.md`, `projects.json`).

> Версии `qt-interface` и `gcode-viewer` ведите раздельно, даже если правите
> одну папку: правка интерфейса не обязана менять версию вьювера и наоборот.

## 5. Проверки перед фиксацией

**3D-вьювер** (Python 3 + PySide6):
```
python -m py_compile modules/gcode-viewer/mock_backend.py
set QT_QPA_PLATFORM=offscreen
python modules/gcode-viewer/preview_3d.py "data/cad-testdata/Test_model.ngc"      # offscreen -> "loaded OK"
python modules/gcode-viewer/smoke_test_qml.py                       # offscreen -> "RESULT: OK"
set QT_QPA_PLATFORM=
python modules/gcode-viewer/preview_3d.py "data/cad-testdata/Test_model.ngc"      # живое окно -> "segments: N", без "[QML warning]"
```

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

- обновляйте и пост (`data/cad-testdata/linuxcnc_ui.cps`), и парсер (`mock_backend.py`);
- поднимайте **MINOR**, а при несовместимости — **MAJOR**;
- опишите изменение в журнале вьювера.
