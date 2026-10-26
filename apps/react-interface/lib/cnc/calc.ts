// The cutting-mode calculator: the formulas and the defaults live here, not in the overlay
// component. Units are in the names: Vc in m/min, D in mm, fz in mm/tooth, rpm in 1/min and
// feed in mm/min.

const MM_PER_M = 1000

/** Defaults of the calculator fields: Vc, tool diameter, feed per tooth, number of flutes. */
export const CALC_DEFAULTS = { vc: 150, d: 10, fz: 0.06, flutes: 3 } as const

/** Spindle speed from the cutting speed and the tool diameter; 0 for non-positive inputs. */
export function cuttingRpm(vc: number, d: number): number {
  if (d <= 0 || vc <= 0) return 0
  return Math.round((MM_PER_M * vc) / (Math.PI * d))
}

/** Feed rate from the feed per tooth, the number of flutes and the spindle speed. */
export function cuttingFeed(fz: number, flutes: number, rpm: number): number {
  return Math.round(fz * flutes * rpm)
}
