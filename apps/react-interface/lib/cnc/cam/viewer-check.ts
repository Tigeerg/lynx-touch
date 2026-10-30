// The saved program through the rules of the viewer parser (PLAN-QUICKPROG, 7.5: "the viewer does not
// check the saved NC program"), part B.
//
// The panel writes the file; the viewer reads it with `gv::parseGcode`
// (`modules/gcode-viewer/cpp/core/gcode_parser.cpp`). That parser is not a G-code validator: it
// models what the 3D scene needs and quietly degrades the rest - an arc without an end point becomes
// a chord, a cycle without `R` drills at the current Z, a `G53` is not a machine move. This module
// repeats only those rules, in TypeScript and without a call into C++, and tells the operator where
// the viewer would read the file differently from what was planned.
//
// It is a check, not a second parser: no geometry, no toolpath, no modal simulation of coordinates.
// A line the parser drops (empty, only a comment, `%`) is not looked at, an unknown `UI_*` tag is
// skipped, as the parser does. A pure function of the lines: same input, same list, no I/O and no
// state between calls.
//
// Every rule names the lines of `gcode_parser.cpp` it comes from; a change there is a change here.

/** One finding of the check: an error blocks a save, a warning does not. */
export interface Issue {
  /** `error` - the viewer reads the program wrong; `warning` - it reads it differently from intent. */
  level: 'error' | 'warning'
  /** The 1-based number of the line the operator sees in the file. */
  line: number
  /** A stable code for a test and a report line. */
  code: string
  /** The finding in the operator's words. */
  text: string
}

/** A word of the line, as `findWords` reads it: a letter and a number (parser 121-151). */
interface Word {
  letter: string
  value: number
}

/** The words of the code part: a letter and a signed number with an optional fraction. */
const wordsOf = (code: string): Word[] => {
  const words: Word[] = []
  const pattern = /([A-Z])\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))/g
  let match = pattern.exec(code)
  while (match !== null) {
    words.push({ letter: match[1], value: Number(match[2]) })
    match = pattern.exec(code)
  }
  return words
}

/** A line as the parser prepares it (parser 550-558): up to `;`, comments out, trimmed, upper-cased. */
const prepare = (raw: string): { up: string; code: string } => ({
  up: raw.toUpperCase(),
  code: raw
    .split(';')[0]
    .replace(/\([^)]*\)/g, '')
    .trim()
    .toUpperCase(),
})

/** The modal words the findings read; the coordinates are not modelled on purpose. */
interface Modal {
  /** The last G0/G1/G2/G3 of a block, null before the first one. */
  motion: number | null
  /** The active canned cycle (73, 81, 82, 83), null when G80 or none. */
  cycle: number | null
  /** The cycle has its R plane: the parser keeps `hasR` once the group names R (parser 692). */
  cycleR: boolean
}

/** The letters whose value the parser reads; a line without them is a no-op for it (parser 637-641). */
const READ_LETTERS = 'GXYZIJKRQF'

/** A finding of one block: the plain G-code half of the check, without the tags. */
const blockIssues = (words: Word[], state: Modal, line: number): Issue[] => {
  const issues: Issue[] = []
  const letters = new Set(words.map((w) => w.letter))
  const gs = words.filter((w) => w.letter === 'G').map((w) => w.value)
  const hasAxis = ['X', 'Y', 'Z'].some((axis) => letters.has(axis))
  const hasOffset = letters.has('I') || letters.has('J') || letters.has('K')

  // Exact comparisons, as the parser: G91.1 is not G91 (parser 644-660).
  const motionHere = gs.find((g) => g === 0 || g === 1 || g === 2 || g === 3)
  const cycleStart = gs.find((g) => g === 73 || g === 81 || g === 82 || g === 83)
  if (gs.includes(80)) {
    state.cycle = null
    state.cycleR = false
  }
  if (cycleStart !== undefined) {
    state.cycle = cycleStart
    state.cycleR = false
  }
  const motion = motionHere ?? state.motion
  if (motionHere !== undefined) state.motion = motionHere

  // A canned cycle block: a cycle word, or a hole of the active cycle that names an axis
  // (parser 686-696). The panel prints no motion word on such a line; only R and Q are checked.
  if (cycleStart !== undefined || (state.cycle !== null && hasAxis)) {
    if (letters.has('R')) state.cycleR = true
    const code = cycleStart ?? state.cycle!
    // G81/G83 without R: the parser takes the Z the tool stands at as the R plane
    // (parser 432: `cyc.hasR ? cyc.r : pos[2]`), so the drill feeds from the surface.
    if (!state.cycleR) {
      issues.push({
        level: 'error',
        line,
        code: 'cycle-no-r',
        text: `Цикл G${code} без R: вьювер берёт плоскость R из текущей точки, сверление пойдёт не с заданной высоты.`,
      })
    }
    // Q belongs to G83/G73 (a peck). G81/G82 is one plunge: the parser drops the Q (parser 448).
    if (letters.has('Q') && (code === 81 || code === 82)) {
      issues.push({
        level: 'warning',
        line,
        code: 'cycle-q-ignored',
        text: `Q у цикла G${code} не читается: это не сверление с ломкой стружки, шаг Q пропадёт.`,
      })
    }
    return issues
  }

  // An arc without an end point: the parser takes the target from the current point when the line
  // names no axis, and the centre branch returns a chord `{start, end}` with end == start
  // (parser 699-704 and 330-336), so the arc disappears in the viewer. The panel always prints the
  // end point; a hand-edited file would not.
  if (motion === 2 || motion === 3) {
    if (!hasAxis) {
      issues.push({
        level: 'error',
        line,
        code: 'arc-no-end',
        text: `Дуга G${motion} без конечной точки (только I/J): вьювер выродит её в точку, дуга пропадёт.`,
      })
    } else if (!hasOffset && !letters.has('R')) {
      issues.push({
        level: 'error',
        line,
        code: 'arc-no-centre',
        text: `Дуга G${motion} без I/J и R: вьювер не знает центра и нарисует прямую.`,
      })
    }
  }
  return issues
}

