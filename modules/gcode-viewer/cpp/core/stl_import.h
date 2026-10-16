// core/stl_import.h - импорт STL (бинарный и ASCII) в суп треугольников
// (зеркало stl_import.py).
//
// Порядок вершин - часть контракта: C++ сверяется с питоновским
// load_stl_vertices побайтово (модули gcode-viewer/test_stl_core.py и оракул
// stl.soup), поэтому вершины идут тройками подряд в порядке файла, без сварки.
#pragma once

#include <string>
#include <vector>

#include "core/path_prep.h"     // gv::Aabb - тип габарита, как у _verts_bounds

namespace gv {

// Суп треугольников из STL: 9 координат float64 на треугольник (a, b, c).
// Пустой вектор - файл не прочитан или треугольников в нём нет.
// Путь - UTF-8: каталог репозитория содержит кириллицу.
std::vector<double> loadStlVertices(const std::string& utf8Path);

// Габарит супа (зеркало _verts_bounds). Пустой суп -> нулевой габарит.
Aabb vertsBounds(const std::vector<double>& verts);

} // namespace gv
