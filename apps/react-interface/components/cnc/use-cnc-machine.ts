'use client'

import { useEffect, useEffectEvent, useReducer, useState } from 'react'
import { keyToAction } from '@/lib/cnc/keys'
import { initialState, reducer, type Action } from '@/lib/cnc/machine'
import { paramValue, THEME_PARAM } from '@/lib/cnc/service-data'
import { getSoftkeys } from '@/lib/cnc/softkeys'
import { stampAction } from '@/lib/cnc/time'
import type { MachineState } from '@/lib/cnc/types'

const TICK_MS = 50

export function useCncMachine() {
  // Every action goes through stampAction once: the clock is read here, the reducer stays pure.
  const [state, dispatch] = useReducer((s: MachineState, a: Action) => reducer(s, stampAction(a)), initialState)
  const [focused, setFocused] = useState(true)
  const softkeys = getSoftkeys(state)
  const cycleView = () => dispatch({ type: 'PATH_VIEW_CYCLE' })

  useEffect(() => {
    const id = window.setInterval(() => dispatch({ type: 'TICK', dt: TICK_MS }), TICK_MS)
    return () => window.clearInterval(id)
  }, [])

  const onKeyDown = useEffectEvent((e: KeyboardEvent) => {
    if (e.ctrlKey || e.metaKey || e.altKey) return
    // The emergency stop is reachable from anywhere: before this branch the editor, the ladder
    // form and a focused input swallowed Shift+Esc, so the operator could not stop the machine
    // while typing.
    if (e.code === 'Escape' && e.shiftKey) {
      e.preventDefault()
      dispatch({ type: 'ESTOP' })
      return
    }
    // Help is modal: H and Esc close it, F1..F8 only keep the browser from acting, and every
    // other key is swallowed. Otherwise Enter, Space, the tabs, the axes and the overrides
    // reached the machine under the overlay.
    if (state.help) {
      if (e.code === 'KeyH' || e.code === 'Escape') {
        e.preventDefault()
        dispatch({ type: 'HELP', open: false })
        return
      }
      if (/^F[1-8]$/.test(e.code)) e.preventDefault()
      return
    }
    const fn = /^F([1-8])$/.exec(e.code)
    if (fn) {
      e.preventDefault()
      const key = softkeys[Number(fn[1]) - 1]
      if (key && !key.disabled) dispatch(key.action)
      else if (key?.disabled) dispatch({ type: 'HINT', text: key.reason ?? 'Сейчас недоступно' })
      return
    }
    if (state.editor) return
    if (state.ladder.form || (e.target instanceof HTMLElement && e.target.closest('input, textarea'))) return
    if (e.code === 'KeyV' && !state.fileDialog && (state.screen === 'pos' || state.screen === 'prog')) {
      e.preventDefault()
      cycleView()
      return
    }
    const action = keyToAction(state, e)
    if (!action) return
    e.preventDefault()
    dispatch(action)
  })

  useEffect(() => {
    const handleKey = (e: KeyboardEvent) => onKeyDown(e)
    const handleFocus = () => setFocused(true)
    const handleBlur = () => setFocused(false)
    window.addEventListener('keydown', handleKey)
    window.addEventListener('focus', handleFocus)
    window.addEventListener('blur', handleBlur)
    setFocused(document.hasFocus())
    return () => {
      window.removeEventListener('keydown', handleKey)
      window.removeEventListener('focus', handleFocus)
      window.removeEventListener('blur', handleBlur)
    }
  }, [])

  const theme = paramValue(state.params, THEME_PARAM)
  useEffect(() => {
    const root = document.documentElement
    root.classList.toggle('dark', theme === 1 || theme === 3 || theme === 4)
    root.classList.toggle('theme-graphite', theme === 4)
    root.classList.toggle('theme-mint', theme === 5)
    root.classList.toggle('theme-minimal', theme === 0 || theme === 1)
    root.classList.toggle('theme-minimal-dark', theme === 1)
  }, [theme])

  return { state, dispatch, softkeys, focused, pathView: state.test.view, cycleView }
}
