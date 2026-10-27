// Theme classes shared by the first-paint boot script and the live panel effect. The order is
// the order of THEME_OPTIONS: minimal light, minimal dark, light, dark, graphite, mint.
export const THEME_STORAGE_KEY = 'cnc-theme'

export const THEME_CLASSES: readonly (readonly string[])[] = [
  ['theme-minimal'],
  ['dark', 'theme-minimal', 'theme-minimal-dark'],
  [],
  ['dark'],
  ['dark', 'theme-graphite'],
  ['theme-mint'],
]

export const themeClasses = (theme: number): readonly string[] => THEME_CLASSES[theme] ?? []
