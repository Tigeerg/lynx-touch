// The saved program through the viewer's parser (PLAN-QUICKPROG, 7.5, part B).
//
// The rules here are the parser's own: `modules/gcode-viewer/cpp/core/gcode_parser.cpp` reads the
// file of the panel, and these cases pin the findings the operator sees on the panel against the
// facts of that file. The reference `.ngc` files of `etl/` are the reference output of the demo the
// panel matches word for word; the viewer must read them without a single error, which is the
// acceptance of §10 ("the generated NC program is loaded by the viewer").
import { readFileSync, readdirSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

import { viewerErrors, viewerIssues, type Issue } from './viewer-check'

const etlDir = fileURLToPath(new URL('./etl/', import.meta.url))
const etlFiles = readdirSync(etlDir)
  .filter((name) => name.endsWith('.ngc'))
  .sort()
const etlLines = (name: string): string[] => readFileSync(`${etlDir}${name}`, 'utf8').replace(/\n$/, '').split('\n')

const codes = (issues: Issue[]): string[] => issues.map((issue) => issue.code)
const ofCode = (issues: Issue[], code: string): Issue[] => issues.filter((issue) => issue.code === code)

/** A clean program: one face pass, header tags, the park, M30. No finding of any level. */
const CLEAN = [
  '%',
  '(MACHINE)',
  '(UI_OP: Плоскость)',
  '(UI_TOOL: T=1 TYPE=FLAT D=12.)',
  'N10 G21 G90 G94 G17 G40 G49 G80',
  'N15 G54',
  'N20 G0 Z5.000',
  'N25 G1 X0.000 Y0.000 Z-1.000 F300',
  'N30 X10.000',
  'N35 G0 Z5.000',
  'N40 M30',
  '%',
]

describe('viewerIssues: a clean program', () => {
  it('reports nothing for a file the panel writes without a park', () => {
    expect(viewerIssues(CLEAN)).toEqual([])
  })

  it('skips the lines the parser drops: empty, bare comment, percent', () => {
    const withNoise = ['', '   ', '(only a comment)', '%;', '(UI_UNKNOWN: ignored)', ...CLEAN]
    expect(codes(viewerIssues(withNoise))).not.toContain('arc-no-end')
    expect(viewerErrors(withNoise)).toEqual([])
  })
})

describe('viewerIssues: arcs', () => {
  it('calls an arc without an end point an error', () => {
    const issues = viewerIssues(['N10 G3 I5.000 J0.000 F800'])
    const arc = ofCode(issues, 'arc-no-end')
    expect(arc).toHaveLength(1)
    expect(arc[0].level).toBe('error')
    expect(arc[0].line).toBe(1)
    expect(arc[0].text).toContain('без конечной точки')
  })

  it('calls an arc without a centre an error and accepts a full arc with I/J and X/Y', () => {
    expect(codes(viewerIssues(['N10 G2 X5.000 Y0.000']))).toContain('arc-no-centre')
    expect(viewerIssues(['N10 G3 X5.000 Y5.000 I5.000 J0.000'])).toEqual([])
    // The radius form is a centre too: the parser builds it from R (parser 310-329).
    expect(viewerIssues(['N10 G2 X5.000 Y0.000 R5.000'])).toEqual([])
  })
})

describe('viewerIssues: drilling cycles', () => {
  it('calls G81 without R an error', () => {
    const issues = viewerIssues(['N10 G81 X0.000 Y0.000 Z-5.000 F200'])
    const found = ofCode(issues, 'cycle-no-r')
    expect(found).toHaveLength(1)
    expect(found[0].level).toBe('error')
  })

  it('calls G83 without R an error and G83 with R clean', () => {
    expect(codes(viewerIssues(['N10 G83 X0.000 Y0.000 Z-8.000 Q2.000 F200']))).toContain('cycle-no-r')
    expect(viewerIssues(['N10 G83 X0.000 Y0.000 Z-8.000 R1.000 Q2.000 F200'])).toEqual([])
  })

  it('warns about Q on G81 and checks a hole block of the cycle against the stored R', () => {
    const issues = viewerIssues(['N10 G81 X0.000 Y0.000 Z-5.000 R1.000 Q2.000 F200', 'N15 X10.000 Y0.000'])
    expect(codes(issues)).toEqual(['cycle-q-ignored'])
    // R is modal inside the group: the next hole without R keeps it.
    expect(viewerIssues(['N10 G81 X0.000 Y0.000 Z-5.000 R1.000 F200', 'N15 X10.000 Y0.000'])).toEqual([])
  })

  it('stops looking at the cycle after G80', () => {
    const issues = viewerIssues(['N10 G80', 'N15 G1 X0.000 Y0.000 Z-1.000 F100'])
    expect(issues).toEqual([])
  })
})

describe('viewerIssues: the final park and the stage tags', () => {
  it('warns that the final G53 park is read as a move', () => {
    const lines = ['(UI_OP: Плоскость)', '(UI_TOOL: T=1 TYPE=FLAT D=12.)', 'N10 G0 Z5.000', 'N15 G53 G0 Z0', 'N20 M30']
    const found = ofCode(viewerIssues(lines), 'park-as-move')
    expect(found).toHaveLength(1)
    expect(found[0].level).toBe('warning')
    expect(found[0].line).toBe(4)
    expect(found[0].text).toContain('машинный ноль')
  })

  it('warns about an operation without UI_OP', () => {
    const lines = ['(UI_TOOL: T=1 TYPE=FLAT D=12.)', 'N10 G1 X0.000 Y0.000 Z-1.000 F200']
    const found = ofCode(viewerIssues(lines), 'file-no-op')
    expect(found).toHaveLength(1)
    expect(found[0].level).toBe('warning')
    expect(found[0].text).toContain('нет UI_OP')
  })

  it('reports an operation without a tool of the second stage', () => {
    const lines = [
      '(UI_OP: A)',
      '(UI_TOOL: T=1 TYPE=FLAT D=12.)',
      'N10 G1 X0.000 Y0.000 Z-1.000 F200',
      '(UI_OP: B)',
      'N15 G1 X5.000 Y0.000 F200',
    ]
    const issues = viewerIssues(lines)
    expect(codes(issues)).toEqual(['op-no-tool'])
    expect(issues[0].line).toBe(4)
  })
})

describe('viewerIssues: the reference files of the panel', () => {
  it('has the reference files to check', () => {
    expect(etlFiles.length).toBeGreaterThan(0)
  })

  for (const name of etlFiles) {
    it(`reads ${name} without an error`, () => {
      const lines = etlLines(name)
      expect(viewerErrors(lines)).toEqual([])
      // The file of the panel has the park and named operations: the viewer reads both, so the
      // findings are the known warnings and nothing else.
      expect(codes(viewerIssues(lines))).toEqual(['park-as-move'])
    })
  }
})
