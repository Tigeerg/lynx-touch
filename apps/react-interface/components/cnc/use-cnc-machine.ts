'use client'

import { useEffect, useEffectEvent, useReducer, useState } from 'react'
import { routeKey } from '@/lib/cnc/keys'
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

  useEffect(() => {
    const id = window.setInterval(() => dispatch({ type: 'TICK', dt: TICK_MS }), TICK_MS)
    return () => window.clearInterval(id)
  }, [])

  const onKeyDown = useEffectEvent((e: KeyboardEvent) => {
    const typing = e.target instanceof HTMLElement && !!e.target.closest('input, textarea')
    const route = routeKey(state, e, typing)
    if (!route) return
    if (route.prevent) e.preventDefault()
    if (route.hint) {
      dispatch({ type: 'HINT', text: route.hint })
      return
    }
    if (route.action) dispatch(route.action)
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

  return { state, dispatch, softkeys, focused, pathView: state.test.view }
}
