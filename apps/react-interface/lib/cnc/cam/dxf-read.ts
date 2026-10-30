// A small reader of ASCII DXF (PLAN-QUICKPROG, 7.6, «Импорт DXF»).
//
// Only the drawing of the `ENTITIES` section is read: `LINE`, `LWPOLYLINE` (with bulges),
// `POLYLINE`/`VERTEX`/`SEQEND` (the old style), `ARC` and `CIRCLE`. The layers, the blocks and the
// extents (`$EXTMIN`/`$EXTMAX`) are ignored: the panel imports the contours, not the drawing. Every
// arc becomes a chain of chords whose sagitta is at most `chordTol`, so a chord count is
// `ceil(theta / (2 acos(1 - tol / r)))` and a full circle of R 10 at 0.1 mm is 45 chords.
//
// The file is not kept: the caller gets the contours (in the DXF coordinates, Z 0) and drops the
// text. An unsupported entity is counted in `skipped`, not refused; no supported entity at all is an
// error with a reason the operator reads.

/** The point of a contour in the DXF plane; Z is zero, the operation puts it on the stock top. */
export interface DxfPoint {
  x: number
  y: number
}

/** A contour of the drawing: the vertices of the walk and whether it closes. */
export interface DxfContour {
  points: DxfPoint[]
  closed: boolean
}

/** The result the caller reads: the contours or the reason, and the unsupported entity types. */
export type DxfResult =
  { ok: true; contours: DxfContour[]; skipped: string[] } | { ok: false; reason: string; skipped: string[] }

/** The sagitta of a chord, mm: the largest distance of an arc from its chords. */
export const CHORD_TOL_MM = 0.1

/** The walks of the file and the arcs of the entities live next to this module; re-exported here. */
export { dxfContours } from './dxf-flow'
export { arcPoints, bulgeArm, chordCount, entityContours, entitiesOf, polylineOf } from './dxf-entities'

/** The `(group code, value)` pairs of the file, blank lines dropped. */
export interface Pair {
  code: number
  value: string
}

/** The pairs of the text; a trailing odd line is ignored, not a crash. */
export function pairsOf(text: string): Pair[] {
  const lines = text
    .replace(/\r\n?/g, '\n')
    .split('\n')
    .map((line) => line.trim())
  const pairs: Pair[] = []
  // The lines are taken pairwise by their position: a blank value line (an empty string of a TEXT)
  // must not shift the rest of the file.
  for (let i = 0; i + 1 < lines.length; i += 2) {
    pairs.push({ code: Number(lines[i]), value: lines[i + 1] })
  }
  return pairs
}
