// tests/checks_version.cpp - the generator version against projects.json.
//
// The header of every .ngc carries kGeneratorVersion (core/version.h), and the
// manifest projects.json carries the version of the quickprog project. The two
// are edited by hand in different places, so a divergence is silent until a
// reader compares them. The case makes the divergence visible to a machine: it
// finds the repository root, reads the version of the quickprog entry and
// compares it with the constant.
//
// The root is found by projects.json above the directory, as manifest_check.h
// does by content; outside the repository the case prints SKIP, not a failure.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "core/version.h"

#include "tests/harness.h"

namespace version_checks {

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

namespace {

// The version of `wanted` in the manifest, empty when absent. Narrow reading:
// the projects array and the id/version fields of an entry.
struct Reader {
    const std::string& text;
    std::size_t pos = 0;
    void skipWs() {
        while (pos < text.size()) {
            const char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos;
                continue;
            }
            return;
        }
    }

    bool eat(char c) {
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }

    // A JSON string; \u escapes are copied as-is, the manifest has none.
    bool readString(std::string& out) {
        out.clear();
        if (!eat('"')) {
            return false;
        }
        while (pos < text.size()) {
            const char c = text[pos++];
            if (c == '"') {
                return true;
            }
            if (c == '\\' && pos < text.size()) {
                out.push_back(text[pos++]);
                continue;
            }
            out.push_back(c);
        }
        return false;
    }

    // The value of an unknown key: a string, a number or one nested object.
    bool skipValue() {
        skipWs();
        if (pos >= text.size()) {
            return false;
        }
        if (text[pos] == '"') {
            std::string ignored;
            return readString(ignored);
        }
        const char open = text[pos];
        if (open != '{') {
            while (pos < text.size() && text[pos] != ',' && text[pos] != '}'
                   && text[pos] != ' ' && text[pos] != '\n' && text[pos] != '\t'
                   && text[pos] != '\r') {
                ++pos;
            }
            return true;
        }
        int depth = 0;
        while (pos < text.size()) {
            if (text[pos] == '"') {
                std::string ignored;
                if (!readString(ignored)) {
                    return false;
                }
                continue;
            }
            if (text[pos] == '{') {
                ++depth;
            } else if (text[pos] == '}' && --depth == 0) {
                ++pos;
                return true;
            }
            ++pos;
        }
        return false;
    }
};

// The version of one project entry: the fields id and version of the object.
bool readEntry(Reader& r, std::string& id, std::string& version) {
    if (!r.eat('{')) {
        return false;
    }
    bool hasId = false;
    bool hasVersion = false;
    for (;;) {
        r.skipWs();
        if (r.eat('}')) {
            return hasId && hasVersion;
        }
        std::string key;
        r.skipWs();
        if (!r.readString(key) || (r.skipWs(), !r.eat(':'))) {
            return false;
        }
        // The key is read: now the whitespace before its value, not before the key.
        r.skipWs();
        if (key == "id" || key == "version") {
            std::string value;
            if (!r.readString(value)) {
                return false;
            }
            if (key == "id") {
                id = value;
                hasId = true;
            } else {
                version = value;
                hasVersion = true;
            }
        } else if (!r.skipValue()) {
            return false;
        }
        r.skipWs();
        if (r.eat(',')) {
            continue;
        }
        if (!r.eat('}')) {
            return false;
        }
        return hasId && hasVersion;
    }
}

std::string projectVersion(const std::string& text, const std::string& wanted) {
    Reader r{text, 0};
    r.skipWs();
    if (!r.eat('{')) {
        return {};
    }
    for (;;) {
        r.skipWs();
        if (r.eat('}')) {
            return {};
        }
        std::string key;
        if (!r.readString(key) || (r.skipWs(), !r.eat(':'))) {
            return {};
        }
        r.skipWs();
        if (key == "projects" && r.eat('[')) {
            for (;;) {
                r.skipWs();
                if (r.eat(']')) {
                    return {};
                }
                std::string id;
                std::string version;
                if (!readEntry(r, id, version)) {
                    return {};
                }
                if (id == wanted) {
                    return version;
                }
                r.skipWs();
                if (r.eat(',')) {
                    continue;
                }
                return {};
            }
        }
        if (!r.skipValue()) {
            return {};
        }
        r.skipWs();
        if (r.eat(',')) {
            continue;
        }
        return {};
    }
}

// The version of the quickprog entry; empty when the manifest is unreadable or
// names no such project.
std::string manifestVersion(const std::filesystem::path& root) {
    std::ifstream fh(root / "projects.json", std::ios::binary);
    if (!fh) {
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());
    return projectVersion(text, qp::kGeneratorName);
}

} // namespace

void runVersionChecks() {
    const std::filesystem::path root = findRoot();
    const std::string want = root.empty() ? std::string() : manifestVersion(root);
    if (want.empty()) {
        std::printf("SKIP generator.version: projects.json с проектом %s не найден "
                    "выше каталога проверки\n", qp::kGeneratorName);
        return;
    }
    qpcheck::check(std::string(qp::kGeneratorVersion) == want, "generator.version",
                   "projects.json " + want + ", kGeneratorVersion " + qp::kGeneratorVersion);
}

} // namespace version_checks
