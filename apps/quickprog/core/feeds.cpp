// core/feeds.cpp - the cutting-data table and the calculator formulas.
//
// Source of truth: apps/react-interface/lib/cnc/calc.ts,
//   CALC_DEFAULTS = { vc: 150, d: 10, fz: 0.06, flutes: 3 }   (line 8),
//   cuttingRpm(vc, d)  = round(1000 * vc / (pi * d))           (line 13),
//   cuttingFeed(fz, n, rpm) = round(fz * n * rpm)              (line 18).
// The mill rows use exactly those two formulas; the drill rows use the first
// one for the speed and `F = f * n` with `f = k * D` for the feed, the same
// round() and the same per-material, per-tool shape of the row.
//
// calc.ts itself has no material table: it is a calculator with one default
// set. The table below repeats the calc.ts defaults for the row calc.ts
// describes (a flat end mill in aluminium) and adds the further rows with the
// usual handbook Vc/fz; every row uses the same formula, so the numbers stay
// comparable with the panel. The row of a given material never depends on the
// diameter: Vc and the rate are per material and tool, and the diameter enters
// the formula, which is exactly how calc.ts is built.
//
// The drill half of the table is a separate table with its own shape (Vc, the
// feed per revolution as a fraction of the diameter, the material of the drill
// body) instead of `flutes = 1` in the mill row: a drill is not "a mill with
// one tooth". Its feed per revolution is split between two cutting edges, so
// the per-tooth field of the mill would name a value the operator never uses;
// and the drill needs a second axis of the table (HSS / carbide) the mill rows
// do not have. The two tables share the material and the diameter and both go
// through the calc.ts speed formula, so the numbers stay one story.
#include "core/feeds.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace qp {
namespace {

constexpr double kMmPerM = 1000.0;

// One row of the mill table: Vc in m/min and the feed per tooth in mm/tooth.
// The flutes are a property of the tool kind, not of the row.
struct FeedRow {
    Material material;
    ToolKind kind;
    double vc;
    double fz;
};

// One row of the drill table: Vc in m/min and the feed per revolution as a
// fraction of the diameter, `f = k * D` mm/rev. The fraction is the handbook
// way of a drill row: the feed per turn grows with the drill, and Vc does not.
struct DrillRow {
    Material material;
    DrillBody kind;
    double vc;
    double k;
};

// The calc.ts row (aluminium flat, 150 m/min, 0.06 mm/tooth) is the first one;
// the tool kind of calc.ts is a flat end mill with three flutes, as in
// CALC_DEFAULTS.
const std::vector<FeedRow>& feedTable() {
    static const std::vector<FeedRow> rows = {
        {Material::Aluminium, ToolKind::Flat, 150.0, 0.06},
        {Material::Steel, ToolKind::Flat, 90.0, 0.03},
    };
    return rows;
}

// The drill rows, an orientation of a handbook table, not a copy of calc.ts:
// calc.ts has no drill. Vc of an HSS twist drill is about 25-40 m/min in
// aluminium and 15-25 m/min in steel, of a carbide drill 100-150 and 60-80;
// the feed per revolution of a small drill is 0.015-0.03 * D. The rows below
// take the middle of those bands, so the numbers survive a correction and stay
// on the safe side of a small stand. The technologist corrects the row here,
// the single place of the numbers.
const std::vector<DrillRow>& drillTable() {
    static const std::vector<DrillRow> rows = {
        // HSS: 25-40 m/min in aluminium, 0.015-0.03 * D.
        {Material::Aluminium, DrillBody::Hss, 40.0, 0.02},
        // Carbide: 100-150 m/min in aluminium, a faster feed than HSS.
        {Material::Aluminium, DrillBody::Carbide, 120.0, 0.03},
        // HSS: 15-25 m/min in steel, a smaller feed than in aluminium.
        {Material::Steel, DrillBody::Hss, 20.0, 0.015},
        // Carbide: 60-80 m/min in steel.
        {Material::Steel, DrillBody::Carbide, 70.0, 0.02},
    };
    return rows;
}

// Flutes per mill kind: a flat end mill takes three, as in CALC_DEFAULTS. A
// drill is not asked here: its row is rated per revolution.
int flutesFor(ToolKind kind) {
    switch (kind) {
    case ToolKind::Flat: return 3;
    case ToolKind::Drill: return 2;
    case ToolKind::Unknown: return 3;
    }
    return 3;
}

// The calculator rounding of calc.ts: Math.round in JS rounds .5 up, not to
// even, so std::round (away from zero) matches it for positive values.
double roundLikeCalc(double value) {
    return std::round(value);
}

std::string fmtD(double d) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", d);
    return std::string(buf);
}

