// The Qt manifest is generated from the React tables, never edited by hand. A normal test run
// compares the committed file with the tables; `scripts/write-panel-data.ps1` (PANEL_DATA_WRITE=1)
// regenerates it after a text change.
import { readFileSync, writeFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'

import { renderPanelData } from './panel-data'

const target = new URL('../../../qt-interface/panel-data.js', import.meta.url)
const text = renderPanelData()

if (process.env.PANEL_DATA_WRITE === '1') {
  writeFileSync(target, text, 'utf8')
}

describe('манифест Qt-порта', () => {
  it('panel-data.js совпадает с таблицами React', () => {
    expect(readFileSync(target, 'utf8')).toBe(text)
  })
})
