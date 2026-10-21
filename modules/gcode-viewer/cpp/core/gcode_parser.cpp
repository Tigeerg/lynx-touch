// core/gcode_parser.cpp - NC program parsing.
//
// Precision matters more than brevity: the reference core is checked byte for
// byte (oracle_manifest.json, path.* keys), so the order of operations, the
// rounding mode and even the CPython math.hypot algorithm are kept here -
// replacing hypot with sqrt(x*x + y*y) shifts 51 points out of 70915 on
// Test_model.ngc (measured).
#include "gcode_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "text_util.h"
#include "ui_tags.h"

namespace gv {
namespace {

// math.pi and math.degrees: in CPython degrees(x) is computed as x * (180/pi),
// where the multiplier is a compile-time constant.
const double kPi = 3.141592653589793238462643383279502884;
const double kRadToDeg = 180.0 / kPi;

// ---- math.hypot from CPython 3.12 (Modules/mathmodule.c, vector_norm) ---------
// The reference computes hypot not as sqrt(x*x + y*y): it scales the terms, adds
// the squares with compensation (dl_mul on fma + dl_fast_sum) and adds a
// differential correction. For two arguments this is below.

struct DoubleLength {
    double hi = 0.0;
    double lo = 0.0;
};

// Algorithm 1.1: compensated addition of two numbers; |a| >= |b| by assumption.
DoubleLength dlFastSum(double a, double b) {
    const double x = a + b;
    const double y = (a - x) + b;
    return DoubleLength{x, y};
}

// Algorithm 3.5: exact product decomposition (the remainder via fma).
DoubleLength dlMul(double x, double y) {
    const double z = x * y;
    const double zz = std::fma(x, y, -z);
    return DoubleLength{z, zz};
}

double hypot2(double a, double b) {
    double vec[2] = {std::fabs(a), std::fabs(b)};
    if (std::isnan(vec[0]) || std::isnan(vec[1])) { return std::numeric_limits<double>::quiet_NaN(); }
    double max = 0.0;
    for (double x : vec) {
        if (x > max) { max = x; }
    }
    if (max == 0.0) { return max; }
    if (std::isinf(max)) { return max; }
    int maxE = 0;
    std::frexp(max, &maxE);
    if (maxE < -1023) {
        // Subnormal maximum: ldexp(1.0, -maxE) would overflow.
        const double dblMin = std::numeric_limits<double>::min();
        vec[0] /= dblMin;
        vec[1] /= dblMin;
        return dblMin * hypot2(vec[0], vec[1]);
    }
    const double scale = std::ldexp(1.0, -maxE);
    double csum = 1.0;
    double frac1 = 0.0;
    double frac2 = 0.0;
    for (int i = 0; i < 2; ++i) {
        const double x = vec[i] * scale;
        const DoubleLength pr = dlMul(x, x);
        const DoubleLength sm = dlFastSum(csum, pr.hi);
        csum = sm.hi;
        frac1 += pr.lo;
        frac2 += sm.lo;
    }
    double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
    const DoubleLength pr = dlMul(-h, h);
    const DoubleLength sm = dlFastSum(csum, pr.hi);
    csum = sm.hi;
    frac1 += pr.lo;
    frac2 += sm.lo;
    const double x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);
    return h / scale;
}

// ---- planes and arc offsets -----------------------------------------------
// Active plane -> (u axis, v axis, w axis); w is the screw/normal axis.
enum class Plane { XY, XZ, YZ };

void planeAxes(Plane plane, int& u, int& v, int& w) {
    if (plane == Plane::XY) { u = 0; v = 1; w = 2; }
    else if (plane == Plane::XZ) { u = 0; v = 2; w = 1; }
    else { u = 1; v = 2; w = 0; }
}

// Active plane -> center offset letters (u, v).
void planeOffsets(Plane plane, char& lu, char& lv) {
    if (plane == Plane::XY) { lu = 'I'; lv = 'J'; }
    else if (plane == Plane::XZ) { lu = 'I'; lv = 'K'; }
    else { lu = 'J'; lv = 'K'; }
}

// Words of the line: a letter and a number (-? \d*.? \d+) - the reference
// _WORD_RE.findall. The number is not a copy but a slice into the line buffer:
// nobody changes or reallocates the line while the value is read (otherwise the
// slice would dangle).
struct Word {
    char letter = 'A';
    size_t valueStart = 0;
    size_t valueLen = 0;
};

std::vector<Word> findWords(const std::string& line) {
    std::vector<Word> out;
    const size_t n = line.size();
    out.reserve(n / 2 + 1);                        // a word takes at least 2 bytes
    size_t i = 0;
    while (i < n) {
        if (!isLetterCh(line[i])) { ++i; continue; }
        size_t p = i + 1;
        while (p < n && isSpaceCh(static_cast<unsigned char>(line[p]))) { ++p; }
        const size_t valueStart = p;               // the sign is part of the number
        if (p < n && (line[p] == '+' || line[p] == '-')) { ++p; }
        const size_t digitsStart = p;
        while (p < n && isDigitCh(line[p])) { ++p; }
        const size_t digitsBefore = p - digitsStart;
        const size_t dotPos = p;
        const bool hasDot = (p < n && line[p] == '.');
        if (hasDot) { ++p; }
        const size_t afterDot = p;
        while (p < n && isDigitCh(line[p])) { ++p; }
        const size_t digitsAfter = p - afterDot;
        if (digitsBefore == 0 && digitsAfter == 0) { ++i; continue; }
        const size_t valueEnd = (hasDot && digitsAfter == 0) ? dotPos : p;
        Word w;
        w.letter = line[i];
        w.valueStart = valueStart;
        w.valueLen = valueEnd - valueStart;
        out.push_back(w);
        i = valueEnd;
    }
    return out;
}

// The reference text.splitlines: line boundaries \n, \r, \r\n, \v, \f,
// 0x1c-0x1e. 0x85 does not count as a boundary: the file is read as bytes, and a
// lone 0x85 inside UTF-8 would be half of another character.
std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool brk = (c == '\n' || c == '\r' || c == '\v' || c == '\f'
                          || (c >= 0x1c && c <= 0x1e));
        if (!brk) { ++i; continue; }
        out.push_back(text.substr(start, i - start));
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') { ++i; }
        ++i;
        start = i;
    }
    if (start < text.size()) { out.push_back(text.substr(start)); }
    return out;
}

