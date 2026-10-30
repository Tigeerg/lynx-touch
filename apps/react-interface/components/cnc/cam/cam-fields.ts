// The fields of the quick-program form and the way a problem finds its input (PLAN-QUICKPROG, 7.4, C5).
//
// The tables are pure data: the form renders them, the tests read them, and one field name links
// the input, the `CAM_PARAM` action and the reason the check returns. The names are the keys of
// `CamForm.values` (`form.ts`), so a typo is a label that no check ever reaches, not a check that
// silently disappears.
//
// The shape checks of `validate-shape.ts` bind some reasons to a shared field: a pocket's stock and
// corner radius both arrive as `geometry`, a drill's peck as `peck`. Those reasons are routed to the
// input they talk about here, in `fieldFor`, so the operator sees the text next to the number that
// is wrong and not under the first input of the shape block.
import type { DrillBody, Material, OperationKind } from '@/lib/cnc/cam/ops'

/** One input of the form: the form value it edits, its caption, unit and the hint under it. */
export interface FieldSpec {
  key: string
  label: string
  unit: string
  hint: string
}

/** One group of inputs: the tab and the caption of the block. */
export interface FieldGroup {
  title: string
  fields: FieldSpec[]
}

export type CamTab = 'geometry' | 'tool' | 'modes'

export const CAM_TAB_LABEL: Record<CamTab, string> = {
  geometry: 'Геометрия',
  tool: 'Инструмент',
  modes: 'Режимы',
}

