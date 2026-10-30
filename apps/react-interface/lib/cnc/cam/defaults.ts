// The defaults a new program and a new operation start from (PLAN-QUICKPROG, 7.2 and 7.4).
//
// Every number here has a counterpart in the console defaults of `apps/quickprog`, so the panel
// and the demo open the same form with the same values. The cutting data is never written down
// here: it comes from `feeds.ts`, which stands on the formulas of `../calc`.
import type { DrillBody, Material } from './feeds'
import { cuttingFor } from './feeds'
import type { NewOperation } from './operation-init'
import { newProbe } from './defaults-probe'
import { common } from './defaults-common'
export { DEFAULT_PROBE_RANGE_MM, TOUCH_FEED_MM_MIN } from './probe-defaults'
import type {
  ChamferOperation,
  CirclePocketOperation,
  ContourOperation,
  CustomOperation,
  DrillOperation,
  FaceOperation,
  Operation,
  ProbeOperation,
  QuickProgram,
  RectPocketOperation,
  SlotOperation,
  StockBox,
  TapOperation,
  ToolRef,
} from './ops'
import { stockTop } from './ops'

export type { Material } from './feeds'
export type { NewOperation } from './operation-init'
export { cuttingFor, millCutting } from './feeds'

/** The console stock of the demo: 100 x 80 x 20 mm, centred on zero, its top at Z 0. */
export const DEFAULT_STOCK: StockBox = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

/** The material a new operation is rated by. */
export const DEFAULT_MATERIAL: Material = 'aluminium'

/** The default tool: number 1, a flat end mill of 10 mm, the H row equal to the tool number. */
export const DEFAULT_TOOL: ToolRef = { number: 1, diameterMm: 10, hNumber: 1 }

/** The step across of a pocket raster and the strip overlap of a face, % of the diameter. */
export const STEP_OVER_PCT = 70

/** The G64 path tolerance of the header, mm, as the reference post writes it. */
export const G64_TOLERANCE_MM = 0.01

/** The safe Z of a new program above the top of the stock, mm. */
export const SAFE_Z_CLEARANCE_MM = 5

/** The depth and the axial step of a new operation, mm, as the `--op` defaults. */
export const DEFAULT_DEPTH_MM = 2
export const DEFAULT_STEP_DOWN_MM = 2

/** The leg of a new chamfer, mm: one pass of a flat mill, under the radius of the default tool. */
export const DEFAULT_CHAMFER_MM = 1

/**
 * The pitch of a new tap, mm per turn: a standard M8 coarse thread, the one an operator starts from
 * and edits. A hole of the default tool is too small for M8, but the form says so, not the default.
 */
export const DEFAULT_TAP_PITCH_MM = 1.25

/** A new program: schema 1, G54, G64 0.01, the M1 stop on and the safe Z 5 mm above the stock. */
export const newProgram = (stock: StockBox = DEFAULT_STOCK, name = 'QUICK'): QuickProgram => ({
  schema: 1,
  name,
  units: 'mm',
  wcs: 'G54',
  stock,
  safeZ: stockTop(stock) + SAFE_Z_CLEARANCE_MM,
  g64P: G64_TOLERANCE_MM,
  g64Q: G64_TOLERANCE_MM,
  stopAfterOp: true,
  operations: [],
})

/** The tool reference of a number and a diameter, with the H row following the number. */
export const toolRef = (number = DEFAULT_TOOL.number, diameterMm = DEFAULT_TOOL.diameterMm): ToolRef => ({
  number,
  diameterMm,
  hNumber: number,
})

/**
 * The hole radius of a new tap, mm: the thread of a standard coarse M8 is 6.8 mm at the tap, and the
 * rule of the thumb is `D - pitch`. The tap diameter the operator typed is the thread, so the hole
 * follows the pitch of the form until the operator types a radius.
 */
const tapRadius = (diameterMm: number): number => Math.max((diameterMm - DEFAULT_TAP_PITCH_MM) / 2, 0)

/**
 * A new operation of `kind`. The face plane sits at the bottom of the pass, both pockets take the
 * 70 % step and a drill has no axial step (one cycle) and no peck (the planner decides).
 */
