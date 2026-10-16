// core/gv_types.h - типы границ ядра вьювера: точка пути, этап, инструмент.
//
// Зеркало gv_types.py для тех структур, что нужны чистому ядру (парсер и
// подготовка траектории). Имена полей совпадают с питоновскими: ядро сверяется с
// Python побайтово, и расхождение в именах мешало бы сверке.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace gv {

// Инструмент из тега UI_TOOL: поля те же, что отдаёт ui_tags.parse_ui_tool.
// `present` - тег найден и разобран: в Python пустой dict от непустого отличает
// именно истинность, и по ней же выбирается подпись этапа.
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

// Вершина 2D-профиля инструмента/оправки в плоскости (радиус, высота), мм.
// Профиль вращается вокруг оси Y, высота отсчитывается от острия инструмента
// либо от основания оправки.
struct ProfilePoint {
    double radius_mm = 0.0;
    double height_mm = 0.0;
};

// Цилиндрическая секция оправки: радиус и пределы по высоте от острия, мм.
// Секции читает скан коллизий: круг радиуса `radius_mm` между `z0_mm` и
// `z1_mm` над острием инструмента.
struct HolderSection {
    double radius_mm = 0.0;
    double z0_mm = 0.0;
    double z1_mm = 0.0;
};

// Точка траектории в машинных координатах, мм. `speed_mm_s` - скорость
// ИСХОДЯЩЕГО сегмента: запрограммированная подача F (мм/мин -> мм/с) у G1/G2/G3
// либо фиксированная машинная скорость быстрого хода у G0.
struct PathPoint {
    std::array<double, 3> point{0.0, 0.0, 0.0};
    bool is_rapid = false;
    double speed_mm_s = 0.0;
};

// Этап УП из parse_gcode: индекс первой точки, подпись и метаданные UI_TOOL.
struct Stage {
    int index = 0;
    std::string label;
    ToolInfo info;
};

// Результат parse_gcode: путь в порядке программы, последняя точка, число
// сегментов и этапы.
struct ParseResult {
    std::vector<PathPoint> ordered;
    std::array<double, 3> last{0.0, 0.0, 0.0};
    int seg_count = 0;
    std::vector<Stage> stages;
};

} // namespace gv
