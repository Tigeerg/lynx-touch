// cpp/bench_probe.cpp - measuring the cold-load phases and the frame cost on the
// C++ path.
//
// The probe is part of neither the plugin nor the core: a separate CMake target
// (like silhouette_probe). It walks the same path as the viewer runtime, step by
// step, and prints the time of each step in milliseconds (std::chrono::steady_clock):
//
// * STEP model phases: shape read, tessellation (and its split into the OCCT
// mesher and the per-face soup assembly), shell orientation, skin normals,
// B-Rep edges, surfaces of revolution, silhouette topology, tangent families,
// outline packing;
// * NC program phases: G-code parsing, path preparation, tool profile with
// holder, collision scan, tool check;
// * frame cost: meshSilhouetteSegments and tangentFromFamilies (over many runs
// on the already built topology/families), float64 -> float32 conversion and
// setting the HLRLineGeometry buffers (hlrTangents, hlrEdges).
//
// Every model phase is measured on a FRESH shape: tessellation runs only on an
// unmeshed shape, and BRep_Mesh caches the mesh on the shape. So the shape is read
// again before every phase, and the read time is printed on a separate line.
//
// Usage: bench_probe <file.step> <file.ngc> [phase_reps] [frame_reps] [level]
// level - low|med|high|safe; without the argument CNC_QUALITY is taken, otherwise med
// (OCCT and Qt DLLs must be in PATH)
// Output: "quality..." lines with the level and its tolerances, "phase <name> <ms>"
// and "frame <name> <ms>", then "RESULT: OK".
// Return code: 0 - the measurement was made; 1 - the file was not read or there
// are too few arguments.

// windows.h goes before the Qt and OCCT headers: the OCCT chain declares _WINDOWS_
// as its guard, and windows.h included after Qt exposes none of its functions.
// NOMINMAX is required: the min/max macros break std::min and std::max.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include <QGuiApplication>
#include <QStringList>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <BRepMesh_IncrementalMesh.hxx>

#include "core/collision.h"
#include "core/gcode_parser.h"
#include "core/normals.h"
#include "core/packing.h"
#include "core/path_prep.h"
#include "core/quality.h"
#include "core/silhouette.h"
#include "core/tangents.h"
#include "core/tooling.h"
#include "core/ui_tags.h"
#include "occt/step_edges.h"
#include "occt/step_import.h"
#include "occt/step_surfaces.h"

#include "qt/gv_geometry.h"

