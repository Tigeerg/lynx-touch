// core/read-kind-keys.cpp - the `op.*` keys of the kinds the demo does not plan
// (PLAN-QUICKPROG, 7.6).
//
// The panel writer (`serialize-write.ts`) is the contract of every key read
// here: a panel file must come back as the panel wrote it, so a key is a
// branch of arity and field and nothing more. No default is invented: a
// missing optional line keeps the model default, as the drill's R does, and
// the required ones are checked by the line loop (`serialize_read.cpp`) from
// the same key table (`keysOf` in `serialize_shared.cpp`).
#include "core/read-kind-keys.h"

#include <string>
#include <variant>
#include <vector>

#include "core/read-kind-common.h"
#include "core/read-kind-layout.h"
#include "core/serialize_internal.h"

namespace qp {

bool readKindKey(Operation& op, const std::string& sub, const std::string& key,
                 const std::string& line, const std::vector<std::string>& words,
                 const std::string& where, std::string& error) {
    auto fail = [&error](const std::string& message) {
        error = message;
        return false;
    };
    if (auto* contour = std::get_if<ContourOp>(&op.geometry)) {
        if (sub == "points") { return pairs(words, contour->points, error, where); }
        if (sub == "closed") { return onOff(words, key, contour->closed, error, where); }
        if (sub == "allowance") { return number(words, key, contour->allowanceMm, error, where); }
        if (sub == "side") {
            if (words.size() != 2 || (words[1] != "material" && words[1] != "air")) {
                return fail(where + "op.side ждёт material или air.");
            }
            contour->side = words[1];
            return true;
        }
        return false;
    }
    if (auto* slot = std::get_if<SlotOp>(&op.geometry)) {
        if (sub == "from") { return walkPoint(key, words, slot->from, error, where); }
        if (sub == "to") { return walkPoint(key, words, slot->to, error, where); }
        if (sub == "width") { return number(words, key, slot->widthMm, error, where); }
        if (sub == "stepOver") { return number(words, key, slot->stepOverPct, error, where); }
        return false;
    }
    if (auto* chamfer = std::get_if<ChamferOp>(&op.geometry)) {
        if (sub == "from") { return walkPoint(key, words, chamfer->from, error, where); }
        if (sub == "to") { return walkPoint(key, words, chamfer->to, error, where); }
        if (sub == "chamfer") { return number(words, key, chamfer->chamferMm, error, where); }
        return false;
    }
    if (auto* tap = std::get_if<TapOp>(&op.geometry)) {
        if (sub == "peck") { return number(words, key, tap->peckMm, error, where); }
        if (sub == "pitch") { return number(words, key, tap->pitchMm, error, where); }
        Layout layout = layoutOfTap(*tap);
        return layoutKey(layout, sub, key, words, where, error);
    }
    if (auto* cb = std::get_if<CounterboreOp>(&op.geometry)) {
        // The R plane of the cycle: an optional line, the panel rule of the
        // counterbore refuses a seat without it before the save.
        if (sub == "r") {
            if (!number(words, key, cb->rMm, error, where)) { return false; }
            cb->hasR = true;
            return true;
        }
        if (sub == "seat") { return number(words, key, cb->seatDiameterMm, error, where); }
        if (sub == "seatDepth") { return number(words, key, cb->seatDepthMm, error, where); }
        if (sub == "dwell") { return number(words, key, cb->dwellSec, error, where); }
        Layout layout = layoutOfCounterbore(*cb);
        return layoutKey(layout, sub, key, words, where, error);
    }
    if (auto* thread = std::get_if<ThreadOp>(&op.geometry)) {
        if (sub == "hole") {
            if (words.size() != 4 || !parseQprogNumber(words[1], thread->hole.x) ||
                !parseQprogNumber(words[2], thread->hole.y) ||
                !parseQprogNumber(words[3], thread->hole.r)) {
                return fail(where + key + " ждёт x y r: '" + words[0] + "'.");
            }
            return true;
        }
        if (sub == "pitch") { return number(words, key, thread->pitchMm, error, where); }
        if (sub == "internal") { return onOff(words, key, thread->internal, error, where); }
        if (sub == "threadDepth") { return number(words, key, thread->threadDepthMm, error, where); }
        if (sub == "allowance") { return number(words, key, thread->threadAllowanceMm, error, where); }
        if (sub == "stepOver") { return number(words, key, thread->stepOverPct, error, where); }
        return false;
    }
    if (auto* engrave = std::get_if<EngraveOp>(&op.geometry)) {
        // The text is the rest of the line, quoted or not, as `op.notes` is: a
        // hand-typed text with spaces is not an arity error.
        if (sub == "text") {
            engrave->text = words.size() > 1 ? unquoteText(restAfter(line, 1)) : std::string();
            return true;
        }
        double& target = sub == "x"        ? engrave->x
                         : sub == "y"      ? engrave->y
                         : sub == "height" ? engrave->heightMm
                                           : engrave->angleDeg;
        return number(words, key, target, error, where);
    }
    if (auto* island = std::get_if<IslandPocketOp>(&op.geometry)) {
        if (sub == "rect") { return rectOf(words, island->rect, error, key, where); }
        if (sub == "island") { return rectOf(words, island->island, error, key, where); }
        if (sub == "islandAllowance") { return number(words, key, island->islandAllowanceMm, error, where); }
        if (sub == "stepOver") { return number(words, key, island->stepOverPct, error, where); }
        return false;
    }
    if (auto* rest = std::get_if<RestOp>(&op.geometry)) {
        if (sub == "source") {
            // An empty list is the bare key: the panel writes `op.source` with no
            // numbers for a rest with no source.
            std::vector<int> list;
            for (std::size_t i = 1; i < words.size(); ++i) {
                int value = 0;
                if (!parseQprogInt(words[i], value) || value < 0) {
                    return fail(where + "op.source ждёт целые индексы: '" + words[i] + "'.");
                }
                bool seen = false;
                for (const int one : list) {
                    if (one == value) { seen = true; }
                }
                if (!seen) { list.push_back(value); }
            }
            rest->source = list;
            return true;
        }
        if (sub == "rect") { return rectOf(words, rest->rect, error, key, where); }
        if (sub == "allowance") { return number(words, key, rest->allowanceMm, error, where); }
        if (sub == "stepOver") { return number(words, key, rest->stepOverPct, error, where); }
        return false;
    }
    if (auto* custom = std::get_if<CustomOp>(&op.geometry)) {
        if (sub != "gcode") { return false; }
        // One line per `op.gcode`, in file order; a bare key is an empty line.
        custom->gcode.push_back(words.size() > 1 ? unquoteText(restAfter(line, 1)) : std::string());
        return true;
    }
    if (auto* probe = std::get_if<ProbeOp>(&op.geometry)) {
        if (sub == "points") { return triples(words, probe->points, error, where); }
        if (sub == "range") { return number(words, key, probe->rangeMm, error, where); }
        if (sub == "touch") { return number(words, key, probe->touchFeedMmMin, error, where); }
        if (sub == "axis") {
            if (words.size() != 2 || (words[1] != "X" && words[1] != "Y" && words[1] != "Z")) {
                return fail(where + "op.axis ждёт X, Y или Z.");
            }
            probe->axis = words[1] == "X" ? ProbeDirection::X
                          : words[1] == "Y" ? ProbeDirection::Y
                                            : ProbeDirection::Z;
            return true;
        }
        return false;
    }
    // A planned kind: its keys are read by `qprog_operation.cpp`, and the key
    // table has already refused a key the kind does not take.
    return false;
}

} // namespace qp
