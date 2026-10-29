'use client'

import { Pause, Play, RotateCcw } from 'lucide-react'
import { JOG_STEPS, RAPID_STEPS, WORKPLACE_LABEL } from '@/lib/cnc/data'
import type { Action } from '@/lib/cnc/machine'
import { MAX_FEED_PARAM, MAX_SPINDLE_PARAM, paramValue } from '@/lib/cnc/service-data'
import { WORKPLACES, type Axis, type MachineState } from '@/lib/cnc/types'
import { WORKPLACE_ICON } from '../header-bar'
import { EmergencyStop, HwGroup, HwKey, Knob } from './hw-parts'
import { AuxBlock } from './hardware-aux'
import { JogPad } from './hardware-panel/jog-pad'
import { ProgramModeBlock } from './hardware-panel/program-mode-block'
import { SpindleBlock } from './hardware-panel/spindle-block'

const MIN_SPINDLE = 50
const OVERRIDE_STEP = 10

const PANEL_GAP = 'w-px shrink-0 self-stretch bg-hw-edge'

type Dispatch = (a: Action) => void

export function HardwarePanel({ state, dispatch }: { state: MachineState; dispatch: Dispatch }) {
  const inService = state.screen === 'service'
  const rapidIndex = RAPID_STEPS.indexOf(state.rapidOverride)
  const rapidFullIndex = RAPID_STEPS.indexOf(100)

  const jog = (axis: Axis, dir: 1 | -1) => {
    if (inService) dispatch({ type: 'SERVICE_AXIS', axis })
    dispatch({ type: 'JOG', axis, dir })
  }

  return (
    <section
      aria-label="Физический пульт управления"
      className="flex min-h-0 flex-1 items-stretch justify-between gap-4 overflow-hidden rounded-lg border-2 border-hw-edge bg-hw-plate px-4 py-3 shadow-inner"
    >
      <HwGroup title="Аварийный стоп" className="w-[186px] shrink-0 items-center justify-center">
        <EmergencyStop pressed={state.run === 'ESTOP'} onPress={() => dispatch({ type: 'ESTOP' })} />
      </HwGroup>

      <span aria-hidden="true" className={PANEL_GAP} />

      <HwGroup title="Цикл и режим" className="w-[375px] shrink-0">
        <div className="flex flex-1 flex-col justify-center gap-2 px-4">
          {/* The three cycle keys share one gutter: the same 16 px between the keys and to the
              edges of the group, and 24 px above for the state lamp over each key. */}
          <div className="grid grid-cols-3 gap-4 pb-4 pt-6">
            <HwKey
              label="Пуск"
              sub="Enter"
              icon={Play}
              lit={state.run === 'RUN'}
              tone="success"
              cap="bg-hw-start text-hw-ink"
              large
              onClick={() => dispatch({ type: 'CYCLE_START' })}
              className="h-[128px] w-full gap-1.5 px-3 py-2 [&_svg]:size-9"
            />
            <HwKey
              label="Стоп"
              sub="Space"
              icon={Pause}
              lit={state.run === 'HOLD'}
              tone="warning"
              cap="bg-hw-stop text-hw-ink"
              large
              onClick={() => dispatch({ type: 'FEED_HOLD' })}
              className="h-[128px] w-full gap-1.5 px-3 py-2 [&_svg]:size-9"
            />
            <HwKey
              label="Сброс"
              sub="Esc"
              icon={RotateCcw}
              lit={state.run === 'ALARM'}
              tone="primary"
              cap="bg-hw-reset text-hw-ink"
              large
              onClick={() => dispatch({ type: 'RESET' })}
              className="h-[128px] w-full gap-1.5 px-3 py-2 [&_svg]:size-9"
            />
          </div>

          {/* The same three-column grid as the cycle keys above: one gutter between the keys and
              to the edges of the group. */}
          <div role="group" aria-label="Режим" className="grid grid-cols-3 gap-4">
            {WORKPLACES.map((w) => (
              <HwKey
                key={w}
                label={WORKPLACE_LABEL[w]}
                icon={WORKPLACE_ICON[w]}
                lit={state.workplace === w}
                tone="primary"
                stretch
                onClick={() => dispatch({ type: 'SET_WORKPLACE', workplace: w })}
                className="gap-1 px-1.5 text-[12px] tracking-tight [&_svg]:size-6"
              />
            ))}
          </div>
        </div>
      </HwGroup>

      <span aria-hidden="true" className={PANEL_GAP} />

      <HwGroup title="Функции и вспомогательные" className="grow">
        <div className="flex flex-1 flex-col justify-center gap-4 pt-4">
          <ProgramModeBlock state={state} dispatch={dispatch} />
          <SpindleBlock state={state} dispatch={dispatch} />
          <AuxBlock state={state} dispatch={dispatch} />
        </div>
      </HwGroup>

      <span aria-hidden="true" className={PANEL_GAP} />

      <HwGroup title="Ручное перемещение" className="shrink-0">
        <div className="flex flex-1 items-center gap-4">
          <JogPad state={state} onJog={jog} onToggleRapid={() => dispatch({ type: 'TOGGLE', key: 'jogRapid' })} />
          <div role="group" aria-label="Шаг перемещения" className="flex shrink-0 flex-col gap-4 pt-4">
            {JOG_STEPS.map((s) => (
              <HwKey
                key={s}
                label={s}
                title={`Шаг ${s} мм`}
                lit={state.jogStep === s}
                tone="success"
                onClick={() => dispatch({ type: 'SET_STEP', step: s })}
                className="font-mono text-sm"
              />
            ))}
          </div>
        </div>
      </HwGroup>

      <span aria-hidden="true" className={PANEL_GAP} />

      <HwGroup title="Коррекции" className="w-[426px] shrink-0">
        <div className="flex flex-1 items-center gap-2">
          <Knob
            label="Подача"
            readout={`${state.feedOverride}%`}
            offset={(state.feedOverride - 100) / OVERRIDE_STEP}
            minOffset={-100 / OVERRIDE_STEP}
            maxOffset={(paramValue(state.params, MAX_FEED_PARAM) - 100) / OVERRIDE_STEP}
            onStep={(dir) => dispatch({ type: 'OVERRIDE', kind: 'feed', dir })}
          />
          <Knob
            label="Шпиндель"
            readout={`${state.spindleOverride}%`}
            offset={(state.spindleOverride - 100) / OVERRIDE_STEP}
            minOffset={(MIN_SPINDLE - 100) / OVERRIDE_STEP}
            maxOffset={(paramValue(state.params, MAX_SPINDLE_PARAM) - 100) / OVERRIDE_STEP}
            onStep={(dir) => dispatch({ type: 'OVERRIDE', kind: 'spindle', dir })}
          />
          <Knob
            label="Ускор. G0"
            readout={`${state.rapidOverride}%`}
            offset={rapidIndex - rapidFullIndex}
            minOffset={-rapidFullIndex}
            maxOffset={RAPID_STEPS.length - 1 - rapidFullIndex}
            onStep={(dir) => dispatch({ type: 'OVERRIDE', kind: 'rapid', dir })}
          />
        </div>
      </HwGroup>
    </section>
  )
}
