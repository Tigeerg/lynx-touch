// The insert of the quick program into the open file: the block goes before the final M30, the
// file keeps its own stock tags and end, and the insert refuses while the machine is busy.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import type { MachineState } from '../types'

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)

const openFile = ['%', 'N10 G21 G90', 'N20 G0 X0 Y0', 'N30 M30', '%']

const panel: MachineState = {
  ...initialState,
  workplace: 'RUN',
  mode: 'AUTO',
  files: [{ name: 'OPEN.ngc', title: 'OPEN', lines: openFile, modified: '' }],
  activeFile: 0,
}

/** The overlay with one accepted face operation. */
const withFace = (s: MachineState = panel): MachineState =>
  run(s, { type: 'CAM_OPEN' }, { type: 'CAM_OP', kind: 'face' }, { type: 'CAM_ACCEPT' })

describe('вставка быстрой УП в открытый файл', () => {
  it('ставит блок перед M30 и не добавляет второй заголовок, хвост и %', () => {
    const s = run(withFace(), { type: 'CAM_INSERT', at: 1_000 })
    const lines = s.files[0].lines

    expect(lines.filter((line) => line.trim() === '%')).toHaveLength(2)
    expect(lines[lines.length - 2]).toMatch(/M30/i)
    expect(lines[lines.length - 1].trim()).toBe('%')
    expect(lines.join('\n')).toContain('(QUICKPROG INSERT:')
    expect(lines.join('\n')).not.toContain('(MACHINE)')
    expect(lines.join('\n')).not.toContain('(UI_STOCK')
    expect(lines.join('\n')).toContain('(UI_OP:')
    // The sequence continues the open file instead of starting from N10 again.
    expect(lines.join('\n')).toMatch(/N40 /)
    expect(s.files[0].modified).not.toBe('')
    expect(s.message?.text).toContain('OPEN.ngc')
  })

  it('отказывает при выполнении: файл не меняется', () => {
    const busy = { ...withFace(), run: 'RUN' as const }
    const s = run(busy, { type: 'CAM_INSERT', at: 1_000 })

    expect(s.files[0].lines).toEqual(panel.files[0].lines)
    expect(s.message?.text).toContain('Выполняется')
  })

  it('отказывает без открытого файла', () => {
    const empty = { ...withFace(), activeFile: 99 }
    const s = run(empty, { type: 'CAM_INSERT', at: 1_000 })

    expect(s.message?.text).toContain('Нет открытой программы')
  })
})
