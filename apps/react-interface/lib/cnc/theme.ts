// Theme classes shared by the first-paint boot script and the live panel effect. The order is
// the order of THEME_OPTIONS: minimal light, minimal dark, light, dark, graphite, mint.
import { THEME_OPTIONS } from './params/display'

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

// "Светлая" is the panel default; it is taken by name so reordering the option list cannot pick
// another theme on a panel that has never saved one.
export const THEME_DEFAULT = THEME_OPTIONS.indexOf('Светлая')

// A theme number saved by an earlier session, or null when nothing usable is stored.
// `Number(null)` is 0 and `Number('')` is 0, so an empty key has to be rejected before the range
// check: otherwise the panel silently opens the first option ("Минимал · светлая") instead of the
// default "Светлая".
export const savedTheme = (raw: string | null): number | null => {
  if (raw === null || raw.trim() === '') return null
  const value = Number(raw)
  return Number.isInteger(value) && value >= 0 && value < THEME_CLASSES.length ? value : null
}

// The first-paint script: the saved theme has to be on <html> before the browser paints, otherwise
// the panel shows the default palette for one frame. It lives next to the class table and reads
// the storage through the same rule as `savedTheme`, so the script and the live effect cannot
// drift apart. Inlined into the document head, hence the string and the self-invoking wrapper.
export const themeBootScript = () =>
  `(()=>{try{const raw=localStorage.getItem(${JSON.stringify(THEME_STORAGE_KEY)});` +
  `const n=raw===null||raw.trim()===''?${THEME_DEFAULT}:Number(raw);` +
  `const t=Number.isInteger(n)&&n>=0&&n<${THEME_CLASSES.length}?n:${THEME_DEFAULT};` +
  `for(const name of ${JSON.stringify(THEME_CLASSES)}[t]||[])document.documentElement.classList.add(name)}catch{}})()`