// The reference _COMMENT_RE.sub("", s): all (...) without a nested ')'.
std::string stripComments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '(') {
            const size_t close = s.find(')', i + 1);
            if (close == std::string::npos) {
                out.append(s, i, std::string::npos);
                break;
            }
            i = close + 1;
            continue;
        }
        out.push_back(s[i]);
        ++i;
    }
    return out;
}

// Tool change: the pattern \bT\s*(\d+)\s*M0?6\b; digits - what the group captured.
struct ToolChange {
    bool found = false;
    std::string digits;
};

ToolChange findToolChange(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != 'T') { continue; }
        if (i > 0 && isWordCh(s[i - 1])) { continue; }        // \b before T
        size_t p = i + 1;
        while (p < s.size() && isSpaceCh(static_cast<unsigned char>(s[p]))) { ++p; }
        const size_t digitsStart = p;
        while (p < s.size() && isDigitCh(s[p])) { ++p; }
        if (p == digitsStart) { continue; }
        const std::string digits = s.substr(digitsStart, p - digitsStart);
        while (p < s.size() && isSpaceCh(static_cast<unsigned char>(s[p]))) { ++p; }
        if (p >= s.size() || s[p] != 'M') { continue; }
        ++p;
        if (p < s.size() && s[p] == '0') { ++p; }
        if (p >= s.size() || s[p] != '6') { continue; }
        ++p;
        if (p < s.size() && isWordCh(s[p])) { continue; }     // \b after 6
        ToolChange tc;
        tc.found = true;
        tc.digits = digits;
        return tc;
    }
    return ToolChange{};
}

bool startsWithNoCase(const std::string& s, const char *prefix) {
    const size_t n = std::strlen(prefix);
    if (s.size() < n) { return false; }
    for (size_t k = 0; k < n; ++k) {
        if (!sameNoCase(s[k], prefix[k])) { return false; }
    }
    return true;
}

double envDouble(const char *name, double def) {
    const char *v = std::getenv(name);
    if (v == nullptr || *v == '\0') { return def; }
    return strtodC(v, nullptr);
}

