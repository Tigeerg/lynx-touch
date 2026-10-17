// cpp/step_probe.cpp - пробник совпадения C++-слоя STEP и нормалей (этап 4a
// плана переноса) с питоновским эталоном modules/gcode-viewer/step_import.py и
// normals.py.
//
// Пробник не входит ни в плагин, ни в ядро: отдельная цель CMake. Он проходит
// тот же путь, что питоновский load_step_model по сетке и нормалям:
// STEPControl_Reader -> BRepMesh_IncrementalMesh(0.05, угловой 0.1, параллельно)
// -> суп вершин с учётом локации грани -> разворот оболочки наружу ->
// crease-aware нормали. Печатает:
//
//   verts: N           строк супа вершин (эталон Test_model.STEP - 136194)
//   triangles: F       треугольников (эталон - 45398)
//   sha256_verts: H    sha256 тех же байтов, что считает Python: float64 LE, (N,3)
//   sha256_tri: H      то же для (F,3,3): байты те же, другое чтение
//   sha256_normals: H  sha256 float32 LE (F,3,3)
//
// Те же массивы пишутся в каталог-дамп файлами step_verts.f64, step_tri.f64,
// step_normals.f32: питоновская проверка читает их numpy и сверяет поэлементно,
// а не только по хэшу. Сверку ведёт modules/gcode-viewer/test_step_core.py.
//
// Рёбра STEP пробник не считает: они в плагине CNC.Edges (cpp/stepledgesgeometry.cpp)
// и сверяются отдельно (test_stepledges_plugin.py).
//
// Запуск: step_probe <файл.step> <каталог-дампа>   (нужны DLL OCCT в PATH)
//         step_probe --manifest <oracle_manifest.json> [<файл.step>]
//         step_probe --check [<файл.step>]
// Код возврата: 0 - посчитано и записано (или дайджесты сошлись с оракулом);
//               1 - файл не прочитан или дамп не записан; 2 - аргументы не разобраны.
//
// Режим --manifest/--check сверяет дайджесты своих массивов (step.verts,
// step.tri, step.normals) с oracle_manifest.json без Python и без каталога-дампа:
// печатает строку на запись и "RESULT: OK" / "RESULT: FAIL (n)". Путь к тестовым
// данным по умолчанию берётся от корня репозитория (путь пробника).

// windows.h идёт до заголовков OCCT: цепочка OCCT объявляет _WINDOWS_ своим
// стражем, и включённый после неё windows.h не даёт ни одной своей функции.
// NOMINMAX обязателен: min/max макросами ломают std::min и std::max.
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

// Чтение модели тем же путём, что питоновский load_step_model: суп вершин,
// треугольники и нормали. Печатает причину отказа и возвращает false - путь общий
// для режима дампа и режима сверки с оракулом.
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

    // Сериализации те же, что у оракула (numpy .tobytes()): float64 LE (N,3) и
    // float32 LE (F,3,3). x86 хранит их little-endian, порядок байтов не меняется.
    const std::vector<unsigned char> vertsBytes =
        asBytes(model.verts.data(), model.verts.size(), sizeof(double));
    const std::vector<unsigned char> normalsBytes =
        asBytes(model.normals.data(), model.normals.size(), sizeof(float));

    // Сверка с оракулом: те же дайджесты, что печатает режим дампа, но против
    // oracle_manifest.json и без записи файлов.
    if (checkMode) {
        gv::manifest::Manifest manifest;
        std::string error;
        if (!manifest.load(manifestPath, error)) {
            std::fprintf(stderr, "step_probe: манифест не прочитан (%s): %s\n",
                         manifestPath.c_str(), error.c_str());
            return 1;
        }
        gv::manifest::Checker checker(manifest);
        // Формы записей - как в oracle_dump.py: суп (N,3), треугольники (F,3,3),
        // нормали (F,3,3); байты step.tri и step.verts одни и те же.
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
