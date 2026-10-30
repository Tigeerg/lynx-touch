// The list of contour points as one text field (PLAN-QUICKPROG, 7.6).
//
// The contour is a list of a variable length, and the console form holds one input line per field;
// a single line of pairs `X Y; X Y; ...` is the same style as `rect` of a pocket and keeps the whole
// profile in one editable value. Decimals use a dot or a comma, as every numeric field does. The
// split is shared by the form (`pointsOf`) and its error (`pointProblem`), so both read the text the
// same way.
import type { Point3 } from './ops'

/** The points as the form shows them: pairs joined by semicolons, `-20 -20; 20 -20`. */
export const pointsText = (points: Point3[]): string =>
  points.map((point) => `${String(point.x)} ${String(point.y)}`).join('; ')

/** One pair of the field: the point, or `null` when the pair is not two numbers. */
const parsePair = (pair: string, z: number): Point3 | null => {
  const words = pair.trim().split(/\s+/)
  const x = Number((words[0] ?? '').replace(',', '.'))
  const y = Number((words[1] ?? '').replace(',', '.'))
  return words.length === 2 && Number.isFinite(x) && Number.isFinite(y) ? { x, y, z } : null
}

/** The pairs of the field, blank ones dropped: `null` at the first pair that is not two numbers. */
export function profileOf(text: string, z: number): (Point3 | null)[] {
  const pairs = text.split(';').filter((pair) => pair.trim() !== '')
  return pairs.map((pair) => parsePair(pair, z))
}

/**
 * The points of a typed field, or `null` when a pair is not two numbers. An empty field keeps the
 * document list: it is the editor of an unfinished value, not an empty contour. The `z` of a vertex
 * is not used by the contour, so the plane point carries the top of the operation.
 */
export function pointsOf(values: Record<string, string>, key: string, fallback: Point3[], z: number): Point3[] | null {
  const text = values[key]
  if (text === undefined || text.trim() === '') return fallback
  const pairs = profileOf(text, z)
  if (pairs.some((point) => point === null)) return null
  return pairs.filter((point): point is Point3 => point !== null)
}
