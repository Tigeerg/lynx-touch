// core/quality.h - quality presets: a level (low/med/high/safe) sets the
// tolerances of model reading, silhouette, tangents and outline with one set.
//
// The med level repeats the numbers that were in effect before the presets, so
// an empty environment changes nothing. The low and high values are proposals:
// for the tolerances whose cost is visible in a bench_probe measurement the
// comment carries the measured time, for the rest the word "proposal". The safe
// level takes the coarsest of the low values.
//
// The NC parsing and collision scan numbers are read from the environment
// (CNC_DECIMATE_TOL, CNC_ARC_DECIMATE_TOL, CNC_COLLISION_STEP) and the level does
// not change them: the preset only reports them through the same read as
// core/gcode_parser and core/collision.
//
// The layer is pure, like the rest of the core: neither Qt nor OCCT.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>

#include "text_util.h"

namespace gv {

// The tolerance set of one load. Values with a dimension carry it in the name:
// Mm - millimetres, Mm2 - square millimetres, Deg - degrees.
struct QualityPreset {
    const char *level = "med";          // level name: low, med, high or safe

    // STEP: linear tessellation deflection of the skin and of the B-Rep edge
    // polylines (mm).
    double stepDeflectionMm = 0.05;
    double edgeDeflectionMm = 0.05;

    // Welding of nearly coincident B-Rep edge segments (occt/step_edges): the
    // grid of the distance to the perpendicular and the axial gap (mm).
    double coalesceDistTolMm = 0.12;
    double coalesceGapTolMm = 0.5;

    // Mesh silhouette (core/silhouette): vertex weld grid (mm), sharp-edge
    // threshold (degrees, steeper - the edge is not a silhouette edge) and the
    // degenerate-face area threshold (mm^2, below - the face normal is numerical
    // noise).
    double silhouetteWeldMm = 1e-4;
    double silhouetteMaxEdgeDeg = 20.0;
    double silhouetteMinFaceAreaMm2 = 1e-6;

    // Skin normals (core/normals): smoothing threshold of adjacent normals
    // (degrees).
    double creaseDeg = 20.0;

    // Mesh outline (core/outline): dihedral angle threshold (degrees) and the
    // Douglas-Peucker chain simplification tolerance (mm).
    double outlineFeatureEdgeDeg = 20.0;
    double outlineFeatureSimplifyMm = 0.15;

    // Tangents of surfaces of revolution (core/tangents): cone slant length
    // threshold (mm), number of concavity probe azimuths, cylinder silhouette arc
    // threshold (degrees), minimum slant span (mm) and the axial overlap at which
    // a generator azimuth leaves the DROP policy (mm).
    double coneMinSlantMm = 2.5;
    int coneProbeAzimuths = 8;
    double silhouetteArcDeg = 85.0;
    double silhouetteMinSpanMm = 2.5;
    double dropOverlapMm = 2.0;

    // NC parsing and collision scan: environment values, the level does not
    // change them.
    double decimateTolMm = 0.5;
    double arcDecimateTolMm = 0.1;
    double collisionStepMm = 1.0;

    // Material removal simulation (core/stock_sim): field grid step in mm.
    // Measured on Test_model.ngc (70 914 moves, 137.8 m, UI_STOCK bounds):
    // 0.5 mm - 60 501 nodes, reset 0.1 ms + removal 192.4 ms, field 236 KiB;
    // 0.2 mm - 376 251 nodes, 2288.6 ms, field 1.44 MiB. 1.0 mm is not measured:
    // the node is twice as coarse as med, the node count is about four times
    // smaller.
    // A repeat measurement (Release, /O2 /Ob2, machine load 46 %) gave 396-425 ms
    // and 7631-8393 ms - exactly twice as slow, and both runs agreed with each
    // other. The difference is not the build but the machine state, so the
    // presets keep the numbers of the first measurement, and both rows are
    // recorded here.
    double stockSimCellMm = 0.5;
};

// Environment variable value: an empty string or absence gives the default,
// otherwise the value parsed by gv::strtodC - the same read as envDouble in
// core/gcode_parser.cpp: the parse does not depend on the process locale.
inline double qualityEnvMm(const char *name, double fallback)
{
    const char *raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') {
        return fallback;
    }
    return strtodC(raw, nullptr);
}

// Collision scan step from the environment: the parse rule is the same as
// collisionStep (an unparsed tail counts as an error and gives the default).
inline double qualityEnvCollisionStepMm()
{
    const char *raw = std::getenv("CNC_COLLISION_STEP");
    if (raw == nullptr || *raw == '\0') {
        return 1.0;
    }
    char *end = nullptr;
    const double value = strtodC(raw, &end);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return 1.0;
    }
    return value;
}

// Removal simulation grid step from CNC_STOCK_CELL: it overrides the preset step
// without changing the level. An empty or unparsable value gives the preset step
// (0), the parse is the same as qualityEnvMm.
inline double qualityEnvStockCellMm()
{
    const double value = qualityEnvMm("CNC_STOCK_CELL", 0.0);
    return value > 0.0 ? value : 0.0;
}

// Level name case-insensitively: the neighbouring environment flags (CNC_CPP,
// CNC_SILHOUETTE) are also read case-insensitively.
inline bool sameLevelName(const char *level, const char *name)
{
    std::size_t i = 0;
    for (; level[i] != '\0' && name[i] != '\0'; ++i) {
        const int a = std::tolower(static_cast<unsigned char>(level[i]));
        const int b = std::tolower(static_cast<unsigned char>(name[i]));
        if (a != b) {
            return false;
        }
    }
    return level[i] == '\0' && name[i] == '\0';
}

