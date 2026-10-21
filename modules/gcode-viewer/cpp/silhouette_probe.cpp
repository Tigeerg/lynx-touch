// cpp/silhouette_probe.cpp - probe of the match between the C++ silhouette
// topology and the silhouette on fixed eyes (stage 4c of the port plan) against
// the Python oracle core/silhouette.cpp.
//
// The probe is part of neither the plugin nor the core: a separate CMake target.
// It takes the vertex soup from STEP along the same gvstep path (loadStepModel)
// as the Python load_step_model, builds the topology (vertex welding on a 1e-4 mm
// grid, edges with exactly two faces, rejection of sharp edges and degenerate
// faces) and computes the silhouette on the five fixed eyes from. Prints:
//
// verts: N vertices of the soup (oracle Test_model.STEP - 136194)
// triangles: F triangles (oracle - 45398)
// candidates: K candidate silhouette edges (oracle - 57842)
// sha256_<key>: H sha256 of the topology arrays: n, d, f0, f1, ea, eb
// eye<i>: M silhouette edges on eye i (oracle - 245, 224, 251, 509, 185)
// sha256_eye<i>: H sha256 float32 (M,2,3) - the same byte view as numpy
//
// The same arrays are written to the dump directory as files topo_*.f32/i32 and
// silhouette_eye<i>.f32: the Python check reads them with numpy and compares both
// elementwise and by hash. It drives the comparison.
//
// Usage: silhouette_probe <file.step> <dump-dir> (OCCT DLLs must be in PATH)
// silhouette_probe --manifest <oracle_manifest.json> [<file.step>]
// silhouette_probe --check [<file.step>]
// Return code: 0 - computed and written (or digests matched the oracle);
// 1 - file not read or dump not written; 2 - arguments not parsed.
//
// The --manifest/--check mode compares the digests of its arrays (topo.n, topo.d,
// topo.f0, topo.f1, topo.ea, topo.eb, silhouette.eye0..eye4) with oracle_manifest.json
// without Python and without the dump directory: prints one line per entry and
// "RESULT: OK" / "RESULT: FAIL (n)". The default path to the test data is taken
// from the repository root (the probe path).

