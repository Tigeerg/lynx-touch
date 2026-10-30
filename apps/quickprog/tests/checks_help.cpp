// tests/checks_help.cpp - the console modes: `--help` and `--version` anywhere
// (7.2).
//
// The two words are recognised in any place of the command line, not only alone:
// `quickprog --help --demo x.ngc` prints the help and returns 0 instead of
// "аргументы не разобраны". `--help` wins over `--version`. The check calls the
// dispatcher itself and compares the text with `kHelp`, the constant the console
// prints, so it does not run the binary and does not trust a copy of the help.
#include <cstddef>
#include <string>
#include <vector>

#include "cli/app.h"
#include "cli/help.h"
#include "core/cli.h"

#include "tests/harness.h"

namespace help_checks {

using qpcheck::check;

// The single word: the reference answer of the help and the version.
struct Mode {
    int code = -1;
    std::string out;
};

// The dispatcher writes to the streams the caller gives it: the check reads the
// text of the help and the version instead of the terminal, and does not run the
// binary.
Mode runMode(const std::vector<std::string>& words) {
    qpcheck::ConsoleCapture console;
    Mode mode;
    mode.code = qpcli::run(words, console.out, console.err);
    mode.out = console.out.str();
    return mode;
}

void checkHelpAnywhere() {
    const Mode alone = runMode({"--help"});
    check(alone.code == qp::kExitOk && alone.out == qpcli::kHelp, "help.alone",
          "a single --help must print the help and return 0");
    const Mode shortAlone = runMode({"-h"});
    check(shortAlone.code == qp::kExitOk && shortAlone.out == qpcli::kHelp, "help.short",
          "a single -h must print the help and return 0");

    // The same help bytes in the first, the middle and the last place of a
    // compound line that would otherwise be a usage error.
    const std::vector<std::vector<std::string>> lines = {
        {"--help", "--demo", "x.ngc"},
        {"--op", "face", "--help", "--depth", "2", "a.ngc"},
        {"--demo", "x.ngc", "--version", "--help"},
        {"--version", "--help"},
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Mode mode = runMode(lines[i]);
        const std::string id = "help.line" + std::to_string(i + 1);
        check(mode.code == qp::kExitOk, id + ".code",
              "a line with --help in place " + std::to_string(i + 1) +
                  " must return 0, got " + std::to_string(mode.code));
        check(mode.out == alone.out, id + ".text",
              "the help of a compound line must be the bytes of the single --help");
    }

    // `--help` wins over `--version` in either order.
    const Mode versionThenHelp = runMode({"--version", "--help"});
    check(versionThenHelp.out == qpcli::kHelp, "help.over_version",
          "--help after --version must still print the help, not the version");
    const Mode helpThenVersion = runMode({"--help", "--version"});
    check(helpThenVersion.out == qpcli::kHelp, "help.over_version_reverse",
          "--help before --version must print the help, not the version");
}

void checkVersionAnywhere() {
    const Mode alone = runMode({"--version"});
    check(alone.code == qp::kExitOk, "version.alone_code",
          "a single --version must return 0, got " + std::to_string(alone.code));
    check(!alone.out.empty() && alone.out != std::string(qpcli::kHelp), "version.alone_text",
          "a single --version must print a version line, not the help");

    const std::vector<std::vector<std::string>> lines = {
        {"--version", "--demo", "x.ngc"},
        {"--op", "drill", "--version", "a.ngc"},
        {"--demo", "x.ngc", "--version"},
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Mode mode = runMode(lines[i]);
        const std::string id = "version.line" + std::to_string(i + 1);
        check(mode.code == qp::kExitOk, id + ".code",
              "a line with --version in place " + std::to_string(i + 1) +
                  " must return 0, got " + std::to_string(mode.code));
        check(mode.out == alone.out, id + ".text",
              "the version of a compound line must be the bytes of the single --version");
    }
}

// The help text itself: every operation section names --save-qprog, so an
// operator who reads the section of the kind learns how to keep the document.
void checkHelpSaveQprog() {
    const std::string help = qpcli::kHelp;
    const char* const sections[] = {"Ключи --op face:", "Ключи --op pocket:",
                                    "Ключи --op circle-pocket:", "Ключи --op drill:"};
    for (const char* section : sections) {
        const std::size_t at = help.find(section);
        check(at != std::string::npos, std::string("help.section.") + section,
              "the help must carry the section " + std::string(section));
        if (at == std::string::npos) { continue; }
        const std::size_t next = help.find("Ключи --op", at + 1);
        const std::string block = help.substr(at, next == std::string::npos ? std::string::npos
                                                                          : next - at);
        check(block.find("--save-qprog") != std::string::npos,
              std::string("help.save_qprog.") + section,
              "the section " + std::string(section) + " must name --save-qprog");
    }
}

// `--demo` takes the post-processor words of the whole program (`--h`,
// `--g64`, `--stop`/`--no-stop`) and nothing of one operation: `--material`
// belongs to `--op`, so the help says it out loud under the shared keys and the
// operator does not look for the key in the `--op` sections alone.
void checkHelpPostKeys() {
    const std::string help = qpcli::kHelp;
    const std::size_t at = help.find("Ключи постпроцессора");
    check(at != std::string::npos, "help.post_section",
          "the help must carry the section of the post-processor keys");
    if (at == std::string::npos) { return; }
    const std::size_t next = help.find("Ключи --op", at + 1);
    const std::string block = help.substr(at, next == std::string::npos ? std::string::npos
                                                                      : next - at);
    check(block.find("--material") != std::string::npos, "help.demo_no_material",
          "the shared keys must say that --material belongs to --op, not to --demo");

    // The words the section promises for `--demo` really work there.
    const Mode withH = runMode({"--h", "5", "--demo", "/tmp/opencode/help-demo-h.ngc"});
    check(withH.code == qp::kExitOk, "help.demo_h_code",
          "--h with --demo must return 0, got " + std::to_string(withH.code));
    const Mode material =
        runMode({"--demo", "--material", "steel", "/tmp/opencode/help-demo-m.ngc"});
    check(material.code == qp::kExitBadArgs, "help.demo_material_refused",
          "--material with --demo must be a usage error, got " + std::to_string(material.code));
}

} // namespace help_checks

void runHelpChecks() {
    help_checks::checkHelpAnywhere();
    help_checks::checkVersionAnywhere();
    help_checks::checkHelpSaveQprog();
    help_checks::checkHelpPostKeys();
}
