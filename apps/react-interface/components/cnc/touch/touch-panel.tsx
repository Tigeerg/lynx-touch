'use client'

import { TOOLPATH_PARAM } from '@/lib/cnc/service-data'
import { MODE_LOCKED_SCREENS } from '@/lib/cnc/types'
import { FhdStage } from '../fhd-stage'
import { FileDialog } from '../file-dialog'
import { CalcOverlay } from '../calc-overlay'
import { ToolCallDialog } from '../tool-call-dialog'
import { HelpOverlay } from '../help-overlay'
import { ProgramSide } from '../program-side'
import { PositionScreen } from '../screens/position-screen'
import { ProgramScreen } from '../screens/program-screen'
import { ServiceScreen } from '../screens/service-screen'
import { AlarmScreen, OffsetScreen, ToolScreen } from '../screens/table-screens'
import { SideStatus } from '../side-status'
import { MessageBar, SoftkeyBar } from '../softkey-bar'
import { SplashScreen } from '../splash-screen'
import { useCncMachine } from '../use-cnc-machine'
import { TouchDeck } from './touch-deck'
import { TouchHeader } from './touch-header'
import { TouchTabs } from './touch-tabs'

// Панель 15.6" FHD 16:9 повёрнута в портрет (1080x1920, ~141 PPI, 0.18 мм/px). Логический холст 1.25x
// (родной масштаб Windows для этой панели) -> 1 логический px ~= 0.225 мм; элемент 44 px ~= 10 мм сенсорной цели.
const SCREEN_W = 864
const SCREEN_H = 1536

export function TouchPanel({ machine }: { machine: ReturnType<typeof useCncMachine> }) {
  const { state, dispatch, softkeys, clock, pathView } = machine
  const showPath = state.params[TOOLPATH_PARAM] === 1 || state.sim.active
  const activeAlarms = state.alarms.filter((a) => a.active).length
  const showStatus = (state.screen === 'pos' || state.screen === 'prog') && !state.sim.active

  return (
    <FhdStage width={SCREEN_W} height={SCREEN_H}>
      <div
        className="touch-ui relative flex touch-manipulation select-none flex-col gap-1.5 overflow-hidden bg-background p-1.5"
        style={{ width: SCREEN_W, height: SCREEN_H }}
      >
        <TouchHeader state={state} clock={clock} dispatch={dispatch} />
        <TouchTabs
          screen={state.screen}
          onSelect={(screen) => dispatch({ type: 'SET_SCREEN', screen })}
          alarmCount={activeAlarms}
          locked={MODE_LOCKED_SCREENS[state.mode]}
        />

        <div className="relative flex min-h-0 flex-1 flex-col gap-1.5">
          <main className="min-h-0 flex-1">
            {state.screen === 'pos' && (
              <PositionScreen state={state} dispatch={dispatch} pathView={pathView} showPath={showPath} />
            )}
            {state.screen === 'prog' && (
              <ProgramScreen state={state} dispatch={dispatch} pathView={pathView} showPath={showPath} />
            )}
            {state.screen === 'offset' && <OffsetScreen state={state} dispatch={dispatch} />}
            {state.screen === 'tool' && <ToolScreen state={state} />}
            {state.screen === 'alarm' && <AlarmScreen state={state} />}
            {state.screen === 'service' && <ServiceScreen state={state} dispatch={dispatch} />}
          </main>
          {showStatus && (
            <div className="flex shrink-0 flex-col">
              {state.screen === 'prog' ? (
                <ProgramSide state={state} dispatch={dispatch} showPad={false} wide />
              ) : (
                <SideStatus state={state} dispatch={dispatch} showPad={false} wide />
              )}
            </div>
          )}
          <MessageBar message={state.message} />
          <SoftkeyBar keys={softkeys} onPress={(k) => !k.disabled && dispatch(k.action)} />
        </div>

        <TouchDeck state={state} dispatch={dispatch} />

        {state.fileDialog && <FileDialog state={state} dispatch={dispatch} />}
        {state.toolDialog && <ToolCallDialog state={state} dispatch={dispatch} />}
        {state.calc && <CalcOverlay dispatch={dispatch} />}
        {state.help && <HelpOverlay onClose={() => dispatch({ type: 'HELP', open: false })} />}
        <SplashScreen />
      </div>
    </FhdStage>
  )
}
