'use client'

import { useEffect, useEffectEvent, useReducer, useState } from 'react'
import { initialState, reducer, type Action } from '@/lib/cnc/machine'
import { THEME_PARAM } from '@/lib/cnc/service-data'
import { getSoftkeys } from '@/lib/cnc/softkeys'
import { TABS, WORKPLACES, screenOfTab, type Axis, type MachineState } from '@/lib/cnc/types'

const TICK_MS = 50
const AXIS_KEYS: Record<string, Axis> = { KeyX: 'X', KeyY: 'Y', KeyZ: 'Z', KeyA: 'A' }

function keyToAction(state: MachineState, e: KeyboardEvent): Action | null {
  if (state.toolDialog) {
    const digit = /^(?:Digit|Numpad)([0-9])$/.exec(e.code)
    if (digit) return { type: 'TOOL_INPUT', value: digit[1] }
    if (e.code === 'Backspace') return { type: 'TOOL_BACK' }
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'TOOL_RUN' }
    if (e.code === 'Escape') return { type: 'TOOL_DIALOG', open: false }
    return null
  }
  if (state.calc) {
    if (e.code === 'Escape') return { type: 'CALC', open: false }
    return null
  }
  if (state.search.open) {
    if (e.code === 'Escape') return { type: 'SEARCH', open: false }
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'SEARCH_FIND' }
    return null
  }
  if (state.fileDialog) {
    if (e.code === 'ArrowUp') return { type: 'FILE_CURSOR', delta: -1 }
    if (e.code === 'ArrowDown') return { type: 'FILE_CURSOR', delta: 1 }
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'FILE_OPEN' }
    if (e.code === 'Escape' || e.code === 'KeyO') return { type: 'FILE_DIALOG', open: false }
    return null
  }
  if (state.screen === 'service' && state.service.section === 'ladder' && !state.help) {
    const enter = e.code === 'Enter' || e.code === 'NumpadEnter'
    if (state.ladder.manager) {
      if (enter) return { type: 'LADDER_LOAD' }
      if (e.code === 'Insert') return { type: 'LADDER_NEW' }
      if (e.code === 'Delete') return { type: 'LADDER_REMOVE' }
      if (e.code === 'Escape' && !e.shiftKey) return { type: 'LADDER_MANAGER', open: false }
    } else if (state.ladder.draft) {
      if (e.code === 'ArrowUp' && e.shiftKey) return { type: 'LADDER_CYCLE_PATH', dir: -1 }
      if (e.code === 'ArrowDown' && e.shiftKey) return { type: 'LADDER_CYCLE_PATH', dir: 1 }
      if (e.code === 'Insert' && e.shiftKey) return { type: 'LADDER_ADD_COMPARE' }
      if (e.code === 'Insert' && e.ctrlKey) return { type: 'LADDER_ADD_BRANCH' }
      if (enter) return { type: 'LADDER_FORM_OPEN' }
      if (e.code === 'Insert') return { type: 'LADDER_ADD_CONTACT' }
      if (e.code === 'Delete') return { type: 'LADDER_DELETE' }
      if (e.code === 'Escape' && !e.shiftKey) return { type: 'LADDER_EDIT', on: false }
      if (e.code === 'ArrowLeft') return { type: 'LADDER_COL', dir: -1 }
      if (e.code === 'ArrowRight') return { type: 'LADDER_COL', dir: 1 }
    }
  }
  if (state.editMode) {
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'EDIT_BEGIN' }
    if (e.code === 'Insert') return { type: 'EDIT_INSERT' }
    if (e.code === 'Delete') return { type: 'EDIT_DELETE' }
    if (e.code === 'Escape' && !e.shiftKey && !state.help) return { type: 'EDIT_MODE', on: false }
  }
  if (e.code === 'KeyO') return { type: 'FILE_DIALOG', open: true }
  if (e.code === 'KeyE') return { type: 'EDIT_MODE' }
  if (e.code === 'KeyP' && (state.screen === 'pos' || state.screen === 'prog')) return { type: 'TOOLPATH_TOGGLE' }

  const digit = /^Digit([1-5])$/.exec(e.code)
  if (digit) return { type: 'SET_SCREEN', screen: screenOfTab(TABS[Number(digit[1]) - 1], state.workplace) }

  if (state.screen === 'service' && !state.help) {
    const sv = state.service
    if (e.code === 'Enter' || e.code === 'NumpadEnter') return { type: 'SERVICE_ENTER' }
    if (e.code === 'Escape' && !e.shiftKey && (sv.auth || sv.editing || sv.focus === 'list'))
      return { type: 'SERVICE_CANCEL' }
  }
  if (state.screen === 'service') {
    if (e.code === 'ArrowLeft') return { type: 'PARAM_ADJUST', dir: -1, coarse: e.shiftKey }
    if (e.code === 'ArrowRight') return { type: 'PARAM_ADJUST', dir: 1, coarse: e.shiftKey }
    if (AXIS_KEYS[e.code]) return { type: 'SERVICE_AXIS', axis: AXIS_KEYS[e.code] }
  }
  if (AXIS_KEYS[e.code]) return { type: 'SELECT_AXIS', axis: AXIS_KEYS[e.code] }

  switch (e.code) {
    case 'Enter':
    case 'NumpadEnter':
      return { type: 'CYCLE_START' }
    case 'Space':
      return { type: 'FEED_HOLD' }
    case 'Escape':
      if (state.help) return { type: 'HELP', open: false }
      return e.shiftKey ? { type: 'ESTOP' } : { type: 'RESET' }
    case 'Pause':
      return { type: 'ESTOP' }
    case 'KeyM': {
      const dir = e.shiftKey ? -1 : 1
      const idx = (WORKPLACES.indexOf(state.workplace) + dir + WORKPLACES.length) % WORKPLACES.length
      return { type: 'SET_WORKPLACE', workplace: WORKPLACES[idx] }
    }
    case 'KeyH':
      return { type: 'HELP' }
    case 'ArrowLeft':
      return { type: 'JOG', dir: -1 }
    case 'ArrowRight':
      return { type: 'JOG', dir: 1 }
    case 'ArrowUp':
      return { type: 'CURSOR', delta: -1 }
    case 'ArrowDown':
      return { type: 'CURSOR', delta: 1 }
    case 'PageUp':
      return { type: 'CURSOR', delta: -10 }
    case 'PageDown':
      return { type: 'CURSOR', delta: 10 }
    case 'Minus':
    case 'NumpadSubtract':
      return { type: 'OVERRIDE', kind: 'feed', dir: -1 }
    case 'Equal':
    case 'NumpadAdd':
      return { type: 'OVERRIDE', kind: 'feed', dir: 1 }
    case 'BracketLeft':
      return { type: 'OVERRIDE', kind: 'spindle', dir: -1 }
    case 'BracketRight':
      return { type: 'OVERRIDE', kind: 'spindle', dir: 1 }
    case 'Digit9':
      return { type: 'OVERRIDE', kind: 'rapid', dir: -1 }
    case 'Digit0':
      return { type: 'OVERRIDE', kind: 'rapid', dir: 1 }
    default:
      return null
  }
}

