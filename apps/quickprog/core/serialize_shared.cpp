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
    if (std::holds_alternative<DrillCircleOp>(geometry)) { return "drill-circle"; }
    if (std::holds_alternative<ContourOp>(geometry)) { return "contour"; }
    if (std::holds_alternative<SlotOp>(geometry)) { return "slot"; }
    if (std::holds_alternative<ChamferOp>(geometry)) { return "chamfer"; }
    if (std::holds_alternative<TapOp>(geometry)) { return "tap"; }
    if (std::holds_alternative<CounterboreOp>(geometry)) { return "counterbore"; }
    if (std::holds_alternative<ThreadOp>(geometry)) { return "thread"; }
    if (std::holds_alternative<EngraveOp>(geometry)) { return "engrave"; }
    if (std::holds_alternative<IslandPocketOp>(geometry)) { return "pocket-island"; }
    if (std::holds_alternative<RestOp>(geometry)) { return "rest"; }
    if (std::holds_alternative<CustomOp>(geometry)) { return "custom"; }
    return "probe";
}

bool isPlanned(const OperationGeometry& geometry) {
    // The four kinds of the demo planner. The rest of the schema is read for
    // the panel file, named as skipped and never planned: a planner of a kind
    // the demo does not own would be a second implementation of the panel.
    return std::holds_alternative<FaceOp>(geometry) || std::holds_alternative<RectPocketOp>(geometry) ||
           std::holds_alternative<CirclePocketOp>(geometry) || std::holds_alternative<DrillHoleOp>(geometry) ||
           std::holds_alternative<DrillGridOp>(geometry) || std::holds_alternative<DrillCircleOp>(geometry);
}

// Which `op.*` keys a kind takes, without the shape keys that are checked
// separately below. The table is the single place both the writer and the
// reader read. `stepOver` of a pocket is in the table: the writer writes it and
// the reader knows it, so the round-trip of a pocket keeps the step
// (PLAN-QUICKPROG, 7.3). The face has had the same key since the first slice.
// The kinds the demo does not plan list the keys of the panel writer
// (`serialize-write.ts`), so a panel file of that kind is read whole.
std::vector<std::string> keysOf(const OperationGeometry& geometry) {
    if (std::holds_alternative<FaceOp>(geometry)) { return {"z", "stepOver"}; }
    if (std::holds_alternative<RectPocketOp>(geometry)) {
        return {"rect", "cornerRadius", "finishAllowance", "stepOver"};
    }
    if (std::holds_alternative<CirclePocketOp>(geometry)) {
        return {"circle", "finishAllowance", "stepOver"};
    }
    if (std::holds_alternative<DrillHoleOp>(geometry)) { return {"hole", "peck"}; }
    if (std::holds_alternative<DrillGridOp>(geometry)) { return {"grid", "peck"}; }
    if (std::holds_alternative<DrillCircleOp>(geometry)) {
        return {"circle", "count", "start", "arc", "fullTurn", "peck"};
    }
    if (std::holds_alternative<ContourOp>(geometry)) { return {"points", "closed", "allowance", "side"}; }
    if (std::holds_alternative<SlotOp>(geometry)) { return {"from", "to", "width", "stepOver"}; }
    if (std::holds_alternative<ChamferOp>(geometry)) { return {"from", "to", "chamfer"}; }
    // The drilling layout words a tap and a counterbore take; the ones of the
    // pattern the file does not name are optional, so the whole set is a known
    // key and only the present ones are required (`serialize_read.cpp`).
    if (std::holds_alternative<TapOp>(geometry)) { return {"pattern", "pitch", "hole", "grid", "count",
                                                          "start", "arc", "fullTurn", "peck"}; }
    if (std::holds_alternative<CounterboreOp>(geometry)) {
        return {"pattern", "seat", "seatDepth", "dwell", "hole", "grid", "count", "start", "arc",
                "fullTurn", "r"};
    }
    if (std::holds_alternative<ThreadOp>(geometry)) {
        return {"hole", "pitch", "internal", "threadDepth", "allowance", "stepOver"};
    }
    if (std::holds_alternative<EngraveOp>(geometry)) { return {"text", "x", "y", "height", "angle"}; }
    if (std::holds_alternative<IslandPocketOp>(geometry)) {
        return {"rect", "island", "islandAllowance", "stepOver"};
    }
    if (std::holds_alternative<RestOp>(geometry)) { return {"source", "rect", "allowance", "stepOver"}; }
    if (std::holds_alternative<CustomOp>(geometry)) { return {"gcode"}; }
    return {"points", "range", "touch", "axis"};
}

const std::vector<std::string>& commonKeys() {
    static const std::vector<std::string> keys = {"enabled", "tool", "spindle", "feed",
                                                  "plunge",  "coolant", "topZ", "depth",
                                                  "stepDown", "notes"};
    return keys;
}

} // namespace qp
