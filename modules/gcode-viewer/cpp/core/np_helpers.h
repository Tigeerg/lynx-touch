// core/np_helpers.h - shared "numpy-like" helpers for the pure core layers:
// dot and cross product, remainder by divisor sign, vector norm and rounding to
// an integer. The implementations live in one place so that the code repeated
// three times (normals, outline, silhouette, tangents, step_surfaces) does not
// drift apart on edits.
//
// The norm is kept as two functions rather than one: the reference uses
// np.linalg.norm as sqrt in one place and as pow(sum, 0.5) in another, and on
// values around one they differ in the low bit (visible in the normalized axis
// direction). The call site decides which one is needed - both implementations
// are here so the body is not copied.
#pragma once

#include <cmath>
#include <cstdint>

namespace gv {
namespace np {

// Dot product: in numpy this is a @ b (summation left to right).
inline double dot3(const double* a, const double* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Cross product, like np.cross for 3-vectors.
inline void cross3(const double* a, const double* b, double* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// Remainder like numpy %: the result sign follows the divisor, while fmod
// takes the sign of the dividend. The difference shows where the input is
// negative.
inline double npMod(double x, double y) {
    const double r = std::fmod(x, y);
    if (r != 0.0 && ((r < 0.0) != (y < 0.0))) {
        return r + y;
    }
    return r;
}

// Square root of the sum of three squares (np.linalg.norm): summation left to
// right.
inline double norm3Sqrt(const double* v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// Norm as (sum of squares) ** 0.5: the reference uses pow here, and on values
// around one it differs from sqrt in the low bit.
inline double norm3Pow(const double* v) {
    return std::pow(v[0] * v[0] + v[1] * v[1] + v[2] * v[2], 0.5);
}

// numpy.round to an integer: round half to even (std::nearbyint in the default
// mode).
inline std::int64_t roundToInt(double v) {
    return static_cast<std::int64_t>(std::nearbyint(v));
}

} // namespace np
} // namespace gv
