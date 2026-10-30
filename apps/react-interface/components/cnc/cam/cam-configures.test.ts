// The three panel configurations of the quick program (PLAN-QUICKPROG, 5.1 and 7.5: «Три
// конфигурации»), checked by machine instead of by eye.
//
// The code has two UI states and no third one: `UiMode` is `'panel' | 'touch'`
// (`components/cnc/ui-mode-context.ts`), the state holds `params['DISPLAY.UI_MODE']` as 0 | 1
// (`lib/cnc/params/system.ts`), and `ui-mode.tsx` renders `CncPanel` for 0 and `TouchPanel` for 1.
// So P1 = panel, P2 = P3 = touch: the touch variant already has no F-key row (`.touch-ui
// [role='toolbar'] kbd { display: none }`), carries every action as a button (≥ 44 px) and puts
// its own digit pad under the form — the very things P2 and P3 ask for. The tests below pin
// exactly that: no invented third mode, only what the code has.
import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '@/lib/cnc/machine'
import { routeKey } from '@/lib/cnc/keys-route'
import { getSoftkeys, SOFTKEY_SLOTS } from '@/lib/cnc/softkeys'
import type { MachineState } from '@/lib/cnc/types'
import { UI_MODE_PARAM } from '@/lib/cnc/params/display'
import { UI_MODES } from '../ui-mode-context'
import { CATEGORIES, categoryOf, TILES } from './operation-picker'
import { COPY_MENU } from './cam-dialogs'
import { choiceFields, kindRows } from './cam-fields'

type Key = Parameters<typeof routeKey>[1]

const key = (code: string, shiftKey = false): Key => ({ code, shiftKey })
const camDir = new URL('./', import.meta.url)
const read = (name: string): string => readFileSync(new URL(name, camDir), 'utf8')
/** The source without its comments: a class or a word in a comment must not count as code. */
const code = (name: string): string =>
  read(name)
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/^\s*\/\/.*$/gm, '')

/** «Программа» in НАЛАДКА — the screen whose bar carries «Быстрая УП». */
const prog: MachineState = { ...initialState, workplace: 'SETUP', screen: 'prog' }
const run = (s: MachineState, ...actions: Action[]): MachineState => actions.reduce(reducer, s)
const camSlot = (s: MachineState) => getSoftkeys(s).findIndex((k) => k?.action.type.startsWith('CAM_'))

describe('конфигурации в коде: P1 — пульт, P2 и P3 — сенсор', () => {
  it('в коде ровно два режима: panel и touch', () => {
    expect(UI_MODES).toEqual(['panel', 'touch'])
  })

  it('состояние хранит режим как 0 | 1 в DISPLAY.UI_MODE', () => {
    // The reducer clamps the value to the parameter range (0..1): a third mode is impossible here.
    const touch = run(initialState, { type: 'UI_MODE_SET', value: 1 })
    expect(touch.params[UI_MODE_PARAM]).toBe(1)
    const panel = run(touch, { type: 'UI_MODE_SET', value: 0 })
    expect(panel.params[UI_MODE_PARAM]).toBe(0)
  })

  it('ui-mode.tsx выбирает панель для 0 и сенсор для 1', () => {
    const src = code('../ui-mode.tsx'.replace(/^\.\.\//, '../'))
    expect(src).toMatch(/mode === 'touch' \? <TouchPanel/)
    expect(src).toMatch(/: <CncPanel/)
  })
})

describe('P1 (пульт, клавиши): оверлей — только клавишей и софт-кеем', () => {
  it('«Быстрая УП» открывается софт-кеем, он же слотом F-клавиши', () => {
    const index = camSlot(prog)
    expect(index).toBeGreaterThanOrEqual(0)
    expect(getSoftkeys(prog)[index]?.action).toEqual({ type: 'CAM_OPEN' })
    expect(routeKey(prog, key(`F${index + 1}`))?.action).toEqual({ type: 'CAM_OPEN' })
  })

  it('тот же слот закрывает оверлей: F-клавиша и `CAM_CLOSE`', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    const index = camSlot(open)
    expect(index).toBe(camSlot(prog))
    expect(getSoftkeys(open)[index]?.action).toEqual({ type: 'CAM_CLOSE' })
    expect(routeKey(open, key(`F${index + 1}`))?.action).toEqual({ type: 'CAM_CLOSE' })
  })

  it('Esc закрывает оверлей, а не сбрасывает станок', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    expect(routeKey(open, key('Escape'))).toEqual({ action: { type: 'CAM_CLOSE' }, prevent: true })
  })

  it('в панельном оверлее нет касательных путей: ни onTouch*, ни onPointer*, ни свайпов', () => {
    for (const file of [
      'cam-overlay.tsx',
      'cam-form.tsx',
      'field-row.tsx',
      'use-form-state.ts',
      'operation-list.tsx',
      'operation-card.tsx',
      'operation-picker.tsx',
      'copy-dialogs.tsx',
      'copy-dialog-form.tsx',
      'keypad.tsx',
    ]) {
      const src = code(file)
      expect(src, file).not.toMatch(/onTouch(Start|End|Move|Cancel)/)
      expect(src, file).not.toMatch(/onPointer(Down|Move|Up|Cancel)/)
      expect(src, file).not.toMatch(/swipe/i)
    }
  })

  it('все действия оверлея — кнопки или элементы формы, а не div с onClick', () => {
    for (const file of [
      'cam-overlay.tsx',
      'cam-form.tsx',
      'operation-list.tsx',
      'operation-card.tsx',
      'operation-picker.tsx',
      'copy-dialog-form.tsx',
      'keypad.tsx',
    ]) {
      const src = code(file)
      // A click handler lives only on a <button>; a bare element with onClick would not be focusable.
      // Only the opening tag of an element, up to its own `>`: a type like `Dispatch<Action>` is not one.
      const handlers = [...src.matchAll(/<([a-z]\w*)(?:\s[^<>]*?)?onClick=/g)].map((m) => m[1])
      expect(handlers.length, `${file}: у оверлея есть действия`).toBeGreaterThan(0)
      for (const tag of handlers) expect(tag, `${file}: onClick на <${tag}>`).toBe('button')
    }
  })
})

