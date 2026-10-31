// core/read-kind-layout.h - the drilling layout of a tap and a counterbore
// (PLAN-QUICKPROG, 7.6).
//
// The layout of the two kinds shares the words of a drill: the pattern word and
// the keys of the chosen pattern. A `Layout` names the fields of the operation
// by reference, so one reader serves both kinds; `read-kind-keys.cpp` builds the
// view from the operation it reads.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"

namespace qp {

// The fields of a tap or a counterbore the layout words fill, by reference.
struct Layout {
    HolePattern& pattern;
    Circle& hole;
    double& x0;
    double& y0;
    int& cols;
    int& rows;
    double& dx;
    double& dy;
    int& count;
    double& startDeg;
    double& arcDeg;
    bool& fullTurn;
};

// The fields of a tap, as the layout reader sees them.
Layout layoutOfTap(TapOp& tap);
// The fields of a counterbore, as the layout reader sees them.
Layout layoutOfCounterbore(CounterboreOp& cb);

// Reads one layout key into the fields of `l`. False and a message in `error`
// for a wrong arity or a word the key does not take; the `pattern` word is read
// here too, so the key table lists it with the layout.
bool layoutKey(Layout& l, const std::string& sub, const std::string& key,
               const std::vector<std::string>& words, const std::string& where, std::string& error);

} // namespace qp
