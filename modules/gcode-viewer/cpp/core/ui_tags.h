// core/ui_tags.h - postprocessor UI_* tags: tool, holder, stock, origin,
// rotation, CAD matrix and stage labels.
//
// The parsing repeats the reference re patterns literally (including the
// behaviour when a tag is absent): the stage 2 result is checked against the
// reference byte for byte, so a "similar" parse is not good enough - it must be
// the same.
#pragma once

#include <array>
#include <optional>
#include <string>

#include "gv_types.h"

namespace gv {

// 'UI_TOOL: T=.. TYPE=.. D=.....' -> ToolInfo; present=false when the tag is
// absent.
ToolInfo parseUiTool(const std::string& text);

// Short stage label from UI_TOOL, e.g. 'T2 drill D3'.
std::string stageLabel(const ToolInfo& info);

// The text inside the first comment (...) of the line, with edge spaces trimmed
// ('' if there is no comment).
std::string commentText(const std::string& raw);

// 'UI_ROTATION: RX.. RY.. RZ..' -> (rx, ry, rz) in degrees; (0,0,0) without the
// tag.
std::array<double, 3> parseUiRotation(const std::string& text);

// 'UI_ORIGIN: X.. Y.. Z..' -> (x, y, z); (0,0,0) without the tag.
std::array<double, 3> parseUiOrigin(const std::string& text);

// 'UI_HOLDER: GAUGE=.. NAME=..' -> holder gauge length and name. An empty
// optional means the tag is absent.
struct HolderTag {
    bool has_gauge = false;
    double gauge = 0.0;
    bool has_name = false;
    std::string name;
};
std::optional<HolderTag> parseUiHolder(const std::string& text);

// 'UI_STOCK: XMIN=.. XMAX=.. YMIN=.. YMAX=.. ZMIN=.. ZMAX=..' -> bounds
// (xmin, xmax, ymin, ymax, zmin, zmax) mm; an empty optional means the tag is
// absent or holds fewer than six numbers.
std::optional<std::array<double, 6>> parseUiStock(const std::string& text);

// 'UI_CAD_MATRIX: <9 numbers>' -> columns X, Y, Z of the CAD matrix.
std::optional<std::array<double, 9>> parseUiCadMatrix(const std::string& text);

// 'UI_CAD_MATRIX_ORIGIN: X.. Y.. Z..' -> origin of the CAD model.
std::optional<std::array<double, 3>> parseUiCadMatrixOrigin(const std::string& text);

} // namespace gv