const GEOMETRY: Record<OperationKind, FieldSpec[]> = {
  face: [
    { key: 'z', label: 'Плоскость Z', unit: 'мм', hint: 'Уровень, до которого снимается слой' },
    { key: 'stepOverPct', label: 'Перекрытие полос', unit: '%', hint: 'Доля диаметра фрезы, 20..90 %' },
  ],
  pocket: [
    { key: 'rect.x', label: 'Центр X', unit: 'мм', hint: 'Центр кармана по X' },
    { key: 'rect.y', label: 'Центр Y', unit: 'мм', hint: 'Центр кармана по Y' },
    { key: 'rect.w', label: 'Ширина', unit: 'мм', hint: 'Размер кармана по X' },
    { key: 'rect.h', label: 'Высота', unit: 'мм', hint: 'Размер кармана по Y' },
    { key: 'cornerRadiusMm', label: 'Радиус угла', unit: 'мм', hint: 'Скругление углов, 0 — острые' },
    { key: 'finishAllowanceMm', label: 'Припуск', unit: 'мм', hint: 'След по стенкам' },
    { key: 'stepOverPct', label: 'Шаг растра', unit: '%', hint: 'Доля диаметра фрезы, 10..100 %' },
  ],
  'circle-pocket': [
    { key: 'circle.x', label: 'Центр X', unit: 'мм', hint: 'Центр кармана по X' },
    { key: 'circle.y', label: 'Центр Y', unit: 'мм', hint: 'Центр кармана по Y' },
    { key: 'circle.r', label: 'Радиус', unit: 'мм', hint: 'Радиус кармана' },
    { key: 'finishAllowanceMm', label: 'Припуск', unit: 'мм', hint: 'След по стенкам' },
    { key: 'stepOverPct', label: 'Шаг растра', unit: '%', hint: 'Доля диаметра фрезы, 10..100 %' },
  ],
  contour: [
    { key: 'points', label: 'Точки', unit: '', hint: 'Профиль парами «X Y» через точку с запятой' },
    { key: 'closed', label: 'Замкнутый', unit: '', hint: 'Обход возвращается к первой точке' },
    { key: 'allowanceMm', label: 'Припуск', unit: 'мм', hint: 'След по профилю сверх радиуса фрезы' },
    { key: 'side', label: 'Сторона', unit: '', hint: 'В материал или наружу' },
  ],
  slot: [
    { key: 'from.x', label: 'Начало X', unit: 'мм', hint: 'Начало оси паза' },
    { key: 'from.y', label: 'Начало Y', unit: 'мм', hint: 'Начало оси паза' },
    { key: 'to.x', label: 'Конец X', unit: 'мм', hint: 'Конец оси паза' },
    { key: 'to.y', label: 'Конец Y', unit: 'мм', hint: 'Конец оси паза' },
    { key: 'widthMm', label: 'Ширина', unit: 'мм', hint: 'Ширина паза поперёк оси, не уже фрезы' },
    { key: 'stepOverPct', label: 'Шаг проходов', unit: '%', hint: 'Доля диаметра между продольными проходами' },
  ],
  chamfer: [
    { key: 'from.x', label: 'Начало X', unit: 'мм', hint: 'Начало кромки' },
    { key: 'from.y', label: 'Начало Y', unit: 'мм', hint: 'Начало кромки' },
    { key: 'to.x', label: 'Конец X', unit: 'мм', hint: 'Конец кромки' },
    { key: 'to.y', label: 'Конец Y', unit: 'мм', hint: 'Конец кромки' },
    { key: 'chamferMm', label: 'Фаска', unit: 'мм', hint: 'Катет фаски, не больше радиуса фрезы' },
  ],
  drill: [
    { key: 'pattern', label: 'Схема', unit: '', hint: 'Отверстие, сетка или по кругу' },
    { key: 'body', label: 'Тело сверла', unit: '', hint: 'Материал сверла задаёт таблицу режимов' },
    { key: 'hole.x', label: 'Центр X', unit: 'мм', hint: 'Центр отверстия или круга отверстий' },
    { key: 'hole.y', label: 'Центр Y', unit: 'мм', hint: 'Центр отверстия или круга отверстий' },
    { key: 'hole.r', label: 'Радиус', unit: 'мм', hint: 'Радиус отверстия и круга' },
    { key: 'cols', label: 'Столбцов', unit: 'шт', hint: 'Сетка: отверстий по X' },
    { key: 'rows', label: 'Рядов', unit: 'шт', hint: 'Сетка: отверстий по Y' },
    { key: 'dx', label: 'Шаг по X', unit: 'мм', hint: 'Сетка: расстояние между отверстиями' },
    { key: 'dy', label: 'Шаг по Y', unit: 'мм', hint: 'Сетка: расстояние между рядами' },
    { key: 'count', label: 'Отверстий', unit: 'шт', hint: 'Круг: сколько отверстий' },
    { key: 'startDeg', label: 'Начало', unit: '°', hint: 'Круг: угол первого отверстия' },
    { key: 'arcDeg', label: 'Дуга', unit: '°', hint: 'Круг: сектор расстановки' },
    { key: 'fullTurn', label: 'Полный круг', unit: '', hint: 'Круг: последнее отверстие на первом' },
    { key: 'peckMm', label: 'Шаг вывода стружки', unit: 'мм', hint: 'G83; 0 — сверлить за один проход' },
  ],
  counterbore: [
    { key: 'pattern', label: 'Схема', unit: '', hint: 'Отверстие, сетка или по кругу' },
    { key: 'seatDiameterMm', label: 'Диаметр цековки', unit: 'мм', hint: 'Гнездо под головку, не уже отверстия' },
    { key: 'seatDepthMm', label: 'Глубина цековки', unit: 'мм', hint: 'Гнездо под головку ниже верха' },
    { key: 'dwellSec', label: 'Выдержка', unit: 'с', hint: 'Пауза G82 на дне, чистит дно гнезда' },
    { key: 'hole.x', label: 'Центр X', unit: 'мм', hint: 'Центр отверстия или круга отверстий' },
    { key: 'hole.y', label: 'Центр Y', unit: 'мм', hint: 'Центр отверстия или круга отверстий' },
    { key: 'hole.r', label: 'Радиус', unit: 'мм', hint: 'Радиус отверстия и круга' },
    { key: 'cols', label: 'Столбцов', unit: 'шт', hint: 'Сетка: отверстий по X' },
    { key: 'rows', label: 'Рядов', unit: 'шт', hint: 'Сетка: отверстий по Y' },
    { key: 'dx', label: 'Шаг по X', unit: 'мм', hint: 'Сетка: расстояние между отверстиями' },
    { key: 'dy', label: 'Шаг по Y', unit: 'мм', hint: 'Сетка: расстояние между рядами' },
    { key: 'count', label: 'Отверстий', unit: 'шт', hint: 'Круг: сколько отверстий' },
    { key: 'startDeg', label: 'Начало', unit: '°', hint: 'Круг: угол первого отверстия' },
    { key: 'arcDeg', label: 'Дуга', unit: '°', hint: 'Круг: сектор расстановки' },
    { key: 'fullTurn', label: 'Полный круг', unit: '', hint: 'Круг: последнее отверстие на первом' },
  ],
  thread: [
    { key: 'hole.x', label: 'Центр X', unit: 'мм', hint: 'Центр резьбы' },
    { key: 'hole.y', label: 'Центр Y', unit: 'мм', hint: 'Центр резьбы' },
    { key: 'hole.r', label: 'Радиус резьбы', unit: 'мм', hint: 'Гребень внутренней, фланец наружной' },
    { key: 'pitchMm', label: 'Шаг резьбы', unit: 'мм', hint: 'Подъём спирали за оборот' },
    { key: 'internal', label: 'Внутренняя', unit: '', hint: 'Да — гайка, нет — наружная резьба' },
    { key: 'threadDepthMm', label: 'Длина резьбы', unit: 'мм', hint: 'Витков = длина / шаг' },
    { key: 'threadAllowanceMm', label: 'Припуск', unit: 'мм', hint: 'След на резьбе под чистовой проход' },
    { key: 'stepOverPct', label: 'Шаг проходов', unit: '%', hint: 'Доля диаметра между проходами по радиусу' },
  ],
  tap: [
    { key: 'pattern', label: 'Схема', unit: '', hint: 'Отверстие, сетка или по кругу' },
    { key: 'pitchMm', label: 'Шаг резьбы', unit: 'мм', hint: 'Подача за один оборот; G84 режет по шагу' },
    { key: 'hole.x', label: 'Центр X', unit: 'мм', hint: 'Центр резьбы или круга отверстий' },
    { key: 'hole.y', label: 'Центр Y', unit: 'мм', hint: 'Центр резьбы или круга отверстий' },
    { key: 'hole.r', label: 'Радиус', unit: 'мм', hint: 'Радиус резьбы и круга' },
    { key: 'cols', label: 'Столбцов', unit: 'шт', hint: 'Сетка: отверстий по X' },
    { key: 'rows', label: 'Рядов', unit: 'шт', hint: 'Сетка: отверстий по Y' },
    { key: 'dx', label: 'Шаг по X', unit: 'мм', hint: 'Сетка: расстояние между отверстиями' },
    { key: 'dy', label: 'Шаг по Y', unit: 'мм', hint: 'Сетка: расстояние между рядами' },
    { key: 'count', label: 'Отверстий', unit: 'шт', hint: 'Круг: сколько отверстий' },
    { key: 'startDeg', label: 'Начало', unit: '°', hint: 'Круг: угол первого отверстия' },
    { key: 'arcDeg', label: 'Дуга', unit: '°', hint: 'Круг: сектор расстановки' },
  ],
  custom: [
    { key: 'gcode', label: 'Строки G-кода', unit: '', hint: 'По строке на строку; блок проверяет только вьювер' },
  ],
  probe: [
    { key: 'points', label: 'Точки обмера', unit: '', hint: 'Тройки «X Y Z» через точку с запятой' },
    { key: 'axis', label: 'Ось касания', unit: '', hint: 'По какой оси щуп ищет поверхность' },
    { key: 'rangeMm', label: 'Диапазон обмера', unit: 'мм', hint: 'Отвод после касания и ход поиска' },
    { key: 'touchFeedMmMin', label: 'Подача касания', unit: 'мм/мин', hint: 'Скорость G38.2, не выше рабочей' },
  ],
}