/**
 * The tags of the file and the findings they make: an operation stage needs both `UI_OP` (its label,
 * parser 568-588) and `UI_TOOL`. An unknown `UI_*` tag is ignored by the viewer, so it is not a
 * finding. A `UI_TOOL` right after a `UI_OP` belongs to the same stage (the panel's order); a
 * `UI_OP` the next `UI_OP` closes without a tool is a stage without a tool.
 */
const tagIssues = (lines: string[]): Issue[] => {
  const issues: Issue[] = []
  let sawTool = false
  let sawOp = false
  let pending = 0
  const report = () => {
    if (pending > 0) {
      issues.push({
        level: 'warning',
        line: pending,
        code: 'op-no-tool',
        text: 'У операции нет UI_TOOL: вьювер покажет этап без инструмента, подпись «T… · фреза» пропадёт.',
      })
      pending = 0
    }
  }
  lines.forEach((raw, index) => {
    const up = raw.toUpperCase()
    if (up.includes('UI_OP')) {
      report()
      sawOp = true
      pending = index + 1
    } else if (up.includes('UI_TOOL')) {
      sawTool = true
      pending = 0
    }
  })
  report()
  // Tool changes but no operation marker: the stages fall back to the tool list (parser 722-726), so
  // the operator loses the names of the operations. The tool itself is there; the stage title is not.
  if (sawTool && !sawOp) {
    issues.push({
      level: 'warning',
      line: 1,
      code: 'file-no-op',
      text: 'В файле есть инструменты, но нет UI_OP: вьювер назовёт этапы по сменам инструмента, имена операций пропадут.',
    })
  }
  return issues
}

/**
 * The findings of a saved program in the viewer's terms, line by line. The files the panel writes
 * pass with warnings only; an error is a reason not to save, a warning is shown to the operator and
 * written into the file (PLAN-QUICKPROG, 7.5).
 */
export function viewerIssues(lines: string[]): Issue[] {
  const issues: Issue[] = []
  const state: Modal = { motion: null, cycle: null, cycleR: false }
  let parkLine = 0
  lines.forEach((raw, index) => {
    const { code } = prepare(raw)
    // The G53 park is a word of the block, and the parser does not know it (parser 644-661 has no
    // G53 branch): the axis words of `G53 G0 Z0` are read as an ordinary move. The check looks at
    // the block itself, not at the modal state, because that is how the parser loses the park.
    if (/G\s*53\b/.test(code)) parkLine = index + 1
    if (code === '') return // an empty line, a bare comment or `%`: the parser drops it
    const words = wordsOf(code)
    if (!words.some((w) => READ_LETTERS.includes(w.letter))) return
    issues.push(...blockIssues(words, state, index + 1))
  })
  // The final park is the last `G53 G0 Z0` of the file. The parser reads its Z as a move; the
  // viewer's last point ends at the machine zero, not at the safe Z of the part.
  if (parkLine > 0) {
    issues.push({
      level: 'warning',
      line: parkLine,
      code: 'park-as-move',
      text: 'Финальная парковка G53 G0 Z0 читается вьювером как перемещение в машинный ноль, а не как парковка: траектория заканчивается в нуле, а не на безопасной Z.',
    })
  }
  return [...issues, ...tagIssues(lines)].sort((a, b) => a.line - b.line)
}

/** Only the errors: a save is blocked by these, a warning is shown and written into the file. */
export const viewerErrors = (lines: string[]): Issue[] => viewerIssues(lines).filter((i) => i.level === 'error')
