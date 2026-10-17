// Подсистема рабочего места ПРОГОН: показ графики и шторки кода, габариты УП,
// ракурс траектории. Вынесена из machine.ts по образцу prog.ts и mpg.ts: редуктор
// сводит подсистемы, а размер машины держит метка tools/check.ps1.
import { PATH_VIEWS } from './toolpath'
import type { MachineState, Message, TestState } from './types'

export const initialTest: TestState = { curtain: false, bbox: false, hidePath: false, view: 'ISO' }

export type TestAction =
  { type: 'TEST_CURTAIN' } | { type: 'TEST_BBOX' } | { type: 'TEST_CLEAR_PATH' } | { type: 'PATH_VIEW_CYCLE' }

const TEST_TYPES = new Set<string>(['TEST_CURTAIN', 'TEST_BBOX', 'TEST_CLEAR_PATH', 'PATH_VIEW_CYCLE'])

export const isTestAction = (a: { type: string }): a is TestAction => TEST_TYPES.has(a.type)

export type Result = { s: MachineState; msg?: string; level?: Message['level'] }

export function testReducer(s: MachineState, a: TestAction): Result {
  switch (a.type) {
    case 'TEST_CURTAIN':
      return { s: { ...s, test: { ...s.test, curtain: !s.test.curtain } } }

    case 'TEST_BBOX':
      return {
        s: { ...s, test: { ...s.test, bbox: !s.test.bbox } },
        msg: s.test.bbox ? 'Габариты УП скрыты' : 'Габариты УП: экстремумы X/Y/Z на экране',
      }

    case 'TEST_CLEAR_PATH':
      return {
        s: { ...s, test: { ...s.test, hidePath: !s.test.hidePath } },
        msg: s.test.hidePath ? 'Графика возвращена' : 'Графика очищена — F6 вернёт отрисовку',
      }

    case 'PATH_VIEW_CYCLE': {
      const view = PATH_VIEWS[(PATH_VIEWS.indexOf(s.test.view) + 1) % PATH_VIEWS.length]
      return { s: { ...s, test: { ...s.test, view } } }
    }
  }
}
