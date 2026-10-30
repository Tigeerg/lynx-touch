// cli/run.cpp - the shared tail of every console run (7.2).
//
// The console prints what the planner computed and writes the files; the checks
// and the summary text live in the core (core/validate.cpp, core/summary.cpp),
// so this file only orders the calls and phrases the two console failures. The
// streams are the caller's, so the check reads the same lines the operator does.
#include "cli/run.h"

#include <cstddef>
#include <cstdio>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "core/cli.h"
#include "core/emit.h"
#include "core/path.h"
#include "core/path_circle_pocket.h"
#include "core/path_drill.h"
#include "core/path_pocket.h"
#include "core/serialize.h"
#include "core/summary.h"
#include "core/validate.h"

namespace qpcli {
namespace {

void printProblems(const std::vector<qp::Check>& problems, std::ostream& out) {
    out << "Проверки не прошли, программа не планируется:\n";
    for (const qp::Check& check : problems) {
        out << "  " << check.field << ": " << check.reason << "\n";
    }
}

// Warnings of one operation that do not block planning, phrased for the
// console. A warning that repeats a blocking check is dropped: the check is
// already on the screen with its field.
void printWarnings(const qp::Program& program, const qp::Operation& op,
                   const std::vector<qp::Check>& problems, std::ostream& out) {
    std::vector<std::string> warnings;
    if (std::holds_alternative<qp::FaceOp>(op.geometry)) {
        warnings = qp::faceWarnings(program, op);
    } else if (std::holds_alternative<qp::RectPocketOp>(op.geometry)) {
        warnings = qp::pocketWarnings(program, op);
    } else if (std::holds_alternative<qp::CirclePocketOp>(op.geometry)) {
        warnings = qp::circleWarnings(program, op);
    }
    for (const std::string& warning : warnings) {
        bool blocked = false;
        for (const qp::Check& check : problems) {
            if (warning.find(check.field) != std::string::npos) { blocked = true; }
        }
        if (!blocked) { out << "Внимание: " << warning << "\n"; }
    }
}

} // namespace

std::string documentText(const qp::Program& program) {
    // The document leaves in the schema of the file format, not in whatever the
    // builder put in the field: a saved file has to be readable by `--qprog`
    // right away, whichever path built it.
    qp::Program document = program;
    document.schema = qp::kQprogSchema;
    return qp::serialize(document);
}

namespace {

// Saves the document next to the .ngc when the operator asked for it: one call
// writes the editable model, the .ngc is written by the caller.
int saveProgram(const qp::Program& program, const std::string& path, std::ostream& out,
                std::ostream& err) {
    if (path.empty()) { return qp::kExitOk; }
    if (!qp::writeFile(path, documentText(program))) {
        err << "Не удалось записать файл: " << path << "\n";
        return qp::kExitFailed;
    }
    out << "Документ записан: " << path << "\n";
    return qp::kExitOk;
}

// A line of the summary that carries one or two numbers: the bytes of the
// summary are the contract of the previous slices, so the console keeps the
// `printf` spelling of the values instead of the default stream spelling.
std::string format2(const char* pattern, double a, double b) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), pattern, a, b);
    return std::string(buf);
}

} // namespace

int finish(const qp::Program& program, const std::string& output, const std::string& savePath,
           std::ostream& out, std::ostream& err) {
    const std::vector<qp::Check> problems = qp::validate(program);
    if (!problems.empty()) {
        printProblems(problems, out);
        for (std::size_t i = 0; i < program.operations.size(); ++i) {
            printWarnings(program, program.operations[i], problems, out);
        }
        return qp::kExitFailed;
    }
    const qp::Path planned = qp::planPath(program);
    const double seconds = qp::estimateSeconds(planned);
    const qp::StockBox& stock = program.stock;
    out << "Программа: " << program.name << "\n";
    out << format2("Заготовка: X %.1f..%.1f,", stock.min.x, stock.max.x)
        << format2(" Y %.1f..%.1f,", stock.min.y, stock.max.y)
        << format2(" Z %.1f..%.1f мм\n", stock.min.z, stock.max.z);
    // The program lines of the post processor: the work offset, the G64
    // tolerance and the M1 stop, which the operator cannot read from one
    // operation (PLAN-QUICKPROG, 7.3).
    out << qp::programSummary(program);
    for (std::size_t i = 0; i < planned.operations.size(); ++i) {
        const std::size_t first = planned.operations[i].first;
        const std::size_t last =
            i + 1 < planned.operations.size() ? planned.operations[i + 1].first : planned.moves.size();
        const std::size_t opIndex = static_cast<std::size_t>(planned.operations[i].opIndex);
        out << qp::operationSummary(program, planned, opIndex, first, last);
        printWarnings(program, program.operations[opIndex], problems, out);
    }
    out << qp::totalSummary(planned.operations.size(), planned.moves.size(), planned.lengthMm,
                            seconds)
        << "\n";
    out << format2("Оценка времени: %.0f с (%.1f мин)", seconds, seconds / 60.0)
        << ", без учёта разгона, реальное время немного больше\n";
    if (!qp::writeFile(output, qp::emitProgram(program, planned))) {
        err << "Не удалось записать файл: " << output << "\n";
        return qp::kExitFailed;
    }
    out << "Записано: " << output << "\n";
    return saveProgram(program, savePath, out, err);
}

} // namespace qpcli
