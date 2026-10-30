// The defaults a new program and a new operation start from (PLAN-QUICKPROG, 7.2 and 7.4).
//
// Every number here has a counterpart in the console defaults of `apps/quickprog`, so the panel
// and the demo open the same form with the same values. The cutting data is never written down
// here: it comes from `feeds.ts`, which stands on the formulas of `../calc`.
import type { DrillBody, Material } from './feeds'
import { cuttingFor } from './feeds'
import type { NewOperation } from './operation-init'
import type {
  CirclePocketOperation,
  DrillOperation,
  FaceOperation,
  Operation,
  QuickProgram,
  RectPocketOperation,
  StockBox,
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

const common = (init: NewOperation, tool: ToolRef, material: Material, body: DrillBody | null) => ({
  id: init.id,
  enabled: true,
  tool,
  cutting: cuttingFor(material, tool.diameterMm, body),
  coolant: 'flood' as const,
  material,
  topZ: init.topZ,
  depthMm: init.depthMm ?? DEFAULT_DEPTH_MM,
  stepDownMm: init.stepDownMm ?? DEFAULT_STEP_DOWN_MM,
  notes: '',
})

/**
 * A new operation of `kind`. The face plane sits at the bottom of the pass, both pockets take the
 * 70 % step and a drill has no axial step (one cycle) and no peck (the planner decides).
 */
export function newOperation(kind: 'face', init: NewOperation): FaceOperation
export function newOperation(kind: 'pocket', init: NewOperation): RectPocketOperation
export function newOperation(kind: 'circle-pocket', init: NewOperation): CirclePocketOperation
export function newOperation(kind: 'drill', init: NewOperation): DrillOperation
export function newOperation(kind: Operation['kind'], init: NewOperation): Operation
export function newOperation(kind: Operation['kind'], init: NewOperation): Operation {
  const tool = init.tool ?? DEFAULT_TOOL
  const material = init.material ?? DEFAULT_MATERIAL
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
