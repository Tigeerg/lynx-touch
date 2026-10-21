// cpp/tangents_probe.cpp - probe of the match between C++ analytical STEP
// surfaces and tangent families (stage 4d of the port plan) against the Python
// oracle occt/step_surfaces.cpp and core/tangents.cpp.
//
// The probe is part of neither the plugin nor the core: a separate CMake target.
// It follows the same path as the Python load_step_model + load_step_surfaces +
// _tangent_families:
// * STEPControl_Reader -> vertex soup via gvstep (skin for the cone policy);
// * analytical surfaces of revolution from the same shape (gvstep);
// * packed STEP edges (float32, 5 per vertex) are built by the probe itself from
// the same shape along the same path as the runtime (cpp/qmlbackend/gv_toolpath.cpp,
// packStepEdges): gvstep::stepRawSegments -> coalesceStepSegments ->
// gvcore::packEdgesNp. The numbers of the edges themselves have already been
// verified against the oracle
//. Next to it in the dump lies packed_edges.f32
// of the Python check, and while the file exists the families take it - so the
// comparison stays independent; the probe prints its own buffer and
// dumps it next to it (packed_self.f32), so that both sha256 are visible in one
// run;
// * tangent families (gvcore) and segments on five fixed eyes.
//
// Prints the numbers and sha256 of the same serializations that Python computes
// (numpy.tobytes): float64 LE for the number arrays, uint8 LE for the boolean
// masks, int32 LE for the surface kind. The same arrays are written as a dump;
// the oracle's tangents.* digests drive the comparison (--manifest key).
//
// Usage: tangents_probe <file.step> <dump-dir> (OCCT DLLs must be in PATH)
// tangents_probe --manifest <oracle_manifest.json> [<file.step>]
// tangents_probe --check [<file.step>]
// Return code: 0 - computed and written (or digests matched the oracle);
// 1 - file not read or dump not written; 2 - arguments not parsed.
//
// The --manifest/--check mode compares the digests of its arrays (tangents.*
// entries) with oracle_manifest.json without Python and without the dump
// directory: prints one line per entry and "RESULT: OK" / "RESULT: FAIL (n)".
// The default path to the test data is taken from the repository root (the probe
// path).
//
// Caveat about the input of this comparison: the families are computed from the
// probe's own edge buffer, and it does not match the Python packed_edges.f32
// byte for byte. On Test_model.STEP: of 24260 float32 values, 276 differ, the
// number of differing segment positions is 80 - 64 segments sit at a foreign
// position (the set of segments is the same), 16 differ in the low bits (values
// of order 5.7e-14, maximum |dx| = 1.8e-15 mm). Therefore the values of the
// entries are taken from the Python input
//, and the probe compares its own against them: all 22 arrays give one and
// the same sha256 with either of the two buffers. The equality of the inputs is
// checked separately
// - it puts its own packed_edges.f32 next to it.
// sha256 of the buffers (full values): the Python one
// 4eff01c23ff4a1fbe8058db6bc2f425efe294408d7ebc55e3e7e7eb651a988fb,
// its own 926b383602ee6c08bc546ed8ad99b334fe33a25ca5cf75b5b389d1b9d64f857f.

// windows.h goes before the OCCT headers: the OCCT chain declares _WINDOWS_ as
// its guard, and windows.h included after it exposes none of its functions.
// NOMINMAX is required: the min/max macros break std::min and std::max.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "core/packing.h"
#include "core/quality.h"
#include "core/tangents.h"
#include "manifest_check.h"
#include "occt/step_edges.h"
#include "occt/step_import.h"
#include "occt/step_surfaces.h"
#include "sha256.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

// Fixed eyes - the same five as in silhouette_probe:
// a discrepancy here would shift the oracle of the frame pass.
const double EYES[5][3] = {{100.0, 150.0, 300.0},
                           {-200.0, 40.0, 260.0},
                           {60.0, -180.0, 120.0},
                           {240.0, 300.0, -60.0},
                           {0.0, 0.0, 400.0}};

const double NAN_ = std::numeric_limits<double>::quiet_NaN();

// The path to the test data lies in a directory with Cyrillic ("Проект по умолчанию"),
// and OCCT accepts the path in UTF-8: command-line arguments are taken in wide
// form and converted to UTF-8. Otherwise the file would not be found at all.
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

// Dump as raw bytes: the path in UTF-8, opened as wide on Windows - otherwise
// the Cyrillic in the directory would not reach the filesystem.
bool writeBytes(const std::string& utf8Path, const std::vector<unsigned char>& bytes) {
    std::ofstream fh(std::filesystem::u8path(utf8Path), std::ios::binary | std::ios::trunc);
    if (!fh) {
        return false;
    }
    if (!bytes.empty()) {
        fh.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    }
    return fh.good();
}

