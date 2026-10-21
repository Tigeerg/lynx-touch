// core/tangents.h - analytic contour tangents: families of cylinders and cones.
//
// The layer is pure: neither Qt nor OCCT - only arithmetic over surface
// descriptors, packed STEP edges and the skin soup. That is why it lives in
// gvcore, while the surface descriptors come from the OCCT layer gvstep
// (cpp/occt/step_surfaces.*): the split is the same as for the silhouette and
// STEP tessellation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/packing.h"     // gv::PackedEdges - packed STEP edges
#include "core/quality.h"     // gv::QualityPreset - silhouette and cone tolerances
#include "gv_types.h"         // gv::ProfilePoint - 2D profile of a surface of revolution

namespace gv {

// Analytic cylindrical STEP face (CylinderSurface in core/gv_types.h): o - a
// point on the axis, d - unit axis direction, e1/e2 - cross-section basis, arc -
// the face arc in radians (has_arc = false for a full circle), r - radius,
// t0/t1 - limits along the axis by the face vertices, mm.
struct CylinderSurface {
    double o[3] = {0.0, 0.0, 0.0};
    double d[3] = {0.0, 0.0, 1.0};
    double e1[3] = {1.0, 0.0, 0.0};
    double e2[3] = {0.0, 0.0, 1.0};
    bool has_arc = false;
    double arc0 = 0.0;
    double arc1 = 0.0;
    double r = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
};

// Analytic conical STEP face (ConeSurface in core/gv_types.h): a - apex, d - unit
// axis direction, psi - half-angle (rad), s0/s1 - limits of the slant parameter
// (length along the axis / cos psi), mm.
struct ConeSurface {
    double a[3] = {0.0, 0.0, 0.0};
    double d[3] = {0.0, 0.0, 1.0};
    double psi = 0.0;
    double s0 = 0.0;
    double s1 = 0.0;
};

// One element of the surface list. In the reference this is a list of objects of
// two types, and its order (TopExp_Explorer face traversal) shows in the family
// order, so the list stores the surface kind rather than two separate arrays.
struct StepSurface {
    bool is_cylinder = true;
    CylinderSurface cyl;
    ConeSurface cone;
};

using StepSurfaceList = std::vector<StepSurface>;

// The cone slant length threshold, the number of concavity probe azimuths, the
// cylinder silhouette arc threshold, the minimum slant span and the axial
// overlap of the DROP policy now come from the quality preset (core/quality.h):
// at the med level these are the same _CONE_MIN_SLANT_MM = 2.5 mm,
// _CONE_PROBE_AZIMUTHS = 8, 85 degrees, 2.5 mm and 2 mm as in the reference.

// Skin soup for the cone policy: triangles (F,3,3), their centroids (F,3) and
// the first-corner normal of a face (F,3) - all float64, as in the reference
// after asarray. `valid` = false repeats skin_tris = None (the skin is not
// passed or is not divisible into vertex triples).
struct SkinTriangles {
    bool valid = false;
    std::size_t count = 0;
    std::vector<double> sktri;   // (F,3,3)
    std::vector<double> skcen;   // (F,3)
    std::vector<double> skirm;   // (F,3)
};

// Precomputed cylindrical families (CylinderFamilies in core/gv_types.h). Flat
// arrays: O/D/E1/E2 - (N,3), R/T0/T1/RREF/A0/A1 - (N,), FULL - (N,), DROP -
// (N,8) with NaN in unused cells, ARC - (N,64).
struct CylinderFamilies {
    std::vector<double> O;
    std::vector<double> D;
    std::vector<double> E1;
    std::vector<double> E2;
    std::vector<double> R;
    std::vector<double> T0;
    std::vector<double> T1;
    std::vector<double> RREF;
    std::vector<double> A0;
    std::vector<double> A1;
    std::vector<std::uint8_t> FULL;
    std::vector<double> DROP;
    std::vector<std::uint8_t> ARC;
    std::size_t count = 0;
};

// STEP tangent families (TangentFamilies in core/gv_types.h).
struct TangentFamilies {
    CylinderFamilies cylinders;
    std::vector<ConeSurface> cones;
};

// Skin soup -> SkinTriangles: mirrors the part of _tangent_families that
// prepares sktri/skcen/skirm from skin=(verts, normals). The centroid is the
// mean of three vertices, the normal is the first face corner; the summation
// order is numpy's.
SkinTriangles buildSkinTriangles(const std::vector<double>& soup,
                                 const std::vector<float>& normals);

// Precompute the families: mirrors _tangent_families. surfs - in STEP face
// order; packedEdges (STEP outline, if known) marks generator azimuths that
// coincide with B-rep seam edges; skin - the cone policy. nullptr means the data
// is absent (the reference's None). Tolerances come from the preset
// (core/quality.h): at the med level they are the same as the former hard-coded
// numbers.
TangentFamilies buildTangentFamilies(const StepSurfaceList& surfs,
                                     const PackedEdges* packedEdges = nullptr,
                                     const SkinTriangles* skin = nullptr,
                                     const QualityPreset& quality = QualityPreset{});

// Tangent families of a surface of revolution from a 2D profile (r, y), mm: a
// constant radius -> a full cylinder, a slanted stretch -> a cone. The STEP
// silhouette thresholds (85 deg arc, 2.5 mm minimum span, seam-edge DROP) do not
// apply here: the tool profile has short sections that are shape, not model
// noise (a drill tip of 1.75 mm, hemisphere segments of ~0.2 mm), and the
// threshold would throw them out. The generator azimuth is computed by
// tangentFromFamilies, so the silhouette math is the same for the part and for a
// surface of revolution.
TangentFamilies profileTangentFamilies(const std::vector<ProfilePoint>& profile);

// Per-frame pass: both tangent sides of each logical cylinder and the cone
// generators at eye eyeMm (raw model frame). The result is a flat row (M,2,3)
// float64: two points per segment, the same order as the reference.
std::vector<double> tangentFromFamilies(const TangentFamilies& fam, const double eyeMm[3]);

// Per-frame tangent buffers that survive the frame: Test_model used to allocate
// 30 buffers and 35 KB of intermediate vectors and finished segments per frame.
// The capacities live here, so a frame only fills them instead of creating
// buffers.
struct TangentScratch {
    std::vector<double> along;      // (N,) eye projection onto the axis
    std::vector<double> perp;       // (N,3) eye offset from the axis
    std::vector<double> rp;         // (N,) eye distance to the axis
    std::vector<double> mh;         // (N,3) unit offset
    std::vector<double> rot;        // (N,3) d x mh
    std::vector<double> nrm;        // (N,3) tangent normal of the side
    std::vector<double> cg;         // (N,) cos of the tangency angle
    std::vector<double> sg;         // (N,) sin of the tangency angle
    std::vector<double> phi;        // (N,) tangency azimuth
    std::vector<double> baseA;      // (N,3) generator start (t0)
    std::vector<double> baseB;      // (N,3) generator end (t1)
    std::vector<std::uint8_t> ok;   // (N,) does the side pass the arc trim and DROP
    std::vector<double> segments;   // (M,2,3) float64 flat row
};

// Tangents into the given buffer: scratch.segments is cleared and filled with
// the same order of operations as the version above (the same numbers bit for
// bit); the return is a reference to it.
const std::vector<double>& tangentFromFamilies(const TangentFamilies& fam,
                                               const double eyeMm[3],
                                               TangentScratch& scratch);

} // namespace gv
