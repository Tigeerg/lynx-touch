// The golden document of Q6: one demo part with all fifteen operation kinds (PLAN-QUICKPROG, 9, Q6).
//
// The document lives here in code, so the `.qprog` of `testdata/quickprog-golden/` is written by the
// same script that writes its `.ngc` and the two never drift by a hand edit. The sizes are the
// console stock (100 x 80 x 20, its top at Z 0) and one operation per kind, enabled, with a tool of
// its own; the fields are the ones the demo and the panel agree on, so the C++ `quickprog` reads
// the file and plans its four kinds with the same coordinates (see `golden.test.ts`).
//
// Three kinds are "a hand check" and the file says why: `custom` carries the operator's own lines
// (the panel does not know what they cut), `probe` measures instead of cutting, and `rest` here has
// no source pocket left behind it (the earlier pockets already took its area), so its plane is empty
// and the check names it a warning.
import type { Operation, QuickProgram } from './ops'
import { newOperation, newProgram, toolRef } from './defaults'
import { RIGID_TAPPING_PARAM } from './validate-tap'

/** The name of the demo part; it stands in the `MODEL` tag of the emitted file. */
export const GOLDEN_NAME = 'DEMO-ALL'

/** The tool of one operation of the golden document, drawn from the tool table rows of its kind. */
const op = <K extends Operation['kind']>(
  kind: K,
  id: string,
  tool: { number: number; diameterMm: number },
  depthMm: number,
  stepDownMm: number,
): Extract<Operation, { kind: K }> =>
  newOperation(kind, {
    id,
    topZ: 0,
    depthMm,
    stepDownMm,
    tool: toolRef(tool.number, tool.diameterMm),
  }) as Extract<Operation, { kind: K }>

/**
 * A kind of one feed: a drill, a tap, a counterbore and the probe repeat the working feed in the
 * plunge line of the `.qprog` (the reader follows the file and sets the plunge from the feed), so
 * the document the panel writes says it up front and the read-back is the same document.
 */
const oneFeed = <T extends Operation>(operation: T): T => ({
  ...operation,
  cutting: { ...operation.cutting, plungeFeedMmMin: operation.cutting.feedMmMin },
})

/** Facing the stock top, 2 mm off, strips at 60 % of a 12 mm mill. */
const face = (): Operation => ({
  ...op('face', 'Торцевание', { number: 1, diameterMm: 12 }, 2, 2),
  z: -2,
  stepOverPct: 60,
})

/** The 60 x 40 pocket of the console, 6 mm deep in two passes of a 8 mm mill. */
const pocket = (): Operation => ({
  ...op('pocket', 'Карман', { number: 2, diameterMm: 8 }, 6, 3),
  rect: { x: -30, y: -20, w: 60, h: 40 },
  cornerRadiusMm: 0,
  finishAllowanceMm: 0,
  stepOverPct: 70,
})

/** A round pocket of radius 12 away from the square one, 4 mm deep. */
const circlePocket = (): Operation => ({
  ...op('circle-pocket', 'Круглый карман', { number: 3, diameterMm: 8 }, 4, 2),
  circle: { x: 25, y: 20, r: 12 },
  finishAllowanceMm: 0,
  stepOverPct: 70,
})

/** A 3 x 2 grid of 10 mm holes with a 2 mm peck, `G83 Q2`. */
const drill = (): Operation => ({
  ...op('drill', 'Сверление', { number: 4, diameterMm: 5 }, 10, 0),
  pattern: 'grid',
  body: 'hss',
  peckMm: 2,
  rMm: 0,
  hasR: false,
  hole: { x: 0, y: 0, r: 2.5 },
  x0: -20,
  y0: -10,
  cols: 3,
  rows: 2,
  dx: 20,
  dy: 20,
  count: 1,
  startDeg: 0,
  arcDeg: 360,
  fullTurn: true,
})

/** A closed 70 x 50 square profile, the mill on the material side. */
const contour = (): Operation => ({
  ...op('contour', 'Контур', { number: 5, diameterMm: 6 }, 3, 3),
  points: [
    { x: -35, y: -25, z: 0 },
    { x: 35, y: -25, z: 0 },
    { x: 35, y: 25, z: 0 },
    { x: -35, y: 25, z: 0 },
  ],
  closed: true,
  allowanceMm: 0,
  side: 'material',
})

/** A 60 mm keyway 10 mm wide along X, 3 mm deep. */
const slot = (): Operation => ({
  ...op('slot', 'Паз', { number: 6, diameterMm: 6 }, 3, 3),
  from: { x: -30, y: 30, z: 0 },
  to: { x: 30, y: 30, z: 0 },
  widthMm: 10,
  stepOverPct: 70,
})

