'use client'

import { useEffect, useEffectEvent, useReducer, useState } from 'react'
import { routeKey } from '@/lib/cnc/keys'
import { initialState, reducer, type Action } from '@/lib/cnc/machine'
import { paramValue, THEME_PARAM } from '@/lib/cnc/service-data'
import { savedTheme, THEME_CLASSES, THEME_STORAGE_KEY, themeClasses } from '@/lib/cnc/theme'
import { getSoftkeys } from '@/lib/cnc/softkeys'
import { stampAction } from '@/lib/cnc/time'
import type { MachineState } from '@/lib/cnc/types'

const TICK_MS = 50

export function useCncMachine() {
  // Every action goes through stampAction once: the clock is read here, the reducer stays pure.
  const [state, dispatch] = useReducer((s: MachineState, a: Action) => reducer(s, stampAction(a)), initialState)
  const [focused, setFocused] = useState(true)
  const [themeReady, setThemeReady] = useState(false)
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

  // The boot script has already painted the saved theme; load it into the state before the
  // live effect touches the html classes, so the first frame is not reset to Light.
  useEffect(() => {
    try {
      // A missing key stays null: `Number(null)` is 0 and would quietly open "Минимал · светлая"
      // instead of the default "Светлая".
      const saved = savedTheme(window.localStorage.getItem(THEME_STORAGE_KEY))
      if (saved !== null) {
        dispatch({ type: 'THEME_SET', value: saved })
      }
    } catch {
      // Storage may be blocked; keep the catalogue default.
    }
    setThemeReady(true)
  }, [])

  const theme = paramValue(state.params, THEME_PARAM)
  useEffect(() => {
    if (!themeReady) return
    const root = document.documentElement
    const active = new Set(themeClasses(theme))
    for (const name of THEME_CLASSES.flat()) root.classList.toggle(name, active.has(name))
    try {
      window.localStorage.setItem(THEME_STORAGE_KEY, String(theme))
    } catch {
      // Storage may be blocked; the panel still applies the theme for this session.
    }
  }, [theme, themeReady])

  // A program that lives outside the bundle (a real CAM file, megabytes of blocks) is fetched the
  // first time the operator opens it. The reducer stays pure: it only receives the finished lines.
  const activeName = state.files[state.activeFile]?.name
  const activeSource = state.files[state.activeFile]?.source
  const activeLines = state.files[state.activeFile]?.lines.length ?? 0
  useEffect(() => {
    if (!activeSource || activeLines > 0) return
    const index = state.activeFile
    let cancelled = false
    fetch(activeSource)
      .then((r) => (r.ok ? r.text() : Promise.reject(new Error(String(r.status)))))
      .then((text) => {
        if (cancelled) return
        const lines = text.split(/\r?\n/)
        if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop()
        dispatch({ type: 'FILE_LOADED', index, lines })
      })
      .catch(() => {
        if (!cancelled) dispatch({ type: 'HINT', text: `Не удалось загрузить ${activeName}` })
      })
    return () => {
      cancelled = true
    }
  }, [activeSource, activeLines, activeName, state.activeFile])

  return { state, dispatch, softkeys, focused, pathView: state.test.view }
}
