// cpp/gcode_probe.cpp - пробник совпадения C++-ядра с питоновским эталоном
// (modules/gcode-viewer/gcode_parser.py, gv_types.py).
//
// Пробник не входит ни в плагин, ни в ядро: отдельная цель CMake. Он читает
// управляющую программу, считает ядро (gvcore) и печатает:
//
//   points / flags / stages / seg_count - размеры массивов path.* из оракула
//   last: x y z                        - последняя машинная точка
//   sha256_points/flags/last/segments/stages - sha256 тех же сериализаций, что
//                                      считает Python (float64 LE, int64 LE,
//                                      numpy U64 = UTF-32LE по 64 знака)
//   tag_*                              - разобранные теги UI_* (улика по ui_tags)
//
// Те же массивы пишутся во второй аргумент-каталог файлами core_points.f64,
// core_flags.f64, core_last.f64, core_segments.i64, core_stages.u64: питоновская
// проверка читает их numpy и сравнивает поэлементно, а не только по хэшу.
//
// Запуск: gcode_probe <файл.ngc> <каталог-дампа>
//         gcode_probe --manifest <oracle_manifest.json> [<файл.ngc>]
//         gcode_probe --check [<файл.ngc>]
// Код возврата: 0 - посчитано и записано (или дайджесты сошлись с оракулом);
//               1 - файл или каталог недоступны; 2 - аргументы не разобраны.
//
// Режим --manifest/--check сверяет дайджесты своих массивов (path.*, pd.* и
// tag.*) с oracle_manifest.json без Python и без каталога-дампа: печатает строку
// на запись и "RESULT: OK" / "RESULT: FAIL (n)". Путь к тестовым данным по
// умолчанию берётся от корня репозитория (путь пробника).

#include "core/gcode_parser.h"
#include "core/gv_types.h"
#include "core/path_prep.h"
#include "core/text_util.h"
#include "core/ui_tags.h"
#include "manifest_check.h"
#include "sha256.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

// windows.h нужен для широкого разбора аргументов: путь к тестовым данным лежит в
// каталоге с кириллицей, а узкий argv несёт её в кодировке консоли.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

using gv::sha256Hex;          // хэш в sha256.h: общий с остальными пробниками сверки

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

// double -> 8 байт little-endian (как numpy float64.tobytes()).
void appendDouble(std::vector<unsigned char>& out, double v) {
    unsigned char buf[8];
    std::memcpy(buf, &v, 8);
    out.insert(out.end(), buf, buf + 8);
}

// UTF-8 -> кодпоинты; numpy str хранит UCS-4 little-endian.
std::vector<unsigned int> utf8ToCodePoints(const std::string& s) {
    std::vector<unsigned int> out;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned int cp = 0;
        size_t extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xe0) == 0xc0) { cp = c & 0x1fU; extra = 1; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0fU; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07U; extra = 3; }
        else { cp = 0xfffdU; }                       // битый байт: как замена в Python
        ++i;
        for (size_t k = 0; k < extra && i < s.size(); ++k, ++i) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3fU);
        }
        out.push_back(cp);
    }
    return out;
}

// numpy U64: кодпоинты UCS-4 little-endian, до 64 знаков, далее нули. Так же
// сериализуются подписи этапов и имя оправки.
void appendU64(std::vector<unsigned char>& out, const std::string& s) {
    std::vector<unsigned int> cps = utf8ToCodePoints(s);
    if (cps.size() > 64) { cps.resize(64); }
    for (int i = 0; i < 64; ++i) {
        const unsigned int cp = (static_cast<size_t>(i) < cps.size()) ? cps[static_cast<size_t>(i)] : 0U;
        for (int k = 0; k < 4; ++k) {
            out.push_back(static_cast<unsigned char>((cp >> (8 * k)) & 0xffU));
        }
    }
}

// Необязательный тег одним массивом: ведущий флаг присутствия 1.0/0.0, далее
// значения (нули, когда тега нет) - так None из Python кодируется однозначно.
template <std::size_t N>
void appendOptional(std::vector<unsigned char>& out,
                    const std::optional<std::array<double, N>>& v) {
    appendDouble(out, v ? 1.0 : 0.0);
    for (std::size_t i = 0; i < N; ++i) {
        appendDouble(out, v ? (*v)[i] : 0.0);
    }
}

