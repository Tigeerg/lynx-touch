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
// Код возврата: 0 - посчитано и записано; 1 - файл или каталог недоступны.

#include "core/gcode_parser.h"
#include "core/gv_types.h"
#include "core/path_prep.h"
#include "core/text_util.h"
#include "core/ui_tags.h"
#include "sha256.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using gv::sha256Hex;          // хэш в sha256.h: общий с остальными пробниками сверки

namespace {

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

bool writeFile(const std::string& path, const std::vector<unsigned char>& bytes) {
    std::ofstream fh(path, std::ios::binary | std::ios::trunc);
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
    if (argc < 3) {
        std::fprintf(stderr, "usage: gcode_probe <file.ngc> <dump-dir>\n");
        return 1;
    }
    const std::string ngcPath = argv[1];
    const std::string dumpDir = argv[2];

    std::ifstream fh(ngcPath, std::ios::binary);
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
        // numpy U64: кодпоинты UCS-4 little-endian, до 64 знаков, далее нули.
        const std::string label = "(" + std::to_string(st.index) + ", '" + st.label + "')";
        std::vector<unsigned int> cps = utf8ToCodePoints(label);
        if (cps.size() > 64) { cps.resize(64); }
        for (int i = 0; i < 64; ++i) {
            const unsigned int cp = (static_cast<size_t>(i) < cps.size()) ? cps[static_cast<size_t>(i)] : 0U;
            for (int k = 0; k < 4; ++k) {
                stageBytes.push_back(static_cast<unsigned char>((cp >> (8 * k)) & 0xffU));
            }
        }
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
    const std::array<double, 3> rot = gv::parseUiRotation(text);
    const std::array<double, 3> org = gv::parseUiOrigin(text);
    printDoubles("tag_rotation", rot.data(), rot.size());
    printDoubles("tag_origin", org.data(), org.size());
    if (const auto stock = gv::parseUiStock(text)) {
        printDoubles("tag_stock", stock->data(), stock->size());
    } else {
        std::printf("tag_stock: none\n");
    }
    if (const auto mat = gv::parseUiCadMatrix(text)) {
        printDoubles("tag_cad_matrix", mat->data(), mat->size());
    } else {
        std::printf("tag_cad_matrix: none\n");
    }
    if (const auto org2 = gv::parseUiCadMatrixOrigin(text)) {
        printDoubles("tag_cad_matrix_origin", org2->data(), org2->size());
    } else {
        std::printf("tag_cad_matrix_origin: none\n");
    }
    if (const auto holder = gv::parseUiHolder(text)) {
        char gauge[64];
        if (holder->has_gauge) { std::snprintf(gauge, sizeof(gauge), "%.17g", holder->gauge); }
        else { std::strcpy(gauge, "none"); }
        std::printf("tag_holder: gauge=%s name=%s\n", gauge,
                    holder->has_name ? ("'" + holder->name + "'").c_str() : "none");
    } else {
        std::printf("tag_holder: none\n");
    }

    // Подготовленная траектория (path_prep.py): буферы потоков lines и метрики.
    // Буферы - те же байты, что принимает геометрия на GPU, поэтому они и есть
    // предмет сверки этапа 2.
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
