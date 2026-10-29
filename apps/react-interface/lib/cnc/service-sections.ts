// The service sections split by what they hold: a catalogue row each (ParamSection), a list
// without catalogue rows (MonitorSection: the HAL monitor and the ladder scheme), or nothing to
// list (HelpSection). The type carries the rule, so paramAt('hal', ...) does not compile instead
// of quietly returning an empty row (CLAUDE.md, section 4). Kept out of types.ts so the state
// types stay apart from the tab data; types.ts re-exports them, the public surface is unchanged.
export type ParamSection = 'system' | 'linuxcnc' | 'motion' | 'joints' | 'pid' | 'mesa'
export type MonitorSection = 'hal' | 'ladder'
export type HelpSection = 'help'
export type ServiceSection = ParamSection | MonitorSection | HelpSection

const PARAM_SECTIONS = new Set<ServiceSection>(['system', 'linuxcnc', 'motion', 'joints', 'pid', 'mesa'])
export const isParamSection = (section: ServiceSection): section is ParamSection => PARAM_SECTIONS.has(section)

export const SERVICE_PAGES: ServiceSection[][] = [
  ['system', 'linuxcnc', 'motion', 'joints', 'pid'],
  ['mesa', 'hal', 'ladder', 'help'],
]
export const SERVICE_PAGE_LABEL = ['ЧПУ LinuxCNC', 'Оборудование']
export const SERVICE_SECTIONS: ServiceSection[] = SERVICE_PAGES.flat()
