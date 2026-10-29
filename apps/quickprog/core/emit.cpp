// core/emit.cpp - the G-code emitter (PLAN-QUICKPROG, 7.1).
//
// The first slice writes the header, the work offset, the stock tags and the
// footer; there are no operations in the path yet. The modal block and the tag
// formats repeat the ones the viewer already parses
// (modules/gcode-viewer/cpp/core/ui_tags), so the file is readable both by the
// eye and by the existing preview.
#include "core/emit.h"

#include <cstdio>
#include <string>
#include <variant>

#include "core/version.h"

namespace qp {
namespace {

// The viewer's tag body ends at ')' or end of line, and the reference files
// write numbers with a trailing dot ("XMIN=-20."). The dot is kept so that the
// byte-for-byte goldens of later slices compare against the reference format.
std::string num(double value) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%g", value);
    std::string out(buf);
    const bool hasExponent = out.find('e') != std::string::npos;
    const bool isFinite = out.find("inf") == std::string::npos && out.find("nan") == std::string::npos;
    if (out.find('.') == std::string::npos && !hasExponent && isFinite) { out += '.'; }
    return out;
}

void line(std::string& out, const std::string& text) {
    out += text;
    out += '\n';
}

// Comment for the viewer: the same "(TAG: ...)" form as the reference files.
void tag(std::string& out, const std::string& body) {
    line(out, "(" + body + ")");
}

// G-code comments may not carry '(' or ')': an unbalanced bracket ends the
// comment early and the rest of the line becomes program words.
std::string commentSafe(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '(' || c == ')') {
            out += ' ';
        } else if (c != '\n' && c != '\r') {
            out += c;
        }
    }
    return out;
}

// The viewer labels the stage by tool family; the demo has milling and drilling
// operations only, so the family follows from the geometry.
std::string toolType(const OperationGeometry& geometry) {
    const bool milling = std::holds_alternative<FaceOp>(geometry) ||
                         std::holds_alternative<RectPocketOp>(geometry) ||
                         std::holds_alternative<CirclePocketOp>(geometry);
    return milling ? "FLAT" : "DRILL";
}

bool validWcs(const std::string& wcs) {
    return wcs == "G54" || wcs == "G55" || wcs == "G56" || wcs == "G57" || wcs == "G58" || wcs == "G59";
}

// G94 (feed per minute) is the only mode the demo writes: feed values are
// mm/min, and an inherited G95 would turn them into mm/rev.
void emitHeader(std::string& out, const Program& program) {
    const StockBox& stock = program.stock;

    line(out, "%");
    tag(out, "MACHINE");
    tag(out, "  MODEL: " + commentSafe(program.name));
    tag(out, "  GENERATOR: " + std::string(kGeneratorName) + " " + kGeneratorVersion);

    // UI_ORIGIN is the machine coordinate of the workpiece zero. The document
    // keeps coordinates in the machine frame already, so the zero is 0,0,0 and
    // G54 holds the offset the machine was set up with.
    tag(out, "UI_ORIGIN: X0. Y0. Z0.");
    tag(out, "UI_STOCK: XMIN=" + num(stock.min.x) + " XMAX=" + num(stock.max.x) +
                 " YMIN=" + num(stock.min.y) + " YMAX=" + num(stock.max.y) +
                 " ZMIN=" + num(stock.min.z) + " ZMAX=" + num(stock.max.z));

    line(out, "N10 G21 G90 G94 G17 G40 G49 G80");
    // A silent fallback to G54 would cut at the wrong offset. The document is
    // validated before emitting; the comment names the problem in the file
    // instead of hiding it.
    if (!validWcs(program.wcs)) {
        tag(out, "  WCS INVALID: " + commentSafe(program.wcs) + " - G54 USED");
    }
    line(out, "N15 " + (validWcs(program.wcs) ? program.wcs : std::string("G54")));
}

void emitOperation(std::string& out, const Operation& operation, int opNumber) {
    tag(out, "UI_OP: " + std::to_string(opNumber) + " " + commentSafe(operation.id));
    tag(out, "UI_TOOL: T=" + std::to_string(operation.tool.number) + " TYPE=" + toolType(operation.geometry) +
                 " D=" + num(operation.tool.diameterMm));
    // No moves are planned yet: the block says so instead of inventing a
    // trajectory. The next slice replaces this line with the planned moves.
    tag(out, "  MOVES NOT PLANNED YET");
}

void emitFooter(std::string& out, const Program& program) {
    line(out, "N900 M9");
    line(out, "N905 M5");
    line(out, "N910 G0 Z" + num(program.safeZ));
    line(out, "N915 M30");
    line(out, "%");
}

} // namespace

std::string emitProgram(const Program& program, const Path& path) {
    std::string out;
    out.reserve(512 + path.moves.size() * 24);

    emitHeader(out, program);
    int operationNumber = 0;
    for (const Operation& operation : program.operations) {
        if (!operation.enabled) { continue; }
        emitOperation(out, operation, ++operationNumber);
    }
    emitFooter(out, program);
    return out;
}

} // namespace qp
