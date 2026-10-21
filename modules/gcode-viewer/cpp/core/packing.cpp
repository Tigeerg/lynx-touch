#include "core/packing.h"

#include <cstddef>
#include <cstring>

namespace gv {
namespace {

constexpr int STRIDE = 5;               // float32 per vertex: position 3 + texcoord 2
constexpr int VERTS_PER_EDGE = 2;       // Lines: two vertices per segment

// float32 from buffer bytes: read through memcpy - reinterpret_cast<float*> on a
// byte buffer would break the strict-aliasing rules.
float readF32(const unsigned char* p) {
    float v = 0.0f;
    std::memcpy(&v, p, sizeof(float));
    return v;
}

} // namespace

PackedEdges packEdgesNp(const std::vector<double>& p0, const std::vector<double>& p1) {
    PackedEdges out;
    const std::size_t m = p0.size() / 3;
    if (m == 0 || p1.size() / 3 != m) {
        return out;                     // empty buffer: raw is empty, nv = 0, no bounds
    }

    const std::size_t floats = m * VERTS_PER_EDGE * STRIDE;
    std::vector<float> buf(floats, 0.0f);
    for (std::size_t i = 0; i < m; ++i) {
        for (int k = 0; k < 3; ++k) {
            buf[i * 10 + k] = static_cast<float>(p0[i * 3 + std::size_t(k)]);
            buf[i * 10 + 5 + k] = static_cast<float>(p1[i * 3 + std::size_t(k)]);
        }
        buf[i * 10 + 3] = 1.0f;         // texcoord.x = 1.0: the segment is fully visible
        buf[i * 10 + 4] = 0.0f;
        buf[i * 10 + 8] = 1.0f;
        buf[i * 10 + 9] = 0.0f;
    }

    out.raw.resize(floats * sizeof(float));
    std::memcpy(out.raw.data(), buf.data(), out.raw.size());
    out.nv = static_cast<int>(m * VERTS_PER_EDGE);

    // Bounds - from the float64 input (in the reference allp = concat([p0, p1])).
    double lo[3] = {p0[0], p0[1], p0[2]};
    double hi[3] = {lo[0], lo[1], lo[2]};
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<double>& src = pass == 0 ? p0 : p1;
        for (std::size_t i = 0; i + 3 <= src.size(); i += 3) {
            for (int k = 0; k < 3; ++k) {
                const double v = src[i + std::size_t(k)];
                if (v < lo[k]) {
                    lo[k] = v;
                }
                if (v > hi[k]) {
                    hi[k] = v;
                }
            }
        }
    }
    out.bounds.lo = {lo[0], lo[1], lo[2]};
    out.bounds.hi = {hi[0], hi[1], hi[2]};
    out.has_bounds = true;
    return out;
}

std::vector<float> packedPositions(const PackedEdges& packed) {
    std::vector<float> out;
    if (packed.raw.empty() || packed.nv < 2 || packed.raw.size() % sizeof(float) != 0) {
        return out;
    }
    const std::size_t total = packed.raw.size() / sizeof(float);
    const std::size_t verts = static_cast<std::size_t>(packed.nv);
    // In the reference stride = f.size // nv; for our buffers that is 5.
    const std::size_t stride = total / verts;
    if (stride < 3) {
        return out;
    }
    const std::size_t segs = verts / VERTS_PER_EDGE;
    out.resize(segs * 6);
    for (std::size_t s = 0; s < segs; ++s) {
        for (int e = 0; e < VERTS_PER_EDGE; ++e) {
            const unsigned char* vp = packed.raw.data()
                + (s * VERTS_PER_EDGE + std::size_t(e)) * stride * sizeof(float);
            for (int k = 0; k < 3; ++k) {
                out[s * 6 + std::size_t(e) * 3 + std::size_t(k)] = readF32(vp + k * 4);
            }
        }
    }
    return out;
}

} // namespace gv