bool samePoint(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    // A tuple in the reference compares element-wise, so -0.0 equals 0.0.
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

// Arc offsets of this line: I, J, K, R, Q and F (F also lives in the reference
// ijkr).
struct Ijkr {
    bool hasI = false, hasJ = false, hasK = false;
    bool hasR = false, hasQ = false, hasF = false;
    double i = 0.0, j = 0.0, k = 0.0, r = 0.0, q = 0.0, f = 0.0;

    bool any() const { return hasI || hasJ || hasK || hasR || hasQ || hasF; }
    bool has(char c) const {
        if (c == 'I') { return hasI; }
        if (c == 'J') { return hasJ; }
        if (c == 'K') { return hasK; }
        if (c == 'R') { return hasR; }
        if (c == 'Q') { return hasQ; }
        return hasF;
    }
    double get(char c) const {
        if (c == 'I') { return i; }
        if (c == 'J') { return j; }
        if (c == 'K') { return k; }
        if (c == 'R') { return r; }
        if (c == 'Q') { return q; }
        return f;
    }
    void set(char c, double v) {
        if (c == 'I') { hasI = true; i = v; }
        else if (c == 'J') { hasJ = true; j = v; }
        else if (c == 'K') { hasK = true; k = v; }
        else if (c == 'R') { hasR = true; r = v; }
        else if (c == 'Q') { hasQ = true; q = v; }
        else { hasF = true; f = v; }
    }
};

// Directed angular length of the arc (the reference _arc_sweep).
double arcSweep(double sx, double sy, double ex, double ey, double cx, double cy, bool cw) {
    double a0 = std::atan2(sy - cy, sx - cx);
    double a1 = std::atan2(ey - cy, ex - cx);
    if (cw) {
        while (a1 >= a0 - 1e-9) { a1 -= 2.0 * kPi; }
    } else {
        while (a1 <= a0 + 1e-9) { a1 += 2.0 * kPi; }
    }
    return a1 - a0;
}

// Tessellation of a G2/G3 arc in the active plane (the screw runs along the
// normal axis).
std::vector<std::array<double, 3>> arcPoints(const std::array<double, 3>& start,
                                             const std::array<double, 3>& end,
                                             const Ijkr& ij, bool cw, Plane plane,
                                             double maxStepDeg = 6.0) {
    int u = 0, v = 1, w = 2;
    planeAxes(plane, u, v, w);
    const double sx = start[u], sy = start[v];
    const double ex = end[u], ey = end[v];
    const double sw = start[w], ew = end[w];
    const double dx = ex - sx, dy = ey - sy;
    const double d = hypot2(dx, dy);
    char lu = 'I', lv = 'J';
    planeOffsets(plane, lu, lv);

    double cx = 0.0, cy = 0.0, sweep = 0.0;
    const bool hasOffset = ij.has(lu) || ij.has(lv);
    if (ij.hasR && !hasOffset && d > 1e-9) {
        const double r = std::fabs(ij.r);
        if (r < d / 2.0 - 1e-6) { return {start, end}; }
        const double mx = (sx + ex) / 2.0, my = (sy + ey) / 2.0;
        const double h = std::sqrt(std::max(0.0, r * r - std::pow(d / 2.0, 2.0)));
        const double ux = -dy / d, uy = dx / d;
        bool found = false;
        for (int s = 0; s < 2 && !found; ++s) {
            const double sign = (s == 0) ? 1.0 : -1.0;
            const double px = mx + sign * h * ux, py = my + sign * h * uy;
            const double sw2 = arcSweep(sx, sy, ex, ey, px, py, cw);
            const bool minor = std::fabs(sw2) <= kPi + 1e-6;
            if ((ij.r > 0) == minor) {
                cx = px;
                cy = py;
                sweep = sw2;
                found = true;
            }
        }
        if (!found) { return {start, end}; }
    } else if (hasOffset) {
        cx = sx + ij.get(lu);
        cy = sy + ij.get(lv);
        sweep = arcSweep(sx, sy, ex, ey, cx, cy, cw);
    } else {
        return {start, end};              // no center: straight chord
    }

    const double r = hypot2(sx - cx, sy - cy);
    if (r < 1e-9) { return {start, end}; }
    const double a0 = std::atan2(sy - cy, sx - cx);
    const int steps = std::max(2, static_cast<int>(std::fabs(sweep) * kRadToDeg / maxStepDeg) + 1);
    std::vector<std::array<double, 3>> pts;
    pts.reserve(static_cast<size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const double t = i / static_cast<double>(steps);
        const double a = a0 + sweep * t;
        std::array<double, 3> p{0.0, 0.0, 0.0};
        p[u] = cx + r * std::cos(a);
        p[v] = cy + r * std::sin(a);
        p[w] = sw + (ew - sw) * t;
        pts.push_back(p);
    }
    pts.front() = start;
    pts.back() = end;
    return pts;
}

// Parameters of the active canned cycle (the reference dict cycle).
struct Cycle {
    bool on = false;
    int code = 81;
    bool hasDepth = false;
    double depth = 0.0;
    bool hasR = false;
    double r = 0.0;
    bool hasQ = false;
    double q = 0.0;
};

// The reference emit: a point (a, b) with the OUTGOING segment flag and its
// speed. The branch order and the last-point replacement are repeated literally:
// the flags depend on them, and the rapid/feed stream is later cut by the flags.
class Emitter {
public:
    Emitter(std::vector<PathPoint>& ordered, const bool& feedSet, const double& feedVal,
            double rapidSpeed, double defaultFeed, double decimateTol2)
        : ordered_(ordered), feedSet_(feedSet), feedVal_(feedVal),
          rapidSpeed_(rapidSpeed), defaultFeed_(defaultFeed), decimateTol2_(decimateTol2) {}

    double decimateTol2() const { return decimateTol2_; }

    double speed(bool isRapid) const {
        if (isRapid) { return rapidSpeed_; }
        return (feedSet_ && feedVal_ > 0.0) ? feedVal_ / 60.0 : defaultFeed_;
    }

    // emit(a, b, is_rapid, decimate=False): without decimation.
    void line(const std::array<double, 3>& a, const std::array<double, 3>& b, bool isRapid) const {
        emit(a, b, isRapid, false, 0.0);
    }

    void emit(const std::array<double, 3>& a, const std::array<double, 3>& b, bool isRapid,
              bool decimate, double tol2) const {
        const double segSpeed = speed(isRapid);
        if (ordered_.empty()) {
            ordered_.push_back(PathPoint{a, isRapid, segSpeed});
            ordered_.push_back(PathPoint{b, isRapid, segSpeed});
            return;
        }
        if (decimate) {
            const std::array<double, 3> last = ordered_.back().point;
            const double dx = b[0] - last[0];
            const double dy = b[1] - last[1];
            const double dz = b[2] - last[2];
            if (dx * dx + dy * dy + dz * dz < tol2) { return; }
            ordered_.back() = PathPoint{last, isRapid, segSpeed};
            ordered_.push_back(PathPoint{b, isRapid, segSpeed});
            return;
        }
        if (samePoint(ordered_.back().point, a)) {
            ordered_.back() = PathPoint{a, isRapid, segSpeed};
        } else {
            ordered_.push_back(PathPoint{a, isRapid, segSpeed});
        }
        ordered_.push_back(PathPoint{b, isRapid, segSpeed});
    }

private:
    std::vector<PathPoint>& ordered_;
    const bool& feedSet_;
    const double& feedVal_;
    double rapidSpeed_;
    double defaultFeed_;
    double decimateTol2_;
};

// One canned drilling cycle at the point (hx, hy); pos is moved to the return
// plane (the reference _run_cycle).
void runCycle(const Cycle& cyc, std::array<double, 3>& pos, double hx, double hy,
              const Emitter& emit, bool retractToR) {
    const double z = cyc.hasDepth ? cyc.depth : pos[2];
    const double r = cyc.hasR ? cyc.r : pos[2];
    const int code = cyc.on ? cyc.code : 81;
    const double q = std::fabs(cyc.hasQ ? cyc.q : 0.0);
    std::array<double, 3> start = pos;
    const double clear = (start[2] >= r) ? start[2] : r;      // exit level
    if (start[2] < clear) {
        emit.line(start, {start[0], start[1], clear}, true);
        start[2] = clear;
    }
    emit.line(start, {hx, hy, start[2]}, true);               // XY exit
    if (start[2] > r) {
        emit.line({hx, hy, start[2]}, {hx, hy, r}, true);     // rapid to R
    }

    const double retractZ = retractToR ? r : clear;

    if ((code == 73 || code == 83) && q > 1e-6 && (r - z) > q + 1e-6) {
        const double clearance = std::min(1.0, std::max(0.2, q * 0.5));  // clearance above the peck bottom
        double bottom = r;                                                // bottom of the current peck
        double cur = r;                                                   // start of the feed
        while (true) {
            bottom = std::max(z, bottom - q);                             // next bottom (-Q)
            emit.line({hx, hy, cur}, {hx, hy, bottom}, false);            // feed to the peck
            if (bottom <= z + 1e-6) { break; }                            // reached the depth
            if (code == 83) {                                             // full retract (G83)
                emit.line({hx, hy, bottom}, {hx, hy, retractZ}, true);
                cur = std::min(bottom + clearance, r);
                emit.line({hx, hy, retractZ}, {hx, hy, cur}, true);
            } else {                                                      // chip breaking (G73)
                const double up = std::min(bottom + clearance, r);
                emit.line({hx, hy, bottom}, {hx, hy, up}, true);
                emit.line({hx, hy, up}, {hx, hy, bottom}, true);
                cur = bottom;
            }
        }
        emit.line({hx, hy, z}, {hx, hy, retractZ}, true);                 // final retract
    } else {
        emit.line({hx, hy, r}, {hx, hy, z}, false);                       // drill to depth
        emit.line({hx, hy, z}, {hx, hy, retractZ}, true);                 // retract
    }

    pos = {hx, hy, retractZ};
}

// Operation stages: the tool of an operation is the first UI_TOOL inside its
// range; operations with the previous tool inherit it (the reference
// _ops_with_tools).
std::vector<Stage> opsWithTools(const std::vector<Stage>& opMarks,
                                const std::vector<Stage>& toolMarks) {
    std::vector<Stage> tmarks = toolMarks;
    std::stable_sort(tmarks.begin(), tmarks.end(),
                     [](const Stage& a, const Stage& b) { return a.index < b.index; });
    const size_t n = opMarks.size();
    std::vector<Stage> out;
    out.reserve(n);
    ToolInfo lastInfo;
    for (size_t i = 0; i < n; ++i) {
        const int idx = opMarks[i].index;
        const std::string name = opMarks[i].label;
        const double end = (i + 1 < n) ? static_cast<double>(opMarks[i + 1].index)
                                       : std::numeric_limits<double>::infinity();
        ToolInfo info;
        for (const Stage& tm : tmarks) {
            if (static_cast<double>(idx) <= tm.index && static_cast<double>(tm.index) < end) {
                info = tm.info;
                break;
            }
        }
        if (info.present) { lastInfo = info; }
        else { info = lastInfo; }                    // same tool as the previous operation
        std::string label = name;
        if (info.present) { label += " \xC2\xB7 " + stageLabel(info); }
        Stage st;
        st.index = idx;
        st.label = label;
        st.info = info;
        out.push_back(st);
    }
    return out;
}

enum class Motion { None, G0, G1, Arc };

} // namespace

