// core/serialize_shared.cpp - the words and the key table of the .qprog format.
//
// The file is the single place of the token spelling: the writer and the reader
// both call it, so a number written by one is a number the other accepts, and a
// key written by the writer is a key the reader knows. A change here changes
// both sides at once.
#include "core/serialize_internal.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <variant>
#include <vector>

namespace qp {

std::vector<std::string> splitWords(const std::string& line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) { ++i; }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') { ++i; }
        if (i > start) { out.push_back(line.substr(start, i - start)); }
    }
    return out;
}

std::string restAfter(const std::string& line, std::size_t count) {
    std::size_t i = 0;
    for (std::size_t n = 0; n < count; ++n) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) { ++i; }
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') { ++i; }
    }
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) { ++i; }
    std::string out = line.substr(i);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
}

std::string quoteText(const std::string& text) {
    std::string out = "\"";
    out += text;
    out += '"';
    return out;
}

std::string unquoteText(const std::string& text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

bool parseQprogNumber(const std::string& text, double& out) {
    if (text.empty()) { return false; }
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE) { return false; }
    if (!std::isfinite(value)) { return false; }
    out = value;
    return true;
}

bool qprogNumber(const std::string& text) {
    double value = 0.0;
    return parseQprogNumber(text, value);
}

bool parseQprogInt(const std::string& text, int& out) {
    if (text.empty()) { return false; }
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size() || errno == ERANGE || value < -2147483647L ||
        value > 2147483647L) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

// %g with 15 significant digits is the usual round-trip-safe spelling of a
// double on a text file: enough digits for a value that came out of a form,
// and short for a value that is whole.
std::string number(double value) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.15g", value);
    std::string out(buf);
    if (out == "-0") { out = "0"; }
    return out;
}

const char* coolantName(Coolant coolant) {
    switch (coolant) {
    case Coolant::Off: return "off";
    case Coolant::Mist: return "mist";
    case Coolant::Flood: return "flood";
    }
    return "flood";
}

bool parseCoolant(const std::string& text, Coolant& out) {
    if (text == "off") {
        out = Coolant::Off;
    } else if (text == "mist") {
        out = Coolant::Mist;
    } else if (text == "flood") {
        out = Coolant::Flood;
    } else {
        return false;
    }
    return true;
}

// Kinds are addressed by the same words the console uses: `face` and `pocket`.
const char* kindName(const OperationGeometry& geometry) {
    if (std::holds_alternative<FaceOp>(geometry)) { return "face"; }
    if (std::holds_alternative<RectPocketOp>(geometry)) { return "pocket"; }
    if (std::holds_alternative<CirclePocketOp>(geometry)) { return "circle"; }
    if (std::holds_alternative<DrillHoleOp>(geometry)) { return "drill"; }
    if (std::holds_alternative<DrillGridOp>(geometry)) { return "drill-grid"; }
    return "drill-circle";
}

// Which `op.*` keys a kind takes, without the shape keys that are checked
// separately below. The table is the single place both the writer and the
// reader read.
std::vector<std::string> keysOf(const OperationGeometry& geometry) {
    if (std::holds_alternative<FaceOp>(geometry)) { return {"z", "stepOver"}; }
    if (std::holds_alternative<RectPocketOp>(geometry)) {
        return {"rect", "cornerRadius", "finishAllowance"};
    }
    if (std::holds_alternative<CirclePocketOp>(geometry)) { return {"circle", "finishAllowance"}; }
    if (std::holds_alternative<DrillHoleOp>(geometry)) { return {"hole", "peck"}; }
    if (std::holds_alternative<DrillGridOp>(geometry)) { return {"grid", "peck"}; }
    return {"circle", "count", "start", "arc", "fullTurn", "peck"};
}

const std::vector<std::string>& commonKeys() {
    static const std::vector<std::string> keys = {"enabled", "tool", "spindle", "feed",
                                                  "plunge",  "coolant", "topZ", "depth",
                                                  "stepDown", "notes"};
    return keys;
}

} // namespace qp
