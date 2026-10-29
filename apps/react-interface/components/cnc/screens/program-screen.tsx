'use client'

// The program screen, public surface: the screen itself (program-screen/program-screen) with the block
// list and the progress strip (program-list), the DRO (dro), the search bar (search-bar) and the inline
// line editor (line-editor) in their own modules (CLAUDE.md, section 2). The directive is kept here as
// it was in the single file: the entry point of the screen stays a client boundary.
export { ProgramScreen } from './program-screen/program-screen'
export { ProgramList } from './program-screen/program-list'
export { SearchBar } from './program-screen/search-bar'
