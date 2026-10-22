// Parsing the words of a program block (address letter + number). A single source for the
// machine (machine.ts) and the handwheel run subsystem (mpg.ts), so both sides read a block
// the same way. Parenthesised comments are dropped, the case is upper-cased.

export interface Word {
  letter: string
  value: number
}

export function parseWords(line: string): Word[] {
  const code = line.replace(/\(.*?\)/g, '').toUpperCase()
  const words: Word[] = []
  for (const m of code.matchAll(/([A-Z])\s*(-?\d+(?:\.\d+)?)/g)) {
    words.push({ letter: m[1], value: Number.parseFloat(m[2]) })
  }
  return words
}
