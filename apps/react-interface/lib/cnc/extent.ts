// Smallest and largest value of a sequence. `Math.min(...values)` throws "Maximum call stack size
// exceeded" on a real CAM program (over a hundred thousand points), so the bounds are found in one
// pass instead of spreading the array into the call.
export type Extent = { min: number; max: number }

export function extent(values: Iterable<number>): Extent {
  let min = Infinity
  let max = -Infinity
  for (const value of values) {
    if (value < min) min = value
    if (value > max) max = value
  }
  return { min, max }
}