bool writeFile(const std::string& path, const std::vector<unsigned char>& bytes) {
    std::ofstream fh(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!fh) { return false; }
    if (!bytes.empty()) { fh.write(reinterpret_cast<const char *>(bytes.data()),
                                   static_cast<std::streamsize>(bytes.size())); }
    return fh.good();
}

void printDoubles(const char *key, const double *v, size_t n) {
    std::printf("%s:", key);
    for (size_t i = 0; i < n; ++i) { std::printf(" %.17g", v[i]); }
    std::printf("\n");
}

} // namespace

int main(int argc, char **argv) {
    const std::vector<std::string> args = probeArgs(argc, argv);
    const bool checkMode = args.size() >= 2
            && (args[1] == "--manifest" || args[1] == "--check");
    std::string manifestPath;
    std::string ngcPath;
    std::string dumpDir;
    if (checkMode) {
        if (!gv::manifest::resolveCheckArgs(args, "Test_model.ngc", manifestPath, ngcPath)) {
            return 2;
        }
    } else {
        if (args.size() < 3) {
            std::fprintf(stderr, "usage: gcode_probe <file.ngc> <dump-dir>\n"
                                 "       gcode_probe --manifest <oracle_manifest.json> [<file.ngc>]\n"
                                 "       gcode_probe --check [<file.ngc>]\n");
            return 1;
        }
        ngcPath = args[1];
        dumpDir = args[2];
    }

    std::ifstream fh(std::filesystem::u8path(ngcPath), std::ios::binary);
    if (!fh) {
        std::fprintf(stderr, "gcode_probe: не удалось открыть %s\n", ngcPath.c_str());
        return 1;
    }
    std::string text((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());

    const gv::ParseResult res = gv::parseGcode(text);

    std::vector<unsigned char> pointsBytes;      // float64 [N,3]
    std::vector<unsigned char> flagBytes;        // float64 [N,2]
    std::vector<unsigned char> lastBytes;        // float64 [3]
    std::vector<unsigned char> segBytes;         // int64 [1]
    std::vector<unsigned char> stageBytes;       // U64 [K]

    pointsBytes.reserve(res.ordered.size() * 3 * 8);
    flagBytes.reserve(res.ordered.size() * 2 * 8);
    for (const gv::PathPoint& p : res.ordered) {
        appendDouble(pointsBytes, p.point[0]);
        appendDouble(pointsBytes, p.point[1]);
        appendDouble(pointsBytes, p.point[2]);
        appendDouble(flagBytes, p.is_rapid ? 1.0 : 0.0);
        appendDouble(flagBytes, p.speed_mm_s);
    }
    for (double v : res.last) { appendDouble(lastBytes, v); }
    const long long segCount = res.seg_count;
    for (int i = 0; i < 8; ++i) {
        segBytes.push_back(static_cast<unsigned char>((static_cast<unsigned long long>(segCount) >> (8 * i)) & 0xffU));
    }
    for (const gv::Stage& st : res.stages) {
        // Подпись этапа - та же строка, что numpy U64 у Python.
        appendU64(stageBytes, "(" + std::to_string(st.index) + ", '" + st.label + "')");
    }

    // Подготовка траектории (path_prep.py): буферы потоков lines и метрики.
    // Буферы - те же байты, что принимает геометрия на GPU, поэтому они и есть
    // предмет сверки этапа 2. Считается один раз: тот же результат идёт и в
    // режим дампа, и в сверку с оракулом.
    const gv::PathData pd = gv::prepareOrdered(res.ordered, res.stages);
    std::vector<unsigned char> pdScene;
    std::vector<unsigned char> pdCum;
    std::vector<unsigned char> pdRapid;
    std::vector<unsigned char> pdSpeeds;
    pdScene.reserve(pd.pts.size() * 8);
    for (double v : pd.pts) { appendDouble(pdScene, v); }
    for (double v : pd.cum) { appendDouble(pdCum, v); }
    pdRapid.assign(pd.rapid.begin(), pd.rapid.end());
    for (double v : pd.speeds) { appendDouble(pdSpeeds, v); }

    // Теги UI_*: улика по ui_tags.cpp (их разбирает не только подпись этапа).
    const std::array<double, 3> rot = gv::parseUiRotation(text);
    const std::array<double, 3> org = gv::parseUiOrigin(text);
    const std::optional<std::array<double, 6>> stock = gv::parseUiStock(text);
    const std::optional<std::array<double, 9>> cadMatrix = gv::parseUiCadMatrix(text);
    const std::optional<std::array<double, 3>> cadMatrixOrigin = gv::parseUiCadMatrixOrigin(text);
    const std::optional<gv::HolderTag> holder = gv::parseUiHolder(text);

    // Сверка с оракулом: те же дайджесты, что печатает режим дампа, но против
    // oracle_manifest.json и без записи файлов.
    if (checkMode) {
        gv::manifest::Manifest manifest;
        std::string error;
        if (!manifest.load(manifestPath, error)) {
            std::fprintf(stderr, "gcode_probe: манифест не прочитан (%s): %s\n",
                         manifestPath.c_str(), error.c_str());
            return 1;
        }
        gv::manifest::Checker checker(manifest);
        const long long points = static_cast<long long>(res.ordered.size());
        checker.check("path.points", "float64", sha256Hex(pointsBytes), {points, 3});
        checker.check("path.flags", "float64", sha256Hex(flagBytes), {points, 2});
        checker.check("path.last", "float64", sha256Hex(lastBytes), {3});
        checker.check("path.segments", "int64", sha256Hex(segBytes), {1});
        checker.check("path.stages", "<U64", sha256Hex(stageBytes),
                      {static_cast<long long>(res.stages.size())});

        // pd.*: подготовка траектории (path_prep._prepare_ordered) - те же
        // массивы, что сверяет test_gcode_core.py. Числовые метрики записаны
        // массивами формы (1,).
        const long long pdPoints = static_cast<long long>(pd.pts.size() / 3);
        checker.check("pd.pts", "float64", sha256Hex(pdScene), {pdPoints, 3});
        checker.check("pd.cum", "float64", sha256Hex(pdCum),
                      {static_cast<long long>(pd.cum.size())});
        checker.check("pd.rapid", "bool", sha256Hex(pdRapid),
                      {static_cast<long long>(pd.rapid.size())});
        checker.check("pd.speeds", "float64", sha256Hex(pdSpeeds),
                      {static_cast<long long>(pd.speeds.size())});
        checker.check("pd.rapid_stream", "uint8", sha256Hex(pd.rapid_stream.bytes),
                      {static_cast<long long>(pd.rapid_stream.bytes.size())});
        checker.check("pd.feed_stream", "uint8", sha256Hex(pd.feed_stream.bytes),
                      {static_cast<long long>(pd.feed_stream.bytes.size())});

        std::vector<unsigned char> pdTotal;
        std::vector<unsigned char> pdRadius;
        std::vector<unsigned char> pdMinY;
        std::vector<unsigned char> pdCenter;
        std::vector<unsigned char> pdBoundsLo;
        std::vector<unsigned char> pdBoundsHi;
        std::vector<unsigned char> pdRapidNv;
        std::vector<unsigned char> pdFeedNv;
        appendDouble(pdTotal, pd.total);
        appendDouble(pdRadius, pd.radius);
        appendDouble(pdMinY, pd.min_y);
        for (double v : pd.center) { appendDouble(pdCenter, v); }
        for (double v : pd.bounds.lo) { appendDouble(pdBoundsLo, v); }
        for (double v : pd.bounds.hi) { appendDouble(pdBoundsHi, v); }
        for (int i = 0; i < 8; ++i) {
            pdRapidNv.push_back(static_cast<unsigned char>(
                (static_cast<unsigned long long>(pd.rapid_stream.nv) >> (8 * i)) & 0xffU));
            pdFeedNv.push_back(static_cast<unsigned char>(
                (static_cast<unsigned long long>(pd.feed_stream.nv) >> (8 * i)) & 0xffU));
        }
        checker.check("pd.total", "float64", sha256Hex(pdTotal), {1});
        checker.check("pd.radius", "float64", sha256Hex(pdRadius), {1});
        checker.check("pd.min_y", "float64", sha256Hex(pdMinY), {1});
        checker.check("pd.center", "float64", sha256Hex(pdCenter), {3});
        if (pd.has_bounds) {
            checker.check("pd.bounds_lo", "float64", sha256Hex(pdBoundsLo), {3});
            checker.check("pd.bounds_hi", "float64", sha256Hex(pdBoundsHi), {3});
        }
        checker.check("pd.rapid_nv", "int64", sha256Hex(pdRapidNv), {1});
        checker.check("pd.feed_nv", "int64", sha256Hex(pdFeedNv), {1});

        std::vector<unsigned char> pdStageStart;
        std::vector<unsigned char> pdStageEnd;
        std::vector<unsigned char> pdStageLabel;
        for (const gv::StageRange& st : pd.stages) {
            appendDouble(pdStageStart, st.start);
            appendDouble(pdStageEnd, st.end);
            appendU64(pdStageLabel, st.label);
        }
        const long long pdStageCount = static_cast<long long>(pd.stages.size());
        checker.check("pd.stage_start", "float64", sha256Hex(pdStageStart), {pdStageCount});
        checker.check("pd.stage_end", "float64", sha256Hex(pdStageEnd), {pdStageCount});
        checker.check("pd.stage_label", "<U64", sha256Hex(pdStageLabel), {pdStageCount});

        // tag.*: разбор тегов UI_* (ui_tags.py). Необязательный тег - ведущий
        // флаг присутствия 1.0/0.0, далее значения (нули, когда тега нет).
        std::vector<unsigned char> tagRotation;
        std::vector<unsigned char> tagOrigin;
        std::vector<unsigned char> tagStock;
        std::vector<unsigned char> tagCadMatrix;
        std::vector<unsigned char> tagCadMatrixOrigin;
        std::vector<unsigned char> tagHolder;
        std::vector<unsigned char> tagHolderName;
        for (double v : rot) { appendDouble(tagRotation, v); }
        for (double v : org) { appendDouble(tagOrigin, v); }
        appendOptional(tagStock, stock);
        appendOptional(tagCadMatrix, cadMatrix);
        appendOptional(tagCadMatrixOrigin, cadMatrixOrigin);
        const bool holderPresent = holder.has_value() && (holder->has_gauge || holder->has_name);
        appendDouble(tagHolder, holderPresent ? 1.0 : 0.0);
        appendDouble(tagHolder, (holder.has_value() && holder->has_gauge) ? holder->gauge : 0.0);
        appendU64(tagHolderName,
                  (holder.has_value() && holder->has_name) ? holder->name : std::string());
        checker.check("tag.rotation", "float64", sha256Hex(tagRotation), {3});
        checker.check("tag.origin", "float64", sha256Hex(tagOrigin), {3});
        checker.check("tag.stock", "float64", sha256Hex(tagStock), {7});
        checker.check("tag.cad_matrix", "float64", sha256Hex(tagCadMatrix), {10});
        checker.check("tag.cad_matrix_origin", "float64", sha256Hex(tagCadMatrixOrigin), {4});
        checker.check("tag.holder", "float64", sha256Hex(tagHolder), {2});
        checker.check("tag.holder_name", "<U64", sha256Hex(tagHolderName), {1});

        checker.printResult();
        return checker.failures() == 0 ? 0 : 1;
    }

    std::printf("file: %s\n", ngcPath.c_str());
    std::printf("points: %zu\n", res.ordered.size());
    std::printf("flags: %zu\n", res.ordered.size());
    std::printf("stages: %zu\n", res.stages.size());
    std::printf("seg_count: %d\n", res.seg_count);
    printDoubles("last", res.last.data(), res.last.size());
    std::printf("sha256_points: %s\n", sha256Hex(pointsBytes).c_str());
    std::printf("sha256_flags: %s\n", sha256Hex(flagBytes).c_str());
    std::printf("sha256_last: %s\n", sha256Hex(lastBytes).c_str());
    std::printf("sha256_segments: %s\n", sha256Hex(segBytes).c_str());
    std::printf("sha256_stages: %s\n", sha256Hex(stageBytes).c_str());
    for (size_t i = 0; i < res.stages.size(); ++i) {
        std::printf("stage_%zu: (%d, '%s')\n", i, res.stages[i].index, res.stages[i].label.c_str());
    }

    // Теги UI_*: улика по ui_tags.cpp (их разбирает не только подпись этапа).
    printDoubles("tag_rotation", rot.data(), rot.size());
    printDoubles("tag_origin", org.data(), org.size());
    if (stock) {
        printDoubles("tag_stock", stock->data(), stock->size());
    } else {
        std::printf("tag_stock: none\n");
    }
    if (cadMatrix) {
        printDoubles("tag_cad_matrix", cadMatrix->data(), cadMatrix->size());
    } else {
        std::printf("tag_cad_matrix: none\n");
    }
    if (cadMatrixOrigin) {
        printDoubles("tag_cad_matrix_origin", cadMatrixOrigin->data(), cadMatrixOrigin->size());
    } else {
        std::printf("tag_cad_matrix_origin: none\n");
    }
    if (holder) {
        char gauge[64];
        if (holder->has_gauge) { std::snprintf(gauge, sizeof(gauge), "%.17g", holder->gauge); }
        else { std::strcpy(gauge, "none"); }
        std::printf("tag_holder: gauge=%s name=%s\n", gauge,
                    holder->has_name ? ("'" + holder->name + "'").c_str() : "none");
    } else {
        std::printf("tag_holder: none\n");
    }

    std::printf("pd_seg_count: %d\n", pd.seg_count);
    std::printf("pd_total: %.17g\n", pd.total);
    printDoubles("pd_center", pd.center.data(), pd.center.size());
    std::printf("pd_radius: %.17g\n", pd.radius);
    std::printf("pd_min_y: %.17g\n", pd.min_y);
    std::printf("pd_rapid_nv: %d\n", pd.rapid_stream.nv);
    std::printf("pd_feed_nv: %d\n", pd.feed_stream.nv);
    if (pd.has_bounds) {
        printDoubles("pd_bounds_lo", pd.bounds.lo.data(), pd.bounds.lo.size());
        printDoubles("pd_bounds_hi", pd.bounds.hi.data(), pd.bounds.hi.size());
    }
    std::printf("sha256_scene: %s\n", sha256Hex(pdScene).c_str());
    std::printf("sha256_cum: %s\n", sha256Hex(pdCum).c_str());
    std::printf("sha256_rapid_stream: %s\n", sha256Hex(pd.rapid_stream.bytes).c_str());
    std::printf("sha256_feed_stream: %s\n", sha256Hex(pd.feed_stream.bytes).c_str());
    for (size_t i = 0; i < pd.stages.size(); ++i) {
        std::printf("pd_stage_%zu: %.17g %.17g %s\n", i, pd.stages[i].start,
                    pd.stages[i].end, pd.stages[i].label.c_str());
    }

    const std::string sep = (dumpDir.empty() || dumpDir.back() == '/' || dumpDir.back() == '\\')
                                ? "" : "/";
    if (!writeFile(dumpDir + sep + "core_points.f64", pointsBytes)
            || !writeFile(dumpDir + sep + "core_flags.f64", flagBytes)
            || !writeFile(dumpDir + sep + "core_last.f64", lastBytes)
            || !writeFile(dumpDir + sep + "core_segments.i64", segBytes)
            || !writeFile(dumpDir + sep + "core_stages.u64", stageBytes)
            || !writeFile(dumpDir + sep + "core_scene.f64", pdScene)
            || !writeFile(dumpDir + sep + "core_cum.f64", pdCum)
            || !writeFile(dumpDir + sep + "core_rapid.u8", pdRapid)
            || !writeFile(dumpDir + sep + "core_speeds.f64", pdSpeeds)
            || !writeFile(dumpDir + sep + "core_rapid_stream.bin", pd.rapid_stream.bytes)
            || !writeFile(dumpDir + sep + "core_feed_stream.bin", pd.feed_stream.bytes)) {
        std::fprintf(stderr, "gcode_probe: не удалось записать дамп в %s\n", dumpDir.c_str());
        return 1;
    }
    std::printf("dump_dir: %s\n", dumpDir.c_str());
    return 0;
}
