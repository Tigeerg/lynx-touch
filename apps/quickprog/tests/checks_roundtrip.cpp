// tests/checks_roundtrip.cpp - the generated NC program through the viewer core
// (PLAN-QUICKPROG, section 10: "the generated NC program is read by the panel
// parser, the modal codes match"; the viewer additionally checks arcs and
// cycles).
//
// The check does not trust the emitter's promise: it parses the bytes with the
// same gv::parseGcode the 3D viewer uses and asks the core what it saw. That is
// the point of the round-trip - a tag the viewer does not read, a cycle it does
// not expand or an arc it cannot follow shows up here, not on the operator's
// screen.
//
// The cases:
//
//   rt.face / rt.pocket / rt.circle / rt.drill   a single operation: the core
//                                                sees segments and the final
//                                                safe Z;
//   rt.mixed                                     face + pocket + drill grid: the
//                                                stage count and labels are the
//                                                UI_OP names of the operations;
//   rt.mixed.tool*                               the UI_TOOL of each stage is the
//                                                operation's tool (gv::parseUiTool);
//   rt.tags                                      UI_STOCK / UI_ORIGIN read by
//                                                gv::parseUiStock / parseUiOrigin
//                                                equal the document.
#include <algorithm>
#include <array>
#include <optional>
#include <string>

#include "core/gcode_parser.h"
#include "core/ui_tags.h"

#include "tests/harness.h"
#include "tests/roundtrip_fixtures.h"

namespace roundtrip_checks {

using qpcheck::check;
using qpcheck::near;
using namespace roundtrip_fixtures;

namespace {

// The real parser of the viewer core: the bytes of the file, not the document.
gv::ParseResult parseOf(const qp::Program& program) {
    return gv::parseGcode(ncgOf(program));
}

// A stage label in the core is "<UI_OP name> · <tool family>" (opsWithTools):
// the separator is the middle dot (UTF-8 C2 B7). The label prefix is the
// operation name, so the check compares the prefix and the count.
bool startsWithLabel(const std::string& label, const std::string& name) {
    return label.size() >= name.size() && label.compare(0, name.size(), name) == 0;
}

// The final point of the core is the machine point at M30: the trailer parks the
// tool at safe Z and leaves XY where the last move put it. The safe Z is the
// contract; XY is the last hole or the last move and is not asserted here.
void checkOne(const std::string& id, const qp::Program& program) {
    const gv::ParseResult parsed = parseOf(program);
    check(parsed.seg_count > 0, id + ".segments",
          "the core found " + std::to_string(parsed.seg_count) + " segments, expected > 0");
    check(near(parsed.last[2], program.safeZ, kPosEps), id + ".safe_z",
          "the last Z of the core is " + std::to_string(parsed.last[2]) +
              ", the safe Z of the document is " + std::to_string(program.safeZ));
}

} // namespace

void runRoundtripChecks() {
    checkOne("rt.face", faceProgram());
    checkOne("rt.pocket", pocketProgram());
    checkOne("rt.circle", circleProgram());
    checkOne("rt.drill", drillProgram());
}

// The mixed file: one stage per operation, the stage labels are the UI_OP names.
void runMixedChecks() {
    const qp::Program program = mixedProgram();
    const gv::ParseResult parsed = parseOf(program);

    check(parsed.seg_count > 0, "rt.mixed.segments",
          "the core found " + std::to_string(parsed.seg_count) + " segments, expected > 0");
    check(near(parsed.last[2], program.safeZ, kPosEps), "rt.mixed.safe_z",
          "the last Z of the core is " + std::to_string(parsed.last[2]) +
              ", the safe Z of the document is " + std::to_string(program.safeZ));

    check(parsed.stages.size() == program.operations.size(), "rt.mixed.stage_count",
          "the core reports " + std::to_string(parsed.stages.size()) +
              " stages for " + std::to_string(program.operations.size()) + " operations");

    const std::size_t n = std::min(parsed.stages.size(), program.operations.size());
    for (std::size_t i = 0; i < n; ++i) {
        const std::string &label = parsed.stages[i].label;
        const std::string &name = program.operations[i].id;
        check(startsWithLabel(label, name), "rt.mixed.label" + std::to_string(i + 1),
              "stage " + std::to_string(i + 1) + " label '" + label +
                  "' does not start with the UI_OP name '" + name + "'");
    }

    // The tool of each stage is the operation's tool: the core reads UI_TOOL
    // through gv::parseUiTool and attaches the first tag inside the span.
    for (std::size_t i = 0; i < n; ++i) {
        const gv::ToolInfo &info = parsed.stages[i].info;
        const qp::Operation &op = program.operations[i];
        const std::string id = "rt.mixed.tool" + std::to_string(i + 1);
        check(info.present, id + ".present", "the core found no UI_TOOL for the stage");
        check(info.number == op.tool.number, id + ".number",
              "the core reads T=" + std::to_string(info.number) + ", the operation has T=" +
                  std::to_string(op.tool.number));
        check(near(info.diameter, op.tool.diameterMm, 1e-6), id + ".diameter",
              "the core reads D=" + std::to_string(info.diameter) + ", the operation has D=" +
                  std::to_string(op.tool.diameterMm));
    }
}

// The UI_* tags of the header: the core reads the stock bounds and the origin
// by the same functions the viewer uses. The origin of the emitter is zero, and
// so is the "tag absent" answer of parseUiOrigin - the case still proves the tag
// sits on a line the core reads as a tag (a broken spelling gives zeros too, so
// the stock case, which returns nullopt, is the strict one).
void runTagChecks() {
    const qp::Program program = mixedProgram();
    const std::string text = ncgOf(program);

    const std::optional<std::array<double, 6>> stock = gv::parseUiStock(text);
    check(stock.has_value(), "rt.tags.stock_present",
          "gv::parseUiStock did not find UI_STOCK in the generated file");
    if (stock.has_value()) {
        const double expected[6] = {program.stock.min.x, program.stock.max.x,
                                    program.stock.min.y, program.stock.max.y,
                                    program.stock.min.z, program.stock.max.z};
        const char *names[6] = {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};
        bool same = true;
        std::string reason = "gv::parseUiStock differs: ";
        for (int i = 0; i < 6; ++i) {
            const double got = (*stock)[static_cast<std::size_t>(i)];
            if (!near(got, expected[i], 1e-6)) {
                same = false;
                reason += std::string(names[i]) + "=" + std::to_string(got) + " ";
            }
        }
        check(same, "rt.tags.stock_values", reason);
    } else {
        check(false, "rt.tags.stock_values", "UI_STOCK unreadable: values not compared");
    }

    const std::array<double, 3> origin = gv::parseUiOrigin(text);
    check(near(origin[0], 0.0) && near(origin[1], 0.0) && near(origin[2], 0.0),
          "rt.tags.origin",
          "gv::parseUiOrigin returned (" + std::to_string(origin[0]) + ", " +
              std::to_string(origin[1]) + ", " + std::to_string(origin[2]) +
              "), the emitter writes X0. Y0. Z0.");
}

} // namespace roundtrip_checks
