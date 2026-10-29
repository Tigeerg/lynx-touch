// core/serialize_read.cpp - the .qprog reader (PLAN-QUICKPROG, 6, 7.2).
//
// The reader is strict on purpose: a generated file is edited by hand between
// two sessions, and a typo must be named, not guessed away. An unknown key, a
// repeated key, a missing required key, a number that does not parse and an
// unsupported schema are errors with the line number or the field name; the
// default of the model is written out by `serialize.cpp`, so the reader never
// applies a default of its own.
#include "core/serialize.h"

#include <cstdio>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/path_drill.h"
#include "core/qprog_reader.h"
#include "core/serialize_internal.h"

namespace qp {

ParseOutcome parseProgram(const std::string& text) {
    ParseOutcome out;
    Program program;
    bool hasSchema = false;
    bool hasName = false;
    bool hasUnits = false;
    bool hasWcs = false;
    bool hasStock = false;
    bool hasSafeZ = false;
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
    // notes line is optional) and the kind-specific keys must be exactly the
    // ones of the kind.
    auto closeOp = [&](int lineNo) -> bool {
        if (!inOp) { return true; }
        auto requireKey = [&](const std::string& key) -> bool {
            for (const std::string& s : opSeen) {
                if (s == "op." + key) { return true; }
            }
            out.ok = false;
            out.error = "строка " + std::to_string(lineNo) + ": в операции '" + op.id +
                        "' нет обязательного поля 'op." + key + "'.";
            return false;
        };
        for (const std::string& key : commonKeys()) {
            if (key == "notes") { continue; }
            if (!requireKey(key)) { return false; }
        }
        for (const std::string& key : keysOf(op.geometry)) {
            // The shape key of a drill or a circle is required as well; the
            // rect and the grid carry their shape in one key each.
            if (!requireKey(key)) { return false; }
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
            if (!hasSchema) {
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
            const std::string& kind = words[1];
            if (kind == "face") {
                op.geometry = FaceOp();
            } else if (kind == "pocket") {
                op.geometry = RectPocketOp();
            } else if (kind == "circle") {
                op.geometry = CirclePocketOp();
            } else if (kind == "drill") {
                op.geometry = DrillHoleOp();
            } else if (kind == "drill-grid") {
                op.geometry = DrillGridOp();
            } else if (kind == "drill-circle") {
                op.geometry = DrillCircleOp();
            } else {
                fail(where + "неизвестный вид операции '" + kind + "'.");
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
            opSeen.push_back(key);
            const std::string sub = key.substr(3);
            // The set of keys the kind takes: a key of another kind is an
            // unknown field, not a silently ignored line.
            bool known = false;
            for (const std::string& k : commonKeys()) {
                if (k == sub) { known = true; }
            }
            for (const std::string& k : keysOf(op.geometry)) {
                if (k == sub) { known = true; }
            }
            // The R plane of a drill is optional: a file without it means "the
            // default R", the same as the document of a form that never set it.
            if (!known && sub == "r" && isDrill(op.geometry)) { known = true; }
            if (!known) {
                fail(where + "неизвестное поле '" + key + "' для операции этого вида.");
                return out;
            }
            if (!readOpValue(op, sub, key, line, words, where, out.error)) {
                out.ok = false;
                return out;
            }
            continue;
        }

        // Header keys.
        seen.push_back(key);
        if (key == "schema") {
            if (words.size() != 2 || !parseQprogInt(words[1], program.schema)) {
                fail(where + "schema ждёт целое число.");
                return out;
            }
            if (program.schema != 1) {
                fail(where + "схема " + std::to_string(program.schema) + " не поддерживается: " +
                     "эта версия читает схему 1.");
                return out;
            }
            hasSchema = true;
        } else if (key == "name") {
            program.name = unquoteText(restAfter(line, 1));
            hasName = true;
        } else if (key == "units") {
            if (words.size() != 2) {
                fail(where + "units ждёт одно значение.");
                return out;
            }
            program.units = words[1];
            hasUnits = true;
        } else if (key == "wcs") {
            if (words.size() != 2) {
                fail(where + "wcs ждёт одно значение.");
                return out;
            }
            program.wcs = words[1];
            hasWcs = true;
        } else if (key == "stock" && !(words.size() == 2 && qprogNumber(words[1]))) {
            double v[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            bool ok = words.size() == 7;
            for (int i = 0; ok && i < 6; ++i) {
                ok = parseQprogNumber(words[static_cast<std::size_t>(i + 1)], v[i]);
            }
            if (!ok) {
                fail(where + "stock ждёт шесть чисел: xmin ymin zmin xmax ymax zmax.");
                return out;
            }
            program.stock.min = {v[0], v[1], v[2]};
            program.stock.max = {v[3], v[4], v[5]};
            hasStock = true;
        } else if (key == "safeZ") {
            if (words.size() != 2 || !parseQprogNumber(words[1], program.safeZ)) {
                fail(where + "safeZ ждёт число.");
                return out;
            }
            hasSafeZ = true;
        } else {
            fail(where + "неизвестное поле '" + key + "'.");
            return out;
        }
        if (end == std::string::npos) { break; }
    }
    if (!closeOp(lineNo)) { return out; }

    // The required header keys are named one by one: the message has to say
    // which field is missing, not "broken file".
    const std::vector<std::pair<bool, const char*>> required = {
        {hasSchema, "schema"}, {hasName, "name"}, {hasUnits, "units"},
        {hasWcs, "wcs"},       {hasStock, "stock"}, {hasSafeZ, "safeZ"}};
    for (const auto& entry : required) {
        const bool present = entry.first;
        const char* field = entry.second;
        if (!present) {
            fail(std::string("нет обязательного поля '") + field + "'.");
            return out;
        }
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
