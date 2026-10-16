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

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// ---- SHA-256 (FIPS 180-4): пробник печатает те же хэши, что hashlib ---------

class Sha256 {
public:
    void update(const unsigned char *data, size_t len) {
        total_ += len;
        for (size_t i = 0; i < len; ++i) {
            block_[filled_++] = data[i];
            if (filled_ == 64) { compress(); }
        }
    }

    std::string hex() {
        const unsigned long long bits = total_ * 8ULL;
        const unsigned char pad = 0x80;
        update(&pad, 1);
        const unsigned char zero = 0x00;
        while (filled_ != 56) { update(&zero, 1); }
        unsigned char tail[8];
        for (int i = 0; i < 8; ++i) { tail[i] = static_cast<unsigned char>(bits >> (56 - 8 * i)); }
        update(tail, 8);
        static const char *digits = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i) {
            for (int k = 3; k >= 0; --k) {
                const unsigned int byte = static_cast<unsigned int>((state_[i] >> (8 * k)) & 0xffU);
                out.push_back(digits[byte >> 4]);
                out.push_back(digits[byte & 0xf]);
            }
        }
        return out;
    }

private:
    static unsigned int rotr(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress() {
        unsigned int w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<unsigned int>(block_[4 * i]) << 24)
                 | (static_cast<unsigned int>(block_[4 * i + 1]) << 16)
                 | (static_cast<unsigned int>(block_[4 * i + 2]) << 8)
                 | static_cast<unsigned int>(block_[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const unsigned int s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const unsigned int s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned int a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        unsigned int e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        static const unsigned int k[64] = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
        for (int i = 0; i < 64; ++i) {
            const unsigned int s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const unsigned int ch = (e & f) ^ ((~e) & g);
            const unsigned int t1 = h + s1 + ch + k[i] + w[i];
            const unsigned int s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            const unsigned int t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
        filled_ = 0;
    }

    unsigned int state_[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                              0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    unsigned char block_[64] = {};
    size_t filled_ = 0;
    unsigned long long total_ = 0;   // счётчик только по полным блокам: длина добирается хвостом
};

std::string sha256Hex(const std::vector<unsigned char>& bytes) {
    Sha256 h;
    h.update(bytes.data(), bytes.size());
    return h.hex();
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
