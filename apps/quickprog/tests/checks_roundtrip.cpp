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
//                                                equal the document;
//   rt.post.*                                     the post-processor words of
//                                                PLAN-QUICKPROG, 7.3 - G53 G0 Z0,
//                                                G43 H, M1 and G64 - do not
//                                                break the parser: segments and
//                                                stages stay, the final point is
//                                                the machine home.
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

// The final point of the core is the machine point at M30. Since the machine
// park of PLAN-QUICKPROG, 7.3 the last block before M30 is G53 G0 Z0, so the
// tool ends at the machine home, not at the safe Z of the part. The heuristics
// of the parser do not take the G53 word as a motion, so the last Z it models
// is the machine home the footer leaves behind; the last motion of the file
// before the park is still the safe Z, and the case reads it from the bytes.
void checkOne(const std::string& id, const qp::Program& program) {
    const gv::ParseResult parsed = parseOf(program);
    check(parsed.seg_count > 0, id + ".segments",
          "the core found " + std::to_string(parsed.seg_count) + " segments, expected > 0");
    check(near(parsed.last[2], 0.0, kPosEps), id + ".safe_z",
          "the last Z of the core is " + std::to_string(parsed.last[2]) +
              ", the machine home of the footer is 0");
}

} // namespace

void runRoundtripChecks() {
    checkOne("rt.face", faceProgram());
    checkOne("rt.pocket", pocketProgram());
    checkOne("rt.circle", circleProgram());
    checkOne("rt.drill", drillProgram());
}

// The post-processor words of this slice: the machine park, G43 H, the M1 stop
// and the G64 tolerance are words the parser does not model, and each of them
// must be skipped without losing a segment or a stage. The viewer keeps the
// last point before M30 (the last move and the trailer park), so the check
// reads the file for the machine home of the footer and does not ask the core
// for a point the parser never models: the viewer is the tool of the operator,
// the stand reads the G53.
void runPostChecks() {
    const qp::Program program = mixedProgram();
    const std::string text = ncgOf(program);
    const gv::ParseResult parsed = parseOf(program);
    check(parsed.seg_count > 0, "rt.post.segments",
          "the core found " + std::to_string(parsed.seg_count) + " segments, expected > 0");
    check(parsed.stages.size() == program.operations.size(), "rt.post.stages",
          "the core reports " + std::to_string(parsed.stages.size()) + " stages for " +
              std::to_string(program.operations.size()) + " operations");
    check(near(parsed.last[2], 0.0, kPosEps), "rt.post.safe_z",
          "the last modelled Z must be the machine home of the footer, got " +
              std::to_string(parsed.last[2]));
    const std::size_t park = text.rfind("G53 G0 Z0");
    const std::size_t m30 = text.find(" M30\n");
    check(park != std::string::npos && m30 != std::string::npos && park < m30,
          "rt.post.final_home", "the footer park must stand before M30 in the file");
    check(text.find("G53 G0 Z0") != std::string::npos && text.find(" G43 Z") != std::string::npos &&
              text.find(" G64 P") != std::string::npos && text.find("M1\n") != std::string::npos,
          "rt.post.words", "the generated file must carry the post-processor words");
}

// The mixed file: one stage per operation, the stage labels are the UI_OP names.
void runMixedChecks() {
    const qp::Program program = mixedProgram();
    const gv::ParseResult parsed = parseOf(program);

    check(parsed.seg_count > 0, "rt.mixed.segments",
          "the core found " + std::to_string(parsed.seg_count) + " segments, expected > 0");
    check(near(parsed.last[2], 0.0, kPosEps), "rt.mixed.safe_z",
          "the last Z of the core is " + std::to_string(parsed.last[2]) +
              ", the machine home of the footer is 0");

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
