// The touch points of a probe as one text field (PLAN-QUICKPROG, 7.6).
//
// A probe block holds a list of points, and the console form holds one input line per field: the
// points are triples `X Y Z` joined by semicolons, the same style as the `X Y` pairs of a contour
// (`contour-points.ts`). The text is shared by the form and its error, so both read it the same way.
import type { Point3, ProbeAxis } from './ops'

/** The points as the form shows them: `10 20 -30; 10 40 -30`. */
export const probePointsText = (points: Point3[]): string =>
  points.map((point) => `${String(point.x)} ${String(point.y)} ${String(point.z)}`).join('; ')

/** One triple of the field: the point, or `null` when the triple is not three numbers. */
const parseTriple = (triple: string): Point3 | null => {
  const words = triple.trim().split(/\s+/)
  const read = (at: number): number => Number((words[at] ?? '').replace(',', '.'))
  const [x, y, z] = [read(0), read(1), read(2)]
  return words.length === 3 && Number.isFinite(x) && Number.isFinite(y) && Number.isFinite(z) ? { x, y, z } : null
}

/** The triples of the field, blank ones dropped: `null` at the first one that is not three numbers. */
export function probeProfileOf(text: string): (Point3 | null)[] {
  return text
    .split(';')
    .filter((triple) => triple.trim() !== '')
    .map(parseTriple)
}

/**
 * The points of a typed field, or `null` when a triple is not three numbers. An empty field keeps the
 * document list: it is the editor of an unfinished value, not a probe without touches.
 */
export function probePointsOf(values: Record<string, string>, key: string, fallback: Point3[]): Point3[] | null {
  const text = values[key]
  if (text === undefined || text.trim() === '') return fallback
  const triples = probeProfileOf(text)
  if (triples.some((point) => point === null)) return null
  return triples.filter((point): point is Point3 => point !== null)
}

/** The axis of the touch from the typed field; a word that is not an axis keeps the document one. */
export const probeAxis = (values: Record<string, string>, key: string, fallback: ProbeAxis): ProbeAxis => {
  const text = values[key]
  return text === 'X' || text === 'Y' || text === 'Z' ? text : fallback
}
