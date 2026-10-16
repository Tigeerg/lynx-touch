export type Point3 = { X: number; Y: number; Z: number }
export type Segment = { from: Point3; to: Point3; rapid: boolean; line: number }
export type PathView = 'XY' | 'XZ' | 'ISO'
export const PATH_VIEWS: PathView[] = ['ISO', 'XY', 'XZ']

const WORD = /([A-Z])\s*(-?\d+(?:\.\d+)?)/g

export function buildToolpath(lines: string[]): Segment[] {
  const segments: Segment[] = []
  let modal = 0
  let pos: Point3 | null = null

  lines.forEach((raw, line) => {
    const code = raw.replace(/\(.*?\)/g, '').toUpperCase()
    const coords: Partial<Point3> = {}
    let skip = false
    for (const m of code.matchAll(WORD)) {
      const letter = m[1]
      const value = Number.parseFloat(m[2])
      if (letter === 'G') {
        if (value === 0 || value === 1) modal = value
        if (value === 28 || value === 91) skip = true
      } else if (letter === 'X' || letter === 'Y' || letter === 'Z') {
        coords[letter] = value
      }
    }
    if (skip || Object.keys(coords).length === 0) return
    const next: Point3 = {
      X: coords.X ?? pos?.X ?? 0,
      Y: coords.Y ?? pos?.Y ?? 0,
      Z: coords.Z ?? pos?.Z ?? 0,
    }
    if (pos) segments.push({ from: pos, to: next, rapid: modal === 0, line })
    pos = next
  })

  return segments
}

export type Waypoint = { pos: Point3; dtg: Point3 | null }

export function simPose(segments: Segment[], simLine: number): Waypoint {
  let pos: Point3 = { X: 0, Y: 0, Z: 0 }
  for (const s of segments) {
    if (s.line > simLine) {
      return { pos, dtg: { X: s.to.X - pos.X, Y: s.to.Y - pos.Y, Z: s.to.Z - pos.Z } }
    }
    pos = s.to
  }
  return { pos, dtg: null }
}

const COS30 = Math.cos(Math.PI / 6)
const SIN30 = 0.5

export function project(p: Point3, view: PathView): [number, number] {
  if (view === 'XY') return [p.X, -p.Y]
  if (view === 'XZ') return [p.X, -p.Z]
  return [(p.X - p.Y) * COS30, (p.X + p.Y) * SIN30 - p.Z]
}
