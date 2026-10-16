'use client'

import { useEffect, useRef, useState } from 'react'
import { UI_MODE_PARAM } from '@/lib/cnc/service-data'
import { CncPanel } from './cnc-panel'
import { TouchPanel } from './touch/touch-panel'
import { UiModeContext, type UiMode } from './ui-mode-context'
import { useCncMachine } from './use-cnc-machine'

const STORAGE_KEY = 'cnc-ui-mode'

const toValue = (m: UiMode) => (m === 'touch' ? 1 : 0)

// forceMode: /classic всегда панельный вариант — сохранённый режим не берётся и не перезаписывается.
export function CncApp({ initialMode = 'panel', forceMode = false }: { initialMode?: UiMode; forceMode?: boolean }) {
  const machine = useCncMachine()
  const { dispatch } = machine
  const [hydrated, setHydrated] = useState(false)
  const skipFirstWrite = useRef(true)

  useEffect(() => {
    const stored = window.localStorage.getItem(STORAGE_KEY)
    const saved: UiMode = stored === 'panel' || stored === 'touch' ? stored : initialMode
    const start: UiMode = forceMode ? initialMode : saved
    dispatch({ type: 'UI_MODE_SET', value: toValue(start) })
    setHydrated(true)
  }, [dispatch, initialMode, forceMode])

  const paramMode: UiMode = machine.state.params[UI_MODE_PARAM] === 0 ? 'panel' : 'touch'
  const mode = forceMode ? initialMode : hydrated ? paramMode : initialMode

  useEffect(() => {
    if (forceMode || !hydrated) return
    if (skipFirstWrite.current) {
      skipFirstWrite.current = false
      return
    }
    window.localStorage.setItem(STORAGE_KEY, paramMode)
  }, [hydrated, paramMode, forceMode])

  const setMode = (m: UiMode) => dispatch({ type: 'UI_MODE_SET', value: toValue(m) })

  return (
    <UiModeContext.Provider value={{ mode, setMode }}>
      {mode === 'touch' ? <TouchPanel machine={machine} /> : <CncPanel machine={machine} />}
    </UiModeContext.Provider>
  )
}