/** The common fields of every kind: the frame of the pass. */
const FRAME: FieldSpec[] = [
  { key: 'topZ', label: 'Верх материала', unit: 'мм', hint: 'Отсчёт глубины' },
  { key: 'depthMm', label: 'Глубина', unit: 'мм', hint: 'Сколько снять ниже верха' },
  { key: 'stepDownMm', label: 'Шаг по Z', unit: 'мм', hint: 'Наибольший съём за один проход' },
]

/** The seat layout of a counterbore: the hole it is cut on, one row of the form. */
const holeFields = ['hole.x', 'hole.y', 'hole.r']

/** The shape fields of a counterbore beyond the hole: the seat, its depth and the dwell. */
const frameOfCounterbore = ['pattern', 'seatDiameterMm', 'seatDepthMm', 'dwellSec']

/** The shape fields only one hole layout uses: the form shows them by `patternFields`. */
const LAYOUT_HIDDEN = new Set(['cols', 'rows', 'dx', 'dy', 'count', 'startDeg', 'arcDeg', 'fullTurn'])

const TOOL: FieldSpec[] = [
  { key: 'tool.number', label: 'Номер инструмента', unit: '', hint: 'Строка таблицы инструмента' },
  { key: 'tool.diameterMm', label: 'Диаметр', unit: 'мм', hint: 'Диаметр фрезы или сверла' },
  { key: 'tool.hNumber', label: 'Коррекция H', unit: '', hint: 'Строка таблицы длин для G43' },
  { key: 'material', label: 'Материал', unit: '', hint: 'Заготовка, по ней режимы' },
  { key: 'coolant', label: 'СОЖ', unit: '', hint: 'Подача смазочно-охлаждающей жидкости' },
]

