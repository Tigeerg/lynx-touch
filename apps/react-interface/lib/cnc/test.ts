// Workpiece PROGON (test) subsystem: showing the graphics and the code curtain, the NC
// bounding box, the toolpath view angle, and the run mode the ПУСК starts (F4). Extracted from
// machine.ts after the pattern of prog.ts and mpg.ts: the reducer composes the subsystems, and
// the size of the machine is held by the tools/check.ps1 mark.
import { AUTO_VIEW_LABEL } from './data'
import { PATH_VIEWS } from './toolpath'
import type { Result } from './subsystem'
import { AUTO_VIEWS, type MachineState, type TestRunMode, type TestState } from './types'

/** The run mode as the operator sees it: the F4 label and the message use one word each. */
export const TEST_RUN_MODE_LABEL: Record<TestRunMode, string> = { sim: 'Симуляция', dry: 'Холостой' }

/** The carousel order of F4: a simulation first, then the dry run, then back. */
const TEST_RUN_MODES: readonly TestRunMode[] = ['sim', 'dry']

export const initialTest: TestState = {
  curtain: false,
  bbox: false,
  hidePath: false,
  autoView: 'both',
  view: 'ISO',
  runMode: 'dry',
}

export type TestAction =
  | { type: 'TEST_CURTAIN' }
  | { type: 'TEST_BBOX' }
  | { type: 'TEST_CLEAR_PATH' }
  | { type: 'AUTO_VIEW_CYCLE' }
  | { type: 'PATH_VIEW_CYCLE' }
  | { type: 'TEST_RUN_MODE' }

const TEST_TYPES = new Set<string>([
  'TEST_CURTAIN',
  'TEST_BBOX',
  'TEST_CLEAR_PATH',
  'AUTO_VIEW_CYCLE',
  'PATH_VIEW_CYCLE',
  'TEST_RUN_MODE',
])

export const isTestAction = (a: { type: string }): a is TestAction => TEST_TYPES.has(a.type)

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

    case 'AUTO_VIEW_CYCLE': {
      const autoView = AUTO_VIEWS[(AUTO_VIEWS.indexOf(s.test.autoView) + 1) % AUTO_VIEWS.length]
      return { s: { ...s, test: { ...s.test, autoView } }, msg: `Вид: ${AUTO_VIEW_LABEL[autoView]}` }
    }

    case 'PATH_VIEW_CYCLE': {
      const view = PATH_VIEWS[(PATH_VIEWS.indexOf(s.test.view) + 1) % PATH_VIEWS.length]
      return { s: { ...s, test: { ...s.test, view } } }
    }

    case 'TEST_RUN_MODE': {
      const runMode = TEST_RUN_MODES[(TEST_RUN_MODES.indexOf(s.test.runMode) + 1) % TEST_RUN_MODES.length]
      // dryRun follows the mode, so the dry run keeps its slow feed and the simulation does not
      // leave a stale "холостой ход" on the panel.
      return {
        s: { ...s, test: { ...s.test, runMode }, dryRun: runMode === 'dry' },
        msg: `Режим прогона: ${TEST_RUN_MODE_LABEL[runMode]}`,
      }
    }
  }
}
