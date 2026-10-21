// cpp/step_probe.cpp - probe of the C++ STEP layer and normals match (stage 4a
// of the port plan): digests are checked against the oracle (step.*), and the
// normals - against the step.normals digest.
//
// The probe belongs to neither the plugin nor the core: a separate CMake target. It takes
// the same path as the Python load_step_model over the soup and normals:
// STEPControl_Reader -> BRepMesh_IncrementalMesh(0.05, angular 0.1, parallel)
// -> vertex soup taking the face location into account -> shell flip outward ->
// crease-aware normals. It prints:
//
// verts: N rows of the vertex soup (reference Test_model.STEP - 136194)
// triangles: F triangles (reference - 45398)
// sha256_verts: H sha256 of the same bytes that Python computes: float64 LE, (N,3)
// sha256_tri: H the same for (F,3,3): the bytes are the same, a different read
// sha256_normals: H sha256 float32 LE (F,3,3)
//
// The same arrays are written to the dump directory as the files step_verts.f64, step_tri.f64,
// step_normals.f32: the Python check reads them with numpy and compares element-wise,
// not only by hash. The Python check drives it.
//
// The STEP probe does not compute edges: they live in the CNC.Edges plugin
// (cpp/stepledgesgeometry.cpp) and are checked separately.
//
// Run: step_probe <file.step> <dump-dir> (OCCT DLLs must be in PATH)
// step_probe --manifest <oracle_manifest.json> [<file.step>]
// step_probe --check [<file.step>]
// Return code: 0 - computed and written (or the digests matched the oracle);
// 1 - the file was not read or the dump was not written; 2 - the arguments were not parsed.
//
// The --manifest/--check mode compares the digests of its arrays (step.verts,
// step.tri, step.normals) with oracle_manifest.json without Python and without a dump directory:
// it prints one line per entry and "RESULT: OK" / "RESULT: FAIL (n)". The path to the test
// data by default is taken from the repository root (the probe path).

// windows.h comes before the OCCT headers: the OCCT chain declares _WINDOWS_ as its
// guard, and windows.h included after it exposes none of its own functions.
// NOMINMAX is required: the min/max macros break std::min and std::max.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

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

// The path to the test data lives in a directory with Cyrillic ("Проект по умолчанию"),
// while OCCT accepts the path in UTF-8: command-line arguments are taken in wide
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

// Dump as raw bytes: the path is UTF-8 and is opened as wide on Windows - otherwise
// the Cyrillic in the directory would not reach the file system.
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

// Read the model the same way as the Python load_step_model: vertex soup,
// triangles and normals. Prints the reason for the failure and returns false - the path is
// common to the dump mode and the oracle check mode.
bool readModel(const std::string& stepPath, gv::StepModel& model) {
    try {
        model = gv::loadStepModel(stepPath);
    } catch (const std::exception& exc) {
        std::fprintf(stderr, "step_probe: %s\n", exc.what());
        return false;
    }
    const std::size_t faces = std::size_t(model.tri_count);
    if (faces == 0 || model.verts.size() != faces * 9) {
        std::fprintf(stderr, "step_probe: суп вершин не собран (%s): %zu координат "
                             "при %zu треугольниках\n",
                     stepPath.c_str(), model.verts.size(), faces);
        return false;
    }
    if (model.normals.size() != faces * 9) {
        std::fprintf(stderr, "step_probe: нормалей %zu при %zu треугольниках\n",
                     model.normals.size(), faces);
        return false;
    }
    return true;
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
            std::fprintf(stderr, "usage: step_probe <file.step> <dump-dir>\n"
                                 "       step_probe --manifest <oracle_manifest.json> [<file.step>]\n"
                                 "       step_probe --check [<file.step>]\n");
            return 1;
        }
        stepPath = args[1];
        dumpDir = args[2];
    }

    gv::StepModel model;
    if (!readModel(stepPath, model)) {
        return 1;
    }
    const std::size_t faces = std::size_t(model.tri_count);

    // Serializations are the same as the oracle's (numpy.tobytes): float64 LE (N,3) and
    // float32 LE (F,3,3). x86 stores them little-endian, the byte order does not change.
    const std::vector<unsigned char> vertsBytes =
        asBytes(model.verts.data(), model.verts.size(), sizeof(double));
    const std::vector<unsigned char> normalsBytes =
        asBytes(model.normals.data(), model.normals.size(), sizeof(float));

    // Oracle check: the same digests that the dump mode prints, but against
    // oracle_manifest.json and without writing files.
    if (checkMode) {
        gv::manifest::Manifest manifest;
        std::string error;
        if (!manifest.load(manifestPath, error)) {
            std::fprintf(stderr, "step_probe: манифест не прочитан (%s): %s\n",
                         manifestPath.c_str(), error.c_str());
            return 1;
        }
        gv::manifest::Checker checker(manifest);
        // Entry shapes are as in: soup (N,3), triangles (F,3,3),
        // normals (F,3,3); the bytes of step.tri and step.verts are the same.
        checker.check("step.verts", "float64", gv::sha256Hex(vertsBytes),
                      {static_cast<long long>(model.verts.size() / 3), 3});
        checker.check("step.tri", "float64", gv::sha256Hex(vertsBytes),
                      {static_cast<long long>(faces), 3, 3});
        checker.check("step.normals", "float32", gv::sha256Hex(normalsBytes),
                      {static_cast<long long>(faces), 3, 3});
        checker.printResult();
        return checker.failures() == 0 ? 0 : 1;
    }

    std::printf("file: %s\n", stepPath.c_str());
    std::printf("verts: %zu\n", model.verts.size() / 3);
    std::printf("triangles: %zu\n", faces);
    std::printf("sha256_verts: %s\n", gv::sha256Hex(vertsBytes).c_str());
    std::printf("sha256_tri: %s\n", gv::sha256Hex(vertsBytes).c_str());
    std::printf("sha256_normals: %s\n", gv::sha256Hex(normalsBytes).c_str());

    const std::string sep = "/";
    if (!writeBytes(dumpDir + sep + "step_verts.f64", vertsBytes)
            || !writeBytes(dumpDir + sep + "step_tri.f64", vertsBytes)
            || !writeBytes(dumpDir + sep + "step_normals.f32", normalsBytes)) {
        std::fprintf(stderr, "step_probe: не удалось записать дамп в %s\n", dumpDir.c_str());
        return 1;
    }
    return 0;
}
