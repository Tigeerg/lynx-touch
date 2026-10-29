// main.cpp - console entry point of the quick programming demo (PLAN-QUICKPROG, 7.2).
//
// The demo proves the pipeline operations -> Path -> .ngc without a screen and
// without Qt. Exit codes: 0 - done, 1 - the document does not pass the checks,
// a file is missing or was not written, 2 - the arguments were not understood
// (the usual console convention, so a script can tell a usage error from a
// failure).
//
// The console is a thin shell over the core: it builds a document, validates
// it, plans it, emits it and writes two files - the editable `.qprog` and the
// ready `.ngc`. Nothing here decides about a toolpath.
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>

#include "core/cli.h"
#include "core/emit.h"
#include "core/face_options.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_pocket.h"
#include "core/pocket_options.h"
#include "core/serialize.h"
#include "core/validate.h"
#include "core/version.h"

namespace {

constexpr const char* kHelp =
    "quickprog - демо быстрого программирования УП.\n"
    "\n"
    "Использование:\n"
    "  quickprog --demo <файл.ngc> [--save-qprog <файл.qprog>]\n"
    "      пример из двух операций: торцевание и карман; .qprog сохраняет его\n"
    "  quickprog --op face <ключи> <файл.ngc>\n"
    "      торцевание плоскости: полосы вдоль длинной стороны заготовки\n"
    "  quickprog --op pocket <ключи> <файл.ngc>\n"
    "      прямоугольный карман растром от стенки к стенке\n"
    "  quickprog --qprog <файл.qprog> <файл.ngc>\n"
    "      готовый документ: все включённые операции по порядку в один .ngc\n"
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
    "Ключи --op pocket:\n"
    "  --size WxH        размер кармана, мм (60x40); можно --width и --height\n"
    "  --x X --y Y       левый нижний угол кармана в координатах станка (0, 0)\n"
    "  --corner R        радиус угла, мм (0): пока не обрабатывается\n"
    "  --allowance A     припуск на стенку и дно, мм (0)\n"
    "  остальные ключи   как у торцевания: --tool, --diameter, --stock, --top,\n"
    "                    --depth, --step-down, --feed, --plunge, --rpm,\n"
    "                    --material, --safe, --name\n"
    "\n"
    "Пример:\n"
    "  quickprog --op face --tool 2 --diameter 10 --stock 100x80x20 --top 0 \\\n"
    "    --depth 5 --step-down 2 --step-over 70 --save-qprog demo.qprog face.ngc\n"
    "  quickprog --op pocket --tool 3 --diameter 8 --size 60x40 --depth 6 \\\n"
    "    --step-down 2.5 --allowance 0.2 pocket.ngc\n"
    "  quickprog --qprog demo.qprog demo.ngc\n"
    "Код возврата: 0 - успех, 1 - проверки не прошли или файл не записан,\n"
    "2 - аргументы не разобраны.\n";

constexpr qp::Point3 kStockMin{-50.0, -40.0, -20.0};
constexpr qp::Point3 kStockMax{50.0, 40.0, 0.0};

// The stock of an `--op` document grows around its own centre so the frame
// stays symmetric, the top of the stock is the given Z.
void setStock(qp::Program& program, double sizeX, double sizeY, double sizeZ, double topZ) {
    program.stock.min = {-sizeX / 2.0, -sizeY / 2.0, topZ - sizeZ};
    program.stock.max = {sizeX / 2.0, sizeY / 2.0, topZ};
}

// The document of `--op face`.
qp::Program faceProgram(const qp::FaceArgs& args) {
    qp::Program program;
    program.name = args.name;
    program.wcs = "G54";
    setStock(program, args.stockX, args.stockY, args.stockZ, args.topZ);
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

// The document of `--op pocket`. The pocket corner is taken as typed, in
// machine coordinates; the stock is centred on zero like the face one.
qp::Program pocketProgram(const qp::PocketArgs& args) {
    qp::Program program;
    program.name = args.name;
    program.wcs = "G54";
    setStock(program, args.stockX, args.stockY, args.stockZ, args.topZ);
    program.safeZ = args.safeGiven ? args.safeZ : args.topZ + 5.0;

    qp::Operation op;
    op.id = "Карман";
    op.tool.number = args.tool;
    op.tool.diameterMm = args.diameter;
    op.topZ = args.topZ;
    op.depthMm = args.depth;
    op.stepDownMm = args.stepDown;
    qp::RectPocketOp pocket;
    pocket.rect = qp::Rect{args.x, args.y, args.width, args.height};
    pocket.cornerRadiusMm = args.cornerRadius;
    pocket.finishAllowanceMm = args.finishAllowance;
    op.geometry = pocket;

    const qp::Feeds table = qp::feedsFor(args.material, args.diameter, qp::ToolKind::Flat);
    op.cutting.spindleRpm = args.rpm > 0.0 ? args.rpm : table.rpm;
    op.cutting.feedMmMin = args.feed > 0.0 ? args.feed : table.feedMmMin;
    op.cutting.plungeFeedMmMin = args.plunge > 0.0 ? args.plunge : table.plungeFeedMmMin;
    program.operations.push_back(op);
    return program;
}

// A demo document: two operations, the facing of a 100x80 stock top and a
// 60x40 pocket with an 8 mm end mill. The tool changes between them, so the
// example shows the tool-change rule of the emitter.
qp::Program demoProgram() {
    qp::Program program;
    program.name = "QUICKPROG DEMO";
    program.wcs = "G54";
    program.stock.min = kStockMin;
    program.stock.max = kStockMax;
    program.safeZ = 5.0;

    qp::Operation face;
    face.id = "Плоскость";
    face.tool.number = 2;
    face.tool.diameterMm = 10.0;
    face.cutting.spindleRpm = 4775.0;
    face.cutting.feedMmMin = 860.0;
    face.cutting.plungeFeedMmMin = 215.0;
    face.topZ = 0.0;
    face.depthMm = 2.0;
    face.stepDownMm = 2.0;
    qp::FaceOp faceGeometry;
    faceGeometry.z = -2.0;
    faceGeometry.stepOverPct = 70.0;
    face.geometry = faceGeometry;
    program.operations.push_back(face);

    qp::Operation pocket;
    pocket.id = "Карман 60x40";
    pocket.tool.number = 3;
    pocket.tool.diameterMm = 8.0;
    pocket.cutting.spindleRpm = 5968.0;
    pocket.cutting.feedMmMin = 859.0;
    pocket.cutting.plungeFeedMmMin = 215.0;
    pocket.topZ = 0.0;
    pocket.depthMm = 6.0;
    pocket.stepDownMm = 2.5;
    qp::RectPocketOp pocketGeometry;
    pocketGeometry.rect = qp::Rect{-30.0, -20.0, 60.0, 40.0};
    pocketGeometry.finishAllowanceMm = 0.2;
    // The demo shows the rounded-corner warning, not a rounded corner.
    pocketGeometry.cornerRadiusMm = 3.0;
    pocket.geometry = pocketGeometry;
    program.operations.push_back(pocket);
    return program;
}

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
    }
    for (const std::string& warning : warnings) {
        bool blocked = false;
        for (const qp::Check& check : problems) {
            if (warning.find(check.field) != std::string::npos) { blocked = true; }
        }
        if (!blocked) { std::printf("Внимание: %s\n", warning.c_str()); }
    }
}

