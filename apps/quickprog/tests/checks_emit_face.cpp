// tests/checks_emit_face.cpp - the face strip layout as the emitter writes it (7.1).
//
// The planner covers the stock from both edges, the strips land on the stock
// plus the X lead and no rapid descends into the material: the cases read the
// moves of the reference face program, so a layout change fails on a number.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace emit_checks {

using namespace qpcheck;

using qpcheck::axisSlot;
using qpcheck::axisValue;
using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::demoProgram;
using qpcheck::faceOp;
using qpcheck::faceProgram;
using qpcheck::hasWord;
using qpcheck::ModalState;
using qpcheck::moveBlocks;
using qpcheck::near;
using qpcheck::setAxis;


// The strip layout of path_face.cpp covers the stock from both edges, not a
// centred grid. `usable = width - D` is the span the centres have to cross;
// with a nominal step `D * stepOver` the count is `ceil(usable / nominal) + 1`
// and the real step is `usable / (count - 1)`, so the edges land exactly at
// `D/2` and no strip is farther apart than the nominal step. 100x80, D10, 70 %:
// usable 70, nominal 7, ceil(70 / 7) + 1 = 12, real step 70 / 11 = 6.3636 mm.
void checkFaceStripFormula() {
    const qp::Program program = faceProgram();
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    // 70 mm of the span over a 7 mm nominal step is exactly 10 intervals, and
    // ceil(10) + 1 is 11 strips — the same count the former floor formula gave
    // here. The difference shows on a span that is not a multiple of the step:
    // there the ceiling keeps the edge strips at half a tool inside the edges
    // instead of centring the grid and leaving a wider margin at both sides.
    check(stats.strips == 11, "face.formula_strips",
          "100x80, D10, 70 % must give ceil(70 / 7) + 1 = 11 strips, got " +
              std::to_string(stats.strips));
    check(near(stats.stripStepMm, 70.0 / 10.0), "face.formula_step",
          "the real step must be 70 / 10 = 7 mm, got " + std::to_string(stats.stripStepMm));
    check(stats.stripStepMm <= 7.0 + 1e-9 && stats.stripStepMm <= 10.0,
          "face.formula_step_bounded",
          "the real step must not exceed the nominal 7 mm nor the 10 mm diameter");

    {
        qp::Program d6 = faceProgram();
        d6.operations[0].tool.diameterMm = 6.0;
        const qp::FaceStats s6 = qp::faceStats(d6, d6.operations.front());
        // usable 74, nominal 4.2, ceil(74 / 4.2) = ceil(17.62) = 18, + 1 = 19.
        check(s6.strips == 19, "face.formula_d6",
              "100x80, D6, 70 % must give ceil(74 / 4.2) + 1 = 19 strips, got " +
                  std::to_string(s6.strips));
        // The stall count (74 / 4.2) is real outside the 1e-9 clamp, so the
        // count has to be the ceil and not a floor with a tail dropped.
        check(near(s6.stripStepMm, 74.0 / 18.0), "face.formula_d6_step",
              "the real step of D6 must be 74 / 18 = 4.1111 mm, got " +
                  std::to_string(s6.stripStepMm));
    }
    {
        // A 100 % step: usable 70, nominal 10, ceil(70 / 10) + 1 = 8 strips,
        // the edges still at D/2 and the real step exactly the diameter.
        qp::Program coarse = faceProgram();
        std::get<qp::FaceOp>(coarse.operations[0].geometry).stepOverPct = 100.0;
        const qp::FaceStats cs = qp::faceStats(coarse, coarse.operations.front());
        check(cs.strips == 8, "face.formula_full_step",
              "100 % step on 80 mm with D10 must give ceil(70 / 10) + 1 = 8 strips, got " +
                  std::to_string(cs.strips));
        check(near(cs.stripStepMm, 10.0), "face.formula_full_step_step",
              "100 % step must give a real step of 10 mm, got " + std::to_string(cs.stripStepMm));
    }
}

