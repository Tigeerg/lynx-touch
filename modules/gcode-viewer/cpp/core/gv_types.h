// core/gv_types.h - boundary types of the viewer core: path point, stage, tool.
//
// Structures needed by the pure core (parser and path preparation). The field
// names repeat the former contract: the core is checked against the reference
// digests byte for byte, and a name mismatch would break the check.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace gv {

// Tool from the UI_TOOL tag: the fields are those ui_tags.parse_ui_tool
// returns. `present` - the tag was found and parsed: in the reference an empty
// dict is told from a non-empty one by truthiness, and the same decides the
// stage label.
struct ToolInfo {
    bool present = false;
    int number = 0;
    std::string type = "FLAT";
    double diameter = 0.0;
    double corner_radius = 0.0;
    double taper_deg = 0.0;
    double tip_angle_deg = 0.0;
    double tip_diameter = 0.0;
    double flute_length = 0.0;
    double shoulder_length = 0.0;
    double body_length = 0.0;
    double shank_diameter = 0.0;
    double overall_length = 0.0;
    int flutes = 0;
};

// Vertex of a 2D tool/holder profile in the (radius, height) plane, mm. The
// profile is revolved around the Y axis; the height is measured from the tool
// tip or from the holder base.
struct ProfilePoint {
    double radius_mm = 0.0;
    double height_mm = 0.0;
};

// Cylindrical holder section: radius and height limits from the tip, mm. The
// collision scan reads the sections: a circle of radius `radius_mm` between
// `z0_mm` and `z1_mm` above the tool tip.
struct HolderSection {
    double radius_mm = 0.0;
    double z0_mm = 0.0;
    double z1_mm = 0.0;
};

// Path point in machine coordinates, mm. `speed_mm_s` is the speed of the
// OUTGOING segment: the programmed feed F (mm/min -> mm/s) for G1/G2/G3, or the
// fixed machine rapid speed for G0.
struct PathPoint {
    std::array<double, 3> point{0.0, 0.0, 0.0};
    bool is_rapid = false;
    double speed_mm_s = 0.0;
};

// NC stage from parse_gcode: index of the first point, label and UI_TOOL
// metadata.
struct Stage {
    int index = 0;
    std::string label;
    ToolInfo info;
};

// Result of parse_gcode: the path in program order, the last point, the
// segment count and the stages.
struct ParseResult {
    std::vector<PathPoint> ordered;
    std::array<double, 3> last{0.0, 0.0, 0.0};
    int seg_count = 0;
    std::vector<Stage> stages;
};

} // namespace gv
