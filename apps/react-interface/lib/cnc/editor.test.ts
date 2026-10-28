// Program screen subsystem: choosing the NC file and editing its blocks. The checks hold
// the contract the reducer shows to the operator: the file dialog guards a running
// program, editing is allowed only in a stop, commit writes the buffer into the file, and
// cancel drops an inserted block.
import { describe, expect, it } from 'vitest'
import { editorReducer, type EditorAction } from './editor'
import { currentFile, programLines } from './files'
import { initialState } from './machine'
import type { MachineState } from './types'

const run = (state: MachineState, ...actions: EditorAction[]) => actions.reduce((s, a) => editorReducer(s, a).s, state)

describe('экран «Программа»: файл и правка кадров', () => {
  it('диалог файлов закрыт, пока идёт программа', () => {
    const r = editorReducer({ ...initialState, run: 'RUN' }, { type: 'FILE_DIALOG', open: true })

    expect(r.s.fileDialog).toBe(false)
    expect(r.msg).toBe('Остановите программу, чтобы открыть другой файл')
  })

  it('открытие файла уводит на экран программы и снимает правку', () => {
    const dirty: MachineState = {
      ...initialState,
      fileCursor: 1,
      editMode: true,
      editor: { line: 0, buffer: 'N10', inserted: false },
    }
    const r = editorReducer(dirty, { type: 'FILE_OPEN' })

    expect(r.s.activeFile).toBe(1)
    expect(r.s.fileDialog).toBe(false)
    expect(r.s.screen).toBe('prog')
    expect(r.s.editMode).toBe(false)
    expect(r.s.editor).toBeNull()
  })

  it('загруженный извне файл открывается и показывает свои кадры', () => {
    const lazy: MachineState = {
      ...initialState,
      files: [
        ...initialState.files,
        { name: 'TEST_MODEL', title: 'ТЕСТОВАЯ МОДЕЛЬ', lines: [], modified: '', source: '/programs/Test_model.ngc' },
      ],
      fileCursor: initialState.files.length,
    }
    const r = editorReducer(lazy, {
      type: 'FILE_LOADED',
      index: initialState.files.length,
      lines: ['%', 'N10 G90 G94', 'N20 G00 X0 Y0'],
    })

    expect(r.s.activeFile).toBe(initialState.files.length)
    expect(r.s.fileDialog).toBe(false)
    expect(r.s.screen).toBe('prog')
    expect(currentFile(r.s).lines).toHaveLength(3)
    expect(currentFile(r.s).source).toBe('/programs/Test_model.ngc')
  })

  it('загрузка чужого номера файла не роняет состояние', () => {
    const r = editorReducer(initialState, { type: 'FILE_LOADED', index: 99, lines: ['N10'] })

    expect(r.s.activeFile).toBe(initialState.activeFile)
    expect(r.msg).toContain('не найден')
  })

  it('правка включается только в останове', () => {
    const busy = editorReducer({ ...initialState, run: 'RUN' }, { type: 'EDIT_MODE', on: true })
    expect(busy.s.editMode).toBe(false)
    expect(busy.msg).toContain('останове')

    const idle = editorReducer(initialState, { type: 'EDIT_MODE', on: true })
    expect(idle.s.editMode).toBe(true)
    expect(idle.s.screen).toBe('prog')
  })

  it('вставка кадра и её отмена возвращают файл к исходной длине', () => {
    const base = run(initialState, { type: 'EDIT_MODE', on: true })
    const before = programLines(base).length

    const inserted = run(base, { type: 'EDIT_INSERT' })
    expect(programLines(inserted).length).toBe(before + 1)
    expect(inserted.editor?.inserted).toBe(true)

    const cancelled = run(inserted, { type: 'EDIT_CANCEL' })
    expect(programLines(cancelled).length).toBe(before)
    expect(cancelled.editor).toBeNull()
  })

  it('ввод кладёт буфер в кадр', () => {
    const begin = run(initialState, { type: 'EDIT_MODE', on: true }, { type: 'EDIT_BEGIN' })
    const typed = run(begin, { type: 'EDIT_BUFFER', value: 'N10 G0 X0' })
    const committed = run(typed, { type: 'EDIT_COMMIT' })

    expect(programLines(committed)[0]).toBe('N10 G0 X0')
    expect(committed.editor).toBeNull()
  })

  it('удаление убирает кадр', () => {
    const base = run(initialState, { type: 'EDIT_MODE', on: true })
    const before = programLines(base).length

    const deleted = run(base, { type: 'EDIT_DELETE' })

    expect(programLines(deleted).length).toBe(before - 1)
    expect(currentFile(deleted).lines).toEqual(programLines(deleted))
  })

  it('без режима правки кадр не правится', () => {
    const r = editorReducer(initialState, { type: 'EDIT_BEGIN' })

    expect(r.s.editor).toBeNull()
  })
})
