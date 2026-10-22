// cpp/version_probe.cpp - checking repository versions.
//
// The viewer version lives in three places (VERSIONING.md, section 3):
// modules/gcode-viewer/VERSION, projects.json (the version field of the project with id
// gcode-viewer) and the viewerVersion property in GcodeViewer3D.qml - the entry
// point prints it and the viewer panel shows it. A forgotten edit of one of the three is
// invisible both to the build and to a run: this probe reads all three and requires equality.
// The same probe holds the product version (the root VERSION against projects.json) and the
// name and version of the react-interface manifest (package.json against projects.json), and
// requires the version files to carry no BOM (UTF-8): with the mark a reader gets
// "\uFEFF0.58.3" instead of the version, while to the eye the strings are identical.
//
// The check is trivial, but no geometry probe replaces it. The Python
// module-name check (test_module_names.py) no longer exists: it guarded the boundaries of
// Python modules, and none of them remain in the repository.
//
// The probe needs neither Qt nor OCCT: only the standard library.
//
// Run: version_probe            (the repository root is taken from the probe path)
//      version_probe <root>     (explicit repository root)
// Return code: 0 - the versions match and the files are clean; 1 - a mismatch, a BOM
// mark or an unreadable file.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "manifest_check.h"   // gv::manifest::repoRoot: the same root search as in the check probes

// windows.h is needed for wide argument parsing: the repository path lies in a directory
// with Cyrillic ("AI проекты"), and the narrow argv carries it in the console encoding.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {

// The repository path lies in a directory with Cyrillic: the arguments are taken in wide
// form and converted to UTF-8, and files are opened via std::filesystem::u8path. Otherwise
// the path would not be found and the run would abort on the invalid bytes.
std::string toUtf8(const std::wstring& w)
{
#ifdef _WIN32
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
#else
    return std::string(w.begin(), w.end());
#endif
}

std::vector<std::string> probeArgs(int argc, char** argv)
{
#ifdef _WIN32
    int count = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) {
        out.push_back(toUtf8(wargv[i]));
    }
    LocalFree(wargv);
    return out;
#else
    std::vector<std::string> out;
    for (int i = 0; i < argc; ++i) {
        out.push_back(argv[i]);
    }
    return out;
#endif
}

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

// BOM (UTF-8) at the start of a file: with it a reader outside the probe gets the version
// with an extra character, while to the eye the strings are indistinguishable.
bool startsWithBom(const std::string& text)
{
    return text.compare(0, 3, "\xEF\xBB\xBF") == 0;
}

std::string trimmed(const std::string& text)
{
    // The BOM (UTF-8) in version files is forbidden - the check below catches it; it is
    // still stripped on reading so that a mismatch does not hide behind the mark.
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

// The version of a project from projects.json: the version field of the nearest object
// after "id": "<id>".
std::string versionFromProject(const std::string& text, const std::string& id)
{
    const std::size_t id_pos = text.find("\"" + id + "\"");
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
    const std::vector<std::string> args = probeArgs(argc, argv);
    std::string root;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
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
    const std::string viewer_version_raw = readText(root + "/modules/gcode-viewer/VERSION", ok);
    if (!ok) {
        std::fprintf(stderr, "version_probe: не прочитан modules/gcode-viewer/VERSION\n");
        return 1;
    }
    const std::string product_version_raw = readText(root + "/VERSION", ok);
    if (!ok) {
        std::fprintf(stderr, "version_probe: не прочитан VERSION\n");
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
    const std::string manifest = readText(root + "/apps/react-interface/package.json", ok);
    if (!ok) {
        std::fprintf(stderr, "version_probe: не прочитан apps/react-interface/package.json\n");
        return 1;
    }

    const std::string version_file = trimmed(viewer_version_raw);
    const std::string product_version = trimmed(product_version_raw);
    const std::string from_projects = versionFromProject(projects, "gcode-viewer");
    const std::string from_qml = versionFromQml(qml);
    const std::string product_from_projects = quotedValueAfter(projects, "version", 0);
    const std::string react_from_projects = versionFromProject(projects, "react-interface");
    const std::string manifest_name = quotedValueAfter(manifest, "name", 0);
    const std::string manifest_version = quotedValueAfter(manifest, "version", 0);

    std::printf("version VERSION=%s\n", version_file.c_str());
    std::printf("version projects.json=%s\n", from_projects.c_str());
    std::printf("version GcodeViewer3D.qml=%s\n", from_qml.c_str());
    std::printf("product VERSION=%s\n", product_version.c_str());
    std::printf("product projects.json=%s\n", product_from_projects.c_str());
    std::printf("react-interface package.json=%s (имя %s)\n", manifest_version.c_str(), manifest_name.c_str());
    std::printf("react-interface projects.json=%s\n", react_from_projects.c_str());

    std::vector<std::string> problems;
    if (startsWithBom(viewer_version_raw)) {
        problems.push_back("modules/gcode-viewer/VERSION записан с меткой BOM (UTF-8)");
    }
    if (startsWithBom(product_version_raw)) {
        problems.push_back("VERSION записан с меткой BOM (UTF-8)");
    }
    if (startsWithBom(manifest)) {
        problems.push_back("apps/react-interface/package.json записан с меткой BOM (UTF-8)");
    }
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
    if (product_from_projects.empty()) {
        problems.push_back("в projects.json нет поля version продукта");
    }
    if (!product_from_projects.empty() && product_from_projects != product_version) {
        problems.push_back("projects.json (" + product_from_projects + ") не равен корневому VERSION (" +
                           product_version + ")");
    }
    if (react_from_projects.empty()) {
        problems.push_back("в projects.json нет проекта react-interface с полем version");
    }
    if (manifest_name != "react-interface") {
        problems.push_back("имя в package.json (" + manifest_name + ") не равно react-interface");
    }
    if (!react_from_projects.empty() && manifest_version != react_from_projects) {
        problems.push_back("package.json (" + manifest_version + ") не равен projects.json (" +
                           react_from_projects + ")");
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