// No strip pair and no edge leaves a gap: the tool body of one strip
// `[y - D/2, y + D/2]` overlaps its neighbour, the first strip reaches the low
// edge and the last reaches the high edge. Checked over the real move list, so
// a planner that drifts from the formula fails here.
void checkFaceCoverage() {
    auto stripYs = [](const qp::Program& program) {
        const qp::Path planned = qp::planPath(program);
        // The deepest plane is the last pass; its long cuts are the strips.
        bool havePlane = false;
        double minZ = 0.0;
        for (const qp::Move& m : planned.moves) {
            if (m.kind == qp::MoveKind::Cut && (!havePlane || m.to.z < minZ)) {
                minZ = m.to.z;
                havePlane = true;
            }
        }
        std::vector<double> ys;
        for (std::size_t i = 1; i < planned.moves.size(); ++i) {
            const qp::Move& m = planned.moves[i];
            if (m.kind != qp::MoveKind::Cut || !near(m.to.z, minZ) || m.hasArc) { continue; }
            const qp::Move& prev = planned.moves[i - 1];
            if (!near(m.to.z, prev.to.z)) { continue; }
            if (std::fabs(m.to.x - prev.to.x) < 1.0) { continue; }
            if (ys.empty() || !near(ys.back(), m.to.y)) { ys.push_back(m.to.y); }
        }
        return ys;
    };

    const qp::Program program = faceProgram();
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    const std::vector<double> ys = stripYs(program);
    const double r = stats.toolRadiusMm;
    int gaps = 0;
    for (std::size_t i = 0; i + 1 < ys.size(); ++i) {
        if (ys[i + 1] - r > ys[i] + r + 1e-9) { ++gaps; }
    }
    const bool edgeLow = !ys.empty() && ys.front() - r <= program.stock.min.y + 1e-9;
    const bool edgeHigh = !ys.empty() && ys.back() + r >= program.stock.max.y - 1e-9;
    check(gaps == 0 && edgeLow && edgeHigh, "face.coverage",
          "the strips must cover Y -40..+40 without gaps: gaps=" + std::to_string(gaps) +
              " low=" + std::to_string(static_cast<int>(edgeLow)) +
              " high=" + std::to_string(static_cast<int>(edgeHigh)));

    // A 100 % step has zero overlap: the bodies only touch. The coverage is
    // still complete, and the narrow overlap is called out as a warning.
    qp::Program full = faceProgram();
    std::get<qp::FaceOp>(full.operations[0].geometry).stepOverPct = 100.0;
    const qp::FaceStats fs = qp::faceStats(full, full.operations.front());
    const std::vector<double> fys = stripYs(full);
    int fgaps = 0;
    for (std::size_t i = 0; i + 1 < fys.size(); ++i) {
        if (fys[i + 1] - fs.toolRadiusMm > fys[i] + fs.toolRadiusMm + 1e-9) { ++fgaps; }
    }
    const bool fEdgeLow = !fys.empty() && fys.front() - fs.toolRadiusMm <= full.stock.min.y + 1e-9;
    const bool fEdgeHigh = !fys.empty() && fys.back() + fs.toolRadiusMm >= full.stock.max.y - 1e-9;
    check(fgaps == 0 && fEdgeLow && fEdgeHigh, "face.coverage_full_step",
          "a 100 % step must still cover the stock: gaps=" + std::to_string(fgaps));
    bool warned = false;
    for (const std::string& text : qp::faceWarnings(full, full.operations.front())) {
        if (text.find("Перекрытие") != std::string::npos) { warned = true; }
    }
    check(warned, "face.coverage_full_step_warning",
          "a 100 % step leaves no overlap and must warn about it");

    // A stock exactly one diameter wide: one strip through the centre, no gap
    // on either edge, no overlap warning.
    qp::Program one = faceProgram();
    one.stock.min.y = -5.0;
    one.stock.max.y = 5.0;
    const std::vector<double> oys = stripYs(one);
    check(oys.size() == 1 && near(oys.front(), 0.0), "face.coverage_one_strip",
          "an 80 mm stock replaced by a 10 mm one must give a single centred strip, got " +
              std::to_string(oys.size()) + " strips");
}

// Every cut stays inside the stock grown by the lead on each side along X and
// exactly inside the stock across Y (no Y lead runout on face); a rapid never
// descends below topZ inside the material. The second check is about the
// descent, not about safe Z: the lead of the plunge is a real Z of the planner
// and must be named by the rule.
void checkFaceBoundsAndRapids() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    // The X lead is 10 % of D clamped to 1..D/2: 1 mm on D10.
    const double lead = stats.edgeOverrunMm;
    check(near(lead, 1.0), "face.lead_length", "the X lead on D10 must be 1 mm, got " +
                                                  std::to_string(lead));
    check(near(stats.sideOverrunMm, 0.0), "face.side_overrun",
          "face must not add a Y runout: got " + std::to_string(stats.sideOverrunMm));
    const double xMin = program.stock.min.x - lead - 1e-6;
    const double xMax = program.stock.max.x + lead + 1e-6;
    // Across the strips the tool centre stays half a diameter inside each edge:
    // the whole stock is covered and the centre never passes the edge.
    const double yMin = program.stock.min.y + stats.toolRadiusMm - 1e-6;
    const double yMax = program.stock.max.y - stats.toolRadiusMm + 1e-6;

    bool inBounds = true;
    bool descentOk = true;
    for (const qp::Move& m : planned.moves) {
        if (m.to.x < xMin || m.to.x > xMax || m.to.y < yMin || m.to.y > yMax) { inBounds = false; }
        if (m.kind == qp::MoveKind::Rapid && m.to.z < program.operations.front().topZ - 1e-6 &&
            m.to.z < program.stock.max.z - 1e-6) {
            // A rapid below the stock top is only allowed when the move stays
            // outside the stock rectangle: the planner never does that, so any
            // such rapid is a bug.
            descentOk = false;
        }
    }
    check(inBounds, "face.cuts_in_stock_plus_lead",
          "a move left the stock plus the X lead or crossed the Y centre window");
    check(descentOk, "face.rapid_not_into_material",
          "a rapid descended below the stock top inside the material");
    // The plunge of one strip hides the first axial step below the top; the
    // ramp back is a cut, so a rapid never descends into the material.
    check(near(stats.leadMm, 2.0), "face.lead",
          "the deepest descent below the top of one strip must be the 2 mm step");
}

} // namespace emit_checks

// Runs the face layout cases of the emitted moves.
void runEmitFaceChecks() {
    emit_checks::checkFaceStripFormula();
    emit_checks::checkFaceCoverage();
    emit_checks::checkFaceBoundsAndRapids();
}
