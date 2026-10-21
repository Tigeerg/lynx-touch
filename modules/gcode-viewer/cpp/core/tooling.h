// core/tooling.h - tool and holder: profiles, rings, gauge length.
//
// Stage 5.4c of the port: the same numbers and the same order of operations as
// the reference - the tool profile is built from UI_TOOL metadata, the holder
// (ER20 nut and spindle body) is constant and uses the same constants as the
// collision scan.
//
// Precision: besides the summation order, which function computes the hypotenuse
// matters here. The reference `math.hypot` uses its own algorithm (CPython,
// `vector_norm`) and differs from the CRT by 1 ulp: on Test_model profiles that
// is 8 pairs out of 51, so the holder outline would diverge byte for byte. That
// is why `pyHypot` repeats CPython rather than `std::hypot`.
#pragma once

#include <vector>

#include "gv_types.h"

namespace gv {

// Holder constants: ER20 nut and spindle body above the tool gauge length, mm.
// The values are read from the environment on first use (in the reference - at
// module import), so a run sees the same value in all calls.
struct HolderConstants {
    double nut_d = 33.0;        // ER20 nut OD
    double nut_h = 22.0;        // ER20 nut length
    double body_d = 46.0;       // holder body OD
    double body_h = 50.0;       // holder body length
};

HolderConstants holderConstants();

// Hypotenuse by the CPython algorithm (math.hypot, vector_norm): scaling by a
// power of two, an exact square via fma and compensated summation.
double pyHypot(double x, double y);

// Rounding to two decimals like the reference round(x, 2): correct rounding of
// the binary value, half to even. std::round(x * 100) / 100 does not do: on
// 12.345 and 2.675 it gives a different sign, and those numbers reach the check
// report.
double pyRound2(double x);

// Tool profile from UI_TOOL metadata: the tip at y = 0, axis +Y. The shapes the
// panel draws are supported: flat and corner-radius end mills, ball, chamfer and
// cone, drill and tap. An empty vector means no diameter (the reference's None).
std::vector<ProfilePoint> toolProfile(const ToolInfo& info);

// Profile outline: dilation outward along the profile's own normals by `pad`. A
// radial offset would leave the tip and the face on the axis, and the outline
// would merge with the body by z-fighting ("many converging lines" at the drill
// tip).
std::vector<ProfilePoint> toolOutlineProfile(const std::vector<ProfilePoint>& profile, double pad);

// Visible length of the cutting part, mm: the top of the profile (body length
// limited by OL).
double toolExposedLength(const ToolInfo& info);

// Holder as cylinder sections relative to the tool tip (machine Z up): the ER20
// nut, then the body above it. The holder bottom is at the tool gauge length.
std::vector<HolderSection> holderProfile(double tool_len);

// Holder profile (r, y) for display: y = 0 at the base (the holder bottom sits
// at the tool gauge length), positive upward.
std::vector<ProfilePoint> holderSolidProfile();

// Outer holder profile for the inverted-hull outline: the flat faces are removed
// so the enlarged copy does not show them as dark caps.
std::vector<ProfilePoint> holderOutlineProfile();

// A segment in profile coordinates after revolution around the Y axis.
struct RingSegment {
    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double z1 = 0.0;
};

// Rings (circles) at every profile vertex with r > 0.
std::vector<RingSegment> ringsSegments(const std::vector<ProfilePoint>& profile, int segments = 48);

// Profile vertices where the direction changes by more than min_deg, plus the
// ends: the horizontal outline rings. Smooth stretches (the ball nose) get no
// rings, otherwise the frame turns into a cage.
//
// The 12 degree threshold sits between two measured discretisation steps and the
// flattest real chamfer of the profile. Steps: the ball nose arc (toolProfile, 12
// segments per 90 degrees) - 7.5, the bottom corner round (8 segments per 90) -
// 11.25. Chamfer: the nut land -> the top chamfer in holderSolidProfile - 14.0
// (the step (16.5, 16) -> (15.5, 20): the angle between (0, 13) and (-1, 4)). At
// the former 20 degrees, 14.0 was below the threshold and the ring on that
// chamfer was not drawn - on a frame it read as a missing horizontal line. The
// threshold must be above the arc step and not above the chamfer: 12 keeps both
// arcs without rings and shows the chamfer; this is checked by the holder_rings
// case of the stock_sim_probe.
std::vector<ProfilePoint> profileRingPoints(const std::vector<ProfilePoint>& profile,
                                            double min_deg = 12.0);

} // namespace gv
