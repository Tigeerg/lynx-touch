// cpp/tangents_probe.cpp - пробник совпадения C++-аналитических поверхностей STEP
// и семейств касательных (этап 4d плана переноса) с питоновским эталоном
// modules/gcode-viewer/step_surfaces.py и tangents.py.
//
// Пробник не входит ни в плагин, ни в ядро: отдельная цель CMake. Он проходит
// тот же путь, что питоновский load_step_model + load_step_surfaces +
// _tangent_families:
//   * STEPControl_Reader -> суп вершин через gvstep (кожа для политики конусов);
//   * аналитические поверхности вращения из той же формы (gvstep);
//   * упакованные рёбра STEP берутся из файла packed_edges.f32 каталога-дампа:
//     их пишет питоновская проверка из load_step_model, ровно как в
//     bench_viewer.py (_pack_edge_segments). Числа рёбер уже сверены с эталоном
//     test_stepledges_plugin.py, здесь они вход;
//   * семейства касательных (gvcore) и отрезки на пяти фиксированных глазах.
//
// Печатает числа и sha256 тех же сериализаций, что считает Python
// (numpy .tobytes()): float64 LE у массивов чисел, uint8 LE у булевых масок,
// int32 LE у вида поверхности. Те же массивы пишутся дампом; сверку ведёт
// modules/gcode-viewer/test_tangents_core.py.
//
// Запуск: tangents_probe <файл.step> <каталог-дампа>   (нужны DLL OCCT в PATH)
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

#include "core/tangents.h"
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

// Фиксированные глаза - те же пять, что в oracle_dump.py и silhouette_probe:
// расхождение здесь сдвинуло бы эталон кадрового прохода.
const double EYES[5][3] = {{100.0, 150.0, 300.0},
                           {-200.0, 40.0, 260.0},
                           {60.0, -180.0, 120.0},
                           {240.0, 300.0, -60.0},
                           {0.0, 0.0, 400.0}};

const double NAN_ = std::numeric_limits<double>::quiet_NaN();

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

// Вход проверки: упакованные рёбра STEP (float32 по 5 на вершину, пары вершин -
// отрезки). Файла нет - пробник работает без пометок DROP, как эталон с
// packed_edges=None.
bool readPackedEdges(const std::string& utf8Path, gv::PackedEdges& out) {
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!fh) {
        return false;
    }
    fh.seekg(0, std::ios::end);
    const std::streamoff size = fh.tellg();
    fh.seekg(0, std::ios::beg);
    if (size <= 0) {
        return true;                 // пустой файл - пустой буфер
    }
    out.raw.assign(static_cast<std::size_t>(size), 0);
    fh.read(reinterpret_cast<char*>(out.raw.data()), size);
    out.nv = static_cast<int>(out.raw.size() / (5 * sizeof(float)));
    return true;
}