export function useCncMachine() {
  const [state, dispatch] = useReducer(reducer, initialState)
  const [clock, setClock] = useState<string | null>(null)
  const [focused, setFocused] = useState(true)
  const softkeys = getSoftkeys(state)
  const cycleView = () => dispatch({ type: 'PATH_VIEW_CYCLE' })

  useEffect(() => {
    const id = window.setInterval(() => dispatch({ type: 'TICK', dt: TICK_MS }), TICK_MS)
    const clockId = window.setInterval(() => setClock(new Date().toLocaleTimeString('ru-RU', { hour12: false })), 1000)
    return () => {
      window.clearInterval(id)
      window.clearInterval(clockId)
    }
  }, [])

  const onKeyDown = useEffectEvent((e: KeyboardEvent) => {
    if (e.ctrlKey || e.metaKey || e.altKey) return
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

  const theme = state.params[THEME_PARAM]
  useEffect(() => {
    const root = document.documentElement
    root.classList.toggle('dark', theme === 1 || theme === 3 || theme === 4)
    root.classList.toggle('theme-graphite', theme === 4)
    root.classList.toggle('theme-mint', theme === 5)
    root.classList.toggle('theme-minimal', theme === 0 || theme === 1)
    root.classList.toggle('theme-minimal-dark', theme === 1)
  }, [theme])

  return { state, dispatch, softkeys, clock, focused, pathView: state.test.view, cycleView }
}