ParseResult parseGcode(const std::string& text) {
    const double decimateTol = envDouble("CNC_DECIMATE_TOL", 0.5);
    const double decimateTol2 = decimateTol * decimateTol;
    const double arcDecimateTol = envDouble("CNC_ARC_DECIMATE_TOL", 0.1);
    const double arcDecimateTol2 = arcDecimateTol * arcDecimateTol;
    const double rapidSpeed = envDouble("CNC_RAPID_SPEED_MMS", 500.0);
    const double defaultFeed = envDouble("CNC_DEFAULT_FEED_MMS", 100.0);

    std::vector<PathPoint> ordered;
    std::vector<Stage> uiToolMarks;      // stages from UI_TOOL tags
    std::vector<Stage> uiOpMarks;        // stages from explicit UI_OP tags
    std::vector<Stage> opMarks;          // stages from operation comment lines
    std::vector<Stage> toolMarks;        // stages from tool changes (fallback path)
    bool seenCode = false;               // a line with motion was seen
    std::string prevCode;                // the last code line (to skip a tool-change comment)
    std::array<double, 3> pos{0.0, 0.0, 0.0};
    Motion motion = Motion::None;
    Plane plane = Plane::XY;
    bool absolute = true;
    bool arcCw = false;
    Cycle cycle;
    bool cycleRetract = false;
    bool feedSet = false;
    double feedVal = 0.0;

    Emitter emit(ordered, feedSet, feedVal, rapidSpeed, defaultFeed, decimateTol2);
    auto markIndex = [&ordered]() { return std::max(0, static_cast<int>(ordered.size()) - 1); };

    // Lines are prepared once per NC line: slice up to ';', strip comments, trim
    // and uppercase. Previously the same was computed twice - separately for the
    // "comment line" test and for word parsing (110585 lines).
    const std::vector<std::string> lines = splitLines(text);
    ordered.reserve(lines.size());                 // estimate: about one path point per line
    for (const std::string& raw : lines) {
        const std::string up = asciiUpper(raw);
        const size_t semi = raw.find(';');
        std::string line = (semi == std::string::npos) ? raw : raw.substr(0, semi);
        line = stripComments(line);
        stripInPlace(line);
        line = asciiUpper(line);
        // A comment line: no code, but '(' is present - the whole text was a
        // comment.
        const bool standaloneComment = line.empty() && raw.find('(') != std::string::npos;
        if (up.find("UI_TOOL") != std::string::npos) {
            // Tool change: sets the cutter geometry for the next section.
            const ToolInfo info = parseUiTool(raw);
            Stage st;
            st.index = markIndex();
            st.label = stageLabel(info);
            st.info = info;
            uiToolMarks.push_back(st);
        } else if (up.find("UI_OP") != std::string::npos) {
            // An explicit operation marker: an unambiguous section start.
            std::string inner = commentText(raw);
            const size_t innerLen = inner.size();
            size_t p = 0;
            while (p < innerLen && isSpaceCh(static_cast<unsigned char>(inner[p]))) { ++p; }
            if (p + 5 <= innerLen && startsWithNoCase(inner.substr(p, 5), "UI_OP")) {
                size_t q = p + 5;
                while (q < innerLen && isSpaceCh(static_cast<unsigned char>(inner[q]))) { ++q; }
                if (q < innerLen && inner[q] == ':') {
                    ++q;
                    while (q < innerLen && isSpaceCh(static_cast<unsigned char>(inner[q]))) { ++q; }
                    inner = inner.substr(q);
                }
            }
            stripInPlace(inner);
            Stage st;
            st.index = markIndex();
            st.label = inner.empty() ? "\xD0\x9E\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F"
                                     : inner;
            uiOpMarks.push_back(st);
        } else if (standaloneComment) {
            // A bare comment after the program start - the section's operation
            // comment. UI_* tags and a comment right after a tool change are
            // skipped.
            if (seenCode) {
                const std::string inner = commentText(raw);
                if (!inner.empty() && !startsWithNoCase(inner, "UI_")
                        && !findToolChange(prevCode).found) {
                    Stage st;
                    st.index = markIndex();
                    st.label = inner;
                    opMarks.push_back(st);
                }
            }
        } else {
            const ToolChange tc = findToolChange(up);
            if (tc.found) {
                Stage st;
                st.index = markIndex();
                st.label = "T" + tc.digits;
                toolMarks.push_back(st);
            }
        }

        if (line.empty()) { continue; }
        prevCode = line;
        const std::vector<Word> words = findWords(line);
        if (words.empty()) { continue; }
        if (!seenCode) {
            for (const Word& w : words) {
                if (w.letter == 'X' || w.letter == 'Y' || w.letter == 'Z') {
                    seenCode = true;             // the first axial move is the start of the program body
                    break;
                }
            }
        }

        std::array<double, 3> target = pos;
        bool hasCoord = false;
        int newCycle = 0;                        // G73/G81/G82/G83 on this line
        Ijkr ij;
        bool zGiven = false, rGiven = false, qGiven = false, fGiven = false;

        for (const Word& w : words) {
            const char letter = w.letter;
            // Only these letters have their value read. For the others (M, S, T,
            // ...) it is unused, and the number is not parsed - on Test_model.ngc
            // that is 110587 skips out of 342038 words.
            if (letter != 'G' && letter != 'X' && letter != 'Y' && letter != 'Z'
                    && letter != 'I' && letter != 'J' && letter != 'K'
                    && letter != 'R' && letter != 'Q' && letter != 'F') {
                continue;
            }
            double val = 0.0;
            if (!toDouble(line.data() + w.valueStart, w.valueLen, val)) { continue; }
            if (letter == 'G') {
                // Exact comparisons: G91.1 must not read as G91.
                if (val == 0.0) { motion = Motion::G0; cycle.on = false; }
                else if (val == 1.0) { motion = Motion::G1; cycle.on = false; }
                else if (val == 2.0) { motion = Motion::Arc; arcCw = true; cycle.on = false; }
                else if (val == 3.0) { motion = Motion::Arc; arcCw = false; cycle.on = false; }
                else if (val == 17.0) { plane = Plane::XY; }
                else if (val == 18.0) { plane = Plane::XZ; }
                else if (val == 19.0) { plane = Plane::YZ; }
                else if (val == 80.0) { cycle.on = false; motion = Motion::None; }
                else if (val == 73.0 || val == 81.0 || val == 82.0 || val == 83.0) {
                    newCycle = static_cast<int>(val);
                }
                else if (val == 98.0) { cycleRetract = false; }   // return to the initial level
                else if (val == 99.0) { cycleRetract = true; }    // return to the R plane
                else if (val == 90.0) { absolute = true; }
                else if (val == 91.0) { absolute = false; }
                // G90.1/G91.1 (arc distance mode) and the rest are ignored
            } else if (letter == 'X' || letter == 'Y' || letter == 'Z') {
                const int idx = (letter == 'X') ? 0 : ((letter == 'Y') ? 1 : 2);
                target[idx] = absolute ? val : target[idx] + val;
                hasCoord = true;
                if (letter == 'Z') { zGiven = true; }
            } else if (letter == 'I' || letter == 'J' || letter == 'K') {
                ij.set(letter, val);
            } else if (letter == 'R') {
                ij.set('R', val);
                rGiven = true;
            } else if (letter == 'Q') {
                qGiven = true;
                ij.set('Q', val);
            } else if (letter == 'F') {
                fGiven = true;
                ij.set('F', val);
                if (val > 0.0) {
                    feedSet = true;
                    feedVal = val;
                }
            }
        }

        // ---- canned drilling cycle --------------------------------------
        if (newCycle != 0 || (cycle.on && hasCoord)) {
            if (newCycle != 0) {
                cycle.on = true;
                cycle.code = newCycle;
            }
            if (zGiven) { cycle.hasDepth = true; cycle.depth = target[2]; }
            if (rGiven) { cycle.hasR = true; cycle.r = ij.r; }
            if (qGiven) { cycle.hasQ = true; cycle.q = ij.q; }
            if (cycle.hasDepth) { runCycle(cycle, pos, target[0], target[1], emit, cycleRetract); }
            continue;
        }

        // ---- arcs -------------------------------------------------------
        if (motion == Motion::Arc && (hasCoord || ij.any())) {
            const std::vector<std::array<double, 3>> pts = arcPoints(pos, target, ij, arcCw, plane);
            for (size_t k = 0; k + 1 < pts.size(); ++k) {
                emit.emit(pts[k], pts[k + 1], false, true, arcDecimateTol2);
            }
            pos = target;
        } else if (hasCoord && (motion == Motion::G0 || motion == Motion::G1)) {
            const bool isRapid = (motion == Motion::G0);
            emit.emit(pos, target, isRapid, !isRapid, decimateTol2);   // only G1 is decimated
            pos = target;
        } else {
            pos = target;
        }
    }

    // The program end is kept exactly: it may have been decimated.
    if (!ordered.empty() && !samePoint(ordered.back().point, pos)) {
        ordered.push_back(PathPoint{pos, ordered.back().is_rapid, ordered.back().speed_mm_s});
    }

    // Operation stages: explicit UI_OP first, then section comments, then tool
    // changes, otherwise one stage. A stage's tool travels with it.
    std::vector<Stage> stages;
    if (!uiOpMarks.empty()) { stages = opsWithTools(uiOpMarks, uiToolMarks); }
    else if (!opMarks.empty()) { stages = opsWithTools(opMarks, uiToolMarks); }
    else if (!uiToolMarks.empty()) { stages = uiToolMarks; }
    else if (!toolMarks.empty()) { stages = toolMarks; }
    else { stages.push_back(Stage{0, "\xD0\xA3\xD0\x9F", ToolInfo{}}); }   // RU "УП"

    // The first stage always starts at the very beginning of the program: the
    // approaches at the start belong to it.
    stages[0] = Stage{0, stages[0].label, stages[0].info};

    ParseResult result;
    result.ordered = ordered;
    result.last = pos;
    result.seg_count = std::max(0, static_cast<int>(ordered.size()) - 1);
    result.stages = stages;
    return result;
}

} // namespace gv