const MODES: FieldSpec[] = [
  { key: 'cutting.spindleRpm', label: 'Обороты', unit: 'об/мин', hint: 'Скорость шпинделя' },
  { key: 'cutting.feedMmMin', label: 'Подача', unit: 'мм/мин', hint: 'Рабочая подача в плоскости' },
  { key: 'cutting.plungeFeedMmMin', label: 'Врезание', unit: 'мм/мин', hint: 'Подача по Z' },
]

/** The groups the form shows on one tab, in the order the operator reads them. */
export const fieldGroups = (kind: OperationKind, tab: CamTab): FieldGroup[] => {
  switch (tab) {
    case 'geometry':
      return [
        { title: 'Размеры', fields: GEOMETRY[kind] },
        { title: 'Кадр прохода', fields: FRAME },
      ]
    case 'tool':
      return [{ title: 'Инструмент', fields: TOOL }]
    case 'modes':
      return [{ title: 'Режимы резания', fields: MODES }]
  }
}

/** Every input of the form, so a problem whose field is not on the open tab can still be counted. */
export const allFields = (kind: OperationKind): FieldSpec[] => [...GEOMETRY[kind], ...FRAME, ...TOOL, ...MODES]

/** The inputs that pick from a list instead of typing a number: the form draws them as a choice. */
export const CHOICES: Record<string, readonly string[]> = {
  material: ['aluminium', 'steel'] as const satisfies readonly Material[],
  body: ['hss', 'carbide'] as const satisfies readonly DrillBody[],
  coolant: ['off', 'mist', 'flood'] as const,
  pattern: ['hole', 'grid', 'circle'] as const,
  closed: ['on', 'off'] as const,
  fullTurn: ['on', 'off'] as const,
  side: ['material', 'air'] as const,
  axis: ['X', 'Y', 'Z'] as const,
  internal: ['on', 'off'] as const,
}

/**
 * The inputs the form draws as a multi-line text: one line of text per list element. The form renders
 * them with a textarea and splits the text back on the line breaks (`linesOf`), so the typed program
 * keeps its own lines. A contour and a probe hold their points as one list typed by hand, so they
 * get the same box: a single line of twenty vertices is unreadable on 1024x600.
 */
export const MULTILINE: readonly string[] = ['gcode', 'points']

/** The list of lines of a multi-line field, as `CustomOperation.gcode` holds it. */
export { linesOf } from '@/lib/cnc/cam/form-lines'

