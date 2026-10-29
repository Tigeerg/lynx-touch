// cli/app.cpp - the top-level dispatcher of the console (7.2).
//
// One place decides the mode and none of them decides about a toolpath: the
// `--op` and `--demo` documents are built in cli/operation_program.cpp and
// cli/demo_program.cpp, the shared tail (validate, plan, emit, summary, files)
// is cli/run.cpp, and the words of each operation are parsed by
// core/*_options.cpp. The `--save-qprog` word is split out first, because both
// `--demo` and `--op ...` accept it and the operation parsers must not see it.
#include "cli/app.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "cli/demo_program.h"
#include "cli/help.h"
#include "cli/operation_program.h"
#include "cli/run.h"
#include "core/circle_pocket_options.h"
#include "core/cli.h"
#include "core/drill_options.h"
#include "core/face_options.h"
#include "core/pocket_options.h"
#include "core/serialize.h"
#include "core/version.h"

namespace qpcli {
namespace {

// Splits `--save-qprog <path>` out of the argument list. False when the word
// has no value: the caller then reports the usage error.
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

int runCirclePocket(std::vector<std::string> args, const std::string& savePath) {
    qp::CirclePocketArgs options;
    if (!qp::parseCirclePocketArgs(args, 2, options)) {
        std::fprintf(stderr, "Аргументы --op circle-pocket не разобраны: см. quickprog --help\n");
        return qp::kExitBadArgs;
    }
    return finish(circlePocketProgram(options), options.output, savePath);
}

int runDrill(std::vector<std::string> args, const std::string& savePath) {
    qp::DrillArgs options;
    if (!qp::parseDrillArgs(args, 2, options)) {
        std::fprintf(stderr, "Аргументы --op drill не разобраны: см. quickprog --help\n");
        return qp::kExitBadArgs;
    }
    return finish(drillProgram(options), options.output, savePath);
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

int run(const std::vector<std::string>& argvArgs) {
    std::vector<std::string> args = argvArgs;
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
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "circle-pocket") {
        return runCirclePocket(args, savePath);
    }
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "drill") {
        return runDrill(args, savePath);
    }

    std::fprintf(stderr, "Аргументы не разобраны: см. quickprog --help\n");
    return qp::kExitBadArgs;
}

} // namespace qpcli
