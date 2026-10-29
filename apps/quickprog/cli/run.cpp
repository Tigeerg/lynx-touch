// cli/run.cpp - the shared tail of every console run (7.2).
//
// The console prints what the planner computed and writes the files; the checks
// and the summary text live in the core (core/validate.cpp, core/summary.cpp),
// so this file only orders the calls and phrases the two console failures.
#include "cli/run.h"

#include <cstddef>
#include <cstdio>
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

void printProblems(const std::vector<qp::Check>& problems) {
    std::printf("Проверки не прошли, программа не планируется:\n");
    for (const qp::Check& check : problems) {
        std::printf("  %s: %s\n", check.field.c_str(), check.reason.c_str());
    }
}

// Warnings of one operation that do not block planning, phrased for the
// console. A warning that repeats a blocking check is dropped: the check is
// already on the screen with its field.
void printWarnings(const qp::Program& program, const qp::Operation& op,
                   const std::vector<qp::Check>& problems) {
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
        if (!blocked) { std::printf("Внимание: %s\n", warning.c_str()); }
    }
}

// Saves the document next to the .ngc when the operator asked for it: one call
// writes the editable model, the .ngc is written by the caller.
int saveProgram(const qp::Program& program, const std::string& path) {
    if (path.empty()) { return qp::kExitOk; }
    if (!qp::writeFile(path, qp::serialize(program))) {
        std::fprintf(stderr, "Не удалось записать файл: %s\n", path.c_str());
        return qp::kExitFailed;
    }
    std::printf("Документ записан: %s\n", path.c_str());
    return qp::kExitOk;
}

} // namespace

int finish(const qp::Program& program, const std::string& output, const std::string& savePath) {
    const std::vector<qp::Check> problems = qp::validate(program);
    if (!problems.empty()) {
        printProblems(problems);
        for (std::size_t i = 0; i < program.operations.size(); ++i) {
            printWarnings(program, program.operations[i], problems);
        }
        return qp::kExitFailed;
    }
    const qp::Path planned = qp::planPath(program);
    const double seconds = qp::estimateSeconds(planned);
    std::printf("Программа: %s\n", program.name.c_str());
    std::printf("Заготовка: X %.1f..%.1f, Y %.1f..%.1f, Z %.1f..%.1f мм\n",
                program.stock.min.x, program.stock.max.x, program.stock.min.y, program.stock.max.y,
                program.stock.min.z, program.stock.max.z);
    for (std::size_t i = 0; i < planned.operations.size(); ++i) {
        const std::size_t first = planned.operations[i].first;
        const std::size_t last =
            i + 1 < planned.operations.size() ? planned.operations[i + 1].first : planned.moves.size();
        const std::size_t opIndex = static_cast<std::size_t>(planned.operations[i].opIndex);
        std::fputs(qp::operationSummary(program, planned, opIndex, first, last).c_str(), stdout);
        printWarnings(program, program.operations[opIndex], problems);
    }
    std::printf("%s\n",
                qp::totalSummary(planned.operations.size(), planned.moves.size(), planned.lengthMm,
                                 seconds)
                    .c_str());
    std::printf("Оценка времени: %.0f с (%.1f мин), без учёта разгона, реальное время немного "
                "больше\n",
                seconds, seconds / 60.0);
    if (!qp::writeFile(output, qp::emitProgram(program, planned))) {
        std::fprintf(stderr, "Не удалось записать файл: %s\n", output.c_str());
        return qp::kExitFailed;
    }
    std::printf("Записано: %s\n", output.c_str());
    const int saved = saveProgram(program, savePath);
    return saved;
}

} // namespace qpcli
