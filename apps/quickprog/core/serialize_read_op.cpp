// core/serialize_read_op.cpp - the per-kind part of the .qprog reader (6, 7.2).
//
// The table of the kinds and the set of keys each kind accepts. The line loop
// and the document header are in serialize_read.cpp; the arity of one key and
// the field it fills are in qprog_operation.cpp.
#include "core/serialize_read_op.h"

#include <string>
#include <vector>

#include "core/path_drill.h"
#include "core/serialize_internal.h"

namespace qp {

bool readOpHead(const std::string& kind, OperationGeometry& geometry, const std::string& where,
                std::string& error) {
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
    // R", the same as the document of a form that never set it.
    return sub == "r" && isDrill(geometry);
}

} // namespace qp
