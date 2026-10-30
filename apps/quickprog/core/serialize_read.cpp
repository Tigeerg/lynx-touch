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

ParseOutcome parseProgram(const std::string& text) {
    ParseOutcome out;
    Program program;
    HeaderSeen header;
    std::vector<std::string> seen;
    // The keys of the operation being read, with the `op.` prefix: the writer
    // writes `op.enabled`, the reader checks the same spelling.
    std::vector<std::string> opSeen;
    Operation op;
    bool inOp = false;

    // Fills the outcome and returns "stop"; the message is already in `out`.
    auto fail = [&out](const std::string& message) {
        out.ok = false;
        out.error = message;
        return false;
    };

    // Appends the finished operation: the common keys must all be there (the
    // notes line is optional) and the kind keys must be exactly the ones of
    // the kind. The list is read by index, so no loop walks a container the
    // lambda can change.
    auto closeOp = [&](int lineNo) -> bool {
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
            const bool pocketStep = required[i] == "stepOver" &&
                                    (std::holds_alternative<RectPocketOp>(op.geometry) ||
                                     std::holds_alternative<CirclePocketOp>(op.geometry));
            if (pocketStep) { continue; }
            const std::string want = "op." + required[i];
            bool found = false;
            for (std::size_t j = 0; j < opSeen.size(); ++j) {
                if (opSeen[j] == want) { found = true; }
            }
            if (found) { continue; }
            out.ok = false;
            out.error = "строка " + std::to_string(lineNo) + ": в операции '" + op.id +
                        "' нет обязательного поля '" + want + "'.";
            return false;
        }
        program.operations.push_back(op);
        return true;
    };

    std::size_t at = 0;
    int lineNo = 0;
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
            for (const std::string& s : (isOpKey ? opSeen : seen)) {
                if (s == key) {
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
