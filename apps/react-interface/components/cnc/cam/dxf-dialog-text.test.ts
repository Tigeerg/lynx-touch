// The words of the DXF import dialog (PLAN-QUICKPROG, 7.6, «Импорт DXF») on the pure helpers.
//
// The project has no DOM renderer, so what is checked here is what the list prints: the number of
// each contour, its points and its close, the warning of the skipped entities, the reason of a
// failed read and the caption of the button. The file text itself never enters: the reader is the
// one of `dxf-source.ts`, and the rows are built from its result.
import { describe, expect, it } from 'vitest'

import { readDxf } from '@/lib/cnc/cam/dxf-source'
import type { DxfContour } from '@/lib/cnc/cam/dxf-read'
import { dxfAddLabel, dxfError, dxfRow, dxfRows, dxfSummary, dxfWarning } from './dxf-dialog-text'

const contour = (points: [number, number][], closed = false): DxfContour => ({
  points: points.map(([x, y]) => ({ x, y })),
  closed,
})

const DXF = (...entities: string[]): string =>
  ['0', 'SECTION', '2', 'ENTITIES', ...entities, '0', 'ENDSEC', '0', 'EOF'].join('\n')
const entity = (type: string, ...pairs: (string | number)[]): string => ['0', type, ...pairs.map(String)].join('\n')

describe('строка контура в списке', () => {
  it('нумерует контур с единицы, а в действие уходит номер с нуля', () => {
    const row = dxfRow(
      contour(
        [
          [0, 0],
          [10, 0],
          [10, 10],
        ],
        true,
      ),
      0,
    )
    expect(row).toEqual({ index: 0, number: 1, points: 3, closed: true, text: '3 тч · замкнут' })
    expect(
      dxfRow(
        contour([
          [0, 0],
          [5, 5],
        ]),
        2,
      ),
    ).toMatchObject({ index: 2, number: 3, closed: false })
    expect(
      dxfRow(
        contour([
          [0, 0],
          [5, 5],
        ]),
        2,
      ).text,
    ).toBe('2 тч · открыт')
  })

  it('читает файл в строки списка: номера по порядку, пропуски в предупреждении', () => {
    const reading = readDxf(
      DXF(
        entity('LINE', 10, 0, 20, 0, 11, 10, 21, 0),
        entity('CIRCLE', 10, 20, 20, 20, 40, 5),
        entity('SPLINE', 10, 0, 20, 0),
      ),
    )
    expect(reading.ok).toBe(true)
    const rows = dxfRows(reading)
    expect(rows.map((row) => row.number)).toEqual([1, 2])
    expect(rows.every((row) => row.points >= 2)).toBe(true)
    expect(dxfWarning(reading)).toContain('SPLINE')
    expect(dxfError(reading)).toBeNull()
  })

  it('битый файл не даёт строк и называет причину', () => {
    const reading = readDxf('не DXF')
    expect(dxfRows(reading)).toEqual([])
    expect(dxfError(reading)).not.toBeNull()
    expect(dxfWarning(reading)).toBe(null)
    expect(dxfSummary(reading)).toBe('Файл не прочитан')
  })
})

describe('предупреждение и ошибка', () => {
  it('файл без поддерживаемых сущностей: причина и предупреждение о том, что пропущено', () => {
    const reading = readDxf(DXF(entity('TEXT', 1, 'X')))
    expect(reading.ok).toBe(false)
    expect(dxfError(reading)).toContain('TEXT')
    expect(dxfWarning(reading)).toContain('TEXT')
  })

  it('читаемый файл без контуров предупреждает словами оператора', () => {
    expect(dxfWarning({ ok: true, contours: [], warning: null })).toContain('нет контуров')
    expect(
      dxfWarning({
        ok: true,
        contours: [
          contour([
            [0, 0],
            [1, 1],
          ]),
        ],
        warning: null,
      }),
    ).toBeNull()
    expect(dxfWarning({ ok: true, contours: [], warning: 'Пропущены сущности DXF: TEXT.' })).toBe(
      'Пропущены сущности DXF: TEXT.',
    )
  })
})

describe('подпись списка и кнопки', () => {
  it('счётчик контуров согласует слово с числом', () => {
    const of = (count: number) =>
      dxfSummary({
        ok: true,
        contours: Array.from({ length: count }, () =>
          contour([
            [0, 0],
            [1, 1],
          ]),
        ),
        warning: null,
      })
    expect(of(1)).toBe('Контуры в файле: 1 контур')
    expect(of(2)).toBe('Контуры в файле: 2 контура')
    expect(of(5)).toBe('Контуры в файле: 5 контуров')
    expect(of(11)).toBe('Контуры в файле: 11 контуров')
    expect(of(21)).toBe('Контуры в файле: 21 контур')
  })

  it('кнопка называет номер, когда выбрать нужно из нескольких', () => {
    const row = dxfRow(
      contour([
        [0, 0],
        [1, 1],
      ]),
      1,
    )
    expect(dxfAddLabel(null, 3)).toBe('Добавить контур')
    expect(dxfAddLabel(row, 1)).toBe('Добавить контур')
    expect(dxfAddLabel(row, 3)).toBe('Добавить контур 2')
  })
})
