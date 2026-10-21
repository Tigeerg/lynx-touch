// core/stl_import.cpp - STL reading: binary and ASCII.
//
// Binary STL layout: an 80-byte header, a uint32 little-endian triangle count,
// then 50-byte records (normal 3f, three vertices of 3f, 2 attribute bytes). The
// record normal is not read: the reference takes only the vertices.
//
// The ASCII branch repeats the reference path line by line: UTF-8 decoding with
// broken bytes ignored, splitting into lines with the str.splitlines set,
// trimming with str.strip, searching for the vertex words. Numbers are parsed by
// strtodC (the same correctly rounding parse as float for ordinary decimal
// notation, but in the C locale and independent of the process LC_NUMERIC).
#include "core/stl_import.h"

#include "core/text_util.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace gv {
namespace {

constexpr std::size_t BINARY_HEAD = 84;         // 80 header bytes + counter
constexpr std::size_t TRIANGLE_BYTES = 50;      // one triangle record
constexpr std::size_t ASCII_PROBE_BYTES = 1024; // the reference decides "is this ASCII" from the first 1024 bytes

bool readAll(const std::string& utf8Path, std::string& out) {
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!fh) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(fh), std::istreambuf_iterator<char>());
    return true;
}

float readF32(const unsigned char* p) {
    float v = 0.0f;
    std::memcpy(&v, p, sizeof(float));
    return v;
}

std::uint32_t readU32(const unsigned char* p) {
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof(std::uint32_t));
    return v;
}

bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// Is the code point whitespace: the str.isspace set (needed for strip and split
// without an argument).
bool isPySpace(std::uint32_t cp) {
    if (cp == 0x20 || (cp >= 0x09 && cp <= 0x0d) || (cp >= 0x1c && cp <= 0x1f)) {
        return true;
    }
    if (cp == 0x85 || cp == 0xa0 || cp == 0x1680) {
        return true;
    }
    if (cp >= 0x2000 && cp <= 0x200a) {
        return true;
    }
    return cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000;
}

// Line boundary in str.splitlines - a narrower set than isspace (0x1f is not in
// it).
bool isLineBreak(std::uint32_t cp) {
    return cp == 0x0a || cp == 0x0b || cp == 0x0c || cp == 0x0d
        || (cp >= 0x1c && cp <= 0x1e) || cp == 0x85 || cp == 0x2028 || cp == 0x2029;
}

// UTF-8 -> code points with broken bytes ignored: str.decode("utf-8", "ignore").
std::vector<std::uint32_t> decodeUtf8(const std::string& data) {
    std::vector<std::uint32_t> out;
    out.reserve(data.size());
    std::size_t i = 0;
    while (i < data.size()) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        std::uint32_t cp = 0;
        std::size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xe0) == 0xc0) {
            cp = c & 0x1fu;
            extra = 1;
        } else if ((c & 0xf0) == 0xe0) {
            cp = c & 0x0fu;
            extra = 2;
        } else if ((c & 0xf8) == 0xf0) {
            cp = c & 0x07u;
            extra = 3;
        } else {
            ++i;
            continue;
        }
        bool ok = i + extra < data.size();
        for (std::size_t k = 1; ok && k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(data[i + k]);
            if ((cc & 0xc0) != 0x80) {
                ok = false;
            } else {
                cp = (cp << 6) | (cc & 0x3fu);
            }
        }
        if (!ok) {
            ++i;
            continue;
        }
        i += extra + 1;
        out.push_back(cp);
    }
    return out;
}

// Token (code points) -> double: strtodC over the token's ASCII bytes. Returns
// false if the number is not parsed in full - in the reference that would be a
// float exception.
bool parseDouble(const std::vector<std::uint32_t>& text, std::size_t from, std::size_t to,
                 double& out) {
    std::string token;
    token.reserve(to - from);
    for (std::size_t i = from; i < to; ++i) {
        if (text[i] < 0x80) {
            token.push_back(static_cast<char>(text[i]));
        }
    }
    if (token.empty()) {
        return false;
    }
    const char* begin = token.c_str();
    char* end = nullptr;
    const double v = strtodC(begin, &end);
    if (end != begin + token.size()) {
        return false;
    }
    out = v;
    return true;
}

