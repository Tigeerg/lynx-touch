// core/read-kind-layout.cpp - the drilling layout of a tap and a counterbore
// (PLAN-QUICKPROG, 7.6).
//
// A tap and a counterbore take the same `op.pattern`, `op.hole`, `op.grid`,
// `op.count`, `op.start`, `op.arc` and `op.fullTurn` words a drill writes: one
// layout, one reader, split from the shape keys of `read-kind-keys.cpp` so the
// file of the shape reader stays in the size guide. The seat of the fastener
// and the pitch of the thread live beside the layout that carries them.
#include "core/read-kind-layout.h"

#include <string>
#include <vector>

#include "core/read-kind-common.h"
#include "core/serialize_internal.h"

namespace qp {

bool layoutKey(Layout& l, const std::string& sub, const std::string& key,
               const std::vector<std::string>& words, const std::string& where, std::string& error) {
    if (sub == "pattern") {
        if (words.size() != 2) {
            error = where + "op.pattern ждёт hole, grid или circle.";
            return false;
        }
        if (words[1] == "hole") {
            l.pattern = HolePattern::Hole;
        } else if (words[1] == "grid") {
            l.pattern = HolePattern::Grid;
        } else if (words[1] == "circle") {
            l.pattern = HolePattern::Circle;
        } else {
            error = where + "op.pattern ждёт hole, grid или circle.";
            return false;
        }
        return true;
    }
    if (sub == "hole" || sub == "circle") {
        if (words.size() != 4 || !parseQprogNumber(words[1], l.hole.x) ||
            !parseQprogNumber(words[2], l.hole.y) || !parseQprogNumber(words[3], l.hole.r)) {
            error = where + key + " ждёт x y r: '" + words[0] + "'.";
            return false;
        }
        return true;
    }
    if (sub == "grid") {
        if (words.size() != 7 || !parseQprogNumber(words[1], l.x0) ||
            !parseQprogNumber(words[2], l.y0) || !parseQprogInt(words[3], l.cols) ||
            !parseQprogInt(words[4], l.rows) || !parseQprogNumber(words[5], l.dx) ||
            !parseQprogNumber(words[6], l.dy)) {
            error = where + "op.grid ждёт x0 y0 cols rows dx dy: '" + words[0] + "'.";
            return false;
        }
        return true;
    }
    if (sub == "count") {
        if (words.size() != 2 || !parseQprogInt(words[1], l.count)) {
            error = where + "op.count ждёт целое.";
            return false;
        }
        return true;
    }
    if (sub == "fullTurn") { return onOff(words, key, l.fullTurn, error, where); }
    double& target = sub == "start" ? l.startDeg : l.arcDeg;
    return number(words, key, target, error, where);
}

Layout layoutOfTap(TapOp& tap) {
    return Layout{tap.pattern, tap.hole,  tap.x0,   tap.y0,    tap.cols,
                  tap.rows,    tap.dx,    tap.dy,   tap.count, tap.startDeg,
                  tap.arcDeg,  tap.fullTurn};
}

Layout layoutOfCounterbore(CounterboreOp& cb) {
    return Layout{cb.pattern, cb.hole,  cb.x0,   cb.y0,    cb.cols,
                  cb.rows,    cb.dx,    cb.dy,   cb.count, cb.startDeg,
                  cb.arcDeg,  cb.fullTurn};
}

} // namespace qp
