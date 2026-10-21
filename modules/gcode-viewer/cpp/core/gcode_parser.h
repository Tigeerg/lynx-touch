// core/gcode_parser.h - NC program parsing: moves, arcs, canned drilling cycles,
// planes, stage tags.
//
// Mirrors gcode_parser.parse_gcode (stage 2 of the C++ port). The result is
// checked against the reference byte for byte (oracle_manifest.json, path.*
// keys), so the order of operations and the line parsing are reproduced
// literally, not "by meaning".
#pragma once

#include <string>

#include "gv_types.h"

namespace gv {

// Returns the path in program order, the last machine point, the segment count
// and the stages. The environment constants are the same as the reference's:
// CNC_DECIMATE_TOL (0.5 mm), CNC_ARC_DECIMATE_TOL (0.1 mm),
// CNC_RAPID_SPEED_MMS (500 mm/s), CNC_DEFAULT_FEED_MMS (100 mm/s).
ParseResult parseGcode(const std::string& text);

} // namespace gv
