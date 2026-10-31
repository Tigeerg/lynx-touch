// «Взять с DRO» (PLAN-QUICKPROG, 5 step 2 and 6): the field of the form takes the work coordinate
// of the machine, the number the operator reads in the position display.
//
// The machine keeps the absolute coordinate, the display subtracts the active work offset (`toWork`,
// `coords.ts`). The action writes that work number into the form as typed text (`CAM_PARAM`), so the
// form shows what the operator would have typed and the preview waits for its timer as usual.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import type { MachineState } from '../types'
import type { CamState } from './cam'

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)
const camOf = (s: MachineState): CamState => {
  if (s.cam === null) throw new Error('the overlay is closed')
  return s.cam
}

/** The panel with the overlay open and a pocket form, the machine standing somewhere. */
const form = (machine: { X: number; Y: number; Z: number }, offset = 0): MachineState =>
  run(
    { ...initialState, machine: { X: machine.X, Y: machine.Y, Z: machine.Z, A: 0 }, activeOffset: offset },
    { type: 'CAM_OPEN' },
    { type: 'CAM_OP', kind: 'pocket' },
  )

describe('CAM_DRO: координата станка в поле формы', () => {
  it('пишет рабочую координату с учётом смещения', () => {
    // `INITIAL_OFFSETS[0]` is G54, not zero: the display the operator reads is machine minus offset.
    const s = form({ X: 10, Y: -20, Z: -30 })
    const offset = s.offsets[s.activeOffset].values
    const after = run(
      s,
      { type: 'CAM_DRO', field: 'x', axis: 'X' },
      { type: 'CAM_DRO', field: 'y', axis: 'Y' },
      { type: 'CAM_DRO', field: 'topZ', axis: 'Z' },
    )
    const values = camOf(after).form?.values ?? {}

    // `rect.y` is the centre of the pocket, so the drop goes into `rect.y`; a pocket has no top of
    // the material beyond `topZ`, and `topZ` is a field of every kind.
    expect(values['rect.x']).toBe(String(Math.round((10 - offset.X) * 100) / 100))
    expect(values['rect.y']).toBe(String(Math.round((-20 - offset.Y) * 100) / 100))
    expect(values.topZ).toBe(String(Math.round((-30 - offset.Z) * 100) / 100))
  })

  it('без открытой формы — warn и сообщение, состояние не меняется', () => {
    const open = run(initialState, { type: 'CAM_OPEN' })
    const after = run(open, { type: 'CAM_DRO', field: 'x', axis: 'X' })

    expect(after.message?.level).toBe('warn')
    expect(after.cam).toEqual(open.cam)
  })

  it('поле, которого у операции нет, не пишется: отказ', () => {
    // A face has no centre X: the pair names no value of the form, and the form stays untouched.
    const s = run(
      { ...initialState, machine: { X: 1, Y: 2, Z: 3, A: 0 } },
      { type: 'CAM_OPEN' },
      { type: 'CAM_OP', kind: 'face' },
    )
    const after = run(s, { type: 'CAM_DRO', field: 'x', axis: 'X' })

    expect(after.message?.level).toBe('warn')
    expect(camOf(after).form).toEqual(camOf(s).form)
  })
})
