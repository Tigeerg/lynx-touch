// core/text_util.cpp - реализация строковых помощников ядра.
#include "text_util.h"

#include <cstdlib>
#include <cstring>

namespace gv {

namespace {

// float(value) для строки с нулевым терминатором: разбор целиком или отказ.
// Оба варианта toDouble зовут этот разбор, поэтому значение совпадает бит в бит.
bool parseFull(const char *s, double& out) {
    if (s == nullptr || *s == '\0') { return false; }
    char *end = nullptr;
    const double v = std::strtod(s, &end);
    if (end == s) { return false; }
    while (*end != '\0' && isSpaceCh(static_cast<unsigned char>(*end))) { ++end; }
    if (*end != '\0') { return false; }
    out = v;
    return true;
}

} // namespace

bool isSpaceCh(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool isDigitCh(char c) { return c >= '0' && c <= '9'; }

bool isLetterCh(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isWordCh(char c) {
    // Граница \b питоновского re в юникодном режиме: буквы, цифры, подчёркивание
    // и вообще всё, что Python считает словом. В UTF-8 байт >= 0x80 - всегда
    // часть не-ASCII символа, а на практике это кириллица (буква, а не знак);
    // однобайтовые знаки вроде '·' дали бы расхождение, но в УП их рядом с T..M6
    // не бывает.
    if (static_cast<unsigned char>(c) >= 0x80) { return true; }
    return isLetterCh(c) || isDigitCh(c) || c == '_';
}

char upperCh(char c) { return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c; }

bool sameNoCase(char a, char b) { return upperCh(a) == upperCh(b); }

void stripInPlace(std::string& s) {
    size_t b = 0;
    while (b < s.size() && isSpaceCh(static_cast<unsigned char>(s[b]))) { ++b; }
    size_t e = s.size();
    while (e > b && isSpaceCh(static_cast<unsigned char>(s[e - 1]))) { --e; }
    // Срезать нечего: substr всё равно скопировал бы буфер. В Test_model.ngc
    // обрезка не нужна ни в одной из 110585 строк.
    if (b == 0 && e == s.size()) { return; }
    s = s.substr(b, e - b);
}

std::string asciiUpper(const std::string& s) {
    std::string out = s;
    for (char& c : out) { c = upperCh(c); }
    return out;
}

bool toDouble(const std::string& s, double& out) {
    return parseFull(s.c_str(), out);
}

// Тот же float(value), но байты берутся из среза строки (без создания
// std::string на каждый токен). Срез короче стекового буфера копируется в буфер
// и завершается нулём; срез длиннее разбирается через std::string.
bool toDouble(const char *first, size_t len, double& out) {
    if (first == nullptr || len == 0) { return false; }
    char buf[64];
    if (len >= sizeof(buf)) { return toDouble(std::string(first, len), out); }
    std::memcpy(buf, first, len);
    buf[len] = '\0';
    return parseFull(buf, out);
}

} // namespace gv
