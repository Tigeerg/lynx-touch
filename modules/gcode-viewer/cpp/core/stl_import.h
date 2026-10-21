// core/stl_import.h - STL import (binary and ASCII) into a triangle soup.
//
// The vertex order is part of the contract: the soup is checked against the
// oracle (stl.soup) byte for byte, so vertices go in triples in file order,
// with no welding.
#pragma once

#include <string>
#include <vector>

#include "core/path_prep.h"     // gv::Aabb - the bounds type, as in _verts_bounds

namespace gv {

// Triangle soup from STL: 9 float64 coordinates per triangle (a, b, c).
// An empty vector means the file was not read or holds no triangles.
// The path is UTF-8: the repository directory contains Cyrillic.
std::vector<double> loadStlVertices(const std::string& utf8Path);

// Bounds of the soup (mirrors _verts_bounds). Empty soup -> zero bounds.
Aabb vertsBounds(const std::vector<double>& verts);

} // namespace gv
