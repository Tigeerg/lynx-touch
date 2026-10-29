'use client'

// The probe wizard, public surface: the wizard itself (probe-wizard/probe-wizard) with the step
// stepper (stepper), the cycle picker and the tool badge (cycle-list), the machine readout and the row
// primitives (readout) and the step card with its bodies (step-card) in their own modules (CLAUDE.md,
// section 2). The directive is kept here as it was in the single file: the entry point of the wizard
// stays a client boundary.
export { ProbeWizard } from './probe-wizard/probe-wizard'
