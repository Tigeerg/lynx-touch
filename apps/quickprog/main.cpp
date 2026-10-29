// main.cpp - console entry point of the quick programming demo (PLAN-QUICKPROG, 7.2).
//
// The demo proves the pipeline operations -> Path -> .ngc without a screen and
// without Qt. Exit codes: 0 - done, 1 - the document does not pass the checks
// or the file was not written, 2 - the arguments were not understood (the
// usual console convention, so a script can tell a usage error from a failure).
#include <cstdio>
#include <string>
#include <vector>

#include "core/cli.h"
#include "core/emit.h"
#include "core/face_options.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/validate.h"
#include "core/version.h"

namespace {

constexpr const char* kHelp =
    "quickprog - демо быстрого программирования УП.\n"
    "\n"
    "Использование:\n"
    "  quickprog --demo <файл.ngc>\n"
    "      пустая программа: шапка, заготовка и финал\n"
    "  quickprog --op face <ключи> <файл.ngc>\n"
    "      торцевание плоскости: полосы вдоль длинной стороны заготовки\n"
    "  quickprog --version | --help\n"
    "\n"
    "Ключи --op face:\n"
    "  --tool N          номер инструмента в таблице станка (по умолчанию 1)\n"
    "  --diameter D      диаметр фрезы, мм (по умолчанию 10)\n"
    "  --stock XxYxZ     габарит заготовки, мм, от нуля вниз по Z (100x80x20)\n"
    "  --top Z           Z верха заготовки, мм (0)\n"
    "  --plane Z         Z плоскости обработки, мм (вершина минус глубина)\n"
    "  --depth D         глубина ниже верха, мм (2)\n"
    "  --step-down S     шаг по Z за проход, мм (2)\n"
    "  --step-over P     шаг полос, % диаметра (70)\n"
    "  --feed F          рабочая подача, мм/мин (из таблицы calc.ts)\n"
    "  --plunge P        подача врезания, мм/мин (четверть рабочей)\n"
    "  --rpm N           обороты шпинделя, об/мин (из таблицы calc.ts)\n"
    "  --material M      aluminium | steel (aluminium)\n"
    "  --safe Z          безопасный Z, мм (верх + 5)\n"
    "  --name TEXT       имя программы в шапке (FACE)\n"
    "\n"
    "Пример:\n"
    "  quickprog --op face --tool 2 --diameter 10 --stock 100x80x20 --top 0 \\\n"
    "    --depth 5 --step-down 2 --step-over 70 out.ngc\n"
    "Код возврата: 0 - успех, 1 - проверки не прошли или файл не записан,\n"
    "2 - аргументы не разобраны.\n";

constexpr qp::Point3 kStockMin{-50.0, -40.0, -20.0};
constexpr qp::Point3 kStockMax{50.0, 40.0, 0.0};

// The document of `--op face`: the stock grows around its own centre so the
// demo frame stays symmetric, the top of the stock is the given Z.
qp::Program faceProgram(const qp::FaceArgs& args) {
    qp::Program program;
    program.name = args.name;
    program.wcs = "G54";
    program.stock.min = {-args.stockX / 2.0, -args.stockY / 2.0, args.topZ - args.stockZ};
    program.stock.max = {args.stockX / 2.0, args.stockY / 2.0, args.topZ};
    program.safeZ = args.safeGiven ? args.safeZ : args.topZ + 5.0;

    qp::Operation op;
    op.id = "Плоскость";
    op.tool.number = args.tool;
    op.tool.diameterMm = args.diameter;
    op.topZ = args.topZ;
    op.depthMm = args.depth;
    op.stepDownMm = args.stepDown;
    qp::FaceOp face;
    face.z = args.planeGiven ? args.planeZ : args.topZ - args.depth;
    face.stepOverPct = args.stepOverPct;
    op.geometry = face;

    const qp::Feeds table = qp::feedsFor(args.material, args.diameter, qp::ToolKind::Flat);
    op.cutting.spindleRpm = args.rpm > 0.0 ? args.rpm : table.rpm;
    op.cutting.feedMmMin = args.feed > 0.0 ? args.feed : table.feedMmMin;
    op.cutting.plungeFeedMmMin = args.plunge > 0.0 ? args.plunge : table.plungeFeedMmMin;
    program.operations.push_back(op);
    return program;
}

// `source` is the table line the form started from: the document does not keep
// the material yet, so the console names the row it used instead of guessing
// it back from the numbers.
void printSummary(const std::string& path, const qp::Program& program, const qp::Path& planned,
                  const std::string& source) {
    const qp::Operation& op = program.operations.front();
    const qp::FaceStats stats = qp::faceStats(program, op);
    const double seconds = qp::estimateSeconds(planned);
    std::printf("Программа: %s\n", program.name.c_str());
    std::printf("Заготовка: X %.1f..%.1f, Y %.1f..%.1f, Z %.1f..%.1f мм\n",
                program.stock.min.x, program.stock.max.x, program.stock.min.y, program.stock.max.y,
                program.stock.min.z, program.stock.max.z);
    std::printf("Инструмент: T%d, ф%.1f мм, режимы %s\n", op.tool.number, op.tool.diameterMm,
                source.c_str());
    std::printf("Об/мин: %.0f, подача: %.0f мм/мин, врезание: %.0f мм/мин\n",
                op.cutting.spindleRpm, op.cutting.feedMmMin, op.cutting.plungeFeedMmMin);
    std::printf("Операций: %zu, полос: %d, проходов по Z: %d, ходов: %zu\n",
                program.operations.size(), stats.strips, stats.passes, planned.moves.size());
    std::printf("Глубины проходов:");
    for (const double z : stats.stepDepthsMm) { std::printf(" %.2f", z); }
    std::printf(" мм\n");
    std::printf("Длина резания: %.1f мм\n", planned.lengthMm);
    std::printf("Оценка времени: %.0f с (%.1f мин), без ускорений и смены инструмента\n", seconds,
                seconds / 60.0);
    std::printf("Записано: %s\n", path.c_str());
}

void printProblems(const std::vector<qp::Check>& problems) {
    if (!problems.empty()) {
        std::printf("Проверки не прошли, операция не планируется:\n");
        for (const qp::Check& check : problems) {
            std::printf("  %s: %s\n", check.field.c_str(), check.reason.c_str());
        }
    }
}

void printWarnings(const qp::Program& program, const std::vector<qp::Check>& problems) {
    // A warning is printed only when it is not the same rule as a blocking
    // check: a safe Z below the top is there already, line by line.
    for (const std::string& warning : qp::faceWarnings(program, program.operations.front())) {
        bool blocked = false;
        for (const qp::Check& check : problems) {
            if (warning.find(check.field) != std::string::npos) { blocked = true; }
        }
        if (!blocked) { std::printf("Внимание: %s\n", warning.c_str()); }
    }
}

int runFace(const std::vector<std::string>& args) {
    qp::FaceArgs options;
    if (!qp::parseFaceArgs(args, 2, options)) {
        std::fprintf(stderr, "Аргументы --op face не разобраны: см. quickprog --help\n");
        return qp::kExitBadArgs;
    }
    const qp::Program program = faceProgram(options);
    const std::vector<qp::Check> problems = qp::validate(program);
    if (!problems.empty()) {
        printProblems(problems);
        printWarnings(program, problems);
        return qp::kExitFailed;
    }
    const qp::Path planned = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, planned);
    if (!qp::writeFile(options.output, gcode)) {
        std::fprintf(stderr, "Не удалось записать файл: %s\n", options.output.c_str());
        return qp::kExitFailed;
    }
    const std::string source =
        qp::feedsFor(options.material, options.diameter, qp::ToolKind::Flat).source;
    printSummary(options.output, program, planned, source);
    printWarnings(program, problems);
    return qp::kExitOk;
}

