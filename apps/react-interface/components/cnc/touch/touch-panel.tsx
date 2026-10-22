'use client'

import { TOOLPATH_PARAM } from '@/lib/cnc/service-data'
import { FhdStage } from '../fhd-stage'
import { FileDialog } from '../file-dialog'
import { CalcOverlay } from '../calc-overlay'
import { ToolMeasureDialog } from '../tool-measure-dialog'
import { ToolCallDialog } from '../tool-call-dialog'
import { HelpOverlay } from '../help-overlay'
import { ProgramSide } from '../program-side'
import { SetupScreen, SetupSide } from '../screens/setup-screen'
import { ProgramScreen } from '../screens/program-screen'
import { TestScreen } from '../screens/test-screen'
import { AutoScreen } from '../screens/auto-screen'
import { ServiceScreen } from '../screens/service-screen'
import { AlarmScreen, OffsetScreen, ToolScreen } from '../screens/table-screens'
import { SideStatus } from '../side-status'
import { MessageBar, SoftkeyBar } from '../softkey-bar'
import { SplashScreen } from '../splash-screen'
import { useCncMachine } from '../use-cnc-machine'
import { cn } from '@/lib/utils'
import { TouchDeck } from './touch-deck'
import { TouchHeader } from './touch-header'
import { TouchTabs } from './touch-tabs'

// The 15.6" FHD 16:9 panel is rotated to portrait (1080x1920, ~141 PPI, 0.18 mm/px). The logical
// canvas is 1.25x (the panel's native Windows scale) -> 1 logical px ~= 0.225 mm; a 44 px element
// ~= a 10 mm touch target.
const SCREEN_W = 864
const SCREEN_H = 1536

export function TouchPanel({ machine }: { machine: ReturnType<typeof useCncMachine> }) {
  const { state, dispatch, softkeys, pathView } = machine
  const showPath = state.params[TOOLPATH_PARAM] === 1 || state.sim.active
  const activeAlarms = state.alarms.filter((a) => a.active).length
  const testMode = state.workplace === 'TEST'
  const showStatus = (state.screen === 'pos' || state.screen === 'prog') && !state.sim.active && !testMode

  return (
    <FhdStage width={SCREEN_W} height={SCREEN_H}>
      <div
        className={cn(
          'touch-ui cnc-screen relative flex touch-manipulation select-none flex-col gap-[var(--panel-gap)] overflow-hidden bg-background p-1.5',
          testMode && 'ring-4 ring-warning',
        )}
        style={{ width: SCREEN_W, height: SCREEN_H }}
      >
        {testMode && (
          <div className="pointer-events-none absolute left-1/2 top-1 z-30 -translate-x-1/2">
            <span className="animate-pulse rounded-sm bg-warning px-3 py-0.5 text-xs font-bold uppercase tracking-wider text-warning-foreground">
              Тестовый режим
            </span>
          </div>
        )}
        <TouchHeader state={state} dispatch={dispatch} />
        <TouchTabs
          screen={state.screen}
          workplace={state.workplace}
          onSelect={(screen) => dispatch({ type: 'SET_SCREEN', screen })}
          alarmCount={activeAlarms}
        />

        <div className="relative flex min-h-0 flex-1 flex-col gap-1.5">
          <main className="min-h-0 flex-1">
            {state.screen === 'pos' && <SetupScreen state={state} dispatch={dispatch} />}
            {state.screen === 'prog' &&
              (testMode ? (
                <TestScreen state={state} dispatch={dispatch} pathView={pathView} />
              ) : state.workplace === 'RUN' ? (
                <AutoScreen state={state} dispatch={dispatch} pathView={pathView} />
              ) : (
                <ProgramScreen state={state} dispatch={dispatch} pathView={pathView} showPath={showPath} />
              ))}
            {state.screen === 'offset' && <OffsetScreen state={state} dispatch={dispatch} />}
            {state.screen === 'tool' && <ToolScreen state={state} />}
            {state.screen === 'alarm' && <AlarmScreen state={state} />}
            {state.screen === 'service' && <ServiceScreen state={state} dispatch={dispatch} />}
          </main>
          {showStatus && (
            <div className="flex shrink-0 flex-col">
              {state.screen === 'prog' ? (
                <ProgramSide state={state} dispatch={dispatch} showPad={false} wide />
              ) : state.workplace === 'SETUP' ? (
                <SetupSide state={state} dispatch={dispatch} showPad={false} />
              ) : (
                <SideStatus state={state} dispatch={dispatch} showPad={false} wide />
              )}
            </div>
          )}
          <MessageBar message={state.message} />
          <SoftkeyBar keys={softkeys} onPress={(k) => !k.disabled && dispatch(k.action)} />
        </div>

        <TouchDeck state={state} dispatch={dispatch} screen={state.screen} />

        {state.fileDialog && <FileDialog state={state} dispatch={dispatch} />}
        {state.toolDialog && <ToolCallDialog state={state} dispatch={dispatch} />}
        {state.calc && <CalcOverlay dispatch={dispatch} />}
        {state.toolMeasure.open && <ToolMeasureDialog state={state} dispatch={dispatch} />}
        {state.help && <HelpOverlay onClose={() => dispatch({ type: 'HELP', open: false })} />}
        <SplashScreen />
      </div>
    </FhdStage>
  )
}
