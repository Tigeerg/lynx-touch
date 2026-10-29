// core/emit_frame.cpp - the header, the trailer and the tag spelling (7.1).
//
// The frame repeats data/cad-testdata/linuxcnc_ui.cps: the percent sign, the
// MACHINE and UI_STOCK tags, the modal block, the work offset and the parking
// move before the first tool change, and M30 with the percent sign at the
// end. The block numbers are the caller's: the header and the operations share
// one counter, so a block keeps a readable number across the whole file.
#include "core/emit_frame.h"

#include <variant>

#include "core/emit_internal.h"
#include "core/version.h"

namespace qp {
namespace {

// A comment line of the file: "(TAG: ...)", the reference spelling.
void tag(std::string& out, const std::string& body) {
    out += "(" + body + ")";
    out += '\n';
}

} // namespace

bool validWcs(const std::string& text) {
    return text == "G54" || text == "G55" || text == "G56" || text == "G57" || text == "G58" ||
           text == "G59";
}

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

std::string toolType(const OperationGeometry& geometry) {
    const bool milling = std::holds_alternative<FaceOp>(geometry) ||
                         std::holds_alternative<RectPocketOp>(geometry) ||
                         std::holds_alternative<CirclePocketOp>(geometry);
    return milling ? "FLAT" : "DRILL";
}

void emitHeader(std::string& out, const Program& program, int& sequence) {
    const StockBox& stock = program.stock;

    out += "%\n";
    tag(out, "MACHINE");
    tag(out, "  MODEL: " + commentSafe(program.name));
    tag(out, "  GENERATOR: " + std::string(kGeneratorName) + " " + kGeneratorVersion);
    tag(out, "UI_ORIGIN: X0. Y0. Z0.");
    tag(out, "UI_STOCK: XMIN=" + num(stock.min.x) + " XMAX=" + num(stock.max.x) +
                 " YMIN=" + num(stock.min.y) + " YMAX=" + num(stock.max.y) +
                 " ZMIN=" + num(stock.min.z) + " ZMAX=" + num(stock.max.z));

    out += numberedBlock(sequence, "G21 G90 G94 G17 G40 G49 G80") + "\n";
    sequence += kSequenceStep;
    // A silent fallback to G54 would cut at the wrong offset. The document is
    // validated before emitting; the comment names the problem in the file
    // instead of hiding it.
    if (!validWcs(program.wcs)) {
        tag(out, "  WCS INVALID: " + commentSafe(program.wcs) + " - G54 USED");
    }
    out += numberedBlock(sequence, validWcs(program.wcs) ? program.wcs : std::string("G54")) + "\n";
    sequence += kSequenceStep;
    // Park above the stock before the first tool change: the tool stays out of
    // the material whatever Z the previous program left.
    out += numberedBlock(sequence, "G0 Z" + coord(program.safeZ)) + "\n";
    sequence += kSequenceStep;
}

void emitFooter(std::string& out, int& sequence) {
    out += numberedBlock(sequence, "M30") + "\n";
    out += "%\n";
}

} // namespace qp
