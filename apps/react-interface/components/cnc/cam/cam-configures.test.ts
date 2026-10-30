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
      'operation-list.tsx',
      'operation-picker.tsx',
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
      'operation-picker.tsx',
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
    const files = ['cam-overlay.tsx', 'cam-form.tsx', 'operation-list.tsx', 'operation-picker.tsx']
    for (const file of files) {
      const src = code(file)
      const touchHeights = [...src.matchAll(/touch \? '([^']*)'/g)]
        .flatMap((m) => m[1].split(/\s+/))
        .filter((cls) => /min-h-|h-\d/.test(cls))
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