// Preset for a level: low, med, high or safe. An empty or unknown name gives
// med - the level whose numbers are those before the presets. The three
// environment numbers (NC parsing and collision scan) are substituted here too.
inline QualityPreset presetFor(const char *level)
{
    QualityPreset q;
    const char *name = (level != nullptr && *level != '\0') ? level : "med";

    if (sameLevelName(name, "low")) {
        q.level = "low";
        // Tessellation deflection 0.2 mm: a proposal; measured on Test_model -
        // 7856 triangles against 45398 at med (tessellation 62.8 ms against
        // 129.4 ms).
        q.stepDeflectionMm = 0.2;
        // Edge polyline deflection 0.2 mm: a proposal; 1682 segments after
        // welding against 2426 at med.
        q.edgeDeflectionMm = 0.2;
        // Wider edge welding: a proposal.
        q.coalesceDistTolMm = 0.2;
        q.coalesceGapTolMm = 0.5;
        // Silhouette weld 0.001 mm: a proposal.
        q.silhouetteWeldMm = 1e-3;
        q.silhouetteMaxEdgeDeg = 25.0;      // proposal
        q.silhouetteMinFaceAreaMm2 = 1e-5;  // proposal
        q.creaseDeg = 25.0;                 // proposal
        q.outlineFeatureEdgeDeg = 25.0;     // proposal
        q.outlineFeatureSimplifyMm = 0.3;   // proposal
        q.coneMinSlantMm = 3.0;             // proposal
        q.coneProbeAzimuths = 4;            // proposal
        q.silhouetteArcDeg = 70.0;          // proposal
        q.silhouetteMinSpanMm = 3.5;        // proposal
        q.dropOverlapMm = 3.0;              // proposal
        // Removal: 0.5 mm - 60 501 nodes and 192.5 ms on Test_model.ngc (measured).
        q.stockSimCellMm = 0.5;
    } else if (sameLevelName(name, "high")) {
        q.level = "high";
        // Tessellation deflection 0.02 mm: a proposal; measured on Test_model -
        // 175808 triangles (tessellation 422.1 ms against 129.4 ms at med).
        q.stepDeflectionMm = 0.02;
        // Edge polyline deflection 0.02 mm: a proposal; 3330 segments after
        // welding against 2426 at med.
        q.edgeDeflectionMm = 0.02;
        q.coalesceDistTolMm = 0.08;         // proposal
        q.coalesceGapTolMm = 0.5;
        q.silhouetteWeldMm = 1e-5;          // proposal
        q.silhouetteMaxEdgeDeg = 15.0;      // proposal
        q.silhouetteMinFaceAreaMm2 = 1e-7;  // proposal
        q.creaseDeg = 15.0;                 // proposal
        q.outlineFeatureEdgeDeg = 15.0;     // proposal
        q.outlineFeatureSimplifyMm = 0.08;  // proposal
        q.coneMinSlantMm = 2.0;             // proposal
        q.coneProbeAzimuths = 12;           // proposal
        q.silhouetteArcDeg = 90.0;          // proposal
        q.silhouetteMinSpanMm = 2.0;        // proposal
        q.dropOverlapMm = 1.5;              // proposal
        // Removal: 0.2 mm - 376 251 nodes and 2288.6 ms on Test_model.ngc (measured).
        q.stockSimCellMm = 0.2;
    } else if (sameLevelName(name, "safe")) {
        q.level = "safe";
        // The coarsest values: the level for a weak machine and large models.
        q.stepDeflectionMm = 0.5;           // proposal; 4132 triangles against 45398
        q.edgeDeflectionMm = 0.5;           // proposal; 1427 segments against 2426
        q.coalesceDistTolMm = 0.3;          // proposal
        q.coalesceGapTolMm = 0.5;
        q.silhouetteWeldMm = 1e-2;          // proposal
        q.silhouetteMaxEdgeDeg = 30.0;      // proposal
        q.silhouetteMinFaceAreaMm2 = 1e-4;  // proposal
        q.creaseDeg = 30.0;                 // proposal
        q.outlineFeatureEdgeDeg = 30.0;     // proposal
        q.outlineFeatureSimplifyMm = 0.5;   // proposal
        q.coneMinSlantMm = 4.0;             // proposal
        q.coneProbeAzimuths = 4;            // proposal
        q.silhouetteArcDeg = 60.0;          // proposal
        q.silhouetteMinSpanMm = 4.0;        // proposal
        q.dropOverlapMm = 4.0;              // proposal
        // Removal: 1.0 mm - not measured; the node is twice as coarse as med.
        q.stockSimCellMm = 1.0;
    } else {
        q.level = "med";                    // empty or unknown name
    }

    q.decimateTolMm = qualityEnvMm("CNC_DECIMATE_TOL", 0.5);
    q.arcDecimateTolMm = qualityEnvMm("CNC_ARC_DECIMATE_TOL", 0.1);
    q.collisionStepMm = qualityEnvCollisionStepMm();
    // CNC_STOCK_CELL overrides the preset step: it is used to measure the
    // removal cost at another node without introducing a new level.
    const double stock_cell = qualityEnvStockCellMm();
    if (stock_cell > 0.0) {
        q.stockSimCellMm = stock_cell;
    }
    return q;
}

// Level from CNC_QUALITY (empty or unknown value - med).
inline QualityPreset presetFromEnv()
{
    return presetFor(std::getenv("CNC_QUALITY"));
}

} // namespace gv
