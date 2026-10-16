// cpp/silhouette_probe.cpp - пробник совпадения C++-топологии силуэта и силуэта
// на фиксированных глазах (этап 4c плана переноса) с питоновским эталоном
// modules/gcode-viewer/silhouette.py.
//
// Пробник не входит ни в плагин, ни в ядро: отдельная цель CMake. Он берёт суп
// вершин из STEP тем же путём gvstep (loadStepModel), что питоновский
// load_step_model, строит топологию (сварка вершин по сетке 1e-4 мм, рёбра ровно
// с двумя гранями, отсев острых кромок и вырожденных граней) и считает силуэт на
// пяти фиксированных глазах из oracle_dump.py. Печатает:
//
//   verts: N              вершин супа (эталон Test_model.STEP - 136194)
//   triangles: F          треугольников (эталон - 45398)
//   candidates: K         кандидатных силуэтных рёбер (эталон - 57842)
//   sha256_<ключ>: H      sha256 массивов топологии: n, d, f0, f1, ea, eb
//   eye<i>: M             рёбер силуэта на глазу i (эталон - 245, 224, 251, 509, 185)
//   sha256_eye<i>: H      sha256 float32 (M,2,3) - тот же байтовый вид, что у numpy
//
// Те же массивы пишутся в каталог-дамп файлами topo_*.f32/i32 и
// silhouette_eye<i>.f32: питоновская проверка читает их numpy и сверяет и
// поэлементно, и по хэшу. Сверку ведёт modules/gcode-viewer/test_silhouette_core.py.
//
// Запуск: silhouette_probe <файл.step> <каталог-дампа>  (нужны DLL OCCT в PATH)
// Код возврата: 0 - посчитано и записано; 1 - файл не прочитан или дамп не записан.

// windows.h идёт до заголовков OCCT: цепочка OCCT объявляет _WINDOWS_ своим
// стражем, и включённый после неё windows.h не даёт ни одной своей функции.
// NOMINMAX обязателен: min/max макросами ломают std::min и std::max.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "core/silhouette.h"
#include "occt/step_import.h"
#include "sha256.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Фиксированные глаза - те же пять, что в oracle_dump.py: расхождение здесь
// сдвинуло бы эталон.
const double EYES[5][3] = {{100.0, 150.0, 300.0},
                           {-200.0, 40.0, 260.0},
                           {60.0, -180.0, 120.0},
                           {240.0, 300.0, -60.0},
                           {0.0, 0.0, 400.0}};

// Путь к тестовым данным лежит в каталоге с кириллицей ("Проект по умолчанию"),
// а OCCT принимает путь в UTF-8: аргументы командной строки берутся в широком
// виде и переводятся в UTF-8. Иначе файл не нашёлся бы вовсе.
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

// Дамп сырыми байтами: путь в UTF-8, открывается как wide на Windows - иначе
// кириллица в каталоге не доехала бы до файловой системы.
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
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: silhouette_probe <file.step> <dump-dir>\n");
        return 1;
    }
    const std::string stepPath = args[1];
    const std::string dumpDir = args[2];

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

    std::printf("file: %s\n", stepPath.c_str());
    std::printf("verts: %zu\n", model.verts.size() / 3);
    std::printf("triangles: %zu\n", topo.faces);
    std::printf("candidates: %zu\n", k);

    const std::string sep = "/";
    bool ok = true;

    // Сериализации те же, что у оракула (numpy .tobytes()): float32 LE у n, d, ea,
    // eb и силуэтов, int32 LE у f0/f1. x86 хранит их little-endian.
    struct Dump {
        const char* key;        // имя ключа в выводе и имени файла
        const void* data;
        std::size_t count;
        std::size_t elem;
        const char* suffix;
    };
    const Dump dumps[6] = {
        {"n", topo.n.data(), topo.n.size(), sizeof(float), "f32"},
        {"d", topo.d.data(), topo.d.size(), sizeof(float), "f32"},
        {"f0", topo.f0.data(), topo.f0.size(), sizeof(std::int32_t), "i32"},
        {"f1", topo.f1.data(), topo.f1.size(), sizeof(std::int32_t), "i32"},
        {"ea", topo.ea.data(), topo.ea.size(), sizeof(float), "f32"},
        {"eb", topo.eb.data(), topo.eb.size(), sizeof(float), "f32"},
    };
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