// The name of an operation for the summary: the id, or the kind when the id is
// empty (a document written by hand).
std::string opName(const qp::Operation& op, std::size_t index) {
    if (!op.id.empty()) { return op.id; }
    return "операция " + std::to_string(index + 1);
}

// Per-operation summary of the planned block: what the operator checks before
// a run. `first`/`last` delimit the moves of the operation in the path.
void printOperation(const qp::Program& program, const qp::Path& planned, std::size_t index,
                    std::size_t first, std::size_t last) {
    const qp::Operation& op = program.operations[index];
    const std::size_t moves = last - first;
    double cutLength = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const qp::Move& move = planned.moves[i];
        if (move.kind != qp::MoveKind::Cut || i == 0) { continue; }
        const qp::Point3& from = planned.moves[i - 1].to;
        const double dx = move.to.x - from.x;
        const double dy = move.to.y - from.y;
        const double dz = move.to.z - from.z;
        cutLength += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    std::printf("Операция %zu/%zu: %s, T%d ф%.1f мм\n", index + 1, program.operations.size(),
                opName(op, index).c_str(), op.tool.number, op.tool.diameterMm);
    if (std::holds_alternative<qp::FaceOp>(op.geometry)) {
        const qp::FaceStats stats = qp::faceStats(program, op);
        std::printf("  полос: %d, проходов по Z: %d, ходов: %zu, длина резания: %.1f мм\n",
                    stats.strips, stats.passes, moves, cutLength);
    } else if (std::holds_alternative<qp::RectPocketOp>(op.geometry)) {
        const qp::PocketStats stats = qp::pocketStats(program, op);
        std::printf("  карман %.1fx%.1f, отступ %.2f мм: рядов %d, шаг %.3f мм, проходов по Z: %d\n",
                    std::get<qp::RectPocketOp>(op.geometry).rect.w,
                    std::get<qp::RectPocketOp>(op.geometry).rect.h, stats.insetMm, stats.rows,
                    stats.stepMm, stats.passes);
        std::printf("  ходов: %zu, длина резания: %.1f мм\n", moves, cutLength);
    } else {
        std::printf("  этот вид операции пока не планируется: ходов %zu\n", moves);
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

// Shared tail of every run: validate, plan, emit, print the summary, write the
// files. `savePath` empty means "do not write a .qprog".
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
        printOperation(program, planned, static_cast<std::size_t>(planned.operations[i].opIndex),
                       first, last);
        printWarnings(program, program.operations[static_cast<std::size_t>(planned.operations[i].opIndex)],
                      problems);
    }
    std::printf("Всего: операций %zu, ходов %zu, длина резания %.1f мм\n",
                planned.operations.size(), planned.moves.size(), planned.lengthMm);
    std::printf("Оценка времени: %.0f с (%.1f мин), без ускорений\n", seconds, seconds / 60.0);
    if (!qp::writeFile(output, qp::emitProgram(program, planned))) {
        std::fprintf(stderr, "Не удалось записать файл: %s\n", output.c_str());
        return qp::kExitFailed;
    }
    std::printf("Записано: %s\n", output.c_str());
    const int saved = saveProgram(program, savePath);
    return saved;
}

