'use client'

import { createContext, useContext } from 'react'

export type UiMode = 'panel' | 'touch'

export const UI_MODES: UiMode[] = ['panel', 'touch']

export const UI_MODE_LABEL: Record<UiMode, string> = {
  panel: 'Пульт',
  touch: 'Сенсор',
}

export const UI_MODE_HINT: Record<UiMode, string> = {
  panel: 'Альбом · физические клавиши',
  touch: 'Портрет · сенсорное управление',
}

export const UiModeContext = createContext<{ mode: UiMode; setMode: (m: UiMode) => void } | null>(null)

export function useUiMode() {
  return useContext(UiModeContext)
}
