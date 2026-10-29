// core/feeds.cpp - the cutting-data table and the calculator formulas.
//
// Source of truth: apps/react-interface/lib/cnc/calc.ts,
//   CALC_DEFAULTS = { vc: 150, d: 10, fz: 0.06, flutes: 3 }   (line 8),
//   cuttingRpm(vc, d)  = round(1000 * vc / (pi * d))           (line 13),
//   cuttingFeed(fz, n, rpm) = round(fz * n * rpm)              (line 18).
// The RPM is the 1000 * Vc / (pi * D) formula; the feed is fz * flutes * rpm.
// The number of flutes in calc.ts is a field of the form (default three), not
// derived from the diameter; the table below fixes it per tool kind instead of
// inventing a diameter dependence.
//
// calc.ts itself has no material table: it is a calculator with one default
// set. The material table below therefore repeats the calc.ts defaults for the
// row calc.ts describes (a flat end mill in aluminium) and adds the further
// rows with the usual handbook Vc/fz for a coated carbide tool; every row uses
// the same two formulas, so the numbers stay comparable with the panel. The
// row of a given material never depends on the diameter: Vc and fz are per
// material and tool, and the diameter enters the formula, which is exactly how
// calc.ts is built.
#include "core/feeds.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace qp {
namespace {

constexpr double kMmPerM = 1000.0;

// One row of the table: Vc in m/min and fz in mm/tooth. `documented` marks the
// row that repeats calc.ts; the other rows use the same formulas with the
// handbook values of a coated carbide tool and are named as such in the report.
struct FeedRow {
    Material material;
    ToolKind kind;
    double vc;
    double fz;
};

// The calc.ts row (aluminium flat, 150 m/min, 0.06 mm/tooth) is the first one;
// the tool kind of calc.ts is a flat end mill with three flutes, as in
// CALC_DEFAULTS.
const std::vector<FeedRow>& feedTable() {
    static const std::vector<FeedRow> rows = {
        {Material::Aluminium, ToolKind::Flat, 150.0, 0.06},
        {Material::Aluminium, ToolKind::Drill, 150.0, 0.03},
        {Material::Steel, ToolKind::Flat, 90.0, 0.03},
        {Material::Steel, ToolKind::Drill, 90.0, 0.015},
    };
    return rows;
}

// Flutes per tool kind: a flat end mill takes three, as in CALC_DEFAULTS; a
// drill is a two-flute tool, so the same feed-per-tooth formula gives half the
// feed of a three-flute mill at the same fz.
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

} // namespace

std::string materialName(Material material) {
    switch (material) {
    case Material::Aluminium: return "алюминий";
    case Material::Steel: return "сталь";
    }
    return "материал";
}

Feeds feedsFor(Material material, double diameterMm, ToolKind kind) {
    Feeds out;
    if (diameterMm <= 0.0) { return out; }

    const std::vector<FeedRow>& rows = feedTable();
    const FeedRow* row = nullptr;
    for (const FeedRow& r : rows) {
        if (r.material == material && r.kind == kind) {
            row = &r;
            break;
        }
    }
    // A kind without its own row (an unknown kind) falls back to the flat end
    // mill of the same material and says so in the source line.
    bool fallback = false;
    if (row == nullptr) {
        for (const FeedRow& r : rows) {
            if (r.material == material && r.kind == ToolKind::Flat) {
                row = &r;
                fallback = true;
                break;
            }
        }
    }
    if (row == nullptr) { return out; }

    out.rpm = roundLikeCalc((kMmPerM * row->vc) / (M_PI * diameterMm));
    out.feedMmMin = roundLikeCalc(row->fz * flutesFor(kind) * out.rpm);
    out.plungeFeedMmMin = roundLikeCalc(out.feedMmMin / 4.0);
    out.source = (fallback ? "фреза по умолчанию: " : "таблица: ") + materialName(material) + ", ф" +
                 fmtD(diameterMm);
    return out;
}

} // namespace qp
