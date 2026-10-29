// main.cpp - console entry point of the quick programming demo (PLAN-QUICKPROG, 7.2).
//
// The demo proves the pipeline operations -> Path -> .ngc without a screen and
// without Qt. Exit codes: 0 - done, 2 - the arguments were not understood (the
// usual console convention, so a script can tell a usage error from a failure
// to write a file).
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/version.h"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitBadArgs = 2;
constexpr int kExitWriteFailed = 1;

constexpr const char* kHelp =
    "quickprog - демо быстрого программирования УП.\n"
    "\n"
    "Использование:\n"
    "  quickprog --demo <файл.ngc>   записать шапку и финал УП в файл\n"
    "  quickprog --version           показать версию генератора\n"
    "  quickprog --help              показать эту справку\n"
    "\n"
    "Демо пока не строит траекторию: в файл попадают шапка, заготовка и финал.\n"
    "Код возврата: 0 - успех, 1 - файл не записан, 2 - аргументы не разобраны.\n";

// A demo document: an empty program with a 100x80x20 mm stock and G54. The
// numbers are a placeholder for the console run, not a claim about a part; the
// real document comes from .qprog in a later slice.
qp::Program demoProgram() {
    qp::Program program;
    program.schema = 1;
    program.name = "QUICKPROG DEMO";
    program.units = "mm";
    program.wcs = "G54";
    program.stock.min = {-50.0, -40.0, -20.0};
    program.stock.max = {50.0, 40.0, 0.0};
    program.safeZ = 5.0;
    return program;
}

bool writeFile(const std::string& path, const std::string& text) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) { return false; }
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const bool flushed = std::fclose(file) == 0;
    return written == text.size() && flushed;
}

// The summary the operator reads: how many moves were planned and where the
// file lies. Russian, as the panel texts.
void printSummary(const std::string& path, const qp::Path& planned, const qp::Program& program) {
    std::printf("Программа: %s\n", program.name.c_str());
    std::printf("Заготовка: X %.1f..%.1f, Y %.1f..%.1f, Z %.1f..%.1f мм\n",
                program.stock.min.x, program.stock.max.x, program.stock.min.y, program.stock.max.y,
                program.stock.min.z, program.stock.max.z);
    std::printf("Операций: %zu\n", program.operations.size());
    std::printf("Ходов: %zu\n", planned.moves.size());
    std::printf("Записано: %s\n", path.c_str());
}

int runDemo(const std::string& path) {
    const qp::Program program = demoProgram();
    const qp::Path planned = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, planned);
    if (!writeFile(path, gcode)) {
        std::fprintf(stderr, "Не удалось записать файл: %s\n", path.c_str());
        return kExitWriteFailed;
    }
    printSummary(path, planned, program);
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        std::fputs(kHelp, stderr);
        return kExitBadArgs;
    }

    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        std::fputs(kHelp, stdout);
        return kExitOk;
    }
    if (args.size() == 1 && args[0] == "--version") {
        std::printf("%s %s\n", qp::kGeneratorName, qp::kGeneratorVersion);
        return kExitOk;
    }
    // `--demo` needs exactly one path: a bare flag or extra words are a usage
    // error, not a reason to guess a file name.
    if (args.size() == 2 && args[0] == "--demo" && !args[1].empty()) {
        return runDemo(args[1]);
    }

    std::fprintf(stderr, "Аргументы не разобраны: см. quickprog --help\n");
    return kExitBadArgs;
}
