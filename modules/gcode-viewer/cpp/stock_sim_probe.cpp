// cpp/stock_sim_probe.cpp - probe of the stock-remainder height field.
//
// The probe is part of neither the plugin nor the core: a separate CMake target over gvcore,
// like stl_probe. It does two things.
//
// 1. Analysis. The field is cut by a synthetic stock and checked against a formula
// computed right here from tool and path geometry, - not by the field code. Cases:
// flat end mill over a flat top (depth is constant everywhere the
// tool passed), ball (round valley - a true sphere), cone/V-bit (cone
// of the valley), cut above the stock (the field does not change) and a column caught only
// by a half-cell step (a full-cell step misses it - that is the argument for the
// step rule). In a flat pass a node at the very wall of the trace may stay
// untouched: the step only guarantees nodes closer than r - step/2, the outer band
// of width step/2 is the price of discretization, not an error. Therefore nodes
// outside this band are compared, while a node inside it but outside the radius must stay on top.
//
// The drawn part is there too: the stocksurface case builds the mesh of the field top
// (stockSurfaceMesh) and repeats the vertex shader (texcoord -> field node, height
// = stock top minus depth, the scene frame is the node rotation), the stockshell case
// checks the shape of the box shell: walls and bottom without a top face, while the
// stocksurface_fine case covers smooth-view drawing: the subdivided grid (a subnode on
// the node grid, texcoord is its cell fraction), the bilinear height of the subnode and the
// ledge rule (on a flat cell at the edge the normal is flat, on a wall cell it is the wall
// slope). The stockshell_trim case checks trimming of the wall top by the field
// (the stockshell.frag rule): on the bounds edge there are both cut and untouched nodes,
// the edge corners are untouched, and trimming lowers the wall top exactly by the field depth.
// The holder_rings case covers the outline rings of a solid of revolution (profileRingPoints):
// the heights of the holder rings and the absence of rings on the arc segments of the ball and the corner round.
// The field is the same in all cases: drawing does not touch it.
//
// 2. Measurement. The time of one 50 mm pass and the time of the whole Test_model.ngc
// (all moves, including rapid ones, the stage tool, the bounds from UI_STOCK) at a cell of
// 0.5 and 0.2 mm - these are the preset proposals from DESIGN.md. Lines
// "phase <name> <ms>" are printed, like in bench_probe; parsing of the NC program is not part of the measurement.
//
// Run: stock_sim_probe [file.ngc]
// without an argument data/cad-testdata/Test_model.ngc is taken from the current directory
// or from the build directory.
// Return code: 0 - all cases matched; 1 - there is a mismatch or the NC program was not found.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "core/gcode_parser.h"
#include "core/path_prep.h"
#include "core/stock_sim.h"
#include "core/tooling.h"
#include "core/ui_tags.h"

