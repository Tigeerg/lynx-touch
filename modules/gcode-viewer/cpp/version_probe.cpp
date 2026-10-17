// cpp/version_probe.cpp - сверка версии вьювера в трёх файлах.
//
// Версия вьювера живёт в трёх местах (VERSIONING.md, раздел 3):
// modules/gcode-viewer/VERSION, projects.json (поле version проекта с id
// gcode-viewer) и свойство viewerVersion в GcodeViewer3D.qml - его печатает точка
// входа и показывает панель вьювера. Забытая правка одного из трёх не видна ни
// сборке, ни прогону: этот пробник читает все три и требует равенства.
//
// Проверка тривиальная, но её не заменяет ни один пробник геометрии. Питоновской
// проверки имён модулей (test_module_names.py) больше нет: она стерегла границы
// питоновских модулей, а их в репозитории не осталось.
//
// Пробнику не нужны ни Qt, ни OCCT: только стандартная библиотека.
//
// Запуск: version_probe            (корень репозитория берётся от пути пробника)
//         version_probe <корень>   (явный корень репозитория)
// Код возврата: 0 - все три версии совпали; 1 - расхождение или файл не прочитан.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "manifest_check.h"   // gv::manifest::repoRoot: тот же поиск корня, что у пробников сверки

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
    // VERSION записан с BOM (UTF-8): без снятия метки версия не равна строке из
    // projects.json, хотя на глаз они одинаковы.
    const std::string bom = "\xEF\xBB\xBF";
    const std::string body = text.compare(0, bom.size(), bom) == 0 ? text.substr(bom.size()) : text;
    const std::size_t first = body.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = body.find_last_not_of(" \t\r\n");
    return body.substr(first, last - first + 1);
}

// Версия из текста: значение ключа "key": "…" начиная с позиции from. Пусто -
// ключа нет.
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

// Версия проекта gcode-viewer из projects.json: поле version ближайшего объекта
// после "id": "gcode-viewer".
std::string versionFromProjects(const std::string& text)
{
    const std::size_t id_pos = text.find("\"gcode-viewer\"");
    if (id_pos == std::string::npos) {
        return {};
    }
    return quotedValueAfter(text, "version", id_pos);
}

// Свойство viewerVersion в GcodeViewer3D.qml: строка после двоеточия.
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
