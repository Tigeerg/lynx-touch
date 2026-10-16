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

// str.strip() для ASCII: пробелы и управляющие пробельные по краям.
void stripInPlace(std::string& s);

// str.upper() для ASCII-строки.
std::string asciiUpper(const std::string& s);

// float(value): значение должно быть разобрано целиком; false там, где Python
// поднял бы ValueError (вызывающий берёт значение по умолчанию).
bool toDouble(const std::string& s, double& out);

} // namespace gv