// Lines of ASCII STL: one vertex word per vertex.
std::vector<double> loadAscii(const std::vector<std::uint32_t>& text) {
    std::vector<double> out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t line_end = i;
        while (line_end < text.size() && !isLineBreak(text[line_end])) {
            ++line_end;
        }
        std::size_t a = i;
        std::size_t b = line_end;
        while (a < b && isPySpace(text[a])) {
            ++a;
        }
        while (b > a && isPySpace(text[b - 1])) {
            --b;
        }
        const bool is_vertex = (b - a >= 6) && text[a] == 'v' && text[a + 1] == 'e'
            && text[a + 2] == 'r' && text[a + 3] == 't' && text[a + 4] == 'e' && text[a + 5] == 'x';
        if (is_vertex) {
            // All fields of the line, like s.split in the reference: the first is
            // the word vertex (checked by prefix), then the second, third and
            // fourth are needed.
            double vals[3] = {0.0, 0.0, 0.0};
            int seen = 0;
            bool ok = true;
            std::size_t k = a;
            while (k < b) {
                while (k < b && isPySpace(text[k])) {
                    ++k;
                }
                if (k >= b) {
                    break;
                }
                std::size_t e = k;
                while (e < b && !isPySpace(text[e])) {
                    ++e;
                }
                if (seen >= 1 && seen <= 3 && !parseDouble(text, k, e, vals[seen - 1])) {
                    ok = false;
                }
                ++seen;
                k = e;
            }
            if (!ok) {
                return {};                      // in the reference this would be a float() exception
            }
            if (seen >= 4) {
                out.insert(out.end(), vals, vals + 3);
            }
        }
        i = line_end;
        if (i < text.size()) {
            if (text[i] == 0x0d && i + 1 < text.size() && text[i + 1] == 0x0a) {
                i += 2;                         // "\r\n" is one line boundary
            } else {
                ++i;
            }
        }
    }
    return out;
}

std::vector<double> loadBinary(const std::string& data) {
    std::vector<double> out;
    if (data.size() < BINARY_HEAD) {
        return out;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    std::size_t count = readU32(bytes + 80);
    const std::size_t room = (data.size() - BINARY_HEAD) / TRIANGLE_BYTES;
    if (count > room) {
        count = room;                           // truncated file: read as much as there is
    }
    out.reserve(count * 9);
    for (std::size_t t = 0; t < count; ++t) {
        const unsigned char* rec = bytes + BINARY_HEAD + t * TRIANGLE_BYTES;
        for (int v = 0; v < 3; ++v) {
            const unsigned char* vp = rec + 12 + static_cast<std::size_t>(v) * 12;   // 12 bytes of normal
            for (int k = 0; k < 3; ++k) {
                out.push_back(static_cast<double>(readF32(vp + k * 4)));
            }
        }
    }
    return out;
}

} // namespace

std::vector<double> loadStlVertices(const std::string& utf8Path) {
    std::string data;
    if (!readAll(utf8Path, data)) {
        return {};
    }
    std::string head = data.substr(0, std::min(ASCII_PROBE_BYTES, data.size()));
    if (contains(head, "facet") && contains(head, "vertex")) {
        return loadAscii(decodeUtf8(data));
    }
    return loadBinary(data);
}

Aabb vertsBounds(const std::vector<double>& verts) {
    Aabb box;
    if (verts.size() < 3) {
        return box;                             // zero bounds - as for an empty soup
    }
    double lo[3] = {verts[0], verts[1], verts[2]};
    double hi[3] = {lo[0], lo[1], lo[2]};
    for (std::size_t i = 0; i + 3 <= verts.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
            const double v = verts[i + static_cast<std::size_t>(k)];
            if (v < lo[k]) {
                lo[k] = v;
            }
            if (v > hi[k]) {
                hi[k] = v;
            }
        }
    }
    box.lo = {lo[0], lo[1], lo[2]};
    box.hi = {hi[0], hi[1], hi[2]};
    return box;
}

} // namespace gv