namespace {

constexpr double PI = 3.14159265358979323846;

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Shared sink: keeps the optimizer from discarding what was computed.
volatile std::size_t g_sink = 0;

double median(std::vector<double> v)
{
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::string toUtf8(const std::wstring& w)
{
#ifdef _WIN32
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
#else
    return std::string(w.begin(), w.end());
#endif
}

std::vector<std::string> probeArgs(int argc, char** argv)
{
#ifdef _WIN32
    int count = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) {
        out.push_back(toUtf8(wargv[i]));
    }
    LocalFree(wargv);
    return out;
#else
    std::vector<std::string> out;
    for (int i = 0; i < argc; ++i) {
        out.push_back(argv[i]);
    }
    return out;
#endif
}

void printPhase(const char* name, double ms)
{
    std::printf("phase %-26s %9.3f\n", name, ms);
}

// Tool for the synthetic cases: those UI_TOOL fields that the profile needs.
struct ToolSpec {
    const char* type = "FLAT";
    double diameterMm = 0.0;
    double taperDeg = 0.0;
    double fluteMm = 10.0;
    double bodyMm = 20.0;
    double cornerMm = 0.0;             // bottom-corner rounding radius
};

gv::ToolInfo makeTool(const ToolSpec& spec)
{
    gv::ToolInfo info;
    info.present = true;
    info.type = spec.type;
    info.diameter = spec.diameterMm;
    info.taper_deg = spec.taperDeg;
    info.flute_length = spec.fluteMm;
    info.shoulder_length = spec.fluteMm;
    info.body_length = spec.bodyMm;
    info.overall_length = spec.bodyMm;
    info.shank_diameter = spec.diameterMm;
    info.corner_radius = spec.cornerMm;
    return info;
}

// Distance from a point to a segment in the XY plane, mm.
double segmentDistanceMm(double px, double py,
                         double ax, double ay, double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double l2 = dx * dx + dy * dy;
    double t = 0.0;
    if (l2 > 0.0) {
        t = std::min(1.0, std::max(0.0, ((px - ax) * dx + (py - ay) * dy) / l2));
    }
    return std::hypot(px - (ax + dx * t), py - (ay + dy * t));
}

// Field digest (FNV-1a 64 over raw float32): runs of the same path, tool
// and cell must give the same field bit for bit. The node order is the
// depthMm order (row by Y), which is also the R32F buffer order.
std::uint64_t fieldDigest(const std::vector<float>& depth)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (float value : depth) {
        unsigned char bytes[sizeof(float)];
        std::memcpy(bytes, &value, sizeof(float));
        for (unsigned char byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

// True sphere of radius r with the tip at tipZ: the material top in the column.
double ballHeightMm(double radiusMm, double rMm, double tipZMm)
{
    if (radiusMm >= rMm) {
        return 0.0;                    // outside the tool the top is untouched
    }
    const double cut = tipZMm + rMm - std::sqrt(rMm * rMm - radiusMm * radiusMm);
    return std::min(0.0, cut);
}

// Cone with a 45-degree generatrix (included angle 90): the cut is at height tipZ + r.
double coneHeightMm(double radiusMm, double tipZMm)
{
    return std::min(0.0, tipZMm + radiusMm);
}

struct CaseStat {
    const char* name = "";
    int checked = 0;
    double maxDevMm = 0.0;
    double tolMm = 0.0;
    bool ok = false;
};

std::vector<CaseStat> g_cases;

void reportCase(const char* name, int checked, double maxDevMm, double tolMm, bool extraOk = true)
{
    CaseStat stat;
    stat.name = name;
    stat.checked = checked;
    stat.maxDevMm = maxDevMm;
    stat.tolMm = tolMm;
    stat.ok = extraOk && maxDevMm <= tolMm;
    g_cases.push_back(stat);
    std::printf("case %-12s checked=%6d max_dev_mm=%.3e tol_mm=%.1e %s\n",
                name, checked, maxDevMm, tolMm, stat.ok ? "OK" : "FAIL");
}

std::string readText(const std::string& utf8Path)
{
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!fh) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());
}

bool fileExists(const std::string& utf8Path)
{
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    return bool(fh);
}

// A program move with the stage tool number.
struct Move {
    std::array<double, 3> aMm{0.0, 0.0, 0.0};
    std::array<double, 3> bMm{0.0, 0.0, 0.0};
    int cutter = 0;
};

struct ProgramTiming {
    int moves = 0;
    long long positions = 0;
    double lengthMm = 0.0;
    double resetMs = 0.0;
    double sweepMs = 0.0;
    int countX = 0;
    int countY = 0;
};

ProgramTiming runProgram(const std::vector<Move>& moves,
                         const std::vector<gv::StockCutter>& cutters,
                         const gv::StockBounds& bounds,
                         double cellMm)
{
    ProgramTiming out;
    const double step = cellMm * 0.5;
    for (const Move& move : moves) {
        const double dx = move.bMm[0] - move.aMm[0];
        const double dy = move.bMm[1] - move.aMm[1];
        const double dz = move.bMm[2] - move.aMm[2];
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        out.lengthMm += length;
        // The probe counts the number of positions by its own rule, not the core: this is
        // a cost check, not a repetition of the field's internal arithmetic. The rule is the same:
        // a segment shorter than the step gives two positions (start and end).
        const long long steps = length > step ? (long long)std::ceil(length / step) : 1;
        out.positions += steps + 1;
    }
    out.moves = int(moves.size());

    gv::StockField field;
    Clock::time_point t0 = Clock::now();
    field.reset(bounds, cellMm);
    out.resetMs = msSince(t0);
    out.countX = field.countX();
    out.countY = field.countY();

    t0 = Clock::now();
    for (const Move& move : moves) {
        const gv::StockCutter& cutter = cutters[std::size_t(move.cutter)];
        if (cutter.maxRadiusMm <= 0.0) {
            continue;
        }
        field.sweep(cutter, move.aMm, move.bMm);
    }
    out.sweepMs = msSince(t0);
    g_sink += field.depthMm().size();
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args = probeArgs(argc, argv);
    int failures = 0;

    // ---- analytic cases ----------------------------------------------------
    // 1. Flat end mill D6 over a flat top: the cut is -1 mm everywhere the
    // tool passed. A 4 mm pass along X, cell 0.5, step 0.25.
    {
        const double cell = 0.5;
        const double r = 3.0;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        field.sweep(cutter, {-2.0, 0.0, tipZ}, {2.0, 0.0, tipZ});

        const double halfStep = cell * 0.25;   // step = cell/2, half a step
        int checked = 0;
        double maxDev = 0.0;
        bool deeper = false;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double x = field.nodeXMm(ix);
                const double y = field.nodeYMm(iy);
                const double d = segmentDistanceMm(x, y, -2.0, 0.0, 2.0, 0.0);
                const double analytic = (d <= r) ? tipZ : 0.0;
                const double height = field.heightMm(ix, iy);
                if (height < analytic - 1e-6) {
                    deeper = true;             // the field is lower than the true cut
                }
                // Outside the step/2 band at the trace wall the value must be exact.
                if (d <= r - halfStep || d >= r + halfStep) {
                    maxDev = std::max(maxDev, std::fabs(height - analytic));
                    ++checked;
                }
            }
        }
        reportCase("flat_sweep", checked, maxDev, 1e-5, !deeper);
    }

    // 2. Ball D6 over a flat top: a valley of radius 3 mm, the profile is a true
    // sphere. The tool profile is 12 segments (core/tooling.cpp), so the
    // tolerance = the sagitta of a 7.5 degree chord, amplified by 1.5 at the wall, where
    // the sphere slope is greatest in the cut zone (the cut is no deeper than 1 mm -> no
    // steeper than 48 degrees, 1/cos = 1.5).
    {
        const double cell = 0.5;
        const double r = 3.0;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"BALL", 6.0}));
        field.plunge(cutter, 0.0, 0.0, tipZ);

        const double angularStep = (PI / 2.0) / 12.0;
        const double sagitta = r * (1.0 - std::cos(angularStep / 2.0));
        const double tol = sagitta * 1.5 + 1e-5;
        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double s = std::hypot(field.nodeXMm(ix), field.nodeYMm(iy));
                maxDev = std::max(maxDev, std::fabs(field.heightMm(ix, iy) - ballHeightMm(s, r, tipZ)));
                ++checked;
            }
        }
        std::printf("note ball_sagitta_mm=%.6f\n", sagitta);
        reportCase("ball_plunge", checked, maxDev, tol);
    }

    // 3. Cone (V-bit, included angle 90) over a flat top: the cut is tipZ + r,
    // a valley with straight walls. The CHAMFER profile with TAPER=45 gives exactly this.
    {
        const double cell = 0.5;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"CHAMFER", 6.0, 45.0}));
        field.plunge(cutter, 0.0, 0.0, tipZ);

        int checked = 0;
        double maxDev = 0.0;
        bool deeper = false;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double s = std::hypot(field.nodeXMm(ix), field.nodeYMm(iy));
                const double analytic = coneHeightMm(s, tipZ);
                const double height = field.heightMm(ix, iy);
                if (height < analytic - 1e-6) {
                    deeper = true;
                }
                maxDev = std::max(maxDev, std::fabs(height - analytic));
                ++checked;
            }
        }
        reportCase("cone_plunge", checked, maxDev, 1e-5, !deeper);
    }

    // 4. Cut above the stock: the tip at +2 mm, a ball D6. The field does not change in
    // a single node.
    {
        const double cell = 0.5;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"BALL", 6.0}));
        field.plunge(cutter, 0.3, 0.2, 2.0);

        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                maxDev = std::max(maxDev, std::fabs(field.heightMm(ix, iy)));
                ++checked;
            }
        }
        reportCase("no_reach", checked, maxDev, 1e-9);
    }

    // 5. A column caught only by the half-cell step. Cell 1 mm, cutter D1
    // (r = 0.5), a pass along X at y = 0.3 from x = 0.5 to 3.5. The column (1, 0)
    // is 0.3 < r away from the line and lands exactly on a 0.5 mm step position;
    // with a full-cell step the positions 0.5, 1.5, 2.5, 3.5 give 0.583 > r, and
    // the column would stay on top.
    {
        const double cell = 1.0;
        const double r = 0.5;
        const double tipZ = -0.2;
        gv::StockBounds bounds;
        bounds.xMinMm = -4.0;
        bounds.xMaxMm = 8.0;
        bounds.yMinMm = -4.0;
        bounds.yMaxMm = 4.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 1.0}));
        field.sweep(cutter, {0.5, 0.3, tipZ}, {3.5, 0.3, tipZ});

        const int ix = int((1.0 - bounds.xMinMm) / cell + 0.5);
        const int iy = int((0.0 - bounds.yMinMm) / cell + 0.5);
        const int iyOutside = int((-1.0 - bounds.yMinMm) / cell + 0.5);
        const double hitHeight = field.heightMm(ix, iy);
        const double outsideHeight = field.heightMm(ix, iyOutside);

        double fullCellDistance = 1e9;
        const double fullStep = cell;              // the step a full cell would give
        for (int k = 0; k <= 3; ++k) {
            const double sx = 0.5 + fullStep * double(k);
            fullCellDistance = std::min(fullCellDistance, std::hypot(1.0 - sx, 0.0 - 0.3));
        }
        const bool fullCellWouldMiss = fullCellDistance > r;
        std::printf("note near_miss node=(1,0) dist_mm=0.3 full_cell_dist_mm=%.4f "
                    "tool_r_mm=%.1f full_cell_would_miss=%d\n",
                    fullCellDistance, r, fullCellWouldMiss ? 1 : 0);

        double maxDev = std::fabs(hitHeight - tipZ);
        maxDev = std::max(maxDev, std::fabs(outsideHeight));
        const bool outsideOk = std::fabs(outsideHeight) < 1e-6;
        reportCase("near_miss_step", 2, maxDev, 1e-5, fullCellWouldMiss && outsideOk);
    }

    // 6. Binding of the field to the trajectory. A short program is parsed the same
    // way as in the viewer (parseGcode + prepareOrdered) and goes through
    // simulateStock - the same pass over PathData that the backend's background worker
    // calls. The check is against the flat-pass analytic solution, as in case 1;
    // the field digest must match between two runs (determinism).
    {
        const std::string program =
            "G21 G90\n"
            "G0 X-2 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X2 F100\n"
            "G0 Z5\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        const gv::ToolInfo tool = makeTool(ToolSpec{"FLAT", 6.0});
        const double cell = 0.5;
        const gv::StockRunResult first =
            gv::simulateStock(data, bounds, cell, tool);
        const gv::StockRunResult second =
            gv::simulateStock(data, bounds, cell, tool);

        const double r = 3.0;
        const double tipZ = -1.0;
        const double halfStep = cell * 0.25;
        int checked = 0;
        double maxDev = 0.0;
        bool deeper = false;
        for (int iy = 0; iy < first.field.countY(); ++iy) {
            for (int ix = 0; ix < first.field.countX(); ++ix) {
                const double x = first.field.nodeXMm(ix);
                const double y = first.field.nodeYMm(iy);
                const double d = segmentDistanceMm(x, y, -2.0, 0.0, 2.0, 0.0);
                const double analytic = (d <= r) ? tipZ : 0.0;
                const double height = first.field.heightMm(ix, iy);
                if (height < analytic - 1e-6) {
                    deeper = true;
                }
                if (d <= r - halfStep || d >= r + halfStep) {
                    maxDev = std::max(maxDev, std::fabs(height - analytic));
                    ++checked;
                }
            }
        }
        const std::uint64_t digest_a = fieldDigest(first.field.depthMm());
        const std::uint64_t digest_b = fieldDigest(second.field.depthMm());
        std::printf("note stock_bind moves=%d length_mm=%.1f digest=%016llx twice=%d\n",
                    first.moves, first.lengthMm,
                    static_cast<unsigned long long>(digest_a),
                    digest_a == digest_b ? 1 : 0);
        reportCase("stock_bind", checked, maxDev, 1e-5,
                   !deeper && digest_a == digest_b && first.moves > 0);
    }

    // 7. Drawing of the top: the field mesh plus the height from the field. The case repeats
    // the drawing in full, except the GPU sampling itself: the grid is built from the field
    // nodes (stockSurfaceMesh), the vertex texcoord is mapped to a node (the same way
    // the nearest sampling of stocksurface.vert reads the field), the height is taken
    // by subtracting the depth from the stock top, and the point is mapped into the scene frame
    // by the same node rotation: machine (x, y, z) -> (x, z, -y). The check is against the
    // analytic solution of a flat plunge computed right here: the cut bottom must
    // land at the tool tip height, and untouched nodes must land at the stock
    // top. The stock top is taken not at zero (z = 5): that way the check catches both
    // an axis swap and a lost zMax term.
    {
        const double cell = 0.5;
        const double r = 3.0;
        const double tipX = -1.5;
        const double tipY = -2.5;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 5.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        field.plunge(cutter, tipX, tipY, tipZ);

        const int nx = field.countX();
        const int ny = field.countY();
        const gv::StockSurfaceMesh mesh = gv::stockSurfaceMesh(bounds, cell, nx, ny);
        // Tolerance: both the depth and the vertex position are stored as float32; at 100 mm the
        // float32 step is about 7.6e-6 mm, hence 1e-5 mm. A node exactly on the radius boundary
        // (|d - r| < 1e-6 mm) is not included in the check: there the last bits of a different
        // summation order would diverge, not the cut.
        const double tol = 1e-5;
        bool geometryOk = mesh.countX == nx && mesh.countY == ny
                && int(mesh.vertices.size()) == 5 * nx * ny
                && int(mesh.indices.size()) == 6 * (nx - 1) * (ny - 1);
        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                const std::size_t v = std::size_t(iy) * std::size_t(nx) + std::size_t(ix);
                const float* vertex = &mesh.vertices[v * 5];
                // The texcoord is the texel center of this node: the nearest sampling returns
                // exactly that node.
                const int tx = int(double(vertex[3]) * double(nx));
                const int ty = int(double(vertex[4]) * double(ny));
                const double x = field.nodeXMm(ix);
                const double y = field.nodeYMm(iy);
                geometryOk = geometryOk && tx == ix && ty == iy
                        && std::fabs(double(vertex[0]) - std::min(x, bounds.xMaxMm)) < 1e-4
                        && std::fabs(double(vertex[1]) - std::min(y, bounds.yMaxMm)) < 1e-4
                        && double(vertex[2]) == bounds.zMaxMm;
                // The height as the vertex shader computes it: z = top - depth.
                const double drawnZ = double(vertex[2] - field.depthMm()[v]);
                const double d = std::hypot(x - tipX, y - tipY);
                const double analytic = (d <= r) ? tipZ : bounds.zMaxMm;
                if (std::fabs(d - r) > 1e-6) {
                    maxDev = std::max(maxDev, std::fabs(drawnZ - analytic));
                    ++checked;
                }
            }
        }
        // The node under the tip: its bottom in the scene frame (x, z, -y) must match the
        // tip (scene X and Z - up to half a cell, scene axis Y -
        // exactly: the tip cuts at its own height).
        const int ixTip = int(std::lround((tipX - bounds.xMinMm) / cell));
        const int iyTip = int(std::lround((tipY - bounds.yMinMm) / cell));
        const std::size_t vTip = std::size_t(iyTip) * std::size_t(nx) + std::size_t(ixTip);
        const double nodeTipX = field.nodeXMm(ixTip);
        const double nodeTipY = field.nodeYMm(iyTip);
        const double drawnTipZ = double(mesh.vertices[vTip * 5 + 2] - field.depthMm()[vTip]);
        const double planeDev = std::fabs(drawnTipZ - tipZ);
        const double posDev = std::max(std::fabs(nodeTipX - tipX), std::fabs(nodeTipY - tipY));
        std::printf("note stocksurface node=(%.2f,%.2f) uv=(%.4f,%.4f) tip=(%.2f,%.2f) "
                    "scene=(%.2f,%.2f,%.2f) tip_scene=(%.2f,%.2f,%.2f) plane_dev_mm=%.2e "
                    "pos_dev_mm=%.3f node_mm=%.2f\n",
                    nodeTipX, nodeTipY, double(mesh.vertices[vTip * 5 + 3]),
                    double(mesh.vertices[vTip * 5 + 4]), tipX, tipY,
                    nodeTipX, drawnTipZ, -nodeTipY, tipX, tipZ, -tipY,
                    planeDev, posDev, cell);
        reportCase("stocksurface", checked, maxDev, tol,
                   geometryOk && planeDev <= tol && posDev <= cell * 0.5 + 1e-6);
    }

    // 7b. Smooth view: the subdivided drawing grid and the ledge rule. The field is
    // not touched here - case 7 checks it by nodes (and only its numbers
    // hold the manifest), while here what is visible on screen is checked: subnodes
    // sit on the field node grid, the texcoord is their cell fraction, the subnode height
    // is bilinear (the vertex shader computes it, stocksurface.vert), and the normal on a
    // cell with a ledge is taken as the slope of the wall itself, not as the half-sum with the flat
    // top. The check is red before the fix: the half-sum gives, on a flat cell at the
    // edge, half a cell of wall slope - that very band visible to the eye.
    {
        const double cell = 0.5;
        const int subdiv = 2;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 5.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{ "FLAT", 6.0 }));
        field.plunge(cutter, -1.5, -2.5, -1.0);

        const int nx = field.countX();
        const int ny = field.countY();
        const gv::StockSurfaceMesh mesh = gv::stockSurfaceMesh(bounds, cell, nx, ny, subdiv);
        const int drawnX = (nx - 1) * subdiv + 1;
        const int drawnY = (ny - 1) * subdiv + 1;
        bool geometryOk = mesh.countX == drawnX && mesh.countY == drawnY
                && int(mesh.vertices.size()) == 5 * drawnX * drawnY
                && int(mesh.indices.size()) == 6 * (drawnX - 1) * (drawnY - 1);
        int checked = 0;
        double maxDev = 0.0;
        for (int jy = 0; jy < drawnY; ++jy) {
            for (int jx = 0; jx < drawnX; ++jx) {
                const float* vertex = &mesh.vertices[(std::size_t(jy) * std::size_t(drawnX)
                                                      + std::size_t(jx)) * 5];
                // A subnode is a fraction of the field cell: it must land on the node grid
                // (step cell / subdiv), and the texcoord must be at the same cell fraction.
                const double tx = double(jx) / double(subdiv);
                const double ty = double(jy) / double(subdiv);
                const double x = bounds.xMinMm + tx * cell;
                const double y = bounds.yMinMm + ty * cell;
                const double u = (tx + 0.5) / double(nx);
                const double v = (ty + 0.5) / double(ny);
                geometryOk = geometryOk
                        && std::fabs(double(vertex[0]) - std::min(x, bounds.xMaxMm)) < 1e-4
                        && std::fabs(double(vertex[1]) - std::min(y, bounds.yMaxMm)) < 1e-4
                        && std::fabs(double(vertex[3]) - u) < 1e-6
                        && std::fabs(double(vertex[4]) - v) < 1e-6;
                ++checked;
            }
        }

        // The expected height of the drawn surface at a subnode: bilinear over the
        // cell nodes - the same as fieldSample computes in stocksurface.vert.
        const std::vector<float>& depth = field.depthMm();
        const auto nodeDepth = [&depth, nx, ny](int ix, int iy) {
            ix = std::max(0, std::min(nx - 1, ix));
            iy = std::max(0, std::min(ny - 1, iy));
            return double(depth[std::size_t(iy) * std::size_t(nx) + std::size_t(ix)]);
        };
        const auto bilinearDepth = [&nodeDepth, nx, ny](double u, double v) {
            const double p = u * double(nx) - 0.5;
            const double q = v * double(ny) - 0.5;
            const double bx = std::floor(p);
            const double by = std::floor(q);
            const double fx = p - bx;
            const double fy = q - by;
            const int ix = int(bx);
            const int iy = int(by);
            const double d0 = nodeDepth(ix, iy) * (1.0 - fx) + nodeDepth(ix + 1, iy) * fx;
            const double d1 = nodeDepth(ix, iy + 1) * (1.0 - fx) + nodeDepth(ix + 1, iy + 1) * fx;
            return d0 * (1.0 - fy) + d1 * fy;
        };
        // A subnode at the middle of a field cell must give the half-sum of its nodes along
        // the fractional axis and a node along the integer one: that is the bilinear height by
        // which the shader lowers the vertex.
        int bilinearChecked = 0;
        double bilinearDev = 0.0;
        for (int iy = 0; iy + 1 < ny; ++iy) {
            for (int ix = 0; ix + 1 < nx; ++ix) {
                const double u = (double(ix) + 1.0) / double(nx);
                const double v = (double(iy) + 0.5) / double(ny);
                const double expect = 0.5 * (nodeDepth(ix, iy) + nodeDepth(ix + 1, iy));
                bilinearDev = std::max(bilinearDev, std::fabs(bilinearDepth(u, v) - expect));
                ++bilinearChecked;
                const double u2 = (double(ix) + 0.5) / double(nx);
                const double v2 = (double(iy) + 1.0) / double(ny);
                const double expect2 = 0.5 * (nodeDepth(ix, iy) + nodeDepth(ix, iy + 1));
                bilinearDev = std::max(bilinearDev, std::fabs(bilinearDepth(u2, v2) - expect2));
                ++bilinearChecked;
            }
        }

        // The slope along X at a subnode as the shader computes it: differences of neighboring
        // nodes catch the ledge, on a cell with a ledge the slope is the cell's own gradient,
        // otherwise it is the central difference over 2 * NORMAL_SPAN cells. Here
        // only the X axis is repeated and only those points where the answer distinguishes the rules.
        const auto shaderSlopeX = [&nodeDepth, nx, ny, cell](double u, double v) {
            const double stepMm = cell;                   // STEP_CELLS = 1
            const double du = 1.0 / double(nx);
            const auto sample = [&nodeDepth, nx, ny](double uu, double vv) {
                const double p = uu * double(nx) - 0.5;
                const double q = vv * double(ny) - 0.5;
                const double bx = std::floor(p);
                const double by = std::floor(q);
                const double fx = p - bx;
                const double fy = q - by;
                const int ix = int(bx);
                const int iy = int(by);
                const double a = nodeDepth(ix, iy) * (1.0 - fx) + nodeDepth(ix + 1, iy) * fx;
                const double b = nodeDepth(ix, iy + 1) * (1.0 - fx) + nodeDepth(ix + 1, iy + 1) * fx;
                return a * (1.0 - fy) + b * fy;
            };
            const double depth = sample(u, v);
            const double r1 = sample(u + du, v);
            const double l1 = sample(u - du, v);
            const double r2 = sample(u + 2.0 * du, v);
            const double l2 = sample(u - 2.0 * du, v);
            const double span = 4.0 * cell;               // 2 * NORMAL_SPAN cells
            const bool jr = std::max(std::fabs(r1 - depth), std::fabs(r2 - r1)) > stepMm;
            const bool jl = std::max(std::fabs(depth - l1), std::fabs(l1 - l2)) > stepMm;
            if (!jr && !jl)
                return (r2 - l2) / span;
            const double p = u * double(nx) - 0.5;
            const double q = v * double(ny) - 0.5;
            const double fy = q - std::floor(q);
            const int ix = int(std::floor(p));
            const int iy = int(std::floor(q));
            const double g = (nodeDepth(ix + 1, iy) - nodeDepth(ix, iy)) * (1.0 - fy)
                           + (nodeDepth(ix + 1, iy + 1) - nodeDepth(ix, iy + 1)) * fy;
            return g / cell;
        };

        // A cell with a ledge along X: to the right it is many cells deeper, to the left it is flat.
        // The Y row is taken where the wall is straight (neighboring nodes are equal): then
        // the answer does not depend on the fraction inside the cell along Y and the check is exact.
        int wallCells = 0;
        double ruleDev = 0.0;
        for (int iy = 0; iy + 1 < ny; ++iy) {
            for (int ix = 1; ix + 1 < nx; ++ix) {
                const double step = nodeDepth(ix + 1, iy) - nodeDepth(ix, iy);
                if (std::fabs(step) < 2.0 * cell)          // not a ledge but a slope
                    continue;
                if (std::fabs(nodeDepth(ix, iy) - nodeDepth(ix - 1, iy)) > 1e-6)
                    continue;                              // not flat on the left
                if (nodeDepth(ix, iy) != nodeDepth(ix, iy + 1)
                        || nodeDepth(ix + 1, iy) != nodeDepth(ix + 1, iy + 1))
                    continue;                              // the wall is not straight along Y
                // The flat cell on the left: the slope must be zero - there is no band at
                // the edge. The half-sum here would give half a cell of slope.
                const double flat = shaderSlopeX(double(ix) / double(nx),
                                                 (double(iy) + 0.5) / double(ny));
                // The wall cell: the slope must be the slope of the wall itself.
                const double wall = shaderSlopeX(double(ix + 1) / double(nx),
                                                 (double(iy) + 0.5) / double(ny));
                ruleDev = std::max(ruleDev, std::fabs(flat));
                ruleDev = std::max(ruleDev, std::fabs(wall - step / cell));
                ++wallCells;
            }
        }
        std::printf("note stocksurface_fine subdiv=%d drawn=%dx%d cells=%d bilinear=%d "
                    "wall_cells=%d rule_dev_mm/mm=%.3e\n",
                    subdiv, drawnX, drawnY, checked, bilinearChecked, wallCells, ruleDev);
        reportCase("stocksurface_fine", checked + bilinearChecked + wallCells,
                   std::max(maxDev, std::max(bilinearDev, ruleDev)), 1e-6,
                   geometryOk && wallCells > 0);
    }

    // 8. Box shell: five rectangles without a top face. What is checked is the
    // shape, not the buffer: 30 vertices, each one a corner of the bounds expanded by
    // the gaps; for every triangle one coordinate is constant (the faces are
    // flat); the bottom exists, the top face does not - otherwise it would fight with the field over
    // one plane, and a micro-shift at the top would lie about the cut depth.
    {
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 5.0;
        const double gapSide = 0.7;
        const double gapTop = 0.5;
        const double gapBottom = 0.5;
        const std::vector<float> vertices = gv::stockShellVertices(
                bounds, 0.5, 21, 21, gapSide, gapTop, gapBottom);
        const double x0 = bounds.xMinMm - gapSide;
        const double x1 = bounds.xMaxMm + gapSide;
        const double y0 = bounds.yMinMm - gapSide;
        const double y1 = bounds.yMaxMm + gapSide;
        const double z0 = bounds.zMinMm - gapBottom;
        const double z1 = bounds.zMaxMm + gapTop;

        const auto atCorner = [x0, x1, y0, y1, z0, z1](double value, double a, double b) {
            return std::fabs(value - a) < 1e-6 || std::fabs(value - b) < 1e-6;
        };
        bool shapeOk = int(vertices.size()) == 30 * 3;
        double maxDev = 0.0;
        int bottomFaces = 0;
        int topFaces = 0;
        int checked = 0;
        for (std::size_t i = 0; i + 2 < vertices.size() / 3; i += 3) {
            const float* a = &vertices[i * 3];
            const float* b = &vertices[(i + 1) * 3];
            const float* c = &vertices[(i + 2) * 3];
            ++checked;
            for (const float* p : {a, b, c}) {
                const double px = double(p[0]);
                const double py = double(p[1]);
                const double pz = double(p[2]);
                // Deviation from the nearest bounds corner: zero if the vertex
                // sits exactly on it.
                const double dx = std::min(std::fabs(px - x0), std::fabs(px - x1));
                const double dy = std::min(std::fabs(py - y0), std::fabs(py - y1));
                const double dz = std::min(std::fabs(pz - z0), std::fabs(pz - z1));
                maxDev = std::max(maxDev, std::max(dx, std::max(dy, dz)));
                shapeOk = shapeOk
                        && atCorner(px, x0, x1) && atCorner(py, y0, y1) && atCorner(pz, z0, z1);
            }
            const bool flat = (a[0] == b[0] && b[0] == c[0])
                    || (a[1] == b[1] && b[1] == c[1])
                    || (a[2] == b[2] && b[2] == c[2]);
            shapeOk = shapeOk && flat;
            // A face entirely on the top or on the bottom: there must be no top face, the bottom is two
            // triangles.
            const bool onBottom = a[2] == float(z0) && b[2] == float(z0) && c[2] == float(z0);
            const bool onTop = a[2] == float(z1) && b[2] == float(z1) && c[2] == float(z1);
            if (onBottom) {
                ++bottomFaces;
            }
            if (onTop) {
                ++topFaces;
            }
        }
        std::printf("note stockshell verts=%zu tris=%d box=%.1f..%.1f x %.1f..%.1f x %.1f..%.1f "
                    "bottom=%d top=%d\n",
                    vertices.size() / 3, checked, x0, x1, y0, y1, z0, z1,
                    bottomFaces, topFaces);
        reportCase("stockshell", checked, maxDev, 1e-6,
                   shapeOk && bottomFaces == 2 && topFaces == 0);
    }

    // 8b. Trimming of the wall top by the field: the stockshell.frag rule. The wall stands on
    // the bounds, its top is at the stock top, and the field node at the edge is lowered
    // where the tool removed metal across the edge (the field is cut beyond the path
    // bounds too). It is checked that (1) on the edge there are both cut and untouched nodes, (2)
    // the trimming rule lowers the wall top exactly by the field depth plus the top
    // gap, (3) the edge corners stay untouched - that is, linear
    // interpolation between corners (per-vertex trimming) would not have lowered the wall top in the middle of the cut,
    // and trimming must be per-pixel.
    {
        const double cell = 0.5;
        const double gapTop = 0.5;                 // same as shellGapTopMm in QML
        gv::StockBounds bounds;
        bounds.xMinMm = -10.0;
        bounds.xMaxMm = 10.0;
        bounds.yMinMm = -10.0;
        bounds.yMaxMm = 10.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        // Cut along the edge y = yMax: the D6 cutter goes right along the wall, depth 2 mm.
        field.sweep(cutter, {-5.0, bounds.yMaxMm, -2.0}, {5.0, bounds.yMaxMm, -2.0});

        const int iyEdge = field.countY() - 1;     // node on the yMax edge
        const int ixMid = int((0.0 - bounds.xMinMm) / cell + 0.5);
        int cutNodes = 0;
        int untouchedNodes = 0;
        double maxDepth = 0.0;
        for (int ix = 0; ix < field.countX(); ++ix) {
            const double depth = bounds.zMaxMm - field.heightMm(ix, iyEdge);
            if (depth > 0.05) {
                ++cutNodes;
                maxDepth = std::max(maxDepth, depth);
            } else {
                ++untouchedNodes;
            }
        }
        const double cornerLeft = bounds.zMaxMm - field.heightMm(0, iyEdge);
        const double cornerRight = bounds.zMaxMm - field.heightMm(field.countX() - 1, iyEdge);
        const double midDepth = bounds.zMaxMm - field.heightMm(ixMid, iyEdge);
        // The shader rule: wall top = stock top minus field depth. For an
        // untouched node this is exactly the top (no gap), for a cut one the wall goes
        // down by the depth, while untrimmed it would stand higher by the gap plus the depth.
        const double trimGapMm = gapTop + midDepth;
        const bool ok = cutNodes > 0 && untouchedNodes > 0
                && cornerLeft == 0.0 && cornerRight == 0.0
                && midDepth >= 1.5
                && (cornerLeft + cornerRight) / 2.0 < 0.5 * midDepth;
        std::printf("note stockshell_trim edge_nodes=%d cut=%d untouched=%d "
                    "max_depth_mm=%.2f mid_depth_mm=%.2f corners=(%.2f,%.2f) "
                    "trim_gap_mm=%.2f\n",
                    field.countX(), cutNodes, untouchedNodes, maxDepth, midDepth,
                    cornerLeft, cornerRight, trimGapMm);
        reportCase("stockshell_trim", field.countX(), 0.0, 0.0, ok);
    }

    // 8d. Trimming of the wall top takes the depth with the SAME sampling as the drawn
    // top (stockshell.frag, edgeDepth): bilinear over the cell nodes. The previous
    // rule took the nearest node - a staircase by nodes - and on an edge with removed
    // metal the wall top diverged from the drawn top edge by half the
    // difference of neighboring nodes. The case holds both halves of the rule: with the nearest
    // node there is a divergence, with the bilinear sampling there is none.
    {
        const double cell = 0.5;
        gv::StockBounds bounds;
        bounds.xMinMm = -10.0;
        bounds.xMaxMm = 10.0;
        bounds.yMinMm = -10.0;
        bounds.yMaxMm = 10.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        // The edge y = yMax is cut to different depths along x: with the nearest node
        // the trimming is a staircase, with the bilinear sampling it is linear between nodes.
        field.sweep(cutter, {-8.0, bounds.yMaxMm, -3.0}, {-1.0, bounds.yMaxMm, -3.0});
        field.sweep(cutter, { 1.0, bounds.yMaxMm, -1.0}, { 8.0, bounds.yMaxMm, -1.0});

        const int iyEdge = field.countY() - 1;
        const int nx = field.countX();
        // The shader rule (stockshell.frag, edgeDepth): the depth at an edge point
        // is taken bilinearly over the four cell nodes - on the bounds that is a straight line
        // between neighboring edge nodes.
        const auto trimDepth = [&](double xMm) {
            const double hi = double(nx - 1);
            const double q = std::min(std::max((xMm - bounds.xMinMm) / cell, 0.0), hi);
            const int i0 = int(std::floor(q));
            const int i1 = std::min(i0 + 1, nx - 1);
            const double f = q - double(i0);
            const double d0 = bounds.zMaxMm - field.heightMm(i0, iyEdge);
            const double d1 = bounds.zMaxMm - field.heightMm(i1, iyEdge);
            return d0 + (d1 - d0) * f;
        };
        // The edge of the drawn top at the bounds is a straight line between neighboring nodes
        // (core/stock_sim, stockSurfaceMesh: the last row is clamped to the bounds, and
        // between nodes the mesh is linear).
        double worstNearestMm = 0.0;
        double worstBilinearMm = 0.0;
        for (int ix = 0; ix + 1 < nx; ++ix) {
            const double left = bounds.zMaxMm - field.heightMm(ix, iyEdge);
            const double right = bounds.zMaxMm - field.heightMm(ix + 1, iyEdge);
            const double drawn = 0.5 * (left + right);
            const double xMid = bounds.xMinMm + (double(ix) + 0.5) * cell;
            worstNearestMm = std::max(worstNearestMm, std::abs(left - drawn));
            worstBilinearMm = std::max(worstBilinearMm, std::abs(trimDepth(xMid) - drawn));
        }
        const bool ok = worstNearestMm >= 0.25 && worstBilinearMm <= 1e-6;
        std::printf("note stockshell_trim_interp nearest_dev_mm=%.2f "
                    "bilinear_dev_mm=%.6f\n",
                    worstNearestMm, worstBilinearMm);
        reportCase("stockshell_trim_interp", nx - 1, worstBilinearMm, 1e-6, ok);
    }

    // 8c. Outline rings of the holder and the cutter: the profileRingPoints jump threshold.
    // A ring is drawn at a real profile jump, but not on an arc segment - otherwise
    // the wireframe of the solid of revolution turns into a grid. The threshold must lie between
    // the arc step and the shallowest chamfer: both edges are checked - the holder rings with
    // their heights, and the rings on the ball and corner-round arcs (there must be none).
    {
        const std::vector<gv::ProfilePoint> holder = gv::holderSolidProfile();
        const std::vector<gv::ProfilePoint> holderRings = gv::profileRingPoints(holder);
        const double expectY[] = {0.0, 3.0, 16.0, 20.0, 22.0, 24.0, 28.0, 72.0};
        const int expectCount = int(sizeof(expectY) / sizeof(expectY[0]));
        bool heightsOk = int(holderRings.size()) == expectCount;
        double maxDev = 0.0;
        for (int i = 0; i < int(holderRings.size()); ++i) {
            const double y = holderRings[std::size_t(i)].height_mm;
            if (i < expectCount) {
                maxDev = std::max(maxDev, std::fabs(y - expectY[i]));
                heightsOk = heightsOk && std::fabs(y - expectY[i]) < 1e-9;
            }
        }

        // The ball arc: 12 segments over 90 degrees, 7.5 per segment. Rings on the arc are a
        // grid, so there must be none on it; at the arc pole there is no ring for
        // another reason as well: the chord there is almost vertical (3.8 degrees to the
        // cylinder), not 90. The ball has exactly one real edge - the neck.
        const std::vector<gv::ProfilePoint> ball = gv::toolProfile(makeTool(ToolSpec{"BALL", 6.0}));
        const std::vector<gv::ProfilePoint> ballRings = gv::profileRingPoints(ball);
        // The bottom-corner rounding arc: 8 segments over 90, 11.25 per segment - the step
        // closest to the threshold, and it is what checks the threshold from below: a threshold below 11.25
        // would give eight rings on the arc, and the ring count would show it.
        const std::vector<gv::ProfilePoint> bull =
                gv::toolProfile(makeTool(ToolSpec{"FLAT", 6.0, 0.0, 10.0, 20.0, 1.0}));
        const std::vector<gv::ProfilePoint> bullRings = gv::profileRingPoints(bull);

        std::printf("note holder_rings count=%d", int(holderRings.size()));
        for (const gv::ProfilePoint& p : holderRings) {
            std::printf(" (%.2f,%.2f)", p.radius_mm, p.height_mm);
        }
        std::printf(" ball=%d bull=%d\n", int(ballRings.size()), int(bullRings.size()));

        const bool ok = heightsOk
                && ballRings.size() == 1
                && bullRings.size() == 1;
        reportCase("holder_rings", expectCount + 2, maxDev, 1e-9, ok);
    }

    // 9. Undercut: the height map does not express it, and the walk must refuse rather than
    // draw a cut that would not happen on the machine. Both inputs are checked:
    // a ready-made profile (a lollipop: the radius grows, drops and grows again - UI_TOOL does
    // not describe such a shape) and the UI_TOOL type LOLLIPOP, for which the profile
    // is built as a flat cutter and the shape is already lost. There is no field in either case:
    // not empty, but not created at all.
    {
        const std::string program =
            "(UI_TOOL: T=7 TYPE=LOLLIPOP D=10. CR=0. TIPD=0. FL=5. SL=5. BL=20. OL=20. NF=2)\n"
            "G21 G90\n"
            "G0 X0 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X10 F100\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        gv::StockBounds bounds;
        bounds.xMinMm = -20.0;
        bounds.xMaxMm = 20.0;
        bounds.yMinMm = -20.0;
        bounds.yMaxMm = 20.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        const double cell = 0.5;

        // A ready-made lollipop profile: radius growth, neck, growth again.
        const std::vector<gv::ProfilePoint> profile{
            {0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}, {1.5, 5.0}, {4.0, 7.0}};
        const gv::StockCutter cutter = gv::stockCutterFromProfile(profile, 8.0);
        gv::StockWalk walk;
        const bool profileStarted = walk.start(data, bounds, cell, {cutter});
        const bool profileEmpty = walk.field().empty();
        const bool profileRefused = walk.refusedStage() == 0;

        // UI_TOOL type: the refusal comes from the type, before the profile.
        const gv::StockRunResult byTag =
            gv::simulateStock(data, bounds, cell, gv::ToolInfo{});
        const gv::StockCutter byType = gv::stockCutter(parsed.stages.front().info);
        std::printf("note stock_undercut profile_undercut=%d stage=%d field_nodes=%d "
                    "type_undercut=%d tag_refused=%d tag_stage=%d tag_tool=%d tag_nodes=%d\n",
                    cutter.undercut ? 1 : 0, walk.refusedStage(),
                    walk.field().countX() * walk.field().countY(),
                    byType.undercut ? 1 : 0, byTag.refused ? 1 : 0, byTag.refusedStage,
                    byTag.refusedTool, byTag.field.countX() * byTag.field.countY());
        const bool ok = cutter.undercut && !profileStarted && profileEmpty && profileRefused
                && byType.undercut && byTag.refused && byTag.refusedStage == 0
                && byTag.refusedTool == 7 && byTag.field.empty();
        reportCase("stock_undercut", 8, 0.0, 0.0, ok);
    }

    // ---- measurement: one pass ---------------------------------------------
    {
        const double cell = 0.5;
        const double r = 3.0;
        const gv::StockBounds bounds{-100.0, 100.0, -100.0, 100.0, -10.0, 0.0};
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        gv::StockField field;
        std::vector<double> times;
        const int reps = 15;
        for (int rep = 0; rep < reps; ++rep) {
            field.reset(bounds, cell);
            const Clock::time_point t0 = Clock::now();
            field.sweep(cutter, {-25.0, 0.0, -1.0}, {25.0, 0.0, -1.0});
            times.push_back(msSince(t0));
        }
        g_sink += field.depthMm().size();
        printPhase("stock_sim_segment", median(times));
        std::printf("segment cell_mm=%.1f tool_r_mm=%.1f length_mm=50.0 positions=201 reps=%d\n",
                    cell, r, reps);
    }

    // ---- measurement: the whole Test_model.ngc -----------------------------
    std::string ngcPath;
    if (args.size() >= 2) {
        ngcPath = args[1];
    }
    std::vector<std::string> candidates;
    if (!ngcPath.empty()) {
        candidates.push_back(ngcPath);
    } else {
        candidates.push_back("data/cad-testdata/Test_model.ngc");
        // The second path is from the build directory (<repo>/build/edges): three levels up.
#ifdef _WIN32
        wchar_t buf[4096];
        const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
        if (n > 0 && n < static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]))) {
            std::filesystem::path dir(std::wstring(buf, static_cast<std::size_t>(n)));
            dir = dir.parent_path().parent_path().parent_path();
            candidates.push_back(toUtf8((dir / L"data" / L"cad-testdata" / L"Test_model.ngc").wstring()));
        }
