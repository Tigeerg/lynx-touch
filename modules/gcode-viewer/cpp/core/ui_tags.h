// core/ui_tags.h - теги постпроцессора UI_*: инструмент, оправка, заготовка,
// начало координат, поворот, матрица CAD и подписи этапов.
//
// Зеркало ui_tags.py. Разбор повторяет питоновские re-шаблоны дословно (включая
// поведение при отсутствии тега): результат этапа 2 сверяется побайтово с Python,
// поэтому "похожий" разбор здесь не годится, нужен тот же.
#pragma once

#include <array>
#include <optional>
#include <string>

#include "gv_types.h"

namespace gv {

// 'UI_TOOL: T=.. TYPE=.. D=.. ...' -> ToolInfo; present=false, когда тега нет.
ToolInfo parseUiTool(const std::string& text);

// Короткая подпись этапа из UI_TOOL, например 'T2 сверло D3'.
std::string stageLabel(const ToolInfo& info);

// Текст внутри первого комментария (...) строки, без крайних пробелов ('' если
// комментария нет).
std::string commentText(const std::string& raw);

// 'UI_ROTATION: RX.. RY.. RZ..' -> (rx, ry, rz) в градусах; (0,0,0) без тега.
std::array<double, 3> parseUiRotation(const std::string& text);

// 'UI_ORIGIN: X.. Y.. Z..' -> (x, y, z); (0,0,0) без тега.
std::array<double, 3> parseUiOrigin(const std::string& text);

// 'UI_HOLDER: GAUGE=.. NAME=..' -> вылет и имя оправки. Пустой optional - тега нет.
struct HolderTag {
    bool has_gauge = false;
    double gauge = 0.0;
    bool has_name = false;
    std::string name;
};
std::optional<HolderTag> parseUiHolder(const std::string& text);

// 'UI_STOCK: XMIN=.. XMAX=.. YMIN=.. YMAX=.. ZMIN=.. ZMAX=..' -> габарит
// (xmin, xmax, ymin, ymax, zmin, zmax) мм; пустой optional - тега нет или чисел
// меньше шести.
std::optional<std::array<double, 6>> parseUiStock(const std::string& text);

// 'UI_CAD_MATRIX: <9 чисел>' -> столбцы X, Y, Z матрицы CAD.
std::optional<std::array<double, 9>> parseUiCadMatrix(const std::string& text);

// 'UI_CAD_MATRIX_ORIGIN: X.. Y.. Z..' -> начало координат модели CAD.
std::optional<std::array<double, 3>> parseUiCadMatrixOrigin(const std::string& text);

} // namespace gv
