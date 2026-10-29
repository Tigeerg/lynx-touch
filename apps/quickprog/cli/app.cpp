// cli/app.cpp - the top-level dispatcher of the console (7.2).
//
// One place decides the mode and none of them decides about a toolpath: the
// `--op` and `--demo` documents are built in cli/operation_program.cpp and
// cli/demo_program.cpp, the shared tail (validate, plan, emit, summary, files)
// is cli/run.cpp, and the words of each operation are parsed by
// core/*_options.cpp. The `--save-qprog` word is split out first, because both
// `--demo` and `--op ...` accept it and the operation parsers must not see it.
//
// `--help` and `--version` are recognised in any place of the line, before the
// mode words are read: `quickprog --help --demo x.ngc` prints the help, it is
// not a usage error. `--help` wins over `--version`.
#include "cli/app.h"

#include <cstddef>
#include <ostream>
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

int runFace(std::vector<std::string> args, const std::string& savePath, std::ostream& out,
            std::ostream& err) {
    qp::FaceArgs options;
    if (!qp::parseFaceArgs(args, 2, options)) {
        err << "Аргументы --op face не разобраны: см. quickprog --help\n";
        return qp::kExitBadArgs;
    }
    return finish(faceProgram(options), options.output, savePath, out, err);
}

int runPocket(std::vector<std::string> args, const std::string& savePath, std::ostream& out,
              std::ostream& err) {
    qp::PocketArgs options;
    if (!qp::parsePocketArgs(args, 2, options)) {
        err << "Аргументы --op pocket не разобраны: см. quickprog --help\n";
        return qp::kExitBadArgs;
    }
    return finish(pocketProgram(options), options.output, savePath, out, err);
}

int runCirclePocket(std::vector<std::string> args, const std::string& savePath, std::ostream& out,
                    std::ostream& err) {
    qp::CirclePocketArgs options;
    if (!qp::parseCirclePocketArgs(args, 2, options)) {
        err << "Аргументы --op circle-pocket не разобраны: см. quickprog --help\n";
        return qp::kExitBadArgs;
    }
    return finish(circlePocketProgram(options), options.output, savePath, out, err);
}

int runDrill(std::vector<std::string> args, const std::string& savePath, std::ostream& out,
             std::ostream& err) {
    qp::DrillArgs options;
    if (!qp::parseDrillArgs(args, 2, options)) {
        err << "Аргументы --op drill не разобраны: см. quickprog --help\n";
        return qp::kExitBadArgs;
    }
    return finish(drillProgram(options), options.output, savePath, out, err);
}

int runDemo(const std::string& output, const std::string& savePath, std::ostream& out,
            std::ostream& err) {
    return finish(demoProgram(), output, savePath, out, err);
}

int runQprog(const std::string& input, const std::string& output, const std::string& savePath,
             std::ostream& out, std::ostream& err) {
    std::string text;
    if (!qp::readFile(input, text)) {
        err << "Не удалось прочитать файл: " << input << "\n";
        return qp::kExitFailed;
    }
    const qp::ParseOutcome parsed = qp::parseProgram(text);
    if (!parsed.ok) {
        err << "Документ " << input << " не разобран: " << parsed.error << "\n";
        return qp::kExitFailed;
    }
    return finish(parsed.program, output, savePath, out, err);
}

} // namespace

Mode decideMode(const std::vector<std::string>& args) {
    bool version = false;
    for (const std::string& word : args) {
        if (word == "--help" || word == "-h") { return Mode::Help; }
        if (word == "--version") { version = true; }
    }
    if (version) { return Mode::Version; }
    return args.empty() ? Mode::BadArgs : Mode::Run;
}

int run(const std::vector<std::string>& argvArgs, std::ostream& out, std::ostream& err) {
    switch (decideMode(argvArgs)) {
    case Mode::Help:
        out << kHelp;
        return qp::kExitOk;
    case Mode::Version:
        out << qp::kGeneratorName << " " << qp::kGeneratorVersion << "\n";
        return qp::kExitOk;
    case Mode::BadArgs:
        err << kHelp;
        return qp::kExitBadArgs;
    case Mode::Run: break;
    }

    std::vector<std::string> args = argvArgs;
    std::string savePath;
    if (!takeSaveKey(args, savePath)) {
        err << "--save-qprog: не указан путь: см. quickprog --help\n";
        return qp::kExitBadArgs;
    }
    if (args.size() == 2 && args[0] == "--demo" && !args[1].empty()) {
        return runDemo(args[1], savePath, out, err);
    }
    if (args.size() == 3 && args[0] == "--qprog" && !args[1].empty() && !args[2].empty()) {
        return runQprog(args[1], args[2], savePath, out, err);
    }
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "face") {
        return runFace(args, savePath, out, err);
    }
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "pocket") {
        return runPocket(args, savePath, out, err);
    }
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "circle-pocket") {
        return runCirclePocket(args, savePath, out, err);
    }
    if (args.size() >= 2 && args[0] == "--op" && args[1] == "drill") {
        return runDrill(args, savePath, out, err);
    }

    err << "Аргументы не разобраны: см. quickprog --help\n";
    return qp::kExitBadArgs;
}

} // namespace qpcli
