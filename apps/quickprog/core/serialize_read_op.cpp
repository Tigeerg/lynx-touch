// core/serialize_read_op.cpp - the per-kind part of the .qprog reader (6, 7.2).
//
// The table of the kinds and the set of keys each kind accepts. The line loop
// and the document header are in serialize_read.cpp; the arity of one key and
// the field it fills are in qprog_operation.cpp.
#include "core/serialize_read_op.h"

#include <string>
#include <variant>
#include <vector>

#include "core/path_drill.h"
#include "core/serialize_internal.h"

namespace qp {

bool readOpHead(const std::string& kind, OperationGeometry& geometry, const std::string& where,
                std::string& error) {
    // One branch per word of the schema: the four kinds of the demo planner and
    // the kinds the panel writes. The word is the one the panel writer prints
    // (`serialize-write.ts`), so a panel file reads as the panel wrote it.
    if (kind == "face") {
        geometry = FaceOp();
    } else if (kind == "pocket") {
        geometry = RectPocketOp();
    } else if (kind == "circle") {
        geometry = CirclePocketOp();
    } else if (kind == "drill") {
        geometry = DrillHoleOp();
    } else if (kind == "drill-grid") {
        geometry = DrillGridOp();
    } else if (kind == "drill-circle") {
        geometry = DrillCircleOp();
    } else if (kind == "contour") {
        geometry = ContourOp();
    } else if (kind == "slot") {
        geometry = SlotOp();
    } else if (kind == "chamfer") {
        geometry = ChamferOp();
    } else if (kind == "tap") {
        geometry = TapOp();
    } else if (kind == "counterbore") {
        geometry = CounterboreOp();
    } else if (kind == "thread") {
        geometry = ThreadOp();
    } else if (kind == "engrave") {
        geometry = EngraveOp();
    } else if (kind == "pocket-island") {
        geometry = IslandPocketOp();
    } else if (kind == "rest") {
        geometry = RestOp();
    } else if (kind == "custom") {
        geometry = CustomOp();
    } else if (kind == "probe") {
        geometry = ProbeOp();
    } else {
        error = where + "неизвестный вид операции '" + kind + "'.";
        return false;
    }
    return true;
}

bool opKeyKnown(const OperationGeometry& geometry, const std::string& sub) {
    // The set of keys the kind takes: a key of another kind is an unknown
    // field, not a silently ignored line.
    for (const std::string& k : commonKeys()) {
        if (k == sub) { return true; }
    }
    for (const std::string& k : keysOf(geometry)) {
        if (k == sub) { return true; }
    }
    // The R plane of a drill is optional: a file without it means "the default
    // R", the same as the document of a form that never set it. The H row of
    // G43 is optional for every kind: a file of the previous build has no
    // `op.h` and is read with "H = the tool number".
    // The R plane of a counterbore takes the same optional line.
    if (sub == "r") { return isDrill(geometry) || std::holds_alternative<CounterboreOp>(geometry); }
    // The material of the drill body is optional for a drill: a file of the
    // previous build has no `op.drill` and is read as HSS.
    if (sub == "drill") { return isDrill(geometry); }
    // The stock material is optional for every kind: a file of the previous
    // build has no `op.material` and is read as aluminium.
    if (sub == "material") { return true; }
    return sub == "h";
}

} // namespace qp
