// core/gcode_parser.h - разбор управляющей программы: перемещения, дуги,
// кадровые циклы сверления, плоскости, теги этапов.
//
// Зеркало gcode_parser.parse_gcode (этап 2 плана переноса на C++). Результат
// сверяется с питоновским побайтово (oracle_manifest.json, ключи path.*), поэтому
// порядок операций и разбор строк повторены дословно, а не "по смыслу".
#pragma once

#include <string>

#include "gv_types.h"

namespace gv {

// Возвращает путь в порядке программы, последнюю машинную точку, число сегментов
// и этапы. Константы окружения читаются те же, что у Python: CNC_DECIMATE_TOL
// (0.5 мм), CNC_ARC_DECIMATE_TOL (0.1 мм), CNC_RAPID_SPEED_MMS (500 мм/с),
// CNC_DEFAULT_FEED_MMS (100 мм/с).
ParseResult parseGcode(const std::string& text);

} // namespace gv
