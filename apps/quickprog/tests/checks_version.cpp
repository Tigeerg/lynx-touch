// tests/checks_version.cpp - the generator version against projects.json.
//
// The header of every .ngc carries kGeneratorVersion (core/version.h), and the
// manifest projects.json carries the version of the quickprog project. The two
// are edited by hand in different places, so a divergence is silent until a
// reader compares them. The case makes the divergence visible to a machine: it
// finds the repository root, reads the version of the quickprog entry and
// compares it with the constant.
//
// The manifest is read by name, not by the position of a field: an entry may
// start with any member, and a neighbour may carry every field. A missing
// manifest above the directory is a SKIP, because the binary is built outside
// the repository too; a manifest without the project is a FAIL, because then
// the numbers are not compared at all.
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "core/version.h"

#include "tests/harness.h"

namespace version_checks {

// The id of the project to look up: the manifest names the project, the .ngc
// header carries the name of the generator.
constexpr const char* kProjectId = "quickprog";

// The nearest directory above the current one that has projects.json; empty
// when the walk reaches the root. The binary may live in any build directory,
// so the repository is found by content, not by a fixed place of the check.
std::filesystem::path findRoot() {
    std::error_code ec;
    std::filesystem::path start = std::filesystem::current_path(ec);
    if (ec) {
        return {};
    }
    for (;;) {
        if (std::filesystem::exists(start / "projects.json", ec)) {
            return start;
        }
        if (!start.has_parent_path() || start.parent_path() == start) {
            return {};
        }
        start = start.parent_path();
    }
}

// The result of the lookup: the entry found and the version of it, or the
// entry found without a version, or nothing at all.
struct Lookup {
    bool entryFound = false;
    std::string version;
};

namespace {

// A value of a JSON object as the range of its text.
struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
};

std::size_t skipWs(const std::string& text, std::size_t at) {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' ||
                                text[at] == '\r')) {
        ++at;
    }
    return at;
}

// The end of the JSON string that opens at `at` (the quote): the index past
// the closing quote. A backslash keeps the next character inside the string.
bool stringEnd(const std::string& text, std::size_t at, std::size_t& end) {
    if (at >= text.size() || text[at] != '"') { return false; }
    std::size_t i = at + 1;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '\\') {
            i += 2;
            continue;
        }
        if (c == '"') {
            end = i + 1;
            return true;
        }
        ++i;
    }
    return false;
}

// The text of a string value without its quotes; the manifest has no escapes
// but a quote, and a backslash keeps the next character as it is.
std::string unquote(const std::string& text, const Span& span) {
    std::string out;
    for (std::size_t i = span.begin + 1; i + 1 < span.end; ++i) {
        if (text[i] == '\\' && i + 2 < span.end) { ++i; }
        out.push_back(text[i]);
    }
    return out;
}

// The end of the value that starts at `at`: a string by its closing quote, a
// nested object or array by bracket depth, anything else up to the delimiter.
bool valueEnd(const std::string& text, std::size_t at, std::size_t& end) {
    at = skipWs(text, at);
    if (at >= text.size()) { return false; }
    const char first = text[at];
    if (first == '"') { return stringEnd(text, at, end); }
    if (first != '{' && first != '[') {
        std::size_t i = at;
        while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ']' &&
               text[i] != ' ' && text[i] != '\n' && text[i] != '\t' && text[i] != '\r') {
            ++i;
        }
        end = i;
        return true;
    }
    int depth = 0;
    std::size_t i = at;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '"') {
            std::size_t after = i;
            if (!stringEnd(text, i, after)) { return false; }
            i = after;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            if (--depth == 0) {
                end = i + 1;
                return true;
            }
        }
        ++i;
    }
    return false;
}

// The end of the object that opens at `at`: the index past its closing brace.
bool objectEnd(const std::string& text, std::size_t at, std::size_t& end) {
    if (at >= text.size() || text[at] != '{') { return false; }
    return valueEnd(text, at, end);
}

// The value of `key` in the object `[begin, end)`; the scan reads every member
// as a key and a value, so the position of the member does not matter.
bool memberValue(const std::string& text, std::size_t begin, std::size_t end,
                 const std::string& key, Span& value) {
    std::size_t at = skipWs(text, begin + 1);
    while (at < end && text[at] == '"') {
        Span name;
        if (!stringEnd(text, at, name.end)) { return false; }
        name.begin = at;
        at = skipWs(text, name.end);
        if (at >= end || text[at] != ':') { return false; }
        value.begin = skipWs(text, at + 1);
        if (!valueEnd(text, value.begin, value.end)) { return false; }
        if (unquote(text, name) == key) { return true; }
        at = skipWs(text, value.end);
        if (at < end && text[at] == ',') { at = skipWs(text, at + 1); }
    }
    return false;
}

// The body of the top-level `projects` array: the ranges inside its brackets.
bool projectsArray(const std::string& text, std::size_t& begin, std::size_t& end) {
    const std::size_t root = skipWs(text, 0);
    std::size_t close = 0;
    Span arr;
    if (root >= text.size() || !objectEnd(text, root, close) ||
        !memberValue(text, root, close, "projects", arr) ||
        arr.begin >= text.size() || text[arr.begin] != '[' || arr.end < arr.begin + 2) {
        return false;
    }
    begin = arr.begin + 1;
    end = arr.end - 1;
    return true;
}

// The lookup of the project in the manifest text: every entry of the array is
// read by name, the wanted one is answered with its version whatever the order
// of its fields is.
Lookup projectVersion(const std::string& text, const std::string& wanted) {
    Lookup found;
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!projectsArray(text, begin, end)) { return found; }
    std::size_t at = skipWs(text, begin);
    while (at < end && text[at] == '{') {
        std::size_t close = 0;
        if (!objectEnd(text, at, close) || close > end) { return found; }
        Span id;
        if (memberValue(text, at, close, "id", id) && unquote(text, id) == wanted) {
            found.entryFound = true;
            Span version;
            if (memberValue(text, at, close, "version", version)) {
                found.version = unquote(text, version);
            }
            return found;
        }
        at = skipWs(text, close);
        if (at < end && text[at] == ',') { at = skipWs(text, at + 1); }
    }
    return found;
}

bool readManifest(const std::filesystem::path& root, std::string& text) {
    std::ifstream fh(root / "projects.json", std::ios::binary);
    if (!fh) { return false; }
    text.assign((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

// The case itself: a missing file is the SKIP, every other answer is a check.
void checkManifestVersion() {
    const std::filesystem::path root = findRoot();
    std::string text;
    if (root.empty() || !readManifest(root, text)) {
        std::printf("SKIP generator.version: projects.json не найден выше каталога проверки\n");
        return;
    }
    const Lookup found = projectVersion(text, kProjectId);
    const std::string path = (root / "projects.json").string();
    if (!found.entryFound) {
        qpcheck::check(false, "generator.version.project",
                       path + " не содержит проекта " + kProjectId +
                           ": версия не проверена");
        return;
    }
    if (found.version.empty()) {
        qpcheck::check(false, "generator.version.field",
                       "запись проекта " + std::string(kProjectId) +
                           " в projects.json без поля version");
        return;
    }
    qpcheck::check(std::string(qp::kGeneratorVersion) == found.version, "generator.version",
                   path + ": quickprog " + found.version + ", kGeneratorVersion " +
                       qp::kGeneratorVersion);
}

void runVersionChecks() { checkManifestVersion(); }

} // namespace version_checks