const FeedRow* findMillRow(Material material, ToolKind kind) {
    for (const FeedRow& row : feedTable()) {
        if (row.material == material && row.kind == kind) { return &row; }
    }
    return nullptr;
}

} // namespace

std::string materialName(Material material) {
    switch (material) {
    case Material::Aluminium: return "алюминий";
    case Material::Steel: return "сталь";
    }
    return "материал";
}

std::string bodyName(DrillBody kind) {
    switch (kind) {
    case DrillBody::Hss: return "HSS";
    case DrillBody::Carbide: return "карбид";
    }
    return "HSS";
}

bool parseMaterial(const std::string& text, Material& out) {
    if (text == "aluminium") {
        out = Material::Aluminium;
    } else if (text == "steel") {
        out = Material::Steel;
    } else {
        return false;
    }
    return true;
}

bool parseDrillKind(const std::string& text, DrillBody& out) {
    if (text == "hss") {
        out = DrillBody::Hss;
    } else if (text == "carbide") {
        out = DrillBody::Carbide;
    } else {
        return false;
    }
    return true;
}

Feeds feedsForDrill(Material material, double diameterMm, DrillBody kind) {
    Feeds out;
    if (diameterMm <= 0.0) { return out; }
    const DrillRow* row = nullptr;
    for (const DrillRow& probe : drillTable()) {
        if (probe.material == material && probe.kind == kind) {
            row = &probe;
            break;
        }
    }
    if (row == nullptr) { return out; }
    out.rpm = roundLikeCalc((kMmPerM * row->vc) / (M_PI * diameterMm));
    // A drill is rated per revolution: f = k * D mm/rev, F = f * n.
    out.feedMmMin = roundLikeCalc(row->k * diameterMm * out.rpm);
    out.plungeFeedMmMin = roundLikeCalc(out.feedMmMin / 4.0);
    out.source = "таблица: сверло " + bodyName(kind) + ", " + materialName(material) + ", ф" +
                 fmtD(diameterMm);
    return out;
}

Feeds feedsFor(Material material, double diameterMm, ToolKind kind) {
    if (kind == ToolKind::Drill) { return feedsForDrill(material, diameterMm, DrillBody::Hss); }
    Feeds out;
    if (diameterMm <= 0.0) { return out; }

    const FeedRow* row = findMillRow(material, kind);
    // A kind without its own row (an unknown kind) falls back to the flat end
    // mill of the same material and says so in the source line.
    bool fallback = false;
    if (row == nullptr) {
        row = findMillRow(material, ToolKind::Flat);
        fallback = row != nullptr;
    }
    if (row == nullptr) { return out; }

    out.rpm = roundLikeCalc((kMmPerM * row->vc) / (M_PI * diameterMm));
    out.feedMmMin = roundLikeCalc(row->fz * flutesFor(kind) * out.rpm);
    out.plungeFeedMmMin = roundLikeCalc(out.feedMmMin / 4.0);
    const std::string prefix = fallback ? "фреза по умолчанию: " : "таблица: ";
    out.source = prefix + materialName(material) + ", ф" + fmtD(diameterMm);
    return out;
}

} // namespace qp
