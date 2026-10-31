// core/serialize_read.cpp - the .qprog reader (PLAN-QUICKPROG, 6, 7.2).
//
// The reader is strict on purpose: a generated file is edited by hand between
// two sessions, and a typo must be named, not guessed away. An unknown key, a
// repeated key, a missing required key, a number that does not parse and an
// unsupported schema are errors with the line number or the field name; the
// default of the model is written out by `serialize.cpp`, so the reader never
// applies a default of its own.
//
// This file keeps the line loop and the assembly of the document: the header
// keys are read by serialize_read_fields.cpp, the kind word and the key table
// of an operation by serialize_read_op.cpp.
#include "core/serialize.h"

#include <string>
#include <variant>
#include <vector>

#include "core/qprog_reader.h"
#include "core/serialize_internal.h"
#include "core/serialize_read_fields.h"
#include "core/serialize_read_op.h"

namespace qp {
namespace {

// The layout keys a tap and a counterbore do not require: each writes the keys
// of the pattern it names, so the words of the other two patterns stay absent.
bool optionalLayoutKey(const std::string& key) {
    return key == "hole" || key == "grid" || key == "circle" || key == "count" ||
           key == "start" || key == "arc" || key == "fullTurn";
}

// True when the required key belongs to a drilling layout the operation does
// not use: then the line is optional, and `read-kind-keys.cpp` fills the rest
// of the layout from the pattern the file names.
bool superfluousLayoutKey(const OperationGeometry& geometry, const std::string& key) {
    if (!std::holds_alternative<TapOp>(geometry) && !std::holds_alternative<CounterboreOp>(geometry)) {
        return false;
    }
    return optionalLayoutKey(key);
}

} // namespace

ParseOutcome parseProgram(const std::string& text) {
    ParseOutcome out;
    Program program;
    HeaderSeen header;
    std::vector<std::string> seen;
    // The keys of the operation being read, with the `op.` prefix: the writer
    // writes `op.enabled`, the reader checks the same spelling. The count of
    // the `op.gcode` lines is kept apart: a custom block repeats the key on
    // purpose, so "seen" is not "once".
    std::vector<std::string> opSeen;
    int gcodeLines = 0;
    Operation op;
    bool inOp = false;

    int lineNo = 0;
    // Fills the outcome and returns "stop". A writer may hand over an empty
    // message: the caller names the line it was reading, so a refusal never
    // reads as "broken file" without a reason.
    auto fail = [&out, &lineNo](const std::string& message) {
        out.ok = false;
        out.error = message.empty() ? "строка " + std::to_string(lineNo) + ": не разобрана." : message;
        return false;
    };

    // Appends the finished operation: the common keys must all be there (the
    // notes line is optional) and the kind keys must be exactly the ones of
    // the kind. The list is read by index, so no loop walks a container the
    // lambda can change.
    auto closeOp = [&](int atLine) -> bool {
        if (!inOp) { return true; }
        std::vector<std::string> required = commonKeys();
        const std::vector<std::string> kindKeys = keysOf(op.geometry);
        required.insert(required.end(), kindKeys.begin(), kindKeys.end());
        for (std::size_t i = 0; i < required.size(); ++i) {
            // The notes line of an operation is optional. The raster step of a
            // pocket is optional too: a document of the previous build has no
            // `op.stepOver` for it and is read with the model default
            // (PLAN-QUICKPROG, 7.3). Everything else is required.
            if (required[i] == "notes") { continue; }
            // A drill layout of a tap or a counterbore writes the keys of its
            // own pattern only (`op.hole`, `op.grid` or `op.circle` with the
            // ring words); the other layout keys are optional lines of the same
            // kind, not a missing field.
            if (superfluousLayoutKey(op.geometry, required[i])) { continue; }
            // The peck of a tap is "the planner decides" when the file does not
            // name it, as it is for a drill: a zero is the model default.
            if (required[i] == "peck" && std::holds_alternative<TapOp>(op.geometry)) { continue; }
            const bool pocketStep = required[i] == "stepOver" &&
                                    (std::holds_alternative<RectPocketOp>(op.geometry) ||
                                     std::holds_alternative<CirclePocketOp>(op.geometry));
            if (pocketStep) { continue; }
            const std::string want = "op." + required[i];
            bool found = false;
            for (std::size_t j = 0; j < opSeen.size(); ++j) {
                if (opSeen[j] == want) { found = true; }
            }
            // A custom block writes one `op.gcode` per line; its key is in the
            // list once per line, and the block is required to write at least
            // one line (the panel refuses an empty block before the save). A
            // missing `op.source` of a rest is an empty list, the panel writes
            // it as the bare key.
            if (required[i] == "gcode") { found = gcodeLines > 0; }
            if (required[i] == "source" && std::holds_alternative<RestOp>(op.geometry)) {
                found = true;
            }
            if (found) { continue; }
            out.ok = false;
            out.error = "строка " + std::to_string(atLine) + ": в операции '" + op.id +
                        "' нет обязательного поля '" + want + "'.";
            return false;
        }
        program.operations.push_back(op);
        return true;
    };

    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = text.find('\n', at);
        std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        ++lineNo;
        const std::size_t next = end == std::string::npos ? text.size() + 1 : end + 1;
        if (lineNo == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) { line.erase(0, 3); }
        at = next;

        const std::vector<std::string> words = splitWords(line);
        if (words.empty() || words[0][0] == '#') {
            if (end == std::string::npos) { break; }
            continue;
        }
        const std::string& key = words[0];
        const std::string where = "строка " + std::to_string(lineNo) + ": ";
        const bool isOpKey = key.compare(0, 3, "op.") == 0;
        const bool isOpHead = key == "op";

        // Duplicate keys are a sign of a hand edit that lost track: the file
        // is generated, so the same key twice is an error, not "last wins".
        // The `op` head repeats per operation on purpose - it opens a new one,
        // so it is not a duplicate; every other key is unique in its scope:
        // the header keys in the document, the op keys in the operation.
        if (!isOpHead) {
            // A custom block writes one `op.gcode` per program line: the key
            // repeats by design and its order is the order of the block.
            const bool repeatedGcode = isOpKey && key == "op.gcode";
            for (const std::string& s : (isOpKey ? opSeen : seen)) {
                if (!repeatedGcode && s == key) {
                    fail(where + "поле '" + key + "' повторяется.");
                    return out;
                }
            }
        }

        if ((isOpHead || isOpKey) && !inOp) {
            if (!header.schema) {
                fail(where + "документ начинается не с 'schema 1'.");
                return out;
            }
            for (const char* requiredText : {"schema", "name", "units", "wcs", "stock", "safeZ"}) {
                const std::string required = requiredText;
                bool found = false;
                for (const std::string& s : seen) {
                    if (s == required) { found = true; }
                }
                if (!found) {
                    fail("нет обязательного поля '" + required + "' (перед операциями).");
                    return out;
                }
            }
        } else if (inOp) {
            // Still the header: `op.foo` after the first `op` is an operation
            // key, and an operation key of a kind that does not know it is
            // rejected below with the kind named.
        }

        if (isOpHead) {
            if (!closeOp(lineNo)) { return out; }
            if (words.size() < 2) {
                fail(where + "после 'op' нужен вид операции.");
                return out;
            }
            op = Operation();
            opSeen.clear();
            gcodeLines = 0;
            if (!readOpHead(words[1], op.geometry, where, out.error)) {
                out.ok = false;
                return out;
            }
            op.id = unquoteText(restAfter(line, 2));
            inOp = true;
            continue;
        }

        if (isOpKey) {
            if (!inOp) {
                fail(where + "поле '" + key + "' идёт до первой операции.");
                return out;
            }
            const std::string sub = key.substr(3);
            if (!opKeyKnown(op.geometry, sub)) {
                fail(where + "неизвестное поле '" + key + "' для операции этого вида.");
                return out;
            }
            if (!readOpValue(op, sub, key, line, words, where, out.error)) {
                out.ok = false;
                return out;
            }
            if (sub == "gcode") { ++gcodeLines; }
            opSeen.push_back(key);
            continue;
        }

        // Header keys: the arity and the field are known to
        // serialize_read_fields.cpp, the loop only knows that the key is not
        // an operation key.
        seen.push_back(key);
        if (!readHeaderKey(program, header, key, line, words, where, out.error)) {
            out.ok = false;
            return out;
        }
        if (end == std::string::npos) { break; }
    }
    if (!closeOp(lineNo)) { return out; }

    if (!headerComplete(header, out.error)) {
        out.ok = false;
        return out;
    }
    if (program.operations.empty()) {
        fail("нет ни одной операции: в документе должен быть хотя бы один 'op'.");
        return out;
    }
    out.ok = true;
    out.program = program;
    return out;
}
} // namespace qp