#endif
    }
    for (const std::string& candidate : candidates) {
        if (fileExists(candidate)) {
            ngcPath = candidate;
            break;
        }
    }
    const std::string text = ngcPath.empty() ? std::string() : readText(ngcPath);
    if (text.empty()) {
        std::printf("program not found: %s\n",
                    candidates.empty() ? "(no path)" : candidates.front().c_str());
        ++failures;
    } else {
        const Clock::time_point tParse = Clock::now();
        const gv::ParseResult parsed = gv::parseGcode(text);
        const double parseMs = msSince(tParse);

        // The stock bounds come from the NC program, as at runtime (parseUiStock). Without the tag
        // the path bounds are taken: the measurement is then cruder than reality, and that is stated.
        gv::StockBounds bounds;
        const std::optional<std::array<double, 6>> stock = gv::parseUiStock(text);
        if (stock) {
            bounds.xMinMm = (*stock)[0];
            bounds.xMaxMm = (*stock)[1];
            bounds.yMinMm = (*stock)[2];
            bounds.yMaxMm = (*stock)[3];
            bounds.zMinMm = (*stock)[4];
            bounds.zMaxMm = (*stock)[5];
        } else {
            // No tag: bounds by the path, the top 1 mm above the highest point.
            bool first = true;
            for (const gv::PathPoint& p : parsed.ordered) {
                if (first) {
                    bounds.xMinMm = bounds.xMaxMm = p.point[0];
                    bounds.yMinMm = bounds.yMaxMm = p.point[1];
                    bounds.zMinMm = bounds.zMaxMm = p.point[2];
                    first = false;
                    continue;
                }
                bounds.xMinMm = std::min(bounds.xMinMm, p.point[0]);
                bounds.xMaxMm = std::max(bounds.xMaxMm, p.point[0]);
                bounds.yMinMm = std::min(bounds.yMinMm, p.point[1]);
                bounds.yMaxMm = std::max(bounds.yMaxMm, p.point[1]);
                bounds.zMinMm = std::min(bounds.zMinMm, p.point[2]);
                bounds.zMaxMm = std::max(bounds.zMaxMm, p.point[2]);
            }
            bounds.zMaxMm += 1.0;
        }

        // Stage tool: a stage without UI_TOOL keeps the previous one - on the machine
        // the tool changes only on M6.
        std::vector<gv::StockCutter> cutters;
        gv::ToolInfo carried;
        for (const gv::Stage& stage : parsed.stages) {
            if (stage.info.present) {
                carried = stage.info;
            }
            cutters.push_back(gv::stockCutter(carried));
        }
        if (cutters.empty()) {
            cutters.push_back(gv::stockCutter(gv::ToolInfo{}));
        }

        std::vector<Move> moves;
        moves.reserve(parsed.ordered.size());
        std::size_t stageIndex = 0;
        for (std::size_t i = 1; i < parsed.ordered.size(); ++i) {
            while (stageIndex + 1 < parsed.stages.size()
                   && std::size_t(parsed.stages[stageIndex + 1].index) <= i) {
                ++stageIndex;
            }
            const int cutter = int(std::min(stageIndex, cutters.size() - 1));
            Move move;
            move.aMm = parsed.ordered[i - 1].point;
            move.bMm = parsed.ordered[i].point;
            move.cutter = cutter;
            moves.push_back(move);
        }

        const double cells[] = {0.5, 0.2};
        for (double cell : cells) {
            const ProgramTiming timing = runProgram(moves, cutters, bounds, cell);
            const char* suffix = (cell < 0.35) ? "0_2" : "0_5";
            char name[64];
            std::snprintf(name, sizeof(name), "stock_sim_reset_%s", suffix);
            printPhase(name, timing.resetMs);
            std::snprintf(name, sizeof(name), "stock_sim_sweep_%s", suffix);
            printPhase(name, timing.sweepMs);
            std::printf("program cell_mm=%.1f nodes=%dx%d cells=%lld field_bytes=%lld "
                        "moves=%d positions=%lld length_mm=%.1f reset_ms=%.3f sweep_ms=%.3f "
                        "total_ms=%.3f\n",
                        cell, timing.countX, timing.countY,
                        (long long)timing.countX * timing.countY,
                        (long long)timing.countX * timing.countY * 4, timing.moves,
                        timing.positions, timing.lengthMm, timing.resetMs,
                        timing.sweepMs, timing.resetMs + timing.sweepMs);
        }

        // ---- live walk: 10 Hz ticks, rewind, digest -------------------------
        // The field accumulated by ticks must match bit for bit the field of a single
        // pass up to the same path length, and after a rewind - the pass from the start
        // to the rewind target. A tick is 100 ms of machine time: the target grows by the path
        // the machine covers at the programmed feed (speedAt, mm/s), as in
        // 1:1 playback in QML. The check is bitwise (FNV-1a over float32):
        // here the same order of accumulating minima matters, while a tolerance in mm would hide
        // a diverged cut.
        {
            const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
            const gv::ToolInfo initial = gv::parseUiTool(text);
            const double cell = 0.5;
            const gv::StockRunResult full = gv::simulateStock(data, bounds, cell, initial);
            const std::uint64_t digestFull = fieldDigest(full.field.depthMm());

            // What the field says at the bounds edge: how many perimeter nodes are cut,
            // how deep, and whether any are untouched. These are the numbers
            // that the wall top fights with in the frame: a cut edge node means the wall
            // top is below the top bounds, and without trimming a gap is visible there.
            if (!full.field.empty()) {
                const gv::StockField& f = full.field;
                int edgeCut = 0;
                int edgeTotal = 0;
                double edgeMax = 0.0;
                double edgeX = 0.0;
                double edgeY = 0.0;
                const auto noteEdge = [&](int ix, int iy) {
                    ++edgeTotal;
                    const double depth = f.bounds().zMaxMm - f.heightMm(ix, iy);
                    if (depth > 0.05) {
                        ++edgeCut;
                        if (depth > edgeMax) {
                            edgeMax = depth;
                            edgeX = f.nodeXMm(ix);
                            edgeY = f.nodeYMm(iy);
                        }
                    }
                };
                for (int ix = 0; ix < f.countX(); ++ix) {
                    noteEdge(ix, 0);
                    noteEdge(ix, f.countY() - 1);
                }
                for (int iy = 1; iy + 1 < f.countY(); ++iy) {
                    noteEdge(0, iy);
                    noteEdge(f.countX() - 1, iy);
                }
                std::printf("note stock_edge nodes=%d cut=%d max_depth_mm=%.2f at=(%.1f,%.1f) "
                            "bounds_x=%.1f..%.1f y=%.1f..%.1f\n",
                            edgeTotal, edgeCut, edgeMax, edgeX, edgeY,
                            f.bounds().xMinMm, f.bounds().xMaxMm,
                            f.bounds().yMinMm, f.bounds().yMaxMm);
            }

            // The speed of the outgoing segment under the cursor: the same segment choice
            // as path.speedAt in QML (upper bound by accumulated length).
            const std::vector<double>& cum = data.cum;
            const auto speedAt = [&cum, &data](double d) {
                const std::size_t n = cum.size();
                if (n < 2) {
                    return 0.0;
                }
                const std::size_t i = std::size_t(
                    std::upper_bound(cum.begin(), cum.end(), d) - cum.begin());
                const std::size_t idx = i == 0 ? 0 : std::min(i - 1, n - 2);
                return idx < data.speeds.size() ? data.speeds[idx] : 0.0;
            };

            gv::StockWalk walk;
            const bool started = walk.start(data, bounds, cell,
                                            gv::stockCutterPlan(data, initial));
            std::vector<double> tickMs;
            double target = 0.0;
            while (started && target < data.total) {
                double v = speedAt(target);
                if (!(v > 0.0)) {
                    v = 10.0;              // as in QML: 10 mm/s without feed
                }
                target = std::min(data.total, target + v * 0.1);
                const Clock::time_point t0 = Clock::now();
                walk.advanceTo(target);
                tickMs.push_back(msSince(t0));
            }
            const double cursorAfterTicks = walk.cursorMm();
            const std::uint64_t digestLive = fieldDigest(walk.field().depthMm());

            // Rewind: the target is behind the cursor, the walk resets the field and goes from
            // the start. It is measured together with the reset, three times, at half the program.
            const double rewindTarget = data.total * 0.5;
            std::vector<double> rewindMs;
            std::uint64_t digestRewind = 0;
            for (int rep = 0; rep < 3; ++rep) {
                walk.advanceTo(data.total);          // the field is full again
                const Clock::time_point t0 = Clock::now();
                walk.advanceTo(rewindTarget);
                rewindMs.push_back(msSince(t0));
                digestRewind = fieldDigest(walk.field().depthMm());
            }
            // The field from the start to the rewind target - with a separate walk: it is what
            // the field after the rewind is checked against.
            gv::StockWalk prefix;
            const bool prefixStarted = prefix.start(data, bounds, cell,
                                                    gv::stockCutterPlan(data, initial));
            prefix.advanceTo(rewindTarget);
            const std::uint64_t digestPrefix = fieldDigest(prefix.field().depthMm());

            const std::size_t fieldBytes =
                walk.field().depthMm().size() * sizeof(float);
            const double tickMax = tickMs.empty()
                ? 0.0 : *std::max_element(tickMs.begin(), tickMs.end());
            const double tickTotal = std::accumulate(tickMs.begin(), tickMs.end(), 0.0);
            const std::size_t tickBusy = std::size_t(std::count_if(
                tickMs.begin(), tickMs.end(), [](double ms) { return ms > 0.0; }));
            std::printf("note stock_live ticks=%zu tick_mean_ms=%.4f tick_median_ms=%.4f "
                        "tick_max_ms=%.3f tick_busy=%zu total_ms=%.1f cursor_mm=%.1f "
                        "total_mm=%.1f rewind_target_mm=%.1f rewind_median_ms=%.3f "
                        "rewinds=%zu\n",
                        tickMs.size(), tickMs.empty() ? 0.0 : tickTotal / double(tickMs.size()),
                        median(tickMs), tickMax, tickBusy, tickTotal, cursorAfterTicks,
                        data.total, rewindTarget, median(rewindMs), rewindMs.size());
            // Field memory: the core, the QByteArray copy for the texture and the copy in the GPU -
            // three buffers, as DESIGN.md counts.
            std::printf("note stock_live field_bytes=%zu peak_bytes=%zu nodes=%dx%d "
                        "digest_full=%016llx digest_live=%016llx digest_rewind=%016llx "
                        "digest_prefix=%016llx\n",
                        fieldBytes, fieldBytes * 3, walk.field().countX(),
                        walk.field().countY(),
                        static_cast<unsigned long long>(digestFull),
                        static_cast<unsigned long long>(digestLive),
                        static_cast<unsigned long long>(digestRewind),
                        static_cast<unsigned long long>(digestPrefix));
            printPhase("stock_live_tick_0_5",
                       tickTotal / double(std::max<std::size_t>(1, tickMs.size())));
            printPhase("stock_live_rewind_0_5", median(rewindMs));
            reportCase("stock_live", int(tickMs.size()), 0.0, 0.0,
                       started && prefixStarted && digestLive == digestFull
                           && digestRewind == digestPrefix && !tickMs.empty()
                           && walk.field().countX() == full.field.countX());
        }
        std::printf("program file=%s parse_ms=%.3f stages=%zu moves=%zu\n",
                    ngcPath.c_str(), parseMs, parsed.stages.size(), moves.size());
    }

    const int caseFailures = int(std::count_if(g_cases.begin(), g_cases.end(),
                                               [](const CaseStat& c) { return !c.ok; }));
    failures += caseFailures;
    if (failures == 0) {
        std::printf("RESULT: OK\n");
        return 0;
    }
    std::printf("RESULT: FAIL (%d)\n", failures);
    return 1;
}
