// tests/checks_pocket_qprog.cpp - the pocket document through the .qprog.
//
// A document of three operations comes back with the same fields, the same
// order and the same kind of each operation, and a second serialization is
// byte-identical. A hand edit that breaks the file is named with the line or
// the field: the reader never applies a default of its own.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_pocket.h"
#include "core/serialize.h"
#include "core/validate.h"

#include "tests/checks.h"
#include "tests/checks_pocket_ops.h"
#include "tests/harness.h"

namespace pocket_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::demoProgram;
using qpcheck::near;

void checkPocketRoundTrip() {
    qp::Program program = pocketProgram();
    program.name = "Документ с карманом";
    qp::Operation face = program.operations.front();
    face.id = "Плоскость";
    face.notes = "верхняя операция";
    program.operations.insert(program.operations.begin(), face);
    qp::Operation second = pocketOp();
    second.id = "Второй карман";
    second.tool.number = 4;
    second.tool.diameterMm = 6.0;
    std::get<qp::RectPocketOp>(second.geometry).rect = qp::Rect{-10.0, -5.0, 20.0, 10.0};
    program.operations.push_back(second);

    const std::string text = qp::serialize(program);
    const qp::ParseOutcome parsed = qp::parseProgram(text);
    check(parsed.ok, "qprog.round_trip_ok", parsed.ok ? "" : parsed.error);
    if (!parsed.ok) { return; }
    const qp::Program& back = parsed.program;
    check(back.name == program.name && back.wcs == program.wcs && back.schema == program.schema,
          "qprog.round_trip_header", "the header fields must come back unchanged");
    check(back.operations.size() == program.operations.size(), "qprog.round_trip_count",
          "the operation count must come back unchanged");
    bool same = back.operations.size() == program.operations.size();
    for (std::size_t i = 0; same && i < program.operations.size(); ++i) {
        const qp::Operation& a = program.operations[i];
        const qp::Operation& b = back.operations[i];
        same = a.id == b.id && a.tool.number == b.tool.number &&
               near(a.tool.diameterMm, b.tool.diameterMm) && near(a.depthMm, b.depthMm) &&
               near(a.stepDownMm, b.stepDownMm) && a.geometry.index() == b.geometry.index();
        if (same && std::holds_alternative<qp::RectPocketOp>(a.geometry)) {
            const qp::RectPocketOp& pa = std::get<qp::RectPocketOp>(a.geometry);
            const qp::RectPocketOp& pb = std::get<qp::RectPocketOp>(b.geometry);
            same = near(pa.rect.x, pb.rect.x) && near(pa.rect.y, pb.rect.y) &&
                   near(pa.rect.w, pb.rect.w) && near(pa.rect.h, pb.rect.h) &&
                   near(pa.cornerRadiusMm, pb.cornerRadiusMm) &&
                   near(pa.finishAllowanceMm, pb.finishAllowanceMm);
        }
    }
    check(same, "qprog.round_trip_fields", "every operation field must come back unchanged");
    check(qp::serialize(back) == text, "qprog.round_trip_stable",
          "a second serialization of the parsed document must be byte-identical");

    // Determinism: the same document twice gives the same bytes.
    check(qp::serialize(program) == text, "qprog.deterministic",
          "two serializations of one document must be equal");
}

// An unknown key is an error with the line number; a missing key is an error
// with the field name. The reader never applies a default of its own.

void checkQprogErrors() {
    qp::Program program = pocketProgram();
    const std::string good = qp::serialize(program);

    std::string unknown = good;
    unknown += "op.wobble 3\n";
    const qp::ParseOutcome badUnknown = qp::parseProgram(unknown);
    check(!badUnknown.ok && badUnknown.error.find("строка") != std::string::npos &&
              badUnknown.error.find("op.wobble") != std::string::npos,
          "qprog.unknown_field", "an unknown field must name the line and the key, got '" +
                                      badUnknown.error + "'");

    std::string missing;
    for (const std::string& line : [&]() {
             std::vector<std::string> out;
             std::string rest = good;
             while (!rest.empty()) {
                 const std::size_t end = rest.find('\n');
                 out.push_back(rest.substr(0, end == std::string::npos ? rest.size() : end));
                 if (end == std::string::npos) { break; }
                 rest = rest.substr(end + 1);
             }
             return out;
         }()) {
        if (line.compare(0, 7, "op.topZ") == 0) { continue; }
        missing += line + "\n";
    }
    const qp::ParseOutcome badMissing = qp::parseProgram(missing);
    check(!badMissing.ok && badMissing.error.find("op.topZ") != std::string::npos,
          "qprog.missing_field", "a missing field must name it, got '" + badMissing.error + "'");

    std::string broken = good;
    const std::size_t at = broken.find("safeZ ");
    if (at != std::string::npos) { broken.replace(at, 6, "safeZ abc"); }
    const qp::ParseOutcome badNumber = qp::parseProgram(broken);
    check(!badNumber.ok && badNumber.error.find("safeZ") != std::string::npos,
          "qprog.bad_number", "a broken number must name the field");

    std::string schema = good;
    schema.replace(0, 8, "schema 2");
    const qp::ParseOutcome badSchema = qp::parseProgram(schema);
    check(!badSchema.ok, "qprog.bad_schema", "an unsupported schema must be an error");
}

} // namespace pocket_checks

// Runs the .qprog cases of the pocket document.
void runPocketQprogChecks() {
    pocket_checks::checkPocketRoundTrip();
    pocket_checks::checkQprogErrors();
}
