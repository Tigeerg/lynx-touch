// The quick-program subsystem (PLAN-QUICKPROG, 7.4, C4): the `CAM_*` actions in the registry.
//
// `CamState` is a branch of `MachineState` (`cam: CamState | null`), and the actions reach it the
// same way the other subsystems do: `cam.ts` declares the union and the predicate, `machine.ts`
// registers the entry, `subsystem.ts` holds the shape. A subsystem does not import `machine.ts`.
//
// The document is not the machine: the overlay edits a `QuickProgram`, and only `CAM_SAVE` hands
// the result to the files. The write lives in `cam-save.ts` (`saveCam`), the form and the list in
// `cam-form-run.ts`, the form and header edits in `cam-edit.ts`, the saved file in `cam-open.ts`;
// this file is the table of actions and the switch.
//
// A field edit and the preview are two different clocks (PLAN-QUICKPROG, 7.5): `CAM_PARAM` applies
// the typed text and the problem at once, marks the preview `pending` and leaves it alone, and
// `CAM_PREVIEW` from the UI timer recomputes it. The list and accept actions are not typing, so
// they recompute straight away and clear the flag. A delayed timer never wins over a save: the
// save reads the document, not the preview. Every recompute takes the lines AND the stats of the
// plan from `shownPreview`, so the header describes the program the operator sees.
import { isBusy } from '../guards'
import { arrayCopies, rotatedCopies, type CopyResult } from './copy-ops'
import { newProgram } from './defaults'
import type { Operation } from './ops'
import { saveCam } from './cam-save'
import { openSaved } from './cam-open'
import { amend, type Amending } from './cam-edit'
import { listRun, shownForm } from './cam-form-run'
import { shownPreview } from './cam-preview'
import { dxfResult } from './dxf-result'
import type { Result } from '../subsystem'
import type { MachineState } from '../types'
import type { CamAction, CamState } from './types'

export type { CamAction, CamState }

const CAM_TYPES = new Set<string>([
  'CAM_OPEN',
  'CAM_CLOSE',
  'CAM_OP',
  'CAM_PARAM',
  'CAM_DRO',
  'CAM_PROGRAM',
  'CAM_PREVIEW',
  'CAM_CANCEL',
  'CAM_ACCEPT',
  'CAM_EDIT',
  'CAM_REMOVE',
  'CAM_TOGGLE',
  'CAM_MOVE',
  'CAM_SAVE',
  'CAM_OPEN_SAVED',
  'CAM_ARRAY',
  'CAM_ROTATE',
  'CAM_DXF',
])

export const isCamAction = (a: { type: string }): a is CamAction => CAM_TYPES.has(a.type)

/** The overlay of a state; a CAM_* action without an open overlay is a no-op with a hint. */
const closed = (s: MachineState): Result => ({ s, msg: 'Быстрая УП не открыта', level: 'warn' })

/** The overlay of the action, or the refusal when the overlay is closed. */
const overlay = (s: MachineState, run: (cam: CamState) => Result): Result => (s.cam === null ? closed(s) : run(s.cam))

export function camReducer(s: MachineState, a: CamAction): Result {
  switch (a.type) {
    case 'CAM_OPEN': {
      if (isBusy(s)) return { s, msg: 'Остановите программу перед быстрой УП', level: 'warn' }
      if (s.run === 'ESTOP') return { s, msg: 'Сначала сбросьте аварию', level: 'warn' }
      return { s: { ...s, cam: freshCam(s) } }
    }

    case 'CAM_CLOSE':
      return { s: { ...s, cam: null } }

    case 'CAM_OP':
      return overlay(s, (cam) => ({ s: { ...s, cam: opened(cam, a.kind) } }))

    case 'CAM_PARAM':
    case 'CAM_DRO':
    case 'CAM_PROGRAM':
    case 'CAM_PREVIEW':
    case 'CAM_CANCEL':
      return overlay(s, (cam) => amend(s, cam, a as Amending))

    case 'CAM_ACCEPT':
    case 'CAM_EDIT':
    case 'CAM_TOGGLE':
    case 'CAM_REMOVE':
    case 'CAM_MOVE':
      return overlay(s, (cam) => listRun(s, cam, a))

    case 'CAM_SAVE':
      return overlay(s, (cam) => saveCam(s, cam, a.at))

    case 'CAM_OPEN_SAVED':
      return overlay(s, (cam) => openSaved(s, cam))

    case 'CAM_ARRAY':
    case 'CAM_ROTATE':
      return overlay(s, (cam) => copied(s, cam, a))

    case 'CAM_DXF':
      return overlay(s, (cam) => dxfResult(s, cam, a.text, a.contour))
  }
}

/**
 * A new overlay (PLAN-QUICKPROG, 7.4): an empty document over the machine. The machine parameters
 * travel with the document, not in it: the tap check reads the rigid tapping switch from the same
 * state the panel shows, and a document saved on one machine does not carry the switch of another.
 */
const freshCam = (s: MachineState): CamState => ({
  program: { ...newProgram(), params: { ...s.params } },
  form: null,
  editing: null,
  error: null,
  preview: null,
  stats: null,
  saved: null,
  pendingSave: null,
})

/** The overlay with a fresh form of `kind` over the document, its preview and its numbers at once. */
const opened = (cam: CamState, kind: Operation['kind']): CamState => shownForm(cam, kind)

/** The copies and the turn of an operation, or the reason the kind has none. */
const copied = (s: MachineState, cam: CamState, a: Action): Result => {
  const op = cam.program.operations[a.index]
  if (op === undefined) return { s, msg: 'Операции с таким номером нет', level: 'warn' }
  const result = a.type === 'CAM_ARRAY' ? arrayCopies(op, a) : rotatedCopies(op, a)
  return listed(s, cam, a.index, result)
}

/** The four action shapes `copied` reads: the index and the numbers of the copies. */
type Action = Extract<CamAction, { type: 'CAM_ARRAY' | 'CAM_ROTATE' }>

/**
 * The document with the source and its copies in place of the source (PLAN-QUICKPROG, 7.6), or the
 * refusal as a message. The copies are flattened here, on the action, so the list holds them and the
 * `.qprog` stays flat; a refusal changes nothing but the message line.
 */
const listed = (s: MachineState, cam: CamState, at: number, result: CopyResult): Result => {
  if (!result.ok) return { s, msg: result.reason, level: 'warn' }
  const list = cam.program.operations
  const operations = [...list.slice(0, at), ...result.operations, ...list.slice(at + 1)]
  const program = { ...cam.program, operations }
  return {
    s: { ...s, cam: { ...cam, program, ...shownPreview(program, cam), error: null, pending: false } },
    msg: `Копий: ${String(result.operations.length - 1)}`,
  }
}