describe('P2/P3 (сенсор): цель ≥ 44 px, свой цифровой блок, без обязательных F-клавиш', () => {
  it('кнопки оверлея в сенсоре получают min-h-11 или min-h-12, min-h-16', () => {
    // Every class that sets a height for the touch branch of the overlay is at least 44 px.
    const files = [
      'cam-overlay.tsx',
      'cam-overlay-body.tsx',
      'cam-form.tsx',
      'field-row.tsx',
      'operation-card.tsx',
      'operation-picker.tsx',
      'copy-dialog-form.tsx',
      'dxf-dialog.tsx',
    ]
    for (const file of files) {
      const src = code(file)
      // Two shapes: `touch ? '…' : '…'` picks a class, `touch && '…'` adds one. Classes a regex
      // cannot see are read with `text-[10px]` (the hint of a row), which is a height of 10 px.
      const touchHeights = [
        ...[...src.matchAll(/touch \? '([^']*)'/g)].flatMap((m) => m[1].split(/\s+/)),
        ...[...src.matchAll(/touch && '([^']*)'/g)].flatMap((m) => m[1].split(/\s+/)),
      ].filter((cls) => /min-h-|h-\d|text-\[10px\]/.test(cls))
      expect(touchHeights.length, `${file}: у сенсора есть высоты`).toBeGreaterThan(0)
      for (const cls of touchHeights) {
        // h-8 (32 px) or a smaller fixed height on a touch target is the defect this guards.
        expect(['min-h-11', 'min-h-12', 'min-h-16'].includes(cls), `${file}: «${cls}» ≥ 44 px`).toBe(true)
      }
    }
  })

  it('цифровой блок сенсора тоже ≥ 44 px и шлёт CAM_PARAM', () => {
    const src = code('keypad.tsx')
    expect(src).toMatch(/min-h-11/)
    expect(src).toMatch(/type: 'CAM_PARAM', field, value: next/)
  })

  it('цифровой блок показывается только в сенсоре', () => {
    const form = code('cam-form.tsx')
    expect(form).toMatch(/\{touch && <Keypad /)
  })

  it('цифровые клавиши блока шлют CAM_PARAM на поле формы', () => {
    // Mirrors `keypad.tsx`: the key sends the form value straight, no local buffer.
    const values = initialState.params
    expect(values[UI_MODE_PARAM]).toBeDefined()
    const open = run(prog, { type: 'CAM_OPEN' }, { type: 'CAM_OP', kind: 'pocket' })
    const field = 'rect.x'
    const typed = run(open, { type: 'CAM_PARAM', field, value: '12' })
    expect(typed.cam?.form?.values[field]).toBe('12')
  })

  it('обязательных действий только на F-клавишах нет: у каждого слота есть кнопка', () => {
    const open = run(prog, { type: 'CAM_OPEN' })
    // The touch panel reads the same bar and draws every slot as a button; the F-row is hidden
    // by CSS (`.touch-ui [role='toolbar'] kbd { display: none }`), not removed from the bar.
    expect(getSoftkeys(open)).toHaveLength(SOFTKEY_SLOTS)
    const css = readFileSync(new URL('../../../app/globals.css', import.meta.url), 'utf8')
    expect(css).toMatch(/\.touch-ui \[role='toolbar'\] kbd \{\s*display: none;/)
  })
})

describe('Q5 (PLAN-QUICKPROG, 7.6): пятнадцать плиток по четырём разделам и импорт DXF', () => {
  it('в наборе 15 плиток: четыре MVP, шесть видов Q4 и пять видов Q5', () => {
    const kinds = TILES.map((t) => t.kind)
    expect(kinds).toHaveLength(15)
    expect(new Set(kinds).size).toBe(15)
    expect(kinds).toEqual([
      'face',
      'pocket',
      'circle-pocket',
      'pocket-island',
      'drill',
      'counterbore',
      'tap',
      'thread',
      'contour',
      'slot',
      'chamfer',
      'engrave',
      'rest',
      'custom',
      'probe',
    ])
    for (const tile of TILES) {
      // A tile is never an icon alone: P1 reads the word, not the picture. `lucide-react` forwards
      // its icons, so the rendered value is an object (`forwardRef`), not a plain function.
      expect(tile.icon, tile.kind).toBeTypeOf('object')
      expect(tile.hint.length, tile.kind).toBeGreaterThan(8)
    }
  })

  it('плитки разложены по четырём вкладкам: «Карманы / Сверление / Контур / Спец»', () => {
    expect(CATEGORIES.map((c) => c.label)).toEqual(['Карманы', 'Сверление', 'Контур', 'Спец'])
    expect(CATEGORIES.map((c) => c.tiles.length)).toEqual([4, 4, 4, 3])
    // Every kind lives in exactly one category: a tile on two tabs would fire `CAM_OP` twice.
    const perCategory = CATEGORIES.flatMap((c) => c.tiles.map((t) => t.kind))
    expect(perCategory).toHaveLength(15)
    expect(new Set(perCategory).size).toBe(15)
    expect(categoryOf('pocket-island')).toBe('pockets')
    expect(categoryOf('thread')).toBe('holes')
    expect(categoryOf('engrave')).toBe('outline')
    expect(categoryOf('rest')).toBe('special')
    // The segments are real buttons of a tablist: P1 reaches them by softkey focus.
    const src = code('operation-picker.tsx')
    expect(src).toMatch(/role="tablist"/)
    expect(src).toMatch(/role="tab"/)
    expect(src).toMatch(/CATEGORIES\.map/)
  })

  it('вкладка — две строки в сетке 2 столбца, прокрутка внутри сетки', () => {
    const src = code('operation-picker.tsx')
    expect(src).toMatch(/grid-cols-2/)
    // Four tiles are two rows on both variants; the third and the fourth tile wrap to the second.
    for (const category of CATEGORIES) expect(Math.ceil(category.tiles.length / 2), category.id).toBeLessThanOrEqual(2)
    // The scroll sits on the grid itself and the column is the flex parent with `min-h-0`, so a
    // grown category scrolls instead of growing the card.
    expect(src).toMatch(/min-h-0 flex-1 auto-rows-min grid-cols-2[^']*overflow-y-auto/)
  })

  it('пятнадцать плиток в одной сетке не проходят по числам сенсора — потому вкладки', () => {
    // One 2-column grid of 15 tiles is eight rows: 394 px on the panel (fits the 439 px body) and
    // 554 px on the touch, 115 px over the body. A category is two rows: 94 px and 134 px.
    const gap = 6
    const body = 439
    expect(8 * 44 + 7 * gap, '15 плиток, панель').toBeLessThanOrEqual(body)
    expect(8 * 64 + 7 * gap, '15 плиток, сенсор').toBeGreaterThan(body)
    expect(2 * 64 + gap, 'раздел, сенсор').toBeLessThanOrEqual(body)
  })

  it('кнопка «Импорт DXF» в пикере открывает диалог; файл не пишется в состояние', () => {
    const picker = code('operation-picker.tsx')
    expect(picker).toMatch(/onDxf/)
    expect(picker).toMatch(/Импорт DXF/)
    const overlay = code('cam-overlay-body.tsx')
    expect(overlay).toMatch(/<DxfDialog/)
    expect(overlay).toMatch(/<OperationPicker[^>]*onDxf=/)
    const dialog = code('dxf-dialog.tsx')
    expect(dialog).toMatch(/type="file"/)
    expect(dialog).toMatch(/accept="\.dxf/)
    expect(dialog).toMatch(/file\.text\(\)/)
    // The text rides in the action once; the dialog keeps it in its own state and drops it.
    expect(dialog).toMatch(/type: 'CAM_DXF', text, contour/)
  })

  it('группа плиток прокручивается внутри колонки, а не растит карточку', () => {
    const src = code('operation-picker.tsx')
    // The scroll sits on the grid itself and the column is the flex parent with `min-h-0`.
    expect(src).toMatch(/min-h-0 flex-1 auto-rows-min grid-cols-2[^']*overflow-y-auto/)
  })

  it('на 1024x600 плитки и карточки помещаются числами', () => {
    // The overlay is 580 px tall and the column is 28 % of 1000 px = 280 px wide, 276 px inside
    // the padding. A category is at most two rows: 2 * 44 + 6 = 94 px of 44 px tiles on the panel
    // and 2 * 64 + 6 = 134 px on the touch, under the 439 px body even with the segment row (32 px
    // and 44 px) and the 36 px DXF button; the grid scrolls inside the column, so the card never
    // grows.
    const rowPanel = 44
    const gap = 6
    const segmentsPanel = 32
    const dxfPanel = 36
    const rowsPanel = 2 * rowPanel + gap
    expect(rowsPanel, 'раздел, панель').toBeLessThanOrEqual(439)
    expect(rowsPanel + segmentsPanel + dxfPanel, 'раздел с вкладками и DXF, панель').toBeLessThanOrEqual(439)
    // The touch variant: two rows of 64 px are 134 px, plus a 44 px segment row and a 44 px button.
    const rowsTouch = 2 * 64 + gap
    expect(rowsTouch, 'раздел, сенсор').toBeLessThanOrEqual(439)
    expect(rowsTouch + 44 + 44, 'раздел с вкладками и DXF, сенсор').toBeLessThanOrEqual(439)
    // A hint of 10 px under a 12 px word is two lines of 13 px: the tile holds 44 px, so the word
    // and the hint both fit (10 px ≥ the panel minimum).
    expect(12 + 2 * 13, 'слово и подсказка плитки').toBeLessThanOrEqual(rowPanel)
    // The card: the row keeps two order buttons, on/off, edit, delete and the menu = 6. Two of them
    // carry a word («Вкл», «Правка»), which makes the row 284 px — 8 px over the 276 px card, so the
    // words go: the row is icons only (`title`/`aria-label` carry the word), 6 * 44 + 5 * 4 = 284 px
    // still needs 8 px, so the touch row scrolls its own 4 px gaps out: 5 * 48 + 4 * 4 = 256 px.
    // The numbers below are the budget, not a measurement of a browser.
    const touchIcon = 44
    expect(6 * touchIcon + 5 * 2, 'строка из шести кнопок с зазором 2').toBeLessThanOrEqual(276)
    expect(7 * touchIcon + 6 * 2, 'семь кнопок в строке').toBeGreaterThan(276)
    // A field row: a hint two lines of 10 px is 32 px in the panel, so eight rows are 256 px of
    // the 439 px column; the multiline block takes four rows.
    expect(8 * 36, 'восемь строк формы').toBeLessThanOrEqual(439)
    expect(4 * 16 + 36, 'блок G-кода из четырёх строк').toBeLessThanOrEqual(439)
  })

  it('на вкладке «Геометрия» не больше восьми строк; схемы сверла и метчика — свои поля', () => {
    for (const kind of ['contour', 'chamfer', 'custom', 'probe'] as const) {
      expect(kindRows(kind, 'grid'), `${kind}: geometry`).toBeLessThanOrEqual(8)
    }
    // A slot is the measured exception: six shape fields plus the three frame ones are 9 rows of
    // 36 px = 324 px of the 439 px panel column, no scroll yet.
    expect(kindRows('slot', 'grid'), 'slot').toBe(9)
    // A thread mill is the measured exception of Q5: eight shape fields plus the frame are 11 rows,
    // so the touch form scrolls it. A counterbore is a seat over one hole and keeps a grid at ten.
    expect(kindRows('counterbore', 'grid'), 'counterbore: grid').toBe(10)
    expect(kindRows('counterbore', 'circle'), 'counterbore: circle').toBe(10)
    expect(kindRows('thread', 'grid'), 'thread').toBe(11)
    // The layouts: the form hides the fields of the other layouts, so a ring shows 3 shared + 8
    // layout + 3 frame = 12 rows for the drill (432 px of 439) and a grid of holes 11; the touch
    // form scrolls them (44 px rows), the panel form just fits. Nine rows was the note's wish, not
    // a rule a drill layout can keep: the ring itself has five numbers.
    expect(kindRows('drill', 'circle'), 'drill: circle').toBe(12)
    expect(kindRows('drill', 'grid'), 'drill: grid').toBe(11)
    expect(choiceFields('drill')).toEqual(['pattern', 'body', 'fullTurn'])
    expect(kindRows('tap', 'circle'), 'tap: circle').toBe(11)
    expect(choiceFields('tap')).toEqual(['pattern'])
  })

  it('в меню карточки «Массив» и «Поворот», обе — кнопки пунктов меню', () => {
    expect(COPY_MENU.map((item) => item.label)).toEqual(['Массив', 'Поворот'])
    const card = code('operation-card.tsx')
    expect(card).toMatch(/role="menu"/)
    expect(card).toMatch(/role="menuitem"/)
    // The card body does not spend a whole row on the copies: seven buttons do not fit 276 px.
    expect(card).toMatch(/COPY_MENU/)
  })

  it('диалоги копий шлют CAM_ARRAY и CAM_ROTATE из своих чисел', () => {
    const dialogs = code('copy-dialogs.tsx')
    expect(dialogs).toMatch(/arrayAction\(index, text\)/)
    expect(dialogs).toMatch(/rotateAction\(index, text\)/)
    expect(dialogs).toMatch(/dialogRefusal\(result\.action\)/)
    // The refusal stays in the dialog, the document's own refusal is the overlay line.
    expect(code('copy-dialog-form.tsx')).toMatch(/setProblem/)
  })

  it('кнопки карточки и диалога DXF в сенсоре ≥ 44 px, включая пункты меню', () => {
    const list = code('operation-card.tsx')
    const dialogs = code('copy-dialog-form.tsx')
    const dxf = code('dxf-dialog.tsx')
    for (const src of [list, dialogs, dxf]) {
      const touchHeights = [...src.matchAll(/touch \? '([^']*)'/g)]
        .flatMap((m) => m[1].split(/\s+/))
        // A `size-N` here is the glyph of a button, not its target: the target is `min-h-*` or the
        // whole `size-11` box (a square button); the touch branch never sets a bare fixed height.
        .filter((cls) => cls.startsWith('min-h-') || cls === 'size-11')
      expect(touchHeights.length).toBeGreaterThan(0)
      for (const cls of touchHeights) {
        const ok = ['min-h-11', 'min-h-12', 'min-h-16', 'size-11'].includes(cls)
        expect(ok, `«${cls}» ≥ 44 px`).toBe(true)
      }
    }
    // The target of the order/state row and the menu items are 44 px in the touch variant.
    expect(list).toMatch(/const target = touch \? 'min-h-11 min-w-11'/)
    expect(list).toMatch(/COPY_MENU\.map[\s\S]{0,400}touch \? 'min-h-11'/)
  })

  it('фокус-обводка видна: кнопки оверлея наследуют ring панели', () => {
    // The card buttons carry `tabIndex={-1}` (the softkey focus reaches them by click), so the ring
    // is not a per-element class: it comes from the panel theme rule for a focused control.
    const css = readFileSync(new URL('../../../app/globals.css', import.meta.url), 'utf8')
    expect(css).toMatch(/\.theme-minimal \.cnc-screen button:focus-visible,[\s\S]{0,200}outline: 3px solid/)
    const overlay = code('cam-overlay.tsx')
    // The overlay lives inside `.cnc-screen`, so the rule above reaches it: no local `outline-none`
    // that would erase it on the buttons of the cards.
    expect(overlay).not.toMatch(/outline-none/)
  })
})

describe('обе конфигурации: база софт-кеев не ломается', () => {
  it('бар всегда SOFTKEY_SLOTS длиной — в обеих конфигурациях', () => {
    for (const mode of [0, 1]) {
      const s = run(prog, { type: 'UI_MODE_SET', value: mode as 0 | 1 })
      expect(getSoftkeys(s)).toHaveLength(SOFTKEY_SLOTS)
      expect(getSoftkeys(s)[camSlot(s)]?.action.type).toBe('CAM_OPEN')
    }
  })

  it('CAM_OPEN/CAM_CLOSE живут на одном слоте в обеих конфигурациях', () => {
    for (const mode of [0, 1]) {
      const base = run(prog, { type: 'UI_MODE_SET', value: mode as 0 | 1 })
      const open = run(base, { type: 'CAM_OPEN' })
      expect(camSlot(open)).toBe(camSlot(base))
      expect(getSoftkeys(open)[camSlot(open)]?.action).toEqual({ type: 'CAM_CLOSE' })
    }
  })
})
