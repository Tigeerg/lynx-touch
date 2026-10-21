// cpp/version_probe.cpp - checking the viewer version in three files.
//
// The viewer version lives in three places (VERSIONING.md, section 3):
// modules/gcode-viewer/VERSION, projects.json (the version field of the project with id
// gcode-viewer) and the viewerVersion property in GcodeViewer3D.qml - the entry
// point prints it and the viewer panel shows it. A forgotten edit of one of the three is
// invisible both to the build and to a run: this probe reads all three and requires equality.
//
// The check is trivial, but no geometry probe replaces it. The Python
// module-name check (test_module_names.py) no longer exists: it guarded the boundaries of
// Python modules, and none of them remain in the repository.
//
// The probe needs neither Qt nor OCCT: only the standard library.
//
// Run: version_probe            (the repository root is taken from the probe path)
//      version_probe <root>     (explicit repository root)
// Return code: 0 - all three versions matched; 1 - a mismatch or the file was not read.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "manifest_check.h"   // gv::manifest::repoRoot: the same root search as in the check probes

namespace {

std::string readText(const std::string& utf8Path, bool& ok)
{
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!fh) {
        ok = false;
        return {};
    }
    ok = true;
    return std::string((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());
}

std::string trimmed(const std::string& text)
{
    // VERSION is written with a BOM (UTF-8): without stripping the mark the version does not
    // equal the string from projects.json, although to the eye they look the same.
    const std::string bom = "\xEF\xBB\xBF";
    const std::string body = text.compare(0, bom.size(), bom) == 0 ? text.substr(bom.size()) : text;
    const std::size_t first = body.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = body.find_last_not_of(" \t\r\n");
    return body.substr(first, last - first + 1);
}

// Version from text: the value of the "key": "…" entry starting at position from. Empty -
// the key is absent.
std::string quotedValueAfter(const std::string& text, const std::string& key, std::size_t from)
{
    const std::size_t key_pos = text.find("\"" + key + "\"", from);
    if (key_pos == std::string::npos) {
        return {};
    }
    const std::size_t open = text.find('"', text.find(':', key_pos) + 1);
    if (open == std::string::npos) {
        return {};
    }
    const std::size_t close = text.find('"', open + 1);
    if (close == std::string::npos) {
        return {};
    }
    return text.substr(open + 1, close - open - 1);
}

// The version of the gcode-viewer project from projects.json: the version field of the nearest
// object after "id": "gcode-viewer".
std::string versionFromProjects(const std::string& text)
{
    const std::size_t id_pos = text.find("\"gcode-viewer\"");
    if (id_pos == std::string::npos) {
        return {};
    }
    return quotedValueAfter(text, "version", id_pos);
}

// The viewerVersion property in GcodeViewer3D.qml: the string after the colon.
std::string versionFromQml(const std::string& text)
{
    const std::size_t key_pos = text.find("viewerVersion:");
    if (key_pos == std::string::npos) {
        return {};
    }
    const std::size_t open = text.find('"', key_pos);
    if (open == std::string::npos) {
        return {};
    }
    const std::size_t close = text.find('"', open + 1);
    if (close == std::string::npos) {
        return {};
    }
    return text.substr(open + 1, close - open - 1);
}

} // namespace

int main(int argc, char* argv[])
{
    std::string root;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (!arg.empty() && arg[0] != '-') {
            root = arg;
        }
    }
    if (root.empty()) {
        root = gv::manifest::repoRoot();
    }
    if (root.empty()) {
        std::fprintf(stderr, "version_probe: корень репозитория от своего каталога не найден: "
                             "укажите его аргументом\n");
        return 1;
    }

    bool ok = false;
    const std::string version_file = trimmed(readText(root + "/modules/gcode-viewer/VERSION", ok));
    if (!ok) {
        std::fprintf(stderr, "version_probe: не прочитан modules/gcode-viewer/VERSION\n");
        return 1;
    }
    const std::string projects = readText(root + "/projects.json", ok);
    if (!ok) {
        std::fprintf(stderr, "version_probe: не прочитан projects.json\n");
        return 1;
    }
    const std::string qml = readText(root + "/modules/gcode-viewer/GcodeViewer3D.qml", ok);
    if (!ok) {
        std::fprintf(stderr, "version_probe: не прочитан GcodeViewer3D.qml\n");
        return 1;
    }

    const std::string from_projects = versionFromProjects(projects);
    const std::string from_qml = versionFromQml(qml);

    std::printf("version VERSION=%s\n", version_file.c_str());
    std::printf("version projects.json=%s\n", from_projects.c_str());
    std::printf("version GcodeViewer3D.qml=%s\n", from_qml.c_str());

    std::vector<std::string> problems;
    if (from_projects.empty()) {
        problems.push_back("в projects.json нет проекта gcode-viewer с полем version");
    }
    if (from_qml.empty()) {
        problems.push_back("в GcodeViewer3D.qml нет свойства viewerVersion");
    }
    if (!from_projects.empty() && from_projects != version_file) {
        problems.push_back("projects.json (" + from_projects + ") не равен VERSION (" + version_file + ")");
    }
    if (!from_qml.empty() && from_qml != version_file) {
        problems.push_back("GcodeViewer3D.qml (" + from_qml + ") не равен VERSION (" + version_file + ")");
    }

    if (problems.empty()) {
        std::printf("RESULT: OK (%s)\n", version_file.c_str());
        return 0;
    }
    for (const std::string& text : problems) {
        std::fprintf(stderr, "version_probe: %s\n", text.c_str());
    }
    std::printf("RESULT: FAIL (%zu)\n", problems.size());
    return 1;
}
