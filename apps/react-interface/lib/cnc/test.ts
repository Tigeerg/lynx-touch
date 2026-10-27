// Workpiece PROGON (test) subsystem: the NC bounding box, the toolpath view angle, and the run
// mode the ПУСК starts (F4). Extracted from
// machine.ts after the pattern of prog.ts and mpg.ts: the reducer composes the subsystems, and
// the size of the machine is held by the tools/check.ps1 mark.
import { AUTO_VIEW_LABEL } from './data'
import { PATH_VIEWS } from './toolpath'
import type { Result } from './subsystem'
import { AUTO_VIEWS, type MachineState, type TestRunMode, type TestState } from './types'

/** The run mode as the operator sees it: the F4 label and the message use one word each. */
export const TEST_RUN_MODE_LABEL: Record<TestRunMode, string> = {
  sim: 'Симуляция',
  dry: 'Холостой',
  step: 'По шагам',
  mpg: 'По маховику',
}

/** The carousel order of F4: simulation, dry run, step run, handwheel run, then back. */
const TEST_RUN_MODES: readonly TestRunMode[] = ['sim', 'dry', 'step', 'mpg']

/** The handwheel run turns the toolpath through the dry run feed, so it needs dryRun on too. */
const modeNeedsDryRun = (mode: TestRunMode): boolean => mode === 'dry' || mode === 'mpg'

export const initialTest: TestState = {
  bbox: false,
  autoView: 'trajectory',
  view: 'ISO',
  runMode: 'dry',
}

export type TestAction =
  { type: 'TEST_BBOX' } | { type: 'AUTO_VIEW_CYCLE' } | { type: 'PATH_VIEW_CYCLE' } | { type: 'TEST_RUN_MODE' }

const TEST_TYPES = new Set<string>(['TEST_BBOX', 'AUTO_VIEW_CYCLE', 'PATH_VIEW_CYCLE', 'TEST_RUN_MODE'])

export const isTestAction = (a: { type: string }): a is TestAction => TEST_TYPES.has(a.type)

export function testReducer(s: MachineState, a: TestAction): Result {
  switch (a.type) {
    case 'TEST_BBOX':
      return {
        s: { ...s, test: { ...s.test, bbox: !s.test.bbox } },
        msg: s.test.bbox ? 'Габариты УП скрыты' : 'Габариты УП: экстремумы X/Y/Z на экране',
      }

    case 'AUTO_VIEW_CYCLE': {
      const autoView = AUTO_VIEWS[(AUTO_VIEWS.indexOf(s.test.autoView) + 1) % AUTO_VIEWS.length]
      return { s: { ...s, test: { ...s.test, autoView } }, msg: `Отображение: ${AUTO_VIEW_LABEL[autoView]}` }
    }

    case 'PATH_VIEW_CYCLE': {
      const view = PATH_VIEWS[(PATH_VIEWS.indexOf(s.test.view) + 1) % PATH_VIEWS.length]
      return { s: { ...s, test: { ...s.test, view } } }
    }

    case 'TEST_RUN_MODE': {
      const runMode = TEST_RUN_MODES[(TEST_RUN_MODES.indexOf(s.test.runMode) + 1) % TEST_RUN_MODES.length]
      // dryRun follows the mode, so the dry and handwheel runs keep their slow feed and the other
      // modes do not leave a stale "холостой ход" on the panel.
      return {
        s: { ...s, test: { ...s.test, runMode }, dryRun: modeNeedsDryRun(runMode) },
        msg: `Режим прогона: ${TEST_RUN_MODE_LABEL[runMode]}`,
      }
    }
  }
}
