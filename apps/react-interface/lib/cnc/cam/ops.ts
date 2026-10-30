// The quick-program document (PLAN-QUICKPROG, 6 and 7.4).
//
// This module holds the types and the defaults that turn a form into a document, no planning and
// no output. The operation is a discriminated union by `kind`: the geometry and the parameters
// that exist only for one kind live in that arm, so a form or a planner that forgets a kind does
// not compile. The common fields of every kind sit in `OperationBase`.
//
// Units: millimetres, machine coordinates. `topZ` is the top of the material for the operation,
// `depthMm` is the depth below it (positive), `stepDownMm` is the largest axial cut of one pass.
import type { DrillBody, Material } from './feeds'

export type { DrillBody, Material } from './feeds'

/** A point in machine coordinates, mm; `z` is the tool tip (the trajectory centre). */
export interface Point3 {
  x: number
  y: number
  z: number
}

/** Axis-aligned rectangle in XY; `x`/`y` is the lower-left corner, mm. */
export interface Rect {
  x: number
  y: number
  w: number
  h: number
}

/** A circle in XY: centre and radius, mm. */
export interface Circle {
  x: number
  y: number
  r: number
}

/** The stock as its lower and upper corner, mm: the top of the stock is `max.z`. */
export interface StockBox {
  min: Point3
  max: Point3
}

/**
 * Tool number in the machine table. The diameter is repeated in the operation so the document
 * stays checkable without the table. `hNumber` is the row of the G43 length table; zero means
 * "the tool number", as the survey of a .qprog without the field expects.
 */
export interface ToolRef {
  number: number
  diameterMm: number
  hNumber: number
}

/** Cutting data of one operation; the plunge feed is the Z feed and is never the XY feed. */
export interface Cutting {
  spindleRpm: number
  feedMmMin: number
  plungeFeedMmMin: number
}

export type Coolant = 'off' | 'mist' | 'flood'

export type Wcs = 'G54' | 'G55' | 'G56' | 'G57' | 'G58' | 'G59'

export type OperationKind = 'face' | 'pocket' | 'circle-pocket' | 'drill' | 'contour' | 'slot' | 'chamfer'

/** The fields every kind shares: the tool, the modes, the stock frame and the notes. */
export interface OperationBase {
  id: string
  kind: OperationKind
  enabled: boolean
  tool: ToolRef
  cutting: Cutting
  coolant: Coolant
  material: Material
  topZ: number
  depthMm: number
  stepDownMm: number
  notes: string
}

/** Face milling of the stock top: strips across the stock at `stepOverPct` of the diameter. */
export interface FaceOperation extends OperationBase {
  kind: 'face'
  /** The plane to cut, not always the stock top: the operator may take a light pass. */
  z: number
  stepOverPct: number
}

/** Rectangular pocket; the raster does not cut the corner radius, so the corner keeps the mill. */
export interface RectPocketOperation extends OperationBase {
  kind: 'pocket'
  rect: Rect
  cornerRadiusMm: number
  /** Stock left on the walls only; the floor is cut to the final depth in the same pass. */
  finishAllowanceMm: number
  stepOverPct: number
}

/** Round pocket cut by a spiral; the allowance is the wall stock, the step is radial. */
export interface CirclePocketOperation extends OperationBase {
  kind: 'circle-pocket'
  circle: Circle
  finishAllowanceMm: number
  stepOverPct: number
}

/** A single hole at the centre; the radius of `hole` is the drill radius. */
export interface DrillHoleGeometry {
  pattern: 'hole'
  hole: Circle
}

/** `cols` x `rows` holes from (x0, y0), row by row along X. */
export interface DrillGridGeometry {
  pattern: 'grid'
  x0: number
  y0: number
  cols: number
  rows: number
  dx: number
  dy: number
}

/** `count` holes on a circle: the first at `startDeg`, clockwise, a full turn or `arcDeg`. */
export interface DrillRingGeometry {
  pattern: 'circle'
  x: number
  y: number
  count: number
  r: number
  startDeg: number
  arcDeg: number
  fullTurn: boolean
}

export type DrillGeometry = DrillHoleGeometry | DrillGridGeometry | DrillRingGeometry

/** The hole layout of one drilling operation: one hole, a grid or a ring. */
export type DrillPattern = DrillGeometry['pattern']

/**
 * Drilling: one kind, three layouts. `peckMm` above zero selects G83 with that Q, zero leaves the
 * cycle to the planner; `body` is the drill material and selects the row of the table.
 */
export interface DrillOperation extends OperationBase {
  kind: 'drill'
  pattern: DrillPattern
  body: DrillBody
  peckMm: number
  /**
   * The R plane of the cycle, mm, and whether the document set it. A zero is not "R at the top"
   * but "the default" (one millimetre above the top, clamped to safe Z), the same way a zero peck
   * is not a peck. Both are part of the `.qprog` (`op.r`) and of the demo model.
   */
  rMm: number
  hasR: boolean
  /** The single hole: `hole` is the centre and the drill radius. The ring uses `hole.x/y`. */
  hole: Circle
  x0: number
  y0: number
  cols: number
  rows: number
  dx: number
  dy: number
  count: number
  startDeg: number
  arcDeg: number
  fullTurn: boolean
}