std::vector<unsigned char> asBytes(const void* data, std::size_t count, std::size_t elem) {
    const std::size_t size = count * elem;
    std::vector<unsigned char> out(size);
    if (size > 0) {
        std::memcpy(out.data(), data, size);
    }
    return out;
}

// Input of the check: packed STEP edges (float32, 5 per vertex, vertex pairs -
// segments). If the file is absent, the probe runs without DROP marks, like the
// oracle with packed_edges=None.
bool readPackedEdges(const std::string& utf8Path, gv::PackedEdges& out) {
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!fh) {
        return false;
    }
    fh.seekg(0, std::ios::end);
    const std::streamoff size = fh.tellg();
    fh.seekg(0, std::ios::beg);
    if (size <= 0) {
        return true;                 // empty file - empty buffer
    }
    out.raw.assign(static_cast<std::size_t>(size), 0);
    fh.read(reinterpret_cast<char*>(out.raw.data()), size);
    out.nv = static_cast<int>(out.raw.size() / (5 * sizeof(float)));
    return true;
}

// Packed STEP edges from the shape itself: a repeat of the runtime's
// packStepEdges (cpp/qmlbackend/gv_toolpath.cpp) - raw B-rep edges, coalescing
// and packing. The tolerances are the defaults of the med preset (0.05 mm along
// the generatrix, coalescing 0.12 and 0.5 mm): the same numbers as the defaults
// of the Python _coalesce_segments. The preset is taken as the default rather
// than from CNC_QUALITY: otherwise the manifest would depend on the run
// environment.
gv::PackedEdges packedFromStep(const TopoDS_Shape& shape) {
    const gv::QualityPreset quality{};
    const std::vector<gv::EdgeSegment> raw =
        gv::stepRawSegments(shape, quality.edgeDeflectionMm, true);
    const std::vector<gv::EdgeSegment> merged =
        gv::coalesceStepSegments(raw, quality.coalesceDistTolMm, quality.coalesceGapTolMm);
    std::vector<double> p0(merged.size() * 3);
    std::vector<double> p1(merged.size() * 3);
    for (std::size_t i = 0; i < merged.size(); ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            p0[i * 3 + k] = merged[i].a[k];
            p1[i * 3 + k] = merged[i].b[k];
        }
    }
    return gv::packEdgesNp(p0, p1);
}

