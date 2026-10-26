// Parsing the words of a program block (address letter + number). A single source for the
// machine (machine.ts), the handwheel run subsystem (mpg.ts) and the program screen panels
// (modal codes and the tool list), so every side reads a block the same way. Parenthesised
// comments are dropped, the case is upper-cased.

export interface Word {
  letter: string
  value: number
}

/** The modal G-code state of the program up to and including the executed block. */
export interface ModalCodes {
  /** Motion mode, G0..G3. */
  motion: number
  /** Working plane, G17..G19. */
  plane: number
  /** Units, G20 (inch) or G21 (mm). */
  unit: number
  /** Distance mode, G90 (absolute) or G91 (incremental). */
  distance: number
  /** Cutter radius compensation, G40..G42. */
  radius: number
  /** Tool length compensation, G43 or G49. */
  length: number
  /** Canned cycle, G80..G89. */
  cycle: number
}

const MODAL_DEFAULTS: ModalCodes = { motion: 0, plane: 17, unit: 21, distance: 90, radius: 40, length: 49, cycle: 80 }

export function parseWords(line: string): Word[] {
  const code = line.replace(/\(.*?\)/g, '').toUpperCase()
  const words: Word[] = []
  for (const m of code.matchAll(/([A-Z])\s*(-?\d+(?:\.\d+)?)/g)) {
    words.push({ letter: m[1], value: Number.parseFloat(m[2]) })
  }
  return words
}

function applyGCode(modal: ModalCodes, value: number): void {
  if (value >= 0 && value <= 3) modal.motion = value
  else if (value >= 17 && value <= 19) modal.plane = value
  else if (value === 20 || value === 21) modal.unit = value
  else if (value === 90 || value === 91) modal.distance = value
  else if (value >= 40 && value <= 42) modal.radius = value
  else if (value === 43 || value === 49) modal.length = value
  else if (value >= 80 && value <= 89) modal.cycle = value
}

/**
 * The modal G-code state after the block `executedUpTo` (inclusive). A negative index means
 * nothing has been executed yet, so the safe defaults are returned. A later block is ignored.
 */
export function modalCodes(lines: string[], executedUpTo: number): ModalCodes {
  const modal: ModalCodes = { ...MODAL_DEFAULTS }
  for (let i = 0; i <= executedUpTo && i < lines.length; i++) {
    for (const { letter, value } of parseWords(lines[i])) {
      if (letter === 'G') applyGCode(modal, value)
    }
  }
  return modal
}

/** Unique tool numbers `T` in order of appearance; comments are not scanned. */
export function programTools(lines: string[]): number[] {
  const seen: number[] = []
  for (const line of lines) {
    for (const { letter, value } of parseWords(line)) {
      if (letter === 'T' && !seen.includes(value)) seen.push(value)
    }
  }
  return seen
}
