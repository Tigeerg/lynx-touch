// The tool block shows the tool in the spindle and the next tool the program will call, so the
// operator prepares it in advance instead of seeing only the current one.
import { describe, expect, it } from 'vitest'
import { toolPreview } from './tool-preview'

describe('инструмент и следующий по программе', () => {
  it('в начале программы показывает текущий и следующий', () => {
    expect(toolPreview([1, 2, 3], 1)).toEqual([1, 2])
  })

  it('в середине — текущий и следующий за ним', () => {
    expect(toolPreview([1, 2, 3], 2)).toEqual([2, 3])
  })

  it('на последнем инструменте следующего нет', () => {
    expect(toolPreview([1, 2, 3], 3)).toEqual([3])
  })

  it('повторный вызов не дублируется', () => {
    expect(toolPreview([1, 2, 1, 3], 1)).toEqual([1, 2])
  })

  it('инструмент вне программы показывается вместе с первым вызовом', () => {
    expect(toolPreview([2, 3], 1)).toEqual([1, 2])
  })

  it('пустая программа оставляет только текущий', () => {
    expect(toolPreview([], 1)).toEqual([1])
  })
})
