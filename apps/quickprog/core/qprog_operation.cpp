// core/qprog_operation.cpp - the value reader of one `op.*` line.
//
// The reader of the .qprog file (`serialize_read.cpp`) knows the order of the
// keys and the table of the kinds; this file knows how many words each key
// takes and where the value goes. Split out of the reader so neither file
// carries the whole schema: a new key is one branch here and one line in
// `serialize.cpp`.
#include "core/feeds.h"
#include "core/serialize_internal.h"
#include "core/path_drill.h"
#include "core/qprog_reader.h"

#include <string>
#include <variant>
#include <vector>

namespace qp {

bool readOpValue(Operation& op, const std::string& sub, const std::string& key,
             const std::string& line, const std::vector<std::string>& words,
             const std::string& where, std::string& error) {
auto fail = [&error](const std::string& message) {
    error = message;
    return false;
};
    std::size_t want = 2;
    if (sub == "tool") {
        want = 3;
    } else if (sub == "rect") {
        want = 5;
    } else if (sub == "circle" || sub == "hole") {
        want = 4;
    } else if (sub == "grid") {
        want = 7;
    } else if (sub == "notes") {
        op.notes = words.size() > 1 ? unquoteText(restAfter(line, 1)) : std::string();
        return true;
    }
    if (words.size() != want) {
        return fail(where + "поле '" + key + "' ждёт " + std::to_string(want - 1) +
                    " знач., а получило " + std::to_string(words.size() - 1) + ".");
    }
    if (sub == "enabled") {
        if (words[1] != "on" && words[1] != "off") {
            return fail(where + "op.enabled ждёт 'on' или 'off'.");
        }
        op.enabled = words[1] == "on";
    } else if (sub == "coolant") {
        if (!parseCoolant(words[1], op.coolant)) {
            return fail(where + "op.coolant ждёт off, mist или flood.");
        }
    } else if (sub == "material") {
        // The stock material: the same two words as the console. A missing
        // line is the model default, aluminium.
        if (!parseMaterial(words[1], op.material)) {
            return fail(where + "op.material ждёт aluminium или steel.");
        }
    } else if (sub == "tool") {
        if (!parseQprogInt(words[1], op.tool.number) ||
            !parseQprogNumber(words[2], op.tool.diameterMm)) {
            return fail(where + "op.tool ждёт номер и диаметр: '" + line + "'.");
        }
    } else if (sub == "h") {
        // The H row of G43: a zero is the model default ("the tool number"),
        // so the field is correct only as an explicit row.
        if (!parseQprogInt(words[1], op.tool.hNumber)) {
            return fail(where + "op.h ждёт целый номер коррекции.");
        }
    } else if (sub == "spindle") {
        if (!parseQprogNumber(words[1], op.cutting.spindleRpm)) {
            return fail(where + "op.spindle ждёт число.");
        }
    } else if (sub == "feed") {
        if (!parseQprogNumber(words[1], op.cutting.feedMmMin)) {
            return fail(where + "op.feed ждёт число.");
        }
        // A drill has one feed: the plunge follows the working feed whatever
        // the file says, so the document never keeps two different numbers.
        if (isDrill(op.geometry)) { op.cutting.plungeFeedMmMin = op.cutting.feedMmMin; }
    } else if (sub == "plunge") {
        if (!parseQprogNumber(words[1], op.cutting.plungeFeedMmMin)) {
            return fail(where + "op.plunge ждёт число.");
        }
        if (isDrill(op.geometry)) { op.cutting.plungeFeedMmMin = drillFeed(op); }
    } else if (sub == "topZ") {
        if (!parseQprogNumber(words[1], op.topZ)) { return fail(where + "op.topZ ждёт число."); }
    } else if (sub == "depth") {
        if (!parseQprogNumber(words[1], op.depthMm)) {
            return fail(where + "op.depth ждёт число.");
        }
    } else if (sub == "stepDown") {
        if (!parseQprogNumber(words[1], op.stepDownMm)) {
            return fail(where + "op.stepDown ждёт число.");
        }
    } else if (sub == "stepOver") {
        // The face keeps its own step field; both pocket kinds carry theirs
        // (PLAN-QUICKPROG, 7.3). The kind decides where the number goes, the
        // key is the same word in all three.
        double* target = nullptr;
        if (std::holds_alternative<FaceOp>(op.geometry)) {
            target = &std::get<FaceOp>(op.geometry).stepOverPct;
        } else if (std::holds_alternative<RectPocketOp>(op.geometry)) {
            target = &std::get<RectPocketOp>(op.geometry).stepOverPct;
        } else {
            target = &std::get<CirclePocketOp>(op.geometry).stepOverPct;
        }
        if (!parseQprogNumber(words[1], *target)) {
            return fail(where + "op.stepOver ждёт число.");
        }
    } else if (sub == "z") {
        if (!parseQprogNumber(words[1], std::get<FaceOp>(op.geometry).z)) {
            return fail(where + "op.z ждёт число.");
        }
    } else if (sub == "rect") {
        Rect& r = std::get<RectPocketOp>(op.geometry).rect;
        if (!parseQprogNumber(words[1], r.x) || !parseQprogNumber(words[2], r.y) ||
            !parseQprogNumber(words[3], r.w) || !parseQprogNumber(words[4], r.h)) {
            return fail(where + "op.rect ждёт x y w h: '" + line + "'.");
        }
    } else if (sub == "cornerRadius") {
        if (!parseQprogNumber(words[1], std::get<RectPocketOp>(op.geometry).cornerRadiusMm)) {
            return fail(where + "op.cornerRadius ждёт число.");
        }
    } else if (sub == "finishAllowance") {
        if (std::holds_alternative<RectPocketOp>(op.geometry)) {
            if (!parseQprogNumber(words[1],
                                  std::get<RectPocketOp>(op.geometry).finishAllowanceMm)) {
                return fail(where + "op.finishAllowance ждёт число.");
            }
        } else if (!parseQprogNumber(words[1],
                                     std::get<CirclePocketOp>(op.geometry).finishAllowanceMm)) {
            return fail(where + "op.finishAllowance ждёт число.");
        }
    } else if (sub == "circle" || sub == "hole") {
        double x = 0.0;
        double y = 0.0;
        double r = 0.0;
        if (!parseQprogNumber(words[1], x) || !parseQprogNumber(words[2], y) ||
            !parseQprogNumber(words[3], r)) {
            return fail(where + key + " ждёт x y r: '" + line + "'.");
        }
        const Circle circle{x, y, r};
        if (sub == "hole") {
            std::get<DrillHoleOp>(op.geometry).hole = circle;
        } else if (std::holds_alternative<CirclePocketOp>(op.geometry)) {
            std::get<CirclePocketOp>(op.geometry).circle = circle;
        } else {
            std::get<DrillCircleOp>(op.geometry).circle = circle;
        }
    } else if (sub == "grid") {
        auto& grid = std::get<DrillGridOp>(op.geometry);
        if (!parseQprogNumber(words[1], grid.x0) || !parseQprogNumber(words[2], grid.y0) ||
            !parseQprogInt(words[3], grid.cols) || !parseQprogInt(words[4], grid.rows) ||
            !parseQprogNumber(words[5], grid.dx) || !parseQprogNumber(words[6], grid.dy)) {
            return fail(where + "op.grid ждёт x0 y0 cols rows dx dy: '" + line + "'.");
        }
    } else if (sub == "count") {
        if (!parseQprogInt(words[1], std::get<DrillCircleOp>(op.geometry).count)) {
            return fail(where + "op.count ждёт целое.");
        }
    } else if (sub == "start") {
        if (!parseQprogNumber(words[1], std::get<DrillCircleOp>(op.geometry).startDeg)) {
            return fail(where + "op.start ждёт число.");
        }
    } else if (sub == "arc") {
        if (!parseQprogNumber(words[1], std::get<DrillCircleOp>(op.geometry).arcDeg)) {
            return fail(where + "op.arc ждёт число.");
        }
    } else if (sub == "fullTurn") {
        if (words[1] != "on" && words[1] != "off") {
            return fail(where + "op.fullTurn ждёт 'on' или 'off'.");
        }
        std::get<DrillCircleOp>(op.geometry).fullTurn = words[1] == "on";
    } else if (sub == "drill") {
        // The material of the drill body: the same two words as the console. A
        // missing line is the model default, HSS.
        DrillParams* params = nullptr;
        if (std::holds_alternative<DrillHoleOp>(op.geometry)) {
            params = &std::get<DrillHoleOp>(op.geometry).params;
        } else if (std::holds_alternative<DrillGridOp>(op.geometry)) {
            params = &std::get<DrillGridOp>(op.geometry).params;
        } else {
            params = &std::get<DrillCircleOp>(op.geometry).params;
        }
        if (!parseDrillKind(words[1], params->kind)) {
            return fail(where + "op.drill ждёт hss или carbide.");
        }
    } else if (sub == "peck" || sub == "r") {
        DrillParams* params = nullptr;
        if (std::holds_alternative<DrillHoleOp>(op.geometry)) {
            params = &std::get<DrillHoleOp>(op.geometry).params;
        } else if (std::holds_alternative<DrillGridOp>(op.geometry)) {
            params = &std::get<DrillGridOp>(op.geometry).params;
        } else {
            params = &std::get<DrillCircleOp>(op.geometry).params;
        }
        double* value = sub == "peck" ? &params->peckMm : &params->rMm;
        if (!parseQprogNumber(words[1], *value)) { return fail(where + key + " ждёт число."); }
        if (sub == "r") { params->hasR = true; }
    }
    return true;
}

} // namespace qp
