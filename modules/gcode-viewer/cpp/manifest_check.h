// manifest_check.h - сверка дайджестов пробников с оракулом oracle_manifest.json.
//
// Оракул (modules/gcode-viewer/) считает промежуточные массивы
// питоновского пути и записывает их sha256 в oracle_manifest.json. Этот заголовок
// даёт пробникам режим сверки с ним напрямую, без Python: пробник считает свой
// массив и отдаёт сюда (имя записи, dtype, sha256, форма), а получает обратно
// строку "ok"/"FAIL" на запись и общий итог "RESULT: OK" / "RESULT: FAIL (n)".
//
// Qt здесь не тянется: пробники сверки собраны без Qt (см. sha256.h о том же
// решении), а разбор манифеста узкий - верхний объект, у каждого значения поля
// dtype, sha256 и shape. Полный JSON не нужен и не поддерживается.
//
// Сверка идёт по трём полям записи: sha256 байтов, форма массива и dtype
// сериализации. Расхождение dtype - такая же находка, как расхождение хэша:
// манифест записан меняется только осознанно.
#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace gv {
namespace manifest {

// Запись манифеста: поля те же, что пишет::_digest.
struct Entry {
    std::string dtype;
    std::string sha256;
    std::vector<long long> shape;
};

namespace detail {

// Разборщик JSON манифеста: позиция в тексте и операции над ней.
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
            break;
        }
    }

    bool eat(char c) {
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }

    // Строка JSON; экранирование \u переводится в UTF-8.
    bool readString(std::string& out) {
        out.clear();
        if (!eat('"')) {
            return false;
        }
        while (pos < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[pos]);
            ++pos;
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (pos >= text.size()) {
                return false;
            }
            const char esc = text[pos++];
            switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned int cp = 0;
                if (!readHex4(cp)) {
                    return false;
                }
                appendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool readInt(long long& out) {
        bool neg = false;
        if (eat('-')) {
            neg = true;
        }
        if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') {
            return false;
        }
        long long v = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            v = v * 10 + (text[pos] - '0');
            ++pos;
        }
        out = neg ? -v : v;
        return true;
    }

    bool readShape(std::vector<long long>& out) {
        out.clear();
        if (!eat('[')) {
            return false;
        }
        skipWs();
        if (eat(']')) {
            return true;
        }
        for (;;) {
            long long v = 0;
            if (!readInt(v)) {
                return false;
            }
            out.push_back(v);
            skipWs();
            if (eat(',')) {
                skipWs();
                continue;
            }
            return eat(']');
        }
    }

    // Пропуск значения неизвестного ключа: манифест пишется с известными полями,
    // но лишнее поле не должно ломать разбор.
    bool skipValue() {
        skipWs();
        if (pos >= text.size()) {
            return false;
        }
        const char c = text[pos];
        if (c == '"') {
            std::string ignored;
            return readString(ignored);
        }
        if (c == '{' || c == '[') {
            const char open = c;
            const char close = (c == '{') ? '}' : ']';
            int depth = 0;
            while (pos < text.size()) {
                if (text[pos] == '"') {
                    std::string ignored;
                    if (!readString(ignored)) {
                        return false;
                    }
                    continue;
                }
                const char d = text[pos++];
                if (d == open) {
                    ++depth;
                } else if (d == close) {
                    --depth;
                    if (depth == 0) {
                        return true;
                    }
                }
            }
            return false;
        }
        while (pos < text.size()) {
            const char d = text[pos];
            if (d == ',' || d == '}' || d == ']' || d == ' ' || d == '\t'
                    || d == '\n' || d == '\r') {
                break;
            }
            ++pos;
        }
        return true;
    }

    bool readHex4(unsigned int& out) {
        if (pos + 4 > text.size()) {
            return false;
        }
        unsigned int v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text[pos + std::size_t(i)];
            unsigned int d = 0;
            if (c >= '0' && c <= '9') {
                d = static_cast<unsigned int>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                d = static_cast<unsigned int>(c - 'a') + 10U;
            } else if (c >= 'A' && c <= 'F') {
                d = static_cast<unsigned int>(c - 'A') + 10U;
            } else {
                return false;
            }
            v = (v << 4) | d;
        }
        pos += 4;
        out = v;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned int cp) {
        if (cp < 0x80U) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800U) {
            out.push_back(static_cast<char>(0xc0U | (cp >> 6)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
        } else {
            out.push_back(static_cast<char>(0xe0U | (cp >> 12)));
            out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3fU)));
            out.push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
        }
    }
};

