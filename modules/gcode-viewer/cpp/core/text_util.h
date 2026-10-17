// core/text_util.h - мелкие строковые помощники ядра.
//
// Нужны двум модулям ядра (ui_tags.cpp и gcode_parser.cpp), поэтому вынесены
// отдельно. Своей ценности не несут: это ровно те операции над ASCII-строками,
// которые питоновский код делает через str/re.
#pragma once

#include <string>

namespace gv {

bool isSpaceCh(unsigned char c);
bool isDigitCh(char c);
// [A-Za-z] - класс буквы в шаблонах слов G-кода.
bool isLetterCh(char c);
// Граница слова для \b: буквы, цифры, подчёркивание и байты >= 0x80 (кириллица
// в UTF-8) - питоновский re в юникодном режиме считает буквой и её.
bool isWordCh(char c);
char upperCh(char c);
bool sameNoCase(char a, char b);

// str.strip для ASCII: пробелы и управляющие пробельные по краям.
void stripInPlace(std::string& s);

// str.upper для ASCII-строки.
std::string asciiUpper(const std::string& s);

// strtod по правилам C-локали: значение не зависит от LC_NUMERIC процесса
// (Qt при старте поднимает системную локаль, и в ru_RU разбор чисел УП
// обрывался). Семантика strtod сохранена целиком - меняется только локаль.
double strtodC(const char *s, char **end);

// float(value): значение должно быть разобрано целиком; false там, где Python
// поднял бы ValueError (вызывающий берёт значение по умолчанию).
bool toDouble(const std::string& s, double& out);

// То же, но байты берутся из среза [first, first + len) без создания std::string:
// разбор идёт тем же strtodC, поэтому результат совпадает бит в бит. Строка, из
// которой взят срез, не должна изменять содержимое и перевыделять буфер, пока
// значение читается.
bool toDouble(const char *first, size_t len, double& out);

} // namespace gv