// Dump: key in the output, bytes, file name and serialization. dtype and shape
// are needed by the check mode: a manifest entry is "tangents." + key, the dtype
// and the array shape. The edge buffer is not part of the manifest: it is the
// input, not the result.
struct Dump {
    std::string key;
    std::string dtype;
    std::vector<long long> shape;
    std::vector<unsigned char> bytes;
    std::string file;
    bool manifestEntry = true;
};

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = probeArgs(argc, argv);
    const bool checkMode = args.size() >= 2
            && (args[1] == "--manifest" || args[1] == "--check");
    std::string manifestPath;
    std::string stepPath;
    std::string dumpDir;
    if (checkMode) {
        if (!gv::manifest::resolveCheckArgs(args, "Test_model.STEP", manifestPath, stepPath)) {
            return 2;
        }
    } else {
        if (args.size() < 3) {
            std::fprintf(stderr, "usage: tangents_probe <file.step> <dump-dir>\n"
                                 "       tangents_probe --manifest <oracle_manifest.json> [<file.step>]\n"
                                 "       tangents_probe --check [<file.step>]\n");
            return 1;
        }
        stepPath = args[1];
        dumpDir = args[2];
    }
    const std::string sep = "/";

    gv::StepModel model;
    gv::StepSurfaceList surfs;
    TopoDS_Shape shape;
    try {
        shape = gv::readStepShape(stepPath);
        model = gv::loadStepModel(shape);
        surfs = gv::loadStepSurfaces(shape);
    } catch (const std::exception& exc) {
        std::fprintf(stderr, "tangents_probe: %s\n", exc.what());
        return 1;
    }
    if (model.tri_count <= 0 || model.normals.size() != std::size_t(model.tri_count) * 9) {
        std::fprintf(stderr, "tangents_probe: кожа не собрана (%s)\n", stepPath.c_str());
        return 1;
    }

    // -- surfaces of revolution --------------------------------------------------
    std::vector<std::int32_t> kind;
    std::vector<double> cylRows;      // (NC,17): o, d, e1, e2, r, t0, t1, a0, a1
    std::vector<double> coneRows;     // (CK,9): a, d, psi, s0, s1
    for (const gv::StepSurface& s : surfs) {
        if (s.is_cylinder) {
            kind.push_back(0);
            const gv::CylinderSurface& c = s.cyl;
            cylRows.insert(cylRows.end(), c.o, c.o + 3);
            cylRows.insert(cylRows.end(), c.d, c.d + 3);
            cylRows.insert(cylRows.end(), c.e1, c.e1 + 3);
            cylRows.insert(cylRows.end(), c.e2, c.e2 + 3);
            cylRows.push_back(c.r);
            cylRows.push_back(c.t0);
            cylRows.push_back(c.t1);
            cylRows.push_back(c.has_arc ? c.arc0 : NAN_);
            cylRows.push_back(c.has_arc ? c.arc1 : NAN_);
        } else {
            kind.push_back(1);
            const gv::ConeSurface& c = s.cone;
            coneRows.insert(coneRows.end(), c.a, c.a + 3);
            coneRows.insert(coneRows.end(), c.d, c.d + 3);
            coneRows.push_back(c.psi);
            coneRows.push_back(c.s0);
            coneRows.push_back(c.s1);
        }
    }
    const std::size_t nConeSurf =
        static_cast<std::size_t>(std::count(kind.begin(), kind.end(), std::int32_t(1)));
    const std::size_t nCylSurf = kind.size() - nConeSurf;

    // -- packed STEP edges and tangent families --------------------------------
    // The probe builds the edge buffer itself from the same shape: it uses it to
    // compare against the Python check's packed_edges.f32. In dump mode the input
    // of the families remains the check file, if it exists (otherwise there are
    // no DROP marks at all); in manifest check mode the input is its own buffer
    // (why it is suitable even though it does not match byte for byte is the
    // caveat in the file header).
    const gv::PackedEdges selfPacked = packedFromStep(shape);
    gv::PackedEdges filePacked;
    const bool haveFile = !dumpDir.empty()
            && readPackedEdges(dumpDir + sep + "packed_edges.f32", filePacked);
    const gv::PackedEdges* inputPacked = haveFile ? &filePacked
                                                  : (checkMode ? &selfPacked : nullptr);

    const gv::SkinTriangles skin = gv::buildSkinTriangles(model.verts, model.normals);
    const gv::TangentFamilies fam = gv::buildTangentFamilies(surfs, inputPacked, &skin);
    const gv::CylinderFamilies& cy = fam.cylinders;
    std::size_t nfull = 0;
    for (std::uint8_t f : cy.FULL) {
        nfull += f ? 1 : 0;
    }

    // cone columns: a(3), d(3), psi, s0, s1 - nine numbers per cone
    std::vector<double> famCone(fam.cones.size() * 9);
    for (std::size_t i = 0; i < fam.cones.size(); ++i) {
        const gv::ConeSurface& c = fam.cones[i];
        for (int k = 0; k < 3; ++k) {
            famCone[i * 9 + std::size_t(k)] = c.a[k];
            famCone[i * 9 + 3 + std::size_t(k)] = c.d[k];
        }
        famCone[i * 9 + 6] = c.psi;
        famCone[i * 9 + 7] = c.s0;
        famCone[i * 9 + 8] = c.s1;
    }

    std::vector<std::vector<double>> tangentSegs;
    tangentSegs.reserve(5);
    for (const auto& eye : EYES) {
        tangentSegs.push_back(gv::tangentFromFamilies(fam, eye));
    }

    if (!checkMode) {
        std::printf("file: %s\n", stepPath.c_str());
        std::printf("surfaces: %zu\n", surfs.size());
        std::printf("cylinders: %zu\n", nCylSurf);
        std::printf("cones: %zu\n", nConeSurf);
        std::printf("packed_edges: %d\n", haveFile ? filePacked.nv : 0);
        std::printf("packed_self: %d\n", selfPacked.nv);
        if (haveFile) {
            std::printf("sha256_packed_edges: %s\n", gv::sha256Hex(filePacked.raw).c_str());
        }
        std::printf("families: %zu\n", cy.count);
        std::printf("full: %zu\n", nfull);
        std::printf("tangent_cones: %zu\n", fam.cones.size());
    }

    // Dumps: the same serializations as the oracle's (numpy.tobytes) - float64 LE
    // for the numbers, uint8 LE for the boolean masks, int32 LE for the surface
    // kind. The record shape is the same as in: the list of surfaces (N,), the
    // columns (NC,17) and (CK,9), the families
    // (count,3)/(count,)/(count,8)/(count,64), the segments (M,2,3).
    std::vector<Dump> dumps;
    auto addDump = [&dumps](const std::string& key, const std::string& file, const char* dtype,
                            std::vector<long long> shape, const void* data, std::size_t count,
                            std::size_t elem) {
        Dump d;
        d.key = key;
        d.file = file;
        d.dtype = dtype;
        d.shape = std::move(shape);
        d.bytes = asBytes(data, count, elem);
        dumps.push_back(std::move(d));
    };
    const long long famCount = static_cast<long long>(cy.count);
    const long long coneFamCount = static_cast<long long>(fam.cones.size());
    addDump("surfs_kind", "surfs_kind.i32", "int32",
            {static_cast<long long>(kind.size())},
            kind.data(), kind.size(), sizeof(std::int32_t));
    addDump("surfs_cyl", "surfs_cyl.f64", "float64",
            {static_cast<long long>(nCylSurf), 17},
            cylRows.data(), cylRows.size(), sizeof(double));
    addDump("surfs_cone", "surfs_cone.f64", "float64",
            {static_cast<long long>(nConeSurf), 9},
            coneRows.data(), coneRows.size(), sizeof(double));
    addDump("fam_O", "fam_O.f64", "float64", {famCount, 3},
            cy.O.data(), cy.O.size(), sizeof(double));
    addDump("fam_D", "fam_D.f64", "float64", {famCount, 3},
            cy.D.data(), cy.D.size(), sizeof(double));
    addDump("fam_E1", "fam_E1.f64", "float64", {famCount, 3},
            cy.E1.data(), cy.E1.size(), sizeof(double));
    addDump("fam_E2", "fam_E2.f64", "float64", {famCount, 3},
            cy.E2.data(), cy.E2.size(), sizeof(double));
    addDump("fam_R", "fam_R.f64", "float64", {famCount},
            cy.R.data(), cy.R.size(), sizeof(double));
    addDump("fam_T0", "fam_T0.f64", "float64", {famCount},
            cy.T0.data(), cy.T0.size(), sizeof(double));
    addDump("fam_T1", "fam_T1.f64", "float64", {famCount},
            cy.T1.data(), cy.T1.size(), sizeof(double));
    addDump("fam_RREF", "fam_RREF.f64", "float64", {famCount},
            cy.RREF.data(), cy.RREF.size(), sizeof(double));
    addDump("fam_A0", "fam_A0.f64", "float64", {famCount},
            cy.A0.data(), cy.A0.size(), sizeof(double));
    addDump("fam_A1", "fam_A1.f64", "float64", {famCount},
            cy.A1.data(), cy.A1.size(), sizeof(double));
    addDump("fam_FULL", "fam_FULL.u8", "uint8", {famCount},
            cy.FULL.data(), cy.FULL.size(), sizeof(std::uint8_t));
    addDump("fam_DROP", "fam_DROP.f64", "float64", {famCount, 8},
            cy.DROP.data(), cy.DROP.size(), sizeof(double));
    addDump("fam_ARC", "fam_ARC.u8", "uint8", {famCount, 64},
            cy.ARC.data(), cy.ARC.size(), sizeof(std::uint8_t));
    addDump("cone", "cone.f64", "float64", {coneFamCount, 9},
            famCone.data(), famCone.size(), sizeof(double));

    // The edge buffer built by the probe itself: the Python side uses it to
    // compare against packed_edges.f32 byte by byte (sha256). It is not part of
    // the manifest: it is the input of the check, not its result.
    addDump("packed_self", "packed_self.f32", "uint8",
            {static_cast<long long>(selfPacked.raw.size())},
            selfPacked.raw.data(), selfPacked.raw.size(), 1);
    dumps.back().manifestEntry = false;

    for (std::size_t i = 0; i < tangentSegs.size(); ++i) {
        const std::string key = "tangent" + std::to_string(i);
        if (!checkMode) {
            std::printf("%s: %zu\n", key.c_str(), tangentSegs[i].size() / 6);
        }
        addDump(key, key + ".f64", "float64",
                {static_cast<long long>(tangentSegs[i].size() / 6), 2, 3},
                tangentSegs[i].data(), tangentSegs[i].size(), sizeof(double));
    }

    // Check mode: the same serializations against oracle_manifest.json, without
    // writing files. Entries - "tangents." + key; the edge buffer is skipped.
    if (checkMode) {
        gv::manifest::Manifest manifest;
        std::string error;
        if (!manifest.load(manifestPath, error)) {
            std::fprintf(stderr, "tangents_probe: манифест не прочитан (%s): %s\n",
                         manifestPath.c_str(), error.c_str());
            return 1;
        }
        gv::manifest::Checker checker(manifest);
        for (const Dump& d : dumps) {
            if (!d.manifestEntry) {
                continue;
            }
            checker.check("tangents." + d.key, d.dtype, gv::sha256Hex(d.bytes), d.shape);
        }
        checker.printResult();
        return checker.failures() == 0 ? 0 : 1;
    }

    bool ok = true;
    for (const Dump& d : dumps) {
        std::printf("sha256_%s: %s\n", d.key.c_str(), gv::sha256Hex(d.bytes).c_str());
        ok = writeBytes(dumpDir + sep + d.file, d.bytes) && ok;
    }
    if (!ok) {
        std::fprintf(stderr, "tangents_probe: не удалось записать дамп в %s\n", dumpDir.c_str());
        return 1;
    }
    return 0;
}
