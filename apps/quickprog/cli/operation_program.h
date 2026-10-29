// cli/operation_program.h - the document built from `--op ...` words (7.2).
//
// The console turns parsed words into the document the core works on: the stock
// frame, the operation header and the geometry of the chosen kind. Nothing here
// decides about a toolpath: the fields are copied from the options, the table
// feeds fill the zeros, and the planner does the rest. The stock of an `--op`
// document grows around its own centre so the frame stays symmetric, and the
// top of the stock is the given Z.
#pragma once

#include "core/circle_pocket_options.h"
#include "core/drill_options.h"
#include "core/face_options.h"
#include "core/geom.h"
#include "core/ops.h"
#include "core/pocket_options.h"

namespace qpcli {

// The document of `--op face`: one facing pass over the stock top.
qp::Program faceProgram(const qp::FaceArgs& args);

// The document of `--op pocket`. The pocket corner is taken as typed, in
// machine coordinates.
qp::Program pocketProgram(const qp::PocketArgs& args);

// The document of `--op circle-pocket`: a spiral from the centre of a round
// pocket, the centre taken as typed.
qp::Program circlePocketProgram(const qp::CirclePocketArgs& args);

// The document of `--op drill`: one shape of the three behind the pattern word.
qp::Program drillProgram(const qp::DrillArgs& args);

} // namespace qpcli