/** Contour along a polyline in XY (PLAN-QUICKPROG, 7.6): the centre walks `D/2 + allowanceMm` off the
 * profile, on the material side or outside; a closed contour returns to its first point, an open one
 * leads in and out. The `z` of a point is unused.
 */
export interface ContourOperation extends OperationBase {
  kind: 'contour'
  /** The profile vertices in XY, in the order of the walk; `closed` adds the last segment. */
  points: Point3[]
  closed: boolean
  /** Stock left on the profile beyond the mill radius, mm; zero cuts to size. */
  allowanceMm: number
  /** `material` keeps the mill inside the profile, `air` keeps it outside. */
  side: 'material' | 'air'
}

/**
 * A straight keyway (PLAN-QUICKPROG, 7.6): the mill cuts from `from` to `to` along one straight, and
 * `widthMm` is the width of the slot across the walk. A slot as wide as the mill is one pass; a wider
 * one is several passes across, each `stepOverPct` of the diameter apart.
 */
export interface SlotOperation extends OperationBase {
  kind: 'slot'
  from: Point3
  to: Point3
  /** The width of the slot across the walk, mm; between `D` and `2D`. */
  widthMm: number
  /** The step between the strips across the slot, % of the diameter, as of a pocket. */
  stepOverPct: number
}

/**
 * A chamfer along one straight edge (PLAN-QUICKPROG, 7.6): the mill runs `from` -> `to`, sunk
 * `chamferMm` below the top, so its corner takes the edge off. Only a straight edge is modelled;
 * `grain` names the direction of the edge against the part and is kept for a later planner, the cut
 * itself does not read it yet.
 */
export interface ChamferOperation extends OperationBase {
  kind: 'chamfer'
  from: Point3
  to: Point3
  /** The leg of the chamfer, mm; not above the mill radius. */
  chamferMm: number
  /** The edge against the grain of the part; the direction is not planned yet. */
  grain?: 'along' | 'across'
}

export type Operation =
  | FaceOperation
  | RectPocketOperation
  | CirclePocketOperation
  | DrillOperation
  | ContourOperation
  | SlotOperation
  | ChamferOperation

/** The saved document (`.qprog` schema 1). It is loaded into the machine only by a command. */
export interface QuickProgram {
  schema: 1
  name: string
  units: 'mm'
  wcs: Wcs
  stock: StockBox
  /** Safe height between passes and at the end; above the stock top. */
  safeZ: number
  /** The G64 path tolerance: P is the corner rounding, Q the following error, both mm. */
  g64P: number
  g64Q: number
  /** An M1 stop before every operation after the first. */
  stopAfterOp: boolean
  operations: Operation[]
}

/** The program fields a shape rule reads: the stock, which every kind checks against. */
export type ProgramShape = Pick<QuickProgram, 'stock'>

/** The shape of one hole layout, taken out of a drill operation. */
export const drillGeometry = (op: DrillOperation): DrillGeometry => {
  switch (op.pattern) {
    case 'hole':
      return { pattern: 'hole', hole: op.hole }
    case 'grid':
      return { pattern: 'grid', x0: op.x0, y0: op.y0, cols: op.cols, rows: op.rows, dx: op.dx, dy: op.dy }
    case 'circle':
      return {
        pattern: 'circle',
        x: op.hole.x,
        y: op.hole.y,
        count: op.count,
        r: op.hole.r,
        startDeg: op.startDeg,
        arcDeg: op.arcDeg,
        fullTurn: op.fullTurn,
      }
  }
}

/** The top of the stock whatever the order of the corners. */
export const stockTop = (stock: StockBox): number => Math.max(stock.min.z, stock.max.z)

/** The bottom of the stock whatever the order of the corners. */
export const stockBottom = (stock: StockBox): number => Math.min(stock.min.z, stock.max.z)

/** The empty bounding box of a path: every side is inverted, so any move extends it. */
export const EMPTY_BBOX = {
  xMin: Infinity,
  xMax: -Infinity,
  yMin: Infinity,
  yMax: -Infinity,
  zMin: Infinity,
  zMax: -Infinity,
} as const

/**
 * The frame of a stock of the console size: centred on zero in XY, the given top in Z. The XY size
 * defaults to the console 100 x 80 mm; the document's own stock lives in `defaults.ts`.
 */
export const defaultStock = (sizeZ: number, topZ: number, sizeX = 100, sizeY = 80): StockBox => ({
  min: { x: -sizeX / 2, y: -sizeY / 2, z: topZ - sizeZ },
  max: { x: sizeX / 2, y: sizeY / 2, z: topZ },
})

/** A new document: schema 1, G54, G64 0.01, the stop on and the safe Z 5 mm above the stock. */
export const defaultProgram = (stock: StockBox): QuickProgram => ({
  schema: 1,
  name: 'QUICK',
  units: 'mm',
  wcs: 'G54',
  stock,
  safeZ: stockTop(stock) + 5,
  g64P: 0.01,
  g64Q: 0.01,
  stopAfterOp: true,
  operations: [],
})
