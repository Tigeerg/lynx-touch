'use client'

// The service screen, public surface: the screen itself (service-screen/service-screen) with the
// parameter table, the HAL monitor, the Mesa I/O, the system info and the password dialog in their
// own modules (CLAUDE.md, section 2). The directive is kept here as it was in the single file: the
// entry point of the screen stays a client boundary.
export { ServiceScreen } from './service-screen/service-screen'