export function newOperation(kind: 'face', init: NewOperation): FaceOperation
export function newOperation(kind: 'pocket', init: NewOperation): RectPocketOperation
export function newOperation(kind: 'circle-pocket', init: NewOperation): CirclePocketOperation
export function newOperation(kind: 'drill', init: NewOperation): DrillOperation
export function newOperation(kind: 'contour', init: NewOperation): ContourOperation
export function newOperation(kind: 'slot', init: NewOperation): SlotOperation
export function newOperation(kind: 'chamfer', init: NewOperation): ChamferOperation
export function newOperation(kind: 'tap', init: NewOperation): TapOperation
export function newOperation(kind: 'custom', init: NewOperation): CustomOperation
export function newOperation(kind: 'probe', init: NewOperation): ProbeOperation
export function newOperation(kind: Operation['kind'], init: NewOperation): Operation
export function newOperation(kind: Operation['kind'], init: NewOperation): Operation {
  const tool = init.tool ?? DEFAULT_TOOL
  const material = init.material ?? DEFAULT_MATERIAL
  if (kind === 'probe') return newProbe(init, tool, material)
  switch (kind) {
    case 'face': {
      const fields = common(init, tool, material, null)
      return { ...fields, kind, z: init.topZ - fields.depthMm, stepOverPct: STEP_OVER_PCT }
    }
    case 'pocket': {
      const fields = common(init, tool, material, null)
      return {
        ...fields,
        kind,
        rect: { x: 0, y: 0, w: 60, h: 40 },
        cornerRadiusMm: 0,
        finishAllowanceMm: 0,
        stepOverPct: STEP_OVER_PCT,
      }
    }
    case 'circle-pocket': {
      const fields = common(init, tool, material, null)
      return { ...fields, kind, circle: { x: 0, y: 0, r: 10 }, finishAllowanceMm: 0, stepOverPct: STEP_OVER_PCT }
    }
    case 'contour': {
      const fields = common(init, tool, material, null)
      // A 40 x 40 square against the walk: the console default an operator starts from and edits.
      return {
        ...fields,
        kind,
        points: [
          { x: -20, y: -20, z: init.topZ },
          { x: 20, y: -20, z: init.topZ },
          { x: 20, y: 20, z: init.topZ },
          { x: -20, y: 20, z: init.topZ },
        ],
        closed: true,
        allowanceMm: 0,
        side: 'material',
      }
    }
    case 'slot': {
      const fields = common(init, tool, material, null)
      // A 40 mm slot along X, as wide as the tool: the start an operator edits.
      return {
        ...fields,
        kind,
        from: { x: -20, y: 0, z: init.topZ },
        to: { x: 20, y: 0, z: init.topZ },
        widthMm: tool.diameterMm,
        stepOverPct: STEP_OVER_PCT,
      }
    }
    case 'chamfer': {
      const fields = common(init, tool, material, null)
      // A 30 mm edge along X: one leg of the mill, one pass. The depth is the chamfer, so the
      // operation's depth follows it.
      const chamferMm = DEFAULT_CHAMFER_MM
      return {
        ...fields,
        depthMm: chamferMm,
        kind,
        from: { x: -15, y: 0, z: init.topZ },
        to: { x: 15, y: 0, z: init.topZ },
        chamferMm,
      }
    }
    case 'tap': {
      // The layout and the hole are the drilling ones; the tap has no peck and no R plane of its own.
      const fields = { ...common(init, tool, material, null), stepDownMm: 0 }
      return {
        ...fields,
        kind,
        pattern: 'hole',
        pitchMm: DEFAULT_TAP_PITCH_MM,
        hole: { x: 0, y: 0, r: tapRadius(tool.diameterMm) },
        x0: 0,
        y0: 0,
        cols: 1,
        rows: 1,
        dx: 0,
        dy: 0,
        count: 1,
        startDeg: 0,
        arcDeg: 360,
        fullTurn: true,
      }
    }
    case 'custom': {
      // A free block has no cut of its own: the frame is kept for the header of the operation, the
      // tool is empty (no change, no UI_TOOL) until the operator names one.
      const fields = { ...common(init, tool, material, null), tool: { number: 0, diameterMm: 0, hNumber: 0 } }
      return { ...fields, kind, gcode: [] }
    }
    case 'drill': {
      const body = init.body ?? 'hss'
      const fields = { ...common(init, tool, material, body), stepDownMm: 0 }
      return {
        ...fields,
        kind,
        pattern: 'hole',
        body,
        peckMm: init.peckMm ?? 0,
        rMm: 0,
        hasR: false,
        hole: { x: 0, y: 0, r: tool.diameterMm / 2 },
        x0: 0,
        y0: 0,
        cols: 1,
        rows: 1,
        dx: 0,
        dy: 0,
        count: 1,
        startDeg: 0,
        arcDeg: 360,
        fullTurn: true,
      }
    }
  }
}
