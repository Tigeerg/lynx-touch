// core/feeds.h - cutting data from the material, the diameter and the tool
// kind (PLAN-QUICKPROG, 7.2).
//
// The numbers are the ones of the React calculator, not a second opinion:
// `core/feeds.cpp` carries the Vc/fz table copied from
// `apps/react-interface/lib/cnc/calc.ts` and the same two formulas,
//   rpm  = round(1000 * Vc / (pi * D)),
//   feed = round(fz * flutes * rpm),
// so the demo and the panel print the same values. The number of flutes is a
// property of the tool kind, as in the React defaults (three for a flat end
// mill); the demo does not invent a per-diameter flute count.
#pragma once

#include <string>

#include "core/ops.h"

namespace qp {

enum class Material { Aluminium, Steel };

// Flat end mill, drill, and a "tool kind not in the table" case that gets the
// conservative values of the table plus an explicit warning source.
enum class ToolKind { Flat, Drill, Unknown };

struct Feeds {
    double rpm = 0.0;
    double feedMmMin = 0.0;
    // Z plunge feed: a quarter of the XY feed, the usual conservative rule for
    // an end mill plunging straight down. Kept here so the form and the planner
    // read the same number.
    double plungeFeedMmMin = 0.0;
    // Short Russian source line for the operator, e.g. "таблица: алюминий,
    // ф10" or "нет в таблице: алюминий, ф3".
    std::string source;
};

// Cutting data for the material, the tool diameter and the tool kind. A
// non-positive diameter gives zeros and an empty source: the caller validates
// the diameter first and must not plan a pass from a zero.
Feeds feedsFor(Material material, double diameterMm, ToolKind kind);

// Russian name of the material, as the panel writes it.
std::string materialName(Material material);

} // namespace qp
