// cli/demo_program.h - the document behind `--demo` (7.2).
//
// The demo is a fixed document, not a defaults run: the numbers are typed into
// the example so the console prints the same program on every build and the
// check can compare the bytes. Two operations, a tool change between them.
#pragma once

#include "core/ops.h"

namespace qpcli {

// Two operations, the facing of a 100x80 stock top and a 60x40 pocket with an
// 8 mm end mill. The tool changes between them, so the example shows the
// tool-change rule of the emitter. The pocket carries a corner radius, which
// the demo uses to show the rounded-corner warning, not a rounded corner.
qp::Program demoProgram();

} // namespace qpcli