inline bool parseManifest(const std::string& text, std::map<std::string, Entry>& out,
                          std::string& error) {
    Reader r{text, 0};
    r.skipWs();
    if (!r.eat('{')) {
        error = "ожидался объект";
        return false;
    }
    r.skipWs();
    if (r.eat('}')) {
        return true;
    }
    for (;;) {
        r.skipWs();
        std::string name;
        if (!r.readString(name)) {
            error = "ожидалось имя записи";
            return false;
        }
        r.skipWs();
        if (!r.eat(':')) {
            error = "ожидалось ':' после " + name;
            return false;
        }
        r.skipWs();
        if (!r.eat('{')) {
            error = "ожидалась запись " + name;
            return false;
        }
        Entry entry;
        r.skipWs();
        if (!r.eat('}')) {
            for (;;) {
                r.skipWs();
                std::string key;
                if (!r.readString(key)) {
                    error = "ожидалось поле записи " + name;
                    return false;
                }
                r.skipWs();
                if (!r.eat(':')) {
                    error = "ожидалось ':' после поля " + key;
                    return false;
                }
                r.skipWs();
                if (key == "dtype") {
                    if (!r.readString(entry.dtype)) {
                        error = "dtype не строка в записи " + name;
                        return false;
                    }
                } else if (key == "sha256") {
                    if (!r.readString(entry.sha256)) {
                        error = "sha256 не строка в записи " + name;
                        return false;
                    }
                } else if (key == "shape") {
                    if (!r.readShape(entry.shape)) {
                        error = "shape не массив чисел в записи " + name;
                        return false;
                    }
                } else if (!r.skipValue()) {
                    error = "не прочитано поле " + key + " записи " + name;
                    return false;
                }
                r.skipWs();
                if (r.eat(',')) {
                    continue;
                }
                if (r.eat('}')) {
                    break;
                }
                error = "ожидалась ',' или '}' в записи " + name;
                return false;
            }
        }
        out[name] = std::move(entry);
        r.skipWs();
        if (r.eat(',')) {
            continue;
        }
        if (r.eat('}')) {
            break;
        }
        error = "ожидалась ',' или '}' после записи " + name;
        return false;
    }
    return true;
}

} // namespace detail

// Манифест: имя записи -> её поля.
class Manifest {
public:
    // Чтение манифеста: путь в UTF-8 и открывается через std::filesystem::u8path -
    // каталог репозитория может содержать кириллицу.
    bool load(const std::string& utf8Path, std::string& error) {
        std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
        if (!fh) {
            error = "файл не открыт";
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(fh)),
                               std::istreambuf_iterator<char>());
        entries_.clear();
        if (!detail::parseManifest(text, entries_, error)) {
            return false;
        }
        if (entries_.empty()) {
            error = "манифест пуст";
            return false;
        }
        return true;
    }

    const Entry* find(const std::string& name) const {
        const auto it = entries_.find(name);
        return it == entries_.end() ? nullptr : &it->second;
    }

private:
    std::map<std::string, Entry> entries_;
};

// Форма массива в том же виде, что печатает: [245, 2, 3].
inline std::string shapeText(const std::vector<long long>& shape) {
    std::string out = "[";
    for (std::size_t i = 0; i < shape.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += std::to_string(shape[i]);
    }
    out += "]";
    return out;
}

// Сверка дайджестов с манифестом и накопление расхождений.
class Checker {
public:
    explicit Checker(const Manifest& manifest) : manifest_(manifest) {}

