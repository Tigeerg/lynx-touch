// The screen sets: `switch (s.screen)` of getSoftkeys. Six screens, each with its own bar, some
// with an inner mode (editor, probe wizard, ladder form/manager/draft) that replaces the whole
// set. Each set lives in its own module under ./screens, so the bar of one screen fits in a
// screen of a file (CLAUDE.md, section 2); this module keeps the entry point the bar imports.
export { alarmScreen } from './screens/alarm'
export { offsetScreen } from './screens/offset'
export { posScreen } from './screens/pos'
export { progScreen } from './screens/prog'
export { serviceScreen } from './screens/service'
export { toolScreen } from './screens/tool'
