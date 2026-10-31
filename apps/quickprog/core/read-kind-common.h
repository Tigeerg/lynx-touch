// core/read-kind-common.h - the small value readers of the kinds the demo does
// not plan (PLAN-QUICKPROG, 7.6).
//
// One spelling of a wrong value: the key and what it expects, with the line
// number the caller prefixes. Shared by `read-kind-keys.cpp` and
// `read-kind-layout.cpp`, so the two readers of the panel kinds phrase a refused
// line the same way.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"

namespace qp {

// One number after the key.
bool number(const std::vector<std::string>& words, const std::string& key, double& out,
            std::string& error, const std::string& where);

// `on` or `off` after the key.
bool onOff(const std::vector<std::string>& words, const std::string& key, bool& out,
           std::string& error, const std::string& where);

// A point `x y z`: the ends of a slot and a chamfer.
bool walkPoint(const std::string& key, const std::vector<std::string>& words, Point3& out,
               std::string& error, const std::string& where);

// The list of a contour: pairs `x y`, at least one pair and an even count.
bool pairs(const std::vector<std::string>& words, std::vector<Point3>& out, std::string& error,
           const std::string& where);

// The list of a probe: triples `x y z`, at least one triple and a multiple of
// three.
bool triples(const std::vector<std::string>& words, std::vector<Point3>& out, std::string& error,
             const std::string& where);

// The `x y w h` of a rectangle: the island, the pocket of an island, a rest.
bool rectOf(const std::vector<std::string>& words, Rect& out, std::string& error,
            const std::string& key, const std::string& where);

} // namespace qp