// windows.h goes before the OCCT headers: the OCCT chain declares _WINDOWS_ as
// its guard, and windows.h included after it exposes none of its functions.
// NOMINMAX is required: the min/max macros break std::min and std::max.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "core/silhouette.h"
#include "manifest_check.h"
#include "occt/step_import.h"
#include "sha256.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Fixed eyes - the same five as in: a discrepancy here
// would shift the oracle.
const double EYES[5][3] = {{100.0, 150.0, 300.0},
                           {-200.0, 40.0, 260.0},
                           {60.0, -180.0, 120.0},
                           {240.0, 300.0, -60.0},
                           {0.0, 0.0, 400.0}};

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
            std::fprintf(stderr, "usage: silhouette_probe <file.step> <dump-dir>\n"
                                 "       silhouette_probe --manifest <oracle_manifest.json> [<file.step>]\n"
                                 "       silhouette_probe --check [<file.step>]\n");
            return 1;
        }
        stepPath = args[1];
        dumpDir = args[2];
    }

    gv::StepModel model;
    try {
        model = gv::loadStepModel(stepPath);
    } catch (const std::exception& exc) {
        std::fprintf(stderr, "silhouette_probe: %s\n", exc.what());
        return 1;
    }
    if (model.tri_count <= 0 || model.verts.size() != std::size_t(model.tri_count) * 9) {
        std::fprintf(stderr, "silhouette_probe: суп вершин не собран (%s): %zu координат "
                             "при %d треугольниках\n",
                     stepPath.c_str(), model.verts.size(), model.tri_count);
        return 1;
    }

    const gv::MeshTopology topo = gv::buildSilhouetteTopology(model.verts);
    const std::size_t k = topo.edgeCount();

    // The serializations are the same as the oracle's (numpy.tobytes): float32 LE
    // for n, d, ea, eb and the silhouettes, int32 LE for f0/f1. x86 stores them
    // little-endian.
    struct Dump {
        const char* key;        // key name in the output, in the manifest and in the file name
        const void* data;
        std::size_t count;
        std::size_t elem;
        const char* suffix;
        const char* dtype;      // serialization dtype, as in oracle_manifest.json
        long long rows;         // rows in the array shape
        int cols;               // columns: 0 - one-dimensional array
    };
    const Dump dumps[6] = {
        {"n", topo.n.data(), topo.n.size(), sizeof(float), "f32", "float32",
         static_cast<long long>(topo.faces), 3},
        {"d", topo.d.data(), topo.d.size(), sizeof(float), "f32", "float32",
         static_cast<long long>(topo.faces), 0},
        {"f0", topo.f0.data(), topo.f0.size(), sizeof(std::int32_t), "i32", "int32",
         static_cast<long long>(k), 0},
        {"f1", topo.f1.data(), topo.f1.size(), sizeof(std::int32_t), "i32", "int32",
         static_cast<long long>(k), 0},
        {"ea", topo.ea.data(), topo.ea.size(), sizeof(float), "f32", "float32",
         static_cast<long long>(k), 3},
        {"eb", topo.eb.data(), topo.eb.size(), sizeof(float), "f32", "float32",
         static_cast<long long>(k), 3},
    };

    // Comparison with the oracle: the same digests that the dump mode prints, but
    // against oracle_manifest.json and without writing files.
    if (checkMode) {
        gv::manifest::Manifest manifest;
        std::string error;
        if (!manifest.load(manifestPath, error)) {
            std::fprintf(stderr, "silhouette_probe: манифест не прочитан (%s): %s\n",
                         manifestPath.c_str(), error.c_str());
            return 1;
        }
        gv::manifest::Checker checker(manifest);
        for (const Dump& d : dumps) {
            const std::vector<unsigned char> bytes = asBytes(d.data, d.count, d.elem);
            const std::vector<long long> shape =
                d.cols > 0 ? std::vector<long long>{d.rows, d.cols}
                           : std::vector<long long>{d.rows};
            checker.check(std::string("topo.") + d.key, d.dtype, gv::sha256Hex(bytes), shape);
        }
        int eyeIndex = 0;
        for (const auto& eye : EYES) {
            const std::vector<float> segs = gv::meshSilhouetteSegments(topo, eye);
            const std::vector<unsigned char> bytes =
                asBytes(segs.data(), segs.size(), sizeof(float));
            checker.check("silhouette.eye" + std::to_string(eyeIndex), "float32",
                          gv::sha256Hex(bytes),
                          {static_cast<long long>(segs.size() / 6), 2, 3});
            ++eyeIndex;
        }
        checker.printResult();
        return checker.failures() == 0 ? 0 : 1;
    }

    std::printf("file: %s\n", stepPath.c_str());
    std::printf("verts: %zu\n", model.verts.size() / 3);
    std::printf("triangles: %zu\n", topo.faces);
    std::printf("candidates: %zu\n", k);

    const std::string sep = "/";
    bool ok = true;
    for (const Dump& d : dumps) {
        const std::vector<unsigned char> bytes = asBytes(d.data, d.count, d.elem);
        std::printf("sha256_%s: %s\n", d.key, gv::sha256Hex(bytes).c_str());
        ok = writeBytes(dumpDir + sep + "topo_" + d.key + "." + d.suffix, bytes) && ok;
    }

    int eye_index = 0;
    for (const auto& eye : EYES) {
        const std::vector<float> segs = gv::meshSilhouetteSegments(topo, eye);
        const std::vector<unsigned char> bytes =
            asBytes(segs.data(), segs.size(), sizeof(float));
        std::printf("eye%d: %zu\n", eye_index, segs.size() / 6);
        std::printf("sha256_eye%d: %s\n", eye_index, gv::sha256Hex(bytes).c_str());
        ok = writeBytes(dumpDir + sep + "silhouette_eye" + std::to_string(eye_index) + ".f32",
                        bytes) && ok;
        ++eye_index;
    }

    if (!ok) {
        std::fprintf(stderr, "silhouette_probe: не удалось записать дамп в %s\n", dumpDir.c_str());
        return 1;
    }
    return 0;
}
