// The walk of the DXF drawing (PLAN-QUICKPROG, 7.6, «Импорт DXF»).
//
// The entities become contours one by one (`dxf-entities.ts`); the lines and the open walks whose
// ends meet are joined here into one contour, and the walk that comes back to its first point is
// closed. The rest of the file — the layers, the blocks, the extents — is not read.
import { CHORD_TOL_MM, pairsOf, type DxfContour, type DxfResult } from './dxf-read'
import { entitiesOf, entityContours, polylineOf, same } from './dxf-entities'

/** Joins the open contours and the lines whose ends meet, into one walk. */
function joined(contours: DxfContour[]): DxfContour[] {
  const open = contours.filter((contour) => !contour.closed)
  const done: DxfContour[] = contours.filter((contour) => contour.closed)
  const used = new Set<DxfContour>()
  for (const start of open) {
    if (used.has(start)) continue
    used.add(start)
    const points = [...start.points]
    let grew = true
    while (grew) {
      grew = false
      for (const other of open) {
        if (used.has(other)) continue
        const head = points[0]
        const tail = points[points.length - 1]
        const from = other.points[0]
        const to = other.points[other.points.length - 1]
        if (same(tail, from)) points.push(...other.points.slice(1))
        else if (same(tail, to)) points.push(...other.points.slice().reverse().slice(1))
        else if (same(head, to)) points.unshift(...other.points.slice(0, -1))
        else if (same(head, from)) points.unshift(...other.points.slice().reverse().slice(0, -1))
        else continue
        used.add(other)
        grew = true
      }
    }
    // The walk that comes back to its first point is a closed contour.
    const closes = points.length > 2 && same(points[0], points[points.length - 1])
    done.push({ points: closes ? points.slice(0, -1) : points, closed: closes })
  }
  return done
}

/** The contours of the DXF text, or the reason there are none. */
export function dxfContours(text: string, chordTol = CHORD_TOL_MM): DxfResult {
  const pairs = pairsOf(text)
  if (pairs.some((pair) => !Number.isFinite(pair.code))) {
    return { ok: false, reason: 'DXF: строка не пара «код — значение».', skipped: [] }
  }
  if (!(chordTol > 0)) return { ok: false, reason: 'DXF: допуск хорды должен быть больше нуля.', skipped: [] }
  const entities = entitiesOf(pairs)
  if (entities === null) return { ok: false, reason: 'DXF: нет секции ENTITIES.', skipped: [] }
  const contours: DxfContour[] = []
  const skipped: string[] = []
  for (let i = 0; i < entities.length; i += 1) {
    const { type, pairs: body } = entities[i]
    if (type === 'POLYLINE') {
      const reads = polylineOf(entities, entities[i], i)
      if (reads.length > 0) contours.push(...reads)
      else skipped.push(type)
      continue
    }
    // A VERTEX outside its POLYLINE and a SEQEND are the parts of the old-style walk, not entities
    // of their own: they are not counted as skipped.
    if (type === 'VERTEX' || type === 'SEQEND') continue
    const read = entityContours(type, body, chordTol)
    if (read === null || read.length === 0) skipped.push(type)
    else contours.push(...read)
  }
  if (contours.length === 0) {
    const kinds = [...new Set(skipped)].join(', ')
    const reason =
      skipped.length === 0
        ? 'DXF: в секции ENTITIES нет контуров.'
        : `DXF: нет поддержанных контуров, пропущено сущностей: ${String(skipped.length)} (${kinds}).`
    return { ok: false, reason, skipped }
  }
  return { ok: true, contours: joined(contours).filter((contour) => contour.points.length >= 2), skipped }
}
