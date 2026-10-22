// The DISPLAY group of the machine INI: the parameter keys the panel reads directly and the
// theme option list. Kept apart from the table (./system) so both the table and service-data.ts
// can take them from one place without a cycle.

export const THEME_PARAM = 'DISPLAY.THEME'
export const UI_MODE_PARAM = 'DISPLAY.UI_MODE'
export const TOOLPATH_PARAM = 'DISPLAY.TOOLPATH_PREVIEW'
export const SCREEN_SIZE_PARAM = 'DISPLAY.SCREEN_SIZE'
export const MAX_FEED_PARAM = 'DISPLAY.MAX_FEED_OVERRIDE'
export const MAX_SPINDLE_PARAM = 'DISPLAY.MAX_SPINDLE_OVERRIDE'

export const THEME_OPTIONS = ['Минимал · светлая', 'Минимал · тёмная', 'Светлая', 'Тёмная', 'Графит', 'Мята']