/** What the operator reads in a choice: the same words the console of the demo prints. */
export const CHOICE_LABEL: Record<string, string> = {
  aluminium: 'Алюминий',
  steel: 'Сталь',
  hss: 'HSS',
  carbide: 'Твёрдый сплав',
  off: 'Нет',
  mist: 'Туман',
  flood: 'Поток',
  hole: 'Отверстие',
  grid: 'Сетка',
  circle: 'По кругу',
  on: 'Да',
  internal: 'Да',
  material: 'В материал',
  air: 'Наружу',
  X: 'Ось X',
  Y: 'Ось Y',
  Z: 'Ось Z',
}

/** The fields of a kind that only one hole layout uses: the form hides the rest of the layouts. */
export const patternFields = (pattern: string): string[] =>
  pattern === 'grid'
    ? ['hole.x', 'hole.y', 'cols', 'rows', 'dx', 'dy']
    : pattern === 'circle'
      ? ['hole.x', 'hole.y', 'hole.r', 'count', 'startDeg', 'arcDeg', 'fullTurn']
      : ['hole.x', 'hole.y', 'hole.r']

/**
 * The rows «Геометрия» really shows for a kind: the shape fields, plus the fields every layout
 * shares (`pattern`, a drill's body) and the frame of the pass. A hole kind hides the other layouts
 * (`patternFields`); the form's own rule is `isHidden` in `cam-form.tsx`, and this is the same rule
 * as a number for the budget.
 */
export const kindRows = (kind: OperationKind, pattern: string): number => {
  if (kind !== 'drill' && kind !== 'tap' && kind !== 'counterbore') return GEOMETRY[kind].length + FRAME.length
  if (kind === 'drill') return [...['pattern', 'body'], ...patternFields(pattern)].length + FRAME.length
  // A tap shows the layout of the pattern and adds its pitch; a counterbore shows the seat, its
  // depth and the dwell over the hole it sits on.
  const shared = kind === 'tap' ? ['pattern'] : frameOfCounterbore
  const layout = kind === 'tap' ? patternFields(pattern) : holeFields
  return [...shared, ...layout].length + FRAME.length
}

/** The address of one problem: the field of the check, or the field of the reason's words. */
const HINTS: [RegExp, string][] = [
  [/Радиус угла/, 'cornerRadiusMm'],
  [/Припуск/, 'finishAllowanceMm'],
  [/Шаг \d+ %/, 'stepOverPct'],
  [/Фреза/, 'tool.diameterMm'],
  [/заготовк/i, 'geometry'],
  [/Отверстие|Сетка|дыр(ы|а)/i, 'geometry'],
  [/Шаг вывода стружки/, 'peckMm'],
  [/Плоскость обработки/, 'z'],
  [/радиус .* мм не больше нуля/, 'circle.r'],
]

/** The input a reason talks about; the check's own field when no wording is more specific. */
export const fieldFor = (problem: { field: string; reason: string }): string => {
  if (problem.field === 'geometry' || problem.field === 'peck' || problem.field === 'stepOver') {
    return HINTS.find(([re]) => re.test(problem.reason))?.[1] ?? 'geometry'
  }
  return problem.field
}

/** The inputs of a kind drawn as a choice (`CHOICES`) on its «Геометрия» tab, in form order. */
export const choiceFields = (kind: OperationKind): string[] =>
  GEOMETRY[kind].filter((f) => CHOICES[f.key] !== undefined).map((f) => f.key)

/** The first reason per input: one input gets one line under it, the rest stay in the message. */
export const errorsByField = (problems: { field: string; reason: string }[]): Record<string, string> => {
  const out: Record<string, string> = {}
  for (const problem of problems) {
    const key = fieldFor(problem)
    if (out[key] === undefined) out[key] = problem.reason
  }
  return out
}

/** The reason of the shape as a whole when no single input is at fault (e.g. the stock is edge). */
export const shapeProblem = (problems: { field: string; reason: string }[]): string | null =>
  errorsByField(problems)['geometry'] ?? null
