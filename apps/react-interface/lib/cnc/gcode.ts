// Разбор слов кадра УП (адресная буква + число). Один источник для машины
// (machine.ts) и подсистемы прогона маховиком (mpg.ts), чтобы обе стороны читали
// кадр одинаково. Комментарии в скобках отброшены, регистр приведён к верхнему.

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
