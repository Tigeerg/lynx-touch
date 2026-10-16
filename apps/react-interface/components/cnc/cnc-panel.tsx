'use client'

import { useState } from 'react'
import { TOOLPATH_PARAM, SCREEN_SIZE_PARAM } from '@/lib/cnc/service-data'
import { MODE_LABEL } from '@/lib/cnc/data'
import { MODE_LOCKED_SCREENS } from '@/lib/cnc/types'
import { FhdStage } from './fhd-stage'
import { useCncMachine } from './use-cnc-machine'

const SCREEN_W = 1024
const SCREEN_H = 600
const STACK_RIGHT_W = 520
const SOFTKEY_H = 100
const PANEL_H = 300
const PANEL_MIN_W = 1840
const KEY_STACK_H = 800
import { FileDialog } from './file-dialog'
import { CalcOverlay } from './calc-overlay'
import { ToolCallDialog } from './tool-call-dialog'
import { HardwareKeyboard } from './hardware/hardware-keyboard'
import { HardwareNav } from './hardware/hardware-nav'
import { HardwarePanel } from './hardware/hardware-panel'
import { HardwareSoftkeys } from './hardware/hardware-softkeys'
import { HeaderBar } from './header-bar'
import { HelpOverlay } from './help-overlay'
import { ScreenTabs } from './screen-tabs'
import { SplashScreen } from './splash-screen'
import { cn } from '@/lib/utils'
import { ProgramSide } from './program-side'
import { OperatorPad, SideStatus } from './side-status'
import { MessageBar, SoftkeyBar } from './softkey-bar'
import { PositionScreen } from './screens/position-screen'
import { ProgramScreen } from './screens/program-screen'
import { ServiceScreen } from './screens/service-screen'
import { AlarmScreen, OffsetScreen, ToolScreen } from './screens/table-screens'

export function CncPanel({ machine }: { machine: ReturnType<typeof useCncMachine> }) {
  const { state, dispatch, softkeys, clock, focused, pathView } = machine
  const [hardware, setHardware] = useState(true)

  const showPath = state.params[TOOLPATH_PARAM] === 1 || state.sim.active
  const asideHidden = state.screen === 'prog' && state.sim.active
  const screenSize = state.params[SCREEN_SIZE_PARAM] ?? 0
  const screenScale = screenSize === 1 ? 10 / 7 : 1
  const DISPLAY_W = Math.round(SCREEN_W * screenScale)
  const DISPLAY_H = Math.round(SCREEN_H * screenScale)
  const RACK_W = Math.max(DISPLAY_W + 12 + STACK_RIGHT_W, PANEL_MIN_W) + 16
  const RACK_H = 16 + Math.max(DISPLAY_H + 12 + SOFTKEY_H, KEY_STACK_H) + 12 + PANEL_H

  const activeAlarms = state.alarms.filter((a) => a.active).length
  const showModes = state.screen === 'pos' || state.screen === 'prog'

  const displayInner = (
    <div
      className={cn(
        'relative flex flex-col gap-1.5 overflow-hidden bg-background p-1.5',
        hardware && 'rounded-md ring-2 ring-hw-edge',
      )}
      style={{ width: SCREEN_W, height: SCREEN_H }}
    >
      <HeaderBar
        state={state}
        clock={clock}
        focused={focused}
        hardwareOpen={hardware}
        onToggleHardware={() => setHardware((v) => !v)}
      />
      <ScreenTabs
        screen={state.screen}
        onSelect={(screen) => dispatch({ type: 'SET_SCREEN', screen })}
        alarmCount={activeAlarms}
        locked={MODE_LOCKED_SCREENS[state.mode]}
        lockedTitle={`Недоступно в режиме «${MODE_LABEL[state.mode]}»`}
      />

      <div
        className={cn(
          'grid min-h-0 flex-1 gap-1.5',
          showModes && !asideHidden
            ? 'grid-cols-[minmax(0,1fr)_300px]'
            : hardware || asideHidden
              ? 'grid-cols-1 grid-rows-[minmax(0,1fr)]'
              : 'grid-cols-1 grid-rows-[minmax(0,1fr)_auto]',
        )}
      >
        <main className="min-h-0">
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
        {showModes ? (
          state.screen === 'prog' ? (
            !asideHidden && <ProgramSide state={state} dispatch={dispatch} showPad={!hardware} />
          ) : (
            <SideStatus state={state} dispatch={dispatch} showPad={!hardware} />
          )
        ) : (
          !hardware && <OperatorPad state={state} dispatch={dispatch} layout="compact" />
        )}
      </div>

      <MessageBar message={state.message} />
      <SoftkeyBar keys={softkeys} onPress={(k) => !k.disabled && dispatch(k.action)} />

      {state.fileDialog && <FileDialog state={state} dispatch={dispatch} />}
      {state.toolDialog && <ToolCallDialog state={state} dispatch={dispatch} />}
      {state.calc && <CalcOverlay dispatch={dispatch} />}
      {state.help && <HelpOverlay onClose={() => dispatch({ type: 'HELP', open: false })} />}
      <SplashScreen />
    </div>
  )

  const display = (
    <div className="relative overflow-hidden" style={{ width: DISPLAY_W, height: DISPLAY_H }}>
      <div
        style={{ width: SCREEN_W, height: SCREEN_H, transform: `scale(${screenScale})`, transformOrigin: 'top left' }}
      >
        {displayInner}
      </div>
    </div>
  )

  if (!hardware) {
    return (
      <FhdStage width={DISPLAY_W} height={DISPLAY_H}>
        {display}
      </FhdStage>
    )
  }

  return (
    <FhdStage width={RACK_W} height={RACK_H}>
      <div className="flex h-full w-full flex-col gap-3 overflow-hidden bg-hw-edge p-2">
        <div className="flex shrink-0 gap-3">
          <div className="flex shrink-0 flex-col gap-3" style={{ width: DISPLAY_W }}>
            {display}
            <HardwareSoftkeys keys={softkeys} onPress={(k) => !k.disabled && dispatch(k.action)} />
          </div>
          <div className="flex min-h-0 min-w-0 flex-1 flex-col gap-3">
            <HardwareKeyboard state={state} dispatch={dispatch} />
            <HardwareNav state={state} dispatch={dispatch} className="flex-1" />
          </div>
        </div>
        <HardwarePanel state={state} dispatch={dispatch} />
      </div>
    </FhdStage>
  )
}
