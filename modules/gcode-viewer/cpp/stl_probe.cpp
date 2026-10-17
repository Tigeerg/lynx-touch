// cpp/stl_probe.cpp - пробник совпадения C++-слоя STL (этап 4b плана переноса) с
// питоновским эталоном modules/gcode-viewer/stl_import.py и outline.py.
//
// Пробник не входит ни в плагин, ни в ядро: отдельная цель CMake. Он проходит
// тот же путь, что load_stl_vertices -> _feature_edges_np: чтение STL (бинарного
// или ASCII) в суп треугольников, сварка вершин по сетке 0.001 мм, отбор рёбер по
// двугранному углу, склейка цепочек и упрощение по Дугласу-Пекеру (0.15 мм).
// Печатает:
//
//   verts: N              строк супа (эталон Test_model.STL - 562776)
//   triangles: F          треугольников (эталон - 187592)
//   sha256_soup: H        sha256 тех же байтов, что считает Python: float64 LE, (N,3)
//   segments: M           отрезков обводки после упрощения (эталон - 1435)
//   sha256_feature_edges: H  sha256 float32 LE (M,2,3)
//   bounds_lo / bounds_hi    габарит супа (_verts_bounds), мм: в оракул не входит,
//                            но сверяется с питоновским габаритом
//
// Те же массивы пишутся в каталог-дамп файлами stl_soup.f64 и
// stl_feature_edges.f32: питоновская проверка читает их numpy и сверяет
// поэлементно, а не только по хэшу. Сверку ведёт modules/gcode-viewer/test_stl_core.py.
//
// Запуск: stl_probe <файл.stl> <каталог-дампа>   (DLL не нужны: Qt и OCCT в цели нет)
//         stl_probe --manifest <oracle_manifest.json> [<файл.stl>]
//         stl_probe --check [<файл.stl>]
// Код возврата: 0 - посчитано и записано (или дайджесты сошлись с оракулом);
//               1 - файл не прочитан или дамп не записан; 2 - аргументы не разобраны.
//
// Режим --manifest/--check сверяет дайджесты своих массивов (stl.soup,
// stl.feature_edges) с oracle_manifest.json без Python и без каталога-дампа:
// печатает строку на запись и "RESULT: OK" / "RESULT: FAIL (n)". Путь к тестовым
// данным по умолчанию берётся от корня репозитория (путь пробника).

#include "core/outline.h"
#include "core/packing.h"
#include "core/path_prep.h"
#include "core/stl_import.h"
#include "manifest_check.h"
#include "sha256.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {

// Путь к тестовым данным лежит в каталоге с кириллицей ("Проект по умолчанию"):
// аргументы берутся в широком виде и переводятся в UTF-8, а файлы открываются
// через std::filesystem::u8path. Иначе файл не нашёлся бы.
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
    std::string stlPath;
    std::string dumpDir;
    if (checkMode) {
        if (!gv::manifest::resolveCheckArgs(args, "Test_model.STL", manifestPath, stlPath)) {
            return 2;
        }
    } else {
        if (args.size() < 3) {
            std::fprintf(stderr, "usage: stl_probe <file.stl> <dump-dir>\n"
                                 "       stl_probe --manifest <oracle_manifest.json> [<file.stl>]\n"
                                 "       stl_probe --check [<file.stl>]\n");
            return 1;
        }
        stlPath = args[1];
        dumpDir = args[2];
    }

    const std::vector<double> soup = gv::loadStlVertices(stlPath);
    std::ifstream opened(std::filesystem::u8path(stlPath), std::ios::binary);
    if (!opened) {
        std::fprintf(stderr, "stl_probe: файл не прочитан: %s\n", stlPath.c_str());
        return 1;
    }
    if (soup.size() % 9 != 0) {
        std::fprintf(stderr, "stl_probe: число координат %zu не кратно девяти\n", soup.size());
        return 1;
    }

    const gv::PackedEdges packed = gv::featureEdgesNp(soup, 20.0);
    const std::vector<float> segs = gv::packedPositions(packed);

    // Сериализации те же, что у оракула (numpy .tobytes()): float64 LE (N,3) и
    // float32 LE (M,2,3). x86 хранит их little-endian, порядок байтов не меняется.
    const std::vector<unsigned char> soupBytes =
        asBytes(soup.data(), soup.size(), sizeof(double));
    const std::vector<unsigned char> edgeBytes =
        asBytes(segs.data(), segs.size(), sizeof(float));

    // Сверка с оракулом: те же дайджесты, что печатает режим дампа, но против
    // oracle_manifest.json и без записи файлов.
    if (checkMode) {
        gv::manifest::Manifest manifest;
        std::string error;
        if (!manifest.load(manifestPath, error)) {
            std::fprintf(stderr, "stl_probe: манифест не прочитан (%s): %s\n",
                         manifestPath.c_str(), error.c_str());
            return 1;
        }
        gv::manifest::Checker checker(manifest);
        checker.check("stl.soup", "float64", gv::sha256Hex(soupBytes),
                      {static_cast<long long>(soup.size() / 3), 3});
        checker.check("stl.feature_edges", "float32", gv::sha256Hex(edgeBytes),
                      {static_cast<long long>(segs.size() / 6), 2, 3});
        checker.printResult();
        return checker.failures() == 0 ? 0 : 1;
    }

    const gv::Aabb box = gv::vertsBounds(soup);

    std::printf("file: %s\n", stlPath.c_str());
    std::printf("verts: %zu\n", soup.size() / 3);
    std::printf("triangles: %zu\n", soup.size() / 9);
    std::printf("sha256_soup: %s\n", gv::sha256Hex(soupBytes).c_str());
    std::printf("segments: %zu\n", segs.size() / 6);
    std::printf("sha256_feature_edges: %s\n", gv::sha256Hex(edgeBytes).c_str());
    std::printf("bounds_lo: %.17g %.17g %.17g\n", box.lo[0], box.lo[1], box.lo[2]);
    std::printf("bounds_hi: %.17g %.17g %.17g\n", box.hi[0], box.hi[1], box.hi[2]);

    const std::string sep = "/";
    if (!writeBytes(dumpDir + sep + "stl_soup.f64", soupBytes)
            || !writeBytes(dumpDir + sep + "stl_feature_edges.f32", edgeBytes)) {
        std::fprintf(stderr, "stl_probe: не удалось записать дамп в %s\n", dumpDir.c_str());
        return 1;
    }
    return 0;
}
