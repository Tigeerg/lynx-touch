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
//
// The drill has no row in calc.ts, which describes a flat end mill only. It is
// a second table of its own shape (Vc and the feed per revolution `f = k * D`,
// per material and per drill body material) in `core/feeds.cpp`; it uses the
// same `round(1000 * Vc / (pi * D))` for the speed, so the panel and the drill
// stay comparable.
#pragma once

#include <string>

#include "core/ops.h"

namespace qp {

// Flat end mill, drill, and a "tool kind not in the table" case that gets the
// conservative values of the table plus an explicit warning source.
enum class ToolKind { Flat, Drill, Unknown };

// Russian name of the drill material, as the source line writes it ("HSS",
// "карбид"). The enums themselves (Material, DrillBody) live in ops.h: the
// operation carries them in the document, and this header already includes
// ops.h.
std::string bodyName(DrillBody kind);

// The console and the .qprog word of the drill material: "hss" or "carbide".
bool parseDrillKind(const std::string& text, DrillBody& out);

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
// the diameter first and must not plan a pass from a zero. A drill is always
// drilled with the HSS row here: a caller that knows the material of the drill
// body calls `feedsForDrill`.
Feeds feedsFor(Material material, double diameterMm, ToolKind kind);

// Cutting data of a drill of the given body material: the drill row of the
// table, `f = k * D` mm/rev and `n = 1000 * Vc / (pi * D)`. The same
// non-positive diameter rule as `feedsFor`.
Feeds feedsForDrill(Material material, double diameterMm, DrillBody kind);

// Russian name of the material, as the panel writes it.
std::string materialName(Material material);

// The console and the .qprog word of the material: "aluminium" or "steel".
// False for anything else; the caller names the key and the two words.
bool parseMaterial(const std::string& text, Material& out);

} // namespace qp
