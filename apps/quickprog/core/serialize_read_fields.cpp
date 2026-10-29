// core/serialize_read_fields.cpp - the document header of a .qprog file (6, 7.2).
//
// One branch per header key: the arity it takes and the field it fills. The
// reader never applies a default of its own - the writer of serialize.cpp
// writes every default out - and the schema is checked here, so a file of a
// newer schema is named, not guessed away.
#include "core/serialize_read_fields.h"

#include <string>
#include <vector>

#include "core/serialize_internal.h"

namespace qp {

bool readHeaderKey(Program& program, HeaderSeen& seen, const std::string& key,
                   const std::string& line, const std::vector<std::string>& words,
                   const std::string& where, std::string& error) {
    auto fail = [&error](const std::string& message) {
        error = message;
        return false;
    };
    if (key == "schema") {
        if (words.size() != 2 || !parseQprogInt(words[1], program.schema)) {
            return fail(where + "schema ждёт целое число.");
        }
        if (program.schema != kQprogSchema) {
            return fail(where + "схема " + std::to_string(program.schema) + " не поддерживается: " +
                        "эта версия читает схему " + std::to_string(kQprogSchema) + ".");
        }
        seen.schema = true;
    } else if (key == "name") {
        program.name = unquoteText(restAfter(line, 1));
        seen.name = true;
    } else if (key == "units") {
        if (words.size() != 2) {
            return fail(where + "units ждёт одно значение.");
        }
        program.units = words[1];
        seen.units = true;
    } else if (key == "wcs") {
        if (words.size() != 2) {
            return fail(where + "wcs ждёт одно значение.");
        }
        program.wcs = words[1];
        seen.wcs = true;
    } else if (key == "stock" && !(words.size() == 2 && qprogNumber(words[1]))) {
        double v[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        bool ok = words.size() == 7;
        for (int i = 0; ok && i < 6; ++i) {
            ok = parseQprogNumber(words[static_cast<std::size_t>(i + 1)], v[i]);
        }
        if (!ok) {
            return fail(where + "stock ждёт шесть чисел: xmin ymin zmin xmax ymax zmax.");
        }
        program.stock.min = {v[0], v[1], v[2]};
        program.stock.max = {v[3], v[4], v[5]};
        seen.stock = true;
    } else if (key == "safeZ") {
        if (words.size() != 2 || !parseQprogNumber(words[1], program.safeZ)) {
            return fail(where + "safeZ ждёт число.");
        }
        seen.safeZ = true;
    } else {
        return fail(where + "неизвестное поле '" + key + "'.");
    }
    return true;
}

bool headerComplete(const HeaderSeen& seen, std::string& error) {
    // The required header keys are named one by one: the message has to say
    // which field is missing, not "broken file".
    const std::vector<std::pair<bool, const char*>> required = {
        {seen.schema, "schema"}, {seen.name, "name"}, {seen.units, "units"},
        {seen.wcs, "wcs"},       {seen.stock, "stock"}, {seen.safeZ, "safeZ"}};
    for (const auto& entry : required) {
        if (!entry.first) {
            error = std::string("нет обязательного поля '") + entry.second + "'.";
            return false;
        }
    }
    return true;
}

} // namespace qp
