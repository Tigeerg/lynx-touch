// core/read-kind-common.cpp - the small value readers of the kinds the demo
// does not plan (PLAN-QUICKPROG, 7.6).
//
// One spelling of a wrong value: the key and what it expects, with the line
// number the caller prefixes. Shared by `read-kind-keys.cpp` and
// `read-kind-layout.cpp`, so the two readers of the panel kinds phrase a refused
// line the same way.
#include "core/read-kind-common.h"

#include <cstddef>
#include <string>
#include <vector>

#include "core/serialize_internal.h"

namespace qp {

// A value reader with one message spelling: the key and the expected value.
bool number(const std::vector<std::string>& words, const std::string& key, double& out,
            std::string& error, const std::string& where) {
    if (words.size() != 2 || !parseQprogNumber(words[1], out)) {
        error = where + key + " ждёт число.";
        return false;
    }
    return true;
}

bool onOff(const std::vector<std::string>& words, const std::string& key, bool& out,
           std::string& error, const std::string& where) {
    if (words.size() != 2 || (words[1] != "on" && words[1] != "off")) {
        error = where + key + " ждёт 'on' или 'off'.";
        return false;
    }
    out = words[1] == "on";
    return true;
}

// A point `x y z`: the ends of a slot and a chamfer.
bool walkPoint(const std::string& key, const std::vector<std::string>& words, Point3& out,
               std::string& error, const std::string& where) {
    if (words.size() != 4 || !parseQprogNumber(words[1], out.x) ||
        !parseQprogNumber(words[2], out.y) || !parseQprogNumber(words[3], out.z)) {
        error = where + key + " ждёт x y z: '" + words[0] + "'.";
        return false;
    }
    return true;
}

// The list of a contour: pairs `x y`, at least one pair and an even count.
bool pairs(const std::vector<std::string>& words, std::vector<Point3>& out, std::string& error,
           const std::string& where) {
    const std::size_t count = words.size() - 1;
    if (count < 2 || count % 2 != 0) {
        error = where + "op.points ждёт пары 'x y', а получило " + std::to_string(count) + " знач.";
        return false;
    }
    out.clear();
    for (std::size_t i = 1; i < words.size(); i += 2) {
        Point3 point;
        if (!parseQprogNumber(words[i], point.x) || !parseQprogNumber(words[i + 1], point.y)) {
            error = where + "op.points ждёт числа в паре 'x y'.";
            return false;
        }
        out.push_back(point);
    }
    return true;
}

// The list of a probe: triples `x y z`, at least one triple and a multiple of
// three. The same one-line style as the contour, with the Z of every touch.
bool triples(const std::vector<std::string>& words, std::vector<Point3>& out, std::string& error,
             const std::string& where) {
    const std::size_t count = words.size() - 1;
    if (count < 3 || count % 3 != 0) {
        error = where + "op.points ждёт тройки 'x y z', а получило " + std::to_string(count) + " знач.";
        return false;
    }
    out.clear();
    for (std::size_t i = 1; i < words.size(); i += 3) {
        Point3 point;
        if (!parseQprogNumber(words[i], point.x) || !parseQprogNumber(words[i + 1], point.y) ||
            !parseQprogNumber(words[i + 2], point.z)) {
            error = where + "op.points ждёт числа в тройке 'x y z'.";
            return false;
        }
        out.push_back(point);
    }
    return true;
}

// The `x y w h` of a rectangle: the island, the pocket of an island, a rest.
bool rectOf(const std::vector<std::string>& words, Rect& out, std::string& error,
            const std::string& key, const std::string& where) {
    if (words.size() != 5 || !parseQprogNumber(words[1], out.x) ||
        !parseQprogNumber(words[2], out.y) || !parseQprogNumber(words[3], out.w) ||
        !parseQprogNumber(words[4], out.h)) {
        error = where + key + " ждёт x y w h: '" + words[0] + "'.";
        return false;
    }
    return true;
}

} // namespace qp
