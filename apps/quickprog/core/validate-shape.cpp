// core/validate-shape.cpp - the geometry and cycle rules of every kind (8).
//
// Coverage of the row "operation is not entirely outside the stock": the strip
// layout of a face operation covers the stock rectangle by construction, so
// only the explicit Z placement is checked here. The pockets check that the
// tool body with the allowance fits inside their contour; the drilling kinds
// check every hole centre with the tool body against the stock rectangle.
#include <cmath>
#include <string>
#include <variant>

#include "core/path_drill.h"
#include "core/validate_internal.h"

namespace qp {
namespace valdetail {
namespace {

// Face: the tool has to fit the stock width in Y (the strip direction) and the
// stock has to be non-empty. A pocket would check its own rectangle; the face
// keeps the stock as the shape.
void checkFaceGeometry(const Program& program, const Operation& op, std::vector<Check>& out) {
    const double widthY = program.stock.max.y - program.stock.min.y;
    if (widthY <= 0.0 || program.stock.max.x - program.stock.min.x <= 0.0) {
        add(out, "geometry", "Заготовка вырождена: ширина или длина не больше нуля.");
        return;
    }
    if (op.tool.diameterMm > widthY) {
        add(out, "geometry", "Фреза " + fmt(op.tool.diameterMm) + " мм шире заготовки " + fmt(widthY) +
                                 " мм: возьмите фрезу поменьше.");
    }
    const auto& face = std::get<FaceOp>(op.geometry);
    if (face.stepOverPct < 20.0 || face.stepOverPct > 90.0) {
        add(out, "geometry", "Перекрытие полос " + fmt(face.stepOverPct) +
                                 " % вне разумных 20..90 %: полосы либо не сомкнутся, либо прожгут.");
    }
    if (face.z > op.topZ + 1e-9) {
        add(out, "geometry", "Плоскость обработки выше верха материала.");
    }
    // The plane has to move from one pass to the next. The step is a limit of
    // one descent, not a regularity rule: a step of 2 mm over 25 mm is normal,
    // so only a step under a tenth of a millimetre is called out. Layout
    // warnings (a thin last pass) belong to path_face, not to this check.
    if (op.depthMm > 0.0 && op.stepDownMm > 0.0 && op.stepDownMm < 0.1) {
        add(out, "stepDownMm", "Шаг врезания " + fmt(op.stepDownMm) +
                                   " мм меньше 0.1 мм: проходов будет очень много.");
    }
}

// Pocket: the tool body with the finish allowance has to fit the pocket, else
// the planner has no trajectory to find and the operator would get a path that
// cuts the wall or nothing at all. The side that fails is named with its
// numbers, so the form points at the field: the diameter or the allowance.
void checkPocketGeometry(const Operation& op, std::vector<Check>& out) {
    const RectPocketOp& pocket = std::get<RectPocketOp>(op.geometry);
    const Rect& rect = pocket.rect;
    if (rect.w <= 0.0 || rect.h <= 0.0) {
        add(out, "geometry", "Карман вырожден: ширина или высота не больше нуля.");
        return;
    }
    if (pocket.finishAllowanceMm < 0.0) {
        add(out, "geometry", "Припуск " + fmt(pocket.finishAllowanceMm) +
                                 " мм отрицательный: траектория выйдет за контур кармана.");
        return;
    }
    if (pocket.cornerRadiusMm < 0.0) {
        add(out, "geometry", "Радиус угла " + fmt(pocket.cornerRadiusMm) +
                                 " мм не может быть отрицательным.");
        return;
    }
    const double shortSide = std::fmin(rect.w, rect.h);
    if (pocket.cornerRadiusMm > shortSide / 2.0 + 1e-9) {
        add(out, "geometry", "Радиус угла " + fmt(pocket.cornerRadiusMm) +
                                 " мм больше половины короткой стороны: такого кармана нет.");
        return;
    }
    const double diam = op.tool.diameterMm;
    const double allowance = pocket.finishAllowanceMm;
    const double inset = diam / 2.0 + allowance;
    // Strict: at equal sizes the inner rectangle is a line and nothing is cut.
    if (inset + 1e-9 >= shortSide / 2.0) {
        add(out, "geometry", "Фреза ф" + fmt(diam) + " мм и припуск " + fmt(allowance) +
                                 " мм не влезают в карман " + fmt(rect.w) + "x" + fmt(rect.h) +
                                 " мм: нужно не больше " + fmt(shortSide / 2.0) +
                                 " мм от стенки, а получается " + fmt(inset) +
                                 " мм. Возьмите фрезу поменьше.");
    }
}

// Circular pocket: the tool body with the finish allowance has to fit inside
// the pocket radius. At equal sizes the centre line is a point and nothing is
// cut, so the rule is strict; the message names the pocket radius and the
// inset, so the form points at the diameter or the allowance.
void checkCirclePocketGeometry(const Operation& op, std::vector<Check>& out) {
    const CirclePocketOp& pocket = std::get<CirclePocketOp>(op.geometry);
    if (pocket.circle.r <= 0.0) {
        add(out, "geometry", "Круглый карман вырожден: радиус " + fmt(pocket.circle.r) +
                                 " мм не больше нуля.");
        return;
    }
    if (pocket.finishAllowanceMm < 0.0) {
        add(out, "geometry", "Припуск " + fmt(pocket.finishAllowanceMm) +
                                 " мм отрицательный: траектория выйдет за контур кармана.");
        return;
    }
    const double diam = op.tool.diameterMm;
    const double inset = diam / 2.0 + pocket.finishAllowanceMm;
    if (inset + 1e-9 >= pocket.circle.r) {
        add(out, "geometry", "Фреза ф" + fmt(diam) + " мм и припуск " +
                                 fmt(pocket.finishAllowanceMm) + " мм не влезают в круглый карман ф" +
                                 fmt(2.0 * pocket.circle.r) + " мм: от центра до стенки " +
                                 fmt(pocket.circle.r) + " мм, а фреза с припуском занимает " +
                                 fmt(inset) + " мм. Возьмите фрезу поменьше.");
    }
}

// One hole has to stand inside the stock with its body: the tool does not cut
// the wall of the part, and a hole partly outside the fixture is a broken
// drill. The stock triangle is the whole check: a hole inside the XY rectangle
// with a diameter smaller than the stock wall is safe.
void checkHoleInside(const Program& program, const Operation& op, const Point3& hole,
                     std::vector<Check>& out) {
    const double r = op.tool.diameterMm / 2.0;
    const StockBox& stock = program.stock;
    if (hole.x - r < stock.min.x - 1e-9 || hole.x + r > stock.max.x + 1e-9 ||
        hole.y - r < stock.min.y - 1e-9 || hole.y + r > stock.max.y + 1e-9) {
        add(out, "geometry", "Отверстие (" + fmt(hole.x) + ", " + fmt(hole.y) +
                                 ") вне заготовки: с инструментом ф" + fmt(op.tool.diameterMm) +
                                 " мм оно заходит за края X " + fmt(stock.min.x) + ".." +
                                 fmt(stock.max.x) + ", Y " + fmt(stock.min.y) + ".." +
                                 fmt(stock.max.y) + " мм. Сдвиньте отверстие или уменьшите фрезу.");
        return;
    }
}

void checkDrillGeometry(const Program& program, const Operation& op, std::vector<Check>& out) {
    const DrillStats stats = drillStats(program, op);
    for (const std::string& error : stats.errors) {
        add(out, "geometry", error);
    }
    if (stats.points.empty()) { return; }
    for (const Point3& hole : stats.points) {
        const std::size_t before = out.size();
        checkHoleInside(program, op, hole, out);
        // One message is enough: the first hole outside names the point, the
        // others would repeat the same verdict with a different number.
        if (out.size() != before) { return; }
    }
}

} // namespace

void checkGeometry(const Program& program, const Operation& op, std::vector<Check>& out) {
    if (std::holds_alternative<FaceOp>(op.geometry)) {
        checkFaceGeometry(program, op, out);
        return;
    }
    if (std::holds_alternative<RectPocketOp>(op.geometry)) {
        checkPocketGeometry(op, out);
        return;
    }
    if (std::holds_alternative<CirclePocketOp>(op.geometry)) {
        checkCirclePocketGeometry(op, out);
        return;
    }
    checkDrillGeometry(program, op, out);
}

// The peck of a G83 cycle is a step down the hole: it cannot be deeper than
// the hole and it cannot be a speck. The default (zero) is "the planner
// decides" and is not checked here.
void checkPeck(const Operation& op, std::vector<Check>& out) {
    double peck = 0.0;
    if (const auto* hole = std::get_if<DrillHoleOp>(&op.geometry)) {
        peck = hole->params.peckMm;
    } else if (const auto* grid = std::get_if<DrillGridOp>(&op.geometry)) {
        peck = grid->params.peckMm;
    } else if (const auto* ring = std::get_if<DrillCircleOp>(&op.geometry)) {
        peck = ring->params.peckMm;
    } else {
        return;
    }
    if (peck <= 0.0) { return; }
    if (op.depthMm > 0.0 && peck > op.depthMm + 1e-9) {
        add(out, "peck", "Шаг вывода стружки Q" + fmt(peck) + " мм больше глубины отверстия " +
                             fmt(op.depthMm) + " мм: такого цикла нет.");
        return;
    }
    if (peck < 0.1 - 1e-9) {
        add(out, "peck", "Шаг вывода стружки Q" + fmt(peck) +
                             " мм меньше 0.1 мм: цикл будет только дробить стружку.");
    }
}

} // namespace valdetail
} // namespace qp