// A demo document: another small program, no operations, 100x80x20 stock. It
// shows the header contract of the file before the first operation lands.
qp::Program demoProgram() {
    qp::Program program;
    program.name = "QUICKPROG DEMO";
    program.wcs = "G54";
    program.stock.min = kStockMin;
    program.stock.max = kStockMax;
    program.safeZ = 5.0;
    return program;
}

int runDemo(const std::string& path) {
    const qp::Program program = demoProgram();
    const qp::Path planned = qp::planPath(program);
    if (!qp::writeFile(path, qp::emitProgram(program, planned))) {
        std::fprintf(stderr, "Не удалось записать файл: %s\n", path.c_str());
        return qp::kExitFailed;
    }
    std::printf("Программа: %s\nЗаписано: %s\n", program.name.c_str(), path.c_str());
    return qp::kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::fputs(kHelp, stderr);
        return qp::kExitBadArgs;
    }
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        std::fputs(kHelp, stdout);
        return qp::kExitOk;
    }
    if (args.size() == 1 && args[0] == "--version") {
        std::printf("%s %s\n", qp::kGeneratorName, qp::kGeneratorVersion);
        return qp::kExitOk;
    }
    if (args.size() == 2 && args[0] == "--demo" && !args[1].empty()) { return runDemo(args[1]); }
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "face") { return runFace(args); }

    std::fprintf(stderr, "Аргументы не разобраны: см. quickprog --help\n");
    return qp::kExitBadArgs;
}