namespace {

// The frame eye - the same as in the controller: the numbers are comparable.
const double EYE[3] = {120.0, 90.0, 140.0};

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Common sink: it does not let the optimizer throw away what was computed.
volatile std::size_t g_sink = 0;

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::string toUtf8(const std::wstring& w) {
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

std::vector<std::string> probeArgs(int argc, char** argv) {
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

std::string readText(const std::string& utf8Path) {
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!fh) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());
}

void printPhase(const char* name, double ms) {
    std::printf("phase %-22s %8.3f\n", name, ms);
}

void printFrame(const char* name, double ms) {
    std::printf("frame %-22s %8.4f\n", name, ms);
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const std::vector<std::string> args = probeArgs(argc, argv);
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: bench_probe <file.step> <file.ngc> [phase_reps] "
                             "[frame_reps] [low|med|high|safe]\n");
        return 1;
    }
    const std::string stepPath = args[1];
    const std::string ngcPath = args[2];
    const int phaseReps = args.size() > 3 ? std::max(1, std::atoi(args[3].c_str())) : 3;
    const int frameReps = args.size() > 4 ? std::max(1, std::atoi(args[4].c_str())) : 30;
    // Level: the fifth argument, otherwise CNC_QUALITY, otherwise med (for med the
    // numbers are the same as before the presets).
    const gv::QualityPreset quality =
        args.size() > 5 ? gv::presetFor(args[5].c_str()) : gv::presetFromEnv();

    // The level and its tolerances - before the measurements: this line shows what
    // was used to measure.
    std::printf("quality level=%s step_deflection=%.4g edge_deflection=%.4g "
                "coalesce_dist=%.4g coalesce_gap=%.4g weld=%.4g crease=%.4g "
                "silhouette_max_edge=%.4g silhouette_min_area=%.4g "
                "outline_edge=%.4g outline_simplify=%.4g cone_min_slant=%.4g "
                "cone_azimuths=%d silhouette_arc=%.4g silhouette_min_span=%.4g "
                "drop_overlap=%.4g decimate=%.4g arc_decimate=%.4g collision_step=%.4g\n",
                quality.level, quality.stepDeflectionMm, quality.edgeDeflectionMm,
                quality.coalesceDistTolMm, quality.coalesceGapTolMm,
                quality.silhouetteWeldMm, quality.creaseDeg,
                quality.silhouetteMaxEdgeDeg, quality.silhouetteMinFaceAreaMm2,
                quality.outlineFeatureEdgeDeg, quality.outlineFeatureSimplifyMm,
                quality.coneMinSlantMm, quality.coneProbeAzimuths,
                quality.silhouetteArcDeg, quality.silhouetteMinSpanMm,
                quality.dropOverlapMm, quality.decimateTolMm,
                quality.arcDecimateTolMm, quality.collisionStepMm);

    // ---- STEP model phases --------------------------------------------------
    // Keep the last result so that the frame measurements run on it as well.
    std::vector<double> soup;
    std::vector<double> verts;
    std::vector<float> normals;
    gv::StepSurfaceList surfaces;
    gv::MeshTopology topo;
    gv::TangentFamilies fams;
    gv::PackedEdges packed;

    std::vector<double> tRead;
    std::vector<double> tTess;
    std::vector<double> tOrient;
    std::vector<double> tNormals;
    std::vector<double> tRawEdges;
    std::vector<double> tCoalesce;
    std::vector<double> tSurfaces;
    std::vector<double> tTopo;
    std::vector<double> tFamilies;

    for (int r = 0; r < phaseReps; ++r) {
        Clock::time_point t0 = Clock::now();
        const TopoDS_Shape shape = gv::readStepShape(stepPath);
        tRead.push_back(msSince(t0));

        t0 = Clock::now();
        soup = gv::tessellateSoup(shape, quality.stepDeflectionMm);
        tTess.push_back(msSince(t0));

        t0 = Clock::now();
        verts = gv::orientShellOutward(soup);
        tOrient.push_back(msSince(t0));

        t0 = Clock::now();
        normals = gv::smoothFaceNormals(verts, quality.creaseDeg);
        tNormals.push_back(msSince(t0));

        t0 = Clock::now();
        const std::vector<gv::EdgeSegment> rawSegs =
            gv::stepRawSegments(shape, quality.edgeDeflectionMm, true);
        tRawEdges.push_back(msSince(t0));

        t0 = Clock::now();
        const std::vector<gv::EdgeSegment> merged =
            gv::coalesceStepSegments(rawSegs, quality.coalesceDistTolMm,
                                     quality.coalesceGapTolMm);
        std::vector<double> p0(merged.size() * 3);
        std::vector<double> p1(merged.size() * 3);
        for (std::size_t i = 0; i < merged.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                p0[i * 3 + k] = merged[i].a[k];
                p1[i * 3 + k] = merged[i].b[k];
            }
        }
        packed = gv::packEdgesNp(p0, p1);
        tCoalesce.push_back(msSince(t0));

        t0 = Clock::now();
        surfaces = gv::loadStepSurfaces(shape, quality.stepDeflectionMm);
        tSurfaces.push_back(msSince(t0));

        t0 = Clock::now();
        topo = gv::buildSilhouetteTopology(verts, quality);
        tTopo.push_back(msSince(t0));

        t0 = Clock::now();
        const gv::SkinTriangles skin = gv::buildSkinTriangles(verts, normals);
        fams = gv::buildTangentFamilies(surfaces, &packed, &skin, quality);
        tFamilies.push_back(msSince(t0));

        g_sink += soup.size() + normals.size() + topo.edgeCount();
    }

    printPhase("step_read", median(tRead));
    printPhase("step_tessellate", median(tTess));
    printPhase("step_orient_shell", median(tOrient));
    printPhase("step_normals_skin", median(tNormals));
    printPhase("step_raw_edges", median(tRawEdges));
    printPhase("step_coalesce_pack", median(tCoalesce));
    printPhase("step_surfaces", median(tSurfaces));
    printPhase("step_topo", median(tTopo));
    printPhase("step_families", median(tFamilies));

    // loadStepSurfaces on a FRESH (unmeshed) shape: the difference with step_surfaces
    // shows what tessellation would have cost in that phase. The loadStepBundleAll
    // path skips it (shapeAlreadyMeshed=true).
    {
        std::vector<double> tCold;
        for (int r = 0; r < phaseReps; ++r) {
            const TopoDS_Shape shape2 = gv::readStepShape(stepPath);
            Clock::time_point t0 = Clock::now();
            const gv::StepSurfaceList s2 =
                gv::loadStepSurfaces(shape2, quality.stepDeflectionMm, false);
            tCold.push_back(msSince(t0));
            g_sink += s2.size();
        }
        printPhase("step_surfaces_cold", median(tCold));
    }

    // Tessellation split: how much of it is the OCCT mesher and how much is the
    // per-face soup assembly (tessellateSoup). The mesher is measured on a fresh
    // shape, the assembly - by the same call on an already meshed shape
    // (shapeAlreadyMeshed), so step_mesh + step_soup = step_tessellate.
    {
        std::vector<double> tMesh;
        std::vector<double> tSoup;
        for (int r = 0; r < phaseReps; ++r) {
            const TopoDS_Shape shape2 = gv::readStepShape(stepPath);
            Clock::time_point t0 = Clock::now();
            BRepMesh_IncrementalMesh mesher(shape2, quality.stepDeflectionMm, false,
                                            quality.stepDeflectionMm * 2.0, true);
            mesher.Perform();
            tMesh.push_back(msSince(t0));
            t0 = Clock::now();
            const std::vector<double> s2 =
                gv::tessellateSoup(shape2, quality.stepDeflectionMm, true);
            tSoup.push_back(msSince(t0));
            g_sink += s2.size();
        }
        printPhase("step_mesh", median(tMesh));
        printPhase("step_soup", median(tSoup));
    }

    // Cost of one materialization of the soup and the normals: that many copies
    // (2b) were removed from the STEP -> soup -> normals -> buffers path.
    {
        std::vector<double> tCopySoup;
        std::vector<double> tCopyNormals;
        for (int r = 0; r < frameReps; ++r) {
            Clock::time_point t0 = Clock::now();
            std::vector<double> c = verts;
            tCopySoup.push_back(msSince(t0));
            g_sink += c.size();
            Clock::time_point t1 = Clock::now();
            std::vector<float> cn = normals;
            tCopyNormals.push_back(msSince(t1));
            g_sink += cn.size();
        }
        std::printf("copy soup_bytes=%zu med_ms=%.4f\n",
                    verts.size() * sizeof(double), median(tCopySoup));
        std::printf("copy normals_bytes=%zu med_ms=%.4f\n",
                    normals.size() * sizeof(float), median(tCopyNormals));
    }

    // ---- NC program phases --------------------------------------------------
    const std::string text = readText(ngcPath);
    if (text.empty()) {
        std::fprintf(stderr, "bench_probe: УП не прочитана: %s\n", ngcPath.c_str());
        return 1;
    }
    std::vector<double> tParse;
    std::vector<double> tPrep;
    gv::ParseResult parsed;
    gv::PathData pd;
    for (int r = 0; r < phaseReps; ++r) {
        Clock::time_point t0 = Clock::now();
        parsed = gv::parseGcode(text);
        tParse.push_back(msSince(t0));
        t0 = Clock::now();
        pd = gv::prepareOrdered(parsed.ordered, parsed.stages);
        tPrep.push_back(msSince(t0));
    }
    printPhase("ngc_parse", median(tParse));
    printPhase("ngc_prepare_path", median(tPrep));

    // Tool/holder: profile, outline, rings (constant holder + the first tool of the
    // stage), then the collision scan and the tool check.
    std::vector<double> tTool;
    for (int r = 0; r < phaseReps; ++r) {
        Clock::time_point t0 = Clock::now();
        const std::vector<gv::ProfilePoint> hp = gv::holderSolidProfile();
        const std::vector<gv::ProfilePoint> hop = gv::holderOutlineProfile();
        const std::vector<gv::ProfilePoint> ho = gv::toolOutlineProfile(hop, 0.1);
        const std::vector<gv::RingSegment> hr = gv::ringsSegments(gv::profileRingPoints(hp));
        g_sink += hp.size() + hop.size() + ho.size() + hr.size();
        if (!parsed.stages.empty() && parsed.stages.front().info.present) {
            const std::vector<gv::ProfilePoint> tp = gv::toolProfile(parsed.stages.front().info);
            g_sink += tp.size();
        }
        tTool.push_back(msSince(t0));
    }
    printPhase("tool_holder", median(tTool));

    std::vector<double> tCollision;
    std::vector<double> tToolCheck;
    const std::optional<std::array<double, 6>> stock = gv::parseUiStock(text);
    for (int r = 0; r < phaseReps; ++r) {
        if (stock) {
            std::vector<double> starts;
            std::vector<double> lens;
            for (const gv::StageRange& st : pd.stages) {
                starts.push_back(st.start * pd.total);
            }
            for (const gv::Stage& st : parsed.stages) {
                lens.push_back(gv::toolExposedLength(st.info));
            }
            while (lens.size() < starts.size()) {
                lens.push_back(lens.empty() ? 0.0 : lens.back());
            }
            if (lens.empty()) {
                lens.push_back(0.0);
            }
            Clock::time_point t0 = Clock::now();
            const std::vector<gv::CollisionRange> ranges = gv::scanCollisions(
                pd.pts, pd.cum, pd.total, starts, lens, *stock, gv::collisionStep());
            tCollision.push_back(msSince(t0));
            g_sink += ranges.size();
        } else {
            tCollision.push_back(0.0);
        }

        std::vector<gv::ToolInfo> specs;
        for (const gv::Stage& st : parsed.stages) {
            specs.push_back(st.info);
        }
        std::map<int, gv::MachineTool> tools;
        gv::MachineTool m1;
        m1.length = 10.0;
        m1.name = "T1";
        tools[1] = m1;
        Clock::time_point t1 = Clock::now();
        const std::vector<gv::ToolCheckRow> rows = gv::toolCheckReport(
            specs, pd.stages, pd.pts, pd.cum, pd.total, tools, gv::MachineLimits{});
        tToolCheck.push_back(msSince(t1));
        g_sink += rows.size();
    }
    printPhase("collision_scan", median(tCollision));
    printPhase("tool_check", median(tToolCheck));

    // ---- frame cost ---------------------------------------------------------
    // mesh silhouette: on the already built topology we repeat the refreshTangents
    // part (only meshSilhouetteSegments). The frame buffers are the same as in the
    // controller: the silhouette is filled into a reusable scratch, not allocated anew.
    const double eye[3] = {EYE[0], EYE[1], EYE[2]};
    std::vector<float> silSegs;
    {
        gv::MeshSilhouetteScratch scratch;
        const std::vector<float> *sil = nullptr;
        Clock::time_point t0 = Clock::now();
        for (int r = 0; r < frameReps; ++r) {
            sil = &gv::meshSilhouetteSegments(topo, eye, scratch);
        }
        g_sink += (sil != nullptr) ? sil->size() : 0;
        printFrame("refresh_mesh_silhouette", msSince(t0) / frameReps);
        if (sil != nullptr) {
            silSegs = *sil;               // not timed: the ribbon buffer needs the content
        }
    }

    // analytic: tangentFromFamilies + float64 -> float32 conversion (like the
    // controller), also in reusable frame buffers.
    {
        gv::TangentScratch scratch;
        std::vector<float> segs;
        Clock::time_point t0 = Clock::now();
        for (int r = 0; r < frameReps; ++r) {
            const std::vector<double> &flat = gv::tangentFromFamilies(fams, eye, scratch);
            segs.clear();
            segs.reserve(flat.size());
            for (double v : flat) {
                segs.push_back(float(v));
            }
            g_sink += segs.size();
        }
        printFrame("refresh_analytic", msSince(t0) / frameReps);
    }

    // Setting the hlrTangents buffer (silhouette ribbons, stride 48).
    {
        gvgeom::HLRLineGeometry hlr;
        const double center[3] = {0.0, 0.0, 0.0};
        const int count = int(silSegs.size() / 6);
        Clock::time_point t0 = Clock::now();
        for (int r = 0; r < frameReps; ++r) {
            hlr.setReference(center);
            hlr.setSegments(silSegs.empty() ? nullptr : silSegs.data(), count);
        }
        g_sink += std::size_t(hlr.vertexCount());
        printFrame("setSegments_tangents", msSince(t0) / frameReps);
    }

    // Setting the hlrEdges buffer: positions = packedPositions(outline) + setSegments.
    {
        const std::vector<float> positions = gv::packedPositions(packed);
        const int count = int(positions.size() / 6);
        gvgeom::HLRLineGeometry hlr;
        const double center[3] = {0.0, 0.0, 0.0};
        Clock::time_point t0 = Clock::now();
        for (int r = 0; r < frameReps; ++r) {
            hlr.setReference(center);
            hlr.setSegments(positions.empty() ? nullptr : positions.data(), count);
        }
        g_sink += std::size_t(hlr.vertexCount());
        printFrame("setSegments_edges", msSince(t0) / frameReps);

        Clock::time_point t1 = Clock::now();
        for (int r = 0; r < frameReps; ++r) {
            const std::vector<float> again = gv::packedPositions(packed);
            g_sink += again.size();
        }
        printFrame("packedPositions", msSince(t1) / frameReps);

        // The segs -> a copy removed from HLRLineGeometry::setSegments (2c).
        std::vector<double> tCopy;
        for (int r = 0; r < frameReps; ++r) {
            Clock::time_point t2 = Clock::now();
            const std::vector<float> a(positions.data(), positions.data() + positions.size());
            tCopy.push_back(msSince(t2));
            g_sink += a.size();
        }
        std::printf("copy segs_bytes=%zu med_ms=%.4f\n",
                    positions.size() * sizeof(float), median(tCopy));
    }

    std::printf("sizes verts=%zu faces=%zu candidates=%zu edge_segs=%d tan_fams=%zu\n",
                verts.size() / 3, topo.faces, topo.edgeCount(), packed.nv / 2,
                fams.cylinders.count);
    std::printf("RESULT: OK (sink=%zu)\n", g_sink);
    return 0;
}