/** One pass of a 1 mm chamfer along a 60 mm edge at the front of the part. */
const chamfer = (): Operation => ({
  ...op('chamfer', 'Фаска', { number: 7, diameterMm: 6 }, 1, 1),
  from: { x: -30, y: -35, z: 0 },
  to: { x: 30, y: -35, z: 0 },
  chamferMm: 1,
})

/** One tapped M8 hole at the back-left corner, 8 mm deep, `G84`. */
const tap = (): Operation => ({
  ...oneFeed(op('tap', 'Резьба метчиком', { number: 8, diameterMm: 8 }, 8, 0)),
  pattern: 'hole',
  pitchMm: 1.25,
  hole: { x: -40, y: 30, r: 3.4 },
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
})

/** One counterbored seat of an M6 head at the front-right corner, `G82 P0.2`, R set to 1 mm. */
const counterbore = (): Operation => ({
  ...oneFeed(op('counterbore', 'Цековка', { number: 9, diameterMm: 6 }, 6, 0)),
  pattern: 'hole',
  hole: { x: 40, y: -25, r: 3.3 },
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
  seatDiameterMm: 11,
  seatDepthMm: 6,
  dwellSec: 0.2,
  rMm: 1,
  hasR: true,
})

/** One internal M8x1.25 thread milled on a 5 mm radius hole, in the back-left corner. */
const thread = (): Operation => ({
  ...op('thread', 'Резьбовая фреза', { number: 10, diameterMm: 6 }, 8, 0),
  hole: { x: -40, y: -30, r: 5 },
  pitchMm: 1.25,
  internal: true,
  threadDepthMm: 8,
  threadAllowanceMm: 0.1,
  stepOverPct: 70,
})

/**
 * The word `ДЕТАЛЬ` cut on the top, in the free strip at the front of the part between the square
 * pocket (its band ends at Y -20) and the chamfer (its edge is Y -35): centred on X 0, the strokes
 * of the string take X -10..8.5 and Y -33.05..-28.05, so the box is clear of the slot, of both
 * pockets and of the island, and no letter of the demo drops into a groove (the technician's note
 * of `CHECKLIST.md`). The label is in the gap by its own strokes; the cut of every stroke starts
 * and ends on that stroke, so the box is the cut.
 */
const engrave = (): Operation => ({
  ...op('engrave', 'Гравировка', { number: 11, diameterMm: 3 }, 0.5, 0),
  text: 'ДЕТАЛЬ',
  x: 0,
  y: -33.05,
  heightMm: 5,
  angleDeg: 0,
})

/** A 40 x 30 pocket with a 20 x 16 island in its centre, a 6 mm mill in the 7 mm gap. */
const pocketIsland = (): Operation => ({
  ...op('pocket-island', 'Карман с островом', { number: 12, diameterMm: 6 }, 4, 4),
  rect: { x: -25, y: 5, w: 40, h: 30 },
  island: { x: -17, y: 12, w: 20, h: 16 },
  islandAllowanceMm: 0.1,
  stepOverPct: 70,
})

/** A rest pass of the square pocket's footprint: the earlier pocket already took it (empty, warned). */
const rest = (): Operation => ({
  ...op('rest', 'Остаточная обработка', { number: 13, diameterMm: 4 }, 2, 2),
  source: [1],
  rect: { x: -30, y: -20, w: 60, h: 40 },
  allowanceMm: 0,
  stepOverPct: 70,
})

/** The operator's own lines: a dwell and a short air cut; the panel does not know what they cut. */
const custom = (): Operation => ({
  ...op('custom', 'Свой G-код', { number: 10, diameterMm: 10 }, 0, 0),
  tool: { number: 0, diameterMm: 0, hNumber: 0 },
  gcode: ['G4 P1', 'G0 Z10', 'G1 Z5 F100'],
})

/** One touch of the probe on the part top a millimetre above the stock, retreat of 10 mm. */
const probe = (): Operation => ({
  ...oneFeed(op('probe', 'Обмер', { number: 14, diameterMm: 10 }, 0, 0)),
  points: [{ x: 0, y: 0, z: 0 }],
  rangeMm: 10,
  touchFeedMmMin: 100,
  axis: 'Z',
})

/**
 * The golden document: fifteen operations, one per kind, in the order of the kind list, with the
 * rigid-tapping switch of the machine on (the machine parameter, not the part, so it stays out of
 * the `.qprog` and the test sets it again after the read).
 */
export function goldenProgram(): QuickProgram {
  const operations: Operation[] = []
  operations.push(
    face(),
    pocket(),
    circlePocket(),
    drill(),
    contour(),
    slot(),
    chamfer(),
    tap(),
    counterbore(),
    thread(),
    engrave(),
    pocketIsland(),
    rest(),
    custom(),
    probe(),
  )
  return { ...newProgram(undefined, GOLDEN_NAME), operations, params: { [RIGID_TAPPING_PARAM]: 1 } }
}