    // Одна запись: имя в манифесте, dtype сериализации, sha256 байтов и форма.
    // Печатает строку на запись; расхождение считается один раз, даже если не
    // сошлись и хэш, и форма, и dtype.
    void check(const std::string& name, const std::string& dtype,
               const std::string& sha256, const std::vector<long long>& shape) {
        const Entry* want = manifest_.find(name);
        if (want == nullptr) {
            std::printf("FAIL  %-22s нет в манифесте\n", name.c_str());
            ++failures_;
            return;
        }
        const bool hashOk = want->sha256 == sha256;
        const bool shapeOk = want->shape == shape;
        const bool dtypeOk = want->dtype == dtype;
        if (hashOk && shapeOk && dtypeOk) {
            std::printf("ok    %-22s %s\n", name.c_str(), shapeText(shape).c_str());
            return;
        }
        ++failures_;
        std::printf("FAIL  %-22s expected %s got %s\n", name.c_str(),
                    want->sha256.c_str(), sha256.c_str());
        if (!shapeOk) {
            std::printf("      %-22s форма: манифест %s, пробник %s\n", name.c_str(),
                        shapeText(want->shape).c_str(), shapeText(shape).c_str());
        }
        if (!dtypeOk) {
            std::printf("      %-22s dtype: манифест %s, пробник %s\n", name.c_str(),
                        want->dtype.c_str(), dtype.c_str());
        }
    }

    int failures() const { return failures_; }

    // Итог в форме остальных проверок проекта.
    void printResult() const {
        if (failures_ == 0) {
            std::printf("RESULT: OK\n");
            return;
        }
        std::printf("RESULT: FAIL (%d)\n", failures_);
    }

private:
    const Manifest& manifest_;
    int failures_ = 0;
};

// Путь в UTF-8 из широкого представления (Windows).
inline std::string wideToUtf8(const std::wstring& w) {
#ifdef _WIN32
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
#else
    return std::string(w.begin(), w.end());
#endif
}

// Корень репозитория по пути пробника: <репо>/build/edges/<пробник>.exe - три
// уровня вверх (файл, build/edges, build). Догадка проверяется каталогом
// тестовых данных: сборка в другом каталоге не должна уводить поиск в чужое
// дерево. Возврат - путь в UTF-8, пусто при неудаче.
inline std::string repoRoot() {
#ifdef _WIN32
    wchar_t buf[4096];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]))) {
        return {};
    }
    std::filesystem::path dir(std::wstring(buf, static_cast<std::size_t>(n)));
    dir = dir.parent_path();          // build/edges
    dir = dir.parent_path();          // build
    dir = dir.parent_path();          // корень репозитория
    if (!std::filesystem::exists(dir / L"data" / L"cad-testdata")) {
        return {};
    }
    return wideToUtf8(dir.wstring());
#else
    // Linux: путь процесса читается через /proc/self/exe (симлинк на
    // исполняемый файл), далее та же догадка про build/edges и та же проверка
    // каталога тестовых данных.
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    std::filesystem::path dir = exe.parent_path();
    dir = dir.parent_path();          // build
    dir = dir.parent_path();          // корень репозитория
    if (!std::filesystem::exists(dir / "data" / "cad-testdata")) {
        return {};
    }
    return dir.string();
#endif
}

// Разбор аргументов режима сверки: --manifest <файл> [<файл-данных>] или
// --check [<файл-данных>]. Умолчания берутся от корня репозитория (путь
// пробника). Возврат false - аргументы не разобраны, причина напечатана.
inline bool resolveCheckArgs(const std::vector<std::string>& args,
                             const std::string& dataName,
                             std::string& manifestPath, std::string& dataPath) {
    if (args.size() < 2) {
        return false;
    }
    const bool explicitManifest = (args[1] == "--manifest");
    if (!explicitManifest && args[1] != "--check") {
        return false;
    }
    if (explicitManifest && args.size() < 3) {
        std::fprintf(stderr, "usage: --manifest <oracle_manifest.json> [<data-file>]\n");
        return false;
    }
    if (explicitManifest) {
        manifestPath = args[2];
    }
    const std::size_t dataIndex = explicitManifest ? 3 : 2;
    if (args.size() > dataIndex) {
        dataPath = args[dataIndex];
    }
    if (manifestPath.empty() || dataPath.empty()) {
        const std::string root = repoRoot();
        if (root.empty()) {
            std::fprintf(stderr, "пробник: корень репозитория от своего каталога не найден: "
                                 "укажите файл данных явно\n");
            return false;
        }
        if (manifestPath.empty()) {
            manifestPath = root + "/modules/gcode-viewer/oracle_manifest.json";
        }
        if (dataPath.empty()) {
            dataPath = root + "/data/cad-testdata/" + dataName;
        }
    }
    return true;
}

} // namespace manifest
} // namespace gv