// Splits `--save-qprog <path>` out of the argument list: both `--op ...` and
// `--demo` accept it, and the operation parsers must not see the word.
bool takeSaveKey(std::vector<std::string>& args, std::string& savePath) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] != "--save-qprog") { continue; }
        if (i + 1 >= args.size()) { return false; }
        savePath = args[i + 1];
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                   args.begin() + static_cast<std::ptrdiff_t>(i + 2));
        return true;
    }
    return true;
}

int runFace(std::vector<std::string> args, const std::string& savePath) {
    qp::FaceArgs options;
    if (!qp::parseFaceArgs(args, 2, options)) {
        std::fprintf(stderr, "Аргументы --op face не разобраны: см. quickprog --help\n");
        return qp::kExitBadArgs;
    }
    return finish(faceProgram(options), options.output, savePath);
}

int runPocket(std::vector<std::string> args, const std::string& savePath) {
    qp::PocketArgs options;
    if (!qp::parsePocketArgs(args, 2, options)) {
        std::fprintf(stderr, "Аргументы --op pocket не разобраны: см. quickprog --help\n");
        return qp::kExitBadArgs;
    }
    return finish(pocketProgram(options), options.output, savePath);
}

int runDemo(const std::string& output, const std::string& savePath) {
    return finish(demoProgram(), output, savePath);
}

int runQprog(const std::string& input, const std::string& output, const std::string& savePath) {
    std::string text;
    if (!qp::readFile(input, text)) {
        std::fprintf(stderr, "Не удалось прочитать файл: %s\n", input.c_str());
        return qp::kExitFailed;
    }
    const qp::ParseOutcome parsed = qp::parseProgram(text);
    if (!parsed.ok) {
        std::fprintf(stderr, "Документ %s не разобран: %s\n", input.c_str(), parsed.error.c_str());
        return qp::kExitFailed;
    }
    return finish(parsed.program, output, savePath);
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
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
    std::string savePath;
    if (!takeSaveKey(args, savePath)) {
        std::fprintf(stderr, "--save-qprog: не указан путь: см. quickprog --help\n");
        return qp::kExitBadArgs;
    }
    if (args.size() == 2 && args[0] == "--demo" && !args[1].empty()) {
        return runDemo(args[1], savePath);
    }
    if (args.size() == 3 && args[0] == "--qprog" && !args[1].empty() && !args[2].empty()) {
        return runQprog(args[1], args[2], savePath);
    }
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "face") { return runFace(args, savePath); }
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "pocket") {
        return runPocket(args, savePath);
    }

    std::fprintf(stderr, "Аргументы не разобраны: см. quickprog --help\n");
    return qp::kExitBadArgs;
}