// Дамп: ключ в выводе, байты и имя файла.
struct Dump {
    std::string key;
    std::vector<unsigned char> bytes;
    std::string file;
};

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = probeArgs(argc, argv);
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: tangents_probe <file.step> <dump-dir>\n");
        return 1;
    }
    const std::string stepPath = args[1];
    const std::string dumpDir = args[2];
    const std::string sep = "/";

    gv::StepModel model;
    gv::StepSurfaceList surfs;
    try {
        const TopoDS_Shape shape = gv::readStepShape(stepPath);
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

    std::printf("file: %s\n", stepPath.c_str());

    // -- поверхности вращения --------------------------------------------------
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
    std::printf("surfaces: %zu\n", surfs.size());
    const std::size_t nConeSurf =
        static_cast<std::size_t>(std::count(kind.begin(), kind.end(), std::int32_t(1)));
    std::printf("cylinders: %zu\n", kind.size() - nConeSurf);
    std::printf("cones: %zu\n", nConeSurf);

    // -- семейства касательных -------------------------------------------------
    gv::PackedEdges packed;
    const bool havePacked = readPackedEdges(dumpDir + sep + "packed_edges.f32", packed);
    std::printf("packed_edges: %d\n", havePacked ? packed.nv : 0);

    const gv::SkinTriangles skin = gv::buildSkinTriangles(model.verts, model.normals);
    const gv::TangentFamilies fam =
        gv::buildTangentFamilies(surfs, havePacked ? &packed : nullptr, &skin);
    const gv::CylinderFamilies& cy = fam.cylinders;
    std::printf("families: %zu\n", cy.count);
    std::size_t nfull = 0;
    for (std::uint8_t f : cy.FULL) {
        nfull += f ? 1 : 0;
    }
    std::printf("full: %zu\n", nfull);
    std::printf("tangent_cones: %zu\n", fam.cones.size());

    // столбцы конуса: a(3), d(3), psi, s0, s1 - девять чисел на конус
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

    std::vector<Dump> dumps;
    auto addDump = [&dumps](const std::string& key, const std::string& file, const void* data,
                            std::size_t count, std::size_t elem) {
        Dump d;
        d.key = key;
        d.file = file;
        d.bytes = asBytes(data, count, elem);
        dumps.push_back(std::move(d));
    };
    addDump("surfs_kind", "surfs_kind.i32", kind.data(), kind.size(), sizeof(std::int32_t));
    addDump("surfs_cyl", "surfs_cyl.f64", cylRows.data(), cylRows.size(), sizeof(double));
    addDump("surfs_cone", "surfs_cone.f64", coneRows.data(), coneRows.size(), sizeof(double));
    addDump("fam_O", "fam_O.f64", cy.O.data(), cy.O.size(), sizeof(double));
    addDump("fam_D", "fam_D.f64", cy.D.data(), cy.D.size(), sizeof(double));
    addDump("fam_E1", "fam_E1.f64", cy.E1.data(), cy.E1.size(), sizeof(double));
    addDump("fam_E2", "fam_E2.f64", cy.E2.data(), cy.E2.size(), sizeof(double));
    addDump("fam_R", "fam_R.f64", cy.R.data(), cy.R.size(), sizeof(double));
    addDump("fam_T0", "fam_T0.f64", cy.T0.data(), cy.T0.size(), sizeof(double));
    addDump("fam_T1", "fam_T1.f64", cy.T1.data(), cy.T1.size(), sizeof(double));
    addDump("fam_RREF", "fam_RREF.f64", cy.RREF.data(), cy.RREF.size(), sizeof(double));
    addDump("fam_A0", "fam_A0.f64", cy.A0.data(), cy.A0.size(), sizeof(double));
    addDump("fam_A1", "fam_A1.f64", cy.A1.data(), cy.A1.size(), sizeof(double));
    addDump("fam_FULL", "fam_FULL.u8", cy.FULL.data(), cy.FULL.size(), sizeof(std::uint8_t));
    addDump("fam_DROP", "fam_DROP.f64", cy.DROP.data(), cy.DROP.size(), sizeof(double));
    addDump("fam_ARC", "fam_ARC.u8", cy.ARC.data(), cy.ARC.size(), sizeof(std::uint8_t));

    const std::string nameCone = "cone.f64";
    Dump coneDump;
    coneDump.key = "cone";
    coneDump.file = nameCone;
    coneDump.bytes = asBytes(famCone.data(), famCone.size(), sizeof(double));
    dumps.push_back(std::move(coneDump));

    int eyeIndex = 0;
    for (const auto& eye : EYES) {
        const std::vector<double> segs = gv::tangentFromFamilies(fam, eye);
        const std::string key = "tangent" + std::to_string(eyeIndex);
        std::printf("%s: %zu\n", key.c_str(), segs.size() / 6);
        addDump(key, key + ".f64", segs.data(), segs.size(), sizeof(double));
        ++eyeIndex;
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
