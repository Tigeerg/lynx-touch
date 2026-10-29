// core/validate.cpp - the safety rules of PLAN-QUICKPROG, section 8.
//
// The rules are guard clauses over the document, not over the planned path:
// the form runs them after every keystroke, before a path exists. The planner
// is allowed to assume a valid document; the caller validates first.
//
// Coverage of the row "operation is not entirely outside the stock": the
// strip layout of a face operation covers the stock rectangle by construction,
// so only the explicit Z placement is checked here. The pocket and drilling
// operations of the next slices add their XY containment check; there is no
// silent pass for them because they are not planned yet either.
#include "core/validate.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

#include "core/feeds.h"
#include "core/limits.h"

namespace qp {
namespace {

std::string fmt(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", value);
    return std::string(buf);
}

void add(std::vector<Check>& out, const std::string& field, const std::string& reason) {
    out.push_back(Check{field, reason});
}

// The material of the demo document is not in the model yet: the table row
// "aluminium" is the one the form offers by default, so the limits are checked
// against it. A material field is the next slice.
constexpr Material kDefaultMaterial = Material::Aluminium;

ToolKind toolKind(const OperationGeometry& geometry) {
    if (std::holds_alternative<FaceOp>(geometry) || std::holds_alternative<RectPocketOp>(geometry) ||
        std::holds_alternative<CirclePocketOp>(geometry)) {
        return ToolKind::Flat;
    }
    return ToolKind::Drill;
}

double stockTop(const StockBox& stock) {
    return std::fmax(stock.min.z, stock.max.z);
}

double stockBottom(const StockBox& stock) {
    return std::fmin(stock.min.z, stock.max.z);
}

void checkTool(const Operation& op, std::vector<Check>& out) {
    if (op.tool.number <= 0) {
        add(out, "tool.number", "Инструмент не выбран: укажите номер в таблице инструмента.");
    }
    const MachineLimits& limits = machineLimits();
    if (op.tool.diameterMm <= 0.0) {
        add(out, "tool.diameterMm", "Диаметр инструмента должен быть больше нуля.");
    } else if (op.tool.diameterMm < limits.minToolDiameterMm ||
               op.tool.diameterMm > limits.maxToolDiameterMm) {
        add(out, "tool.diameterMm",
            "Диаметр " + fmt(op.tool.diameterMm) + " мм вне станка: допустимо от " +
                fmt(limits.minToolDiameterMm) + " до " + fmt(limits.maxToolDiameterMm) + " мм.");
    }
}

void checkCutting(const Operation& op, std::vector<Check>& out) {
    const MachineLimits& limits = machineLimits();
    const Feeds table = feedsFor(kDefaultMaterial, op.tool.diameterMm, toolKind(op.geometry));
    if (op.cutting.spindleRpm <= 0.0) {
        add(out, "cutting.spindleRpm", "Обороты шпинделя должны быть больше нуля.");
    } else if (op.cutting.spindleRpm > limits.maxRpm) {
        add(out, "cutting.spindleRpm",
            "Обороты " + fmt(op.cutting.spindleRpm) + " об/мин выше максимума станка " +
                fmt(limits.maxRpm) + " об/мин.");
    }
    if (op.cutting.feedMmMin <= 0.0) {
        add(out, "cutting.feedMmMin", "Рабочая подача должна быть больше нуля.");
    } else if (op.cutting.feedMmMin > limits.maxFeedMmMin) {
        add(out, "cutting.feedMmMin",
            "Подача " + fmt(op.cutting.feedMmMin) + " мм/мин выше максимума станка " +
                fmt(limits.maxFeedMmMin) + " мм/мин.");
    } else if (table.feedMmMin > 0.0 && op.cutting.feedMmMin > table.feedMmMin * 3.0) {
        add(out, "cutting.feedMmMin",
            "Подача " + fmt(op.cutting.feedMmMin) + " мм/мин втрое выше табличной (" +
                table.source + ": " + fmt(table.feedMmMin) + "). Проверьте режим.");
    }
    // The plunge feed burns small end mills first: it is never faster than the
    // cutting feed.
    if (op.cutting.plungeFeedMmMin <= 0.0) {
        add(out, "cutting.plungeFeedMmMin", "Подача врезания должна быть больше нуля.");
    } else if (op.cutting.plungeFeedMmMin > op.cutting.feedMmMin && op.cutting.feedMmMin > 0.0) {
        add(out, "cutting.plungeFeedMmMin", "Подача врезания не должна быть выше рабочей подачи.");
    }
}

void checkDepth(const Program& program, const Operation& op, std::vector<Check>& out) {
    if (op.depthMm <= 0.0) {
        add(out, "depthMm", "Глубина обработки должна быть больше нуля.");
        return;
    }
    if (op.depthMm > stockTop(program.stock) - stockBottom(program.stock) + 1e-9 &&
        op.topZ <= stockTop(program.stock) + 1e-9) {
        add(out, "depthMm", "Глубина " + fmt(op.depthMm) + " мм ниже дна заготовки: снимается не более " +
                                fmt(stockTop(program.stock) - stockBottom(program.stock)) + " мм.");
    }
    if (op.topZ - op.depthMm < stockBottom(program.stock) - 1e-9) {
        add(out, "depthMm", "Дно прохода " + fmt(op.topZ - op.depthMm) + " мм ниже дна заготовки " +
                                fmt(stockBottom(program.stock)) + " мм: уменьшите глубину.");
    }
}

// A face operation cuts strips of one plane, so `topZ` is the plane itself and
// the axial depth is what `depthMm` says. All other kinds descend from `topZ`,
// so there the step is the only limit of one pass.
bool descendsAxially(const OperationGeometry& geometry) {
    return !std::holds_alternative<FaceOp>(geometry);
}

void checkStep(const Operation& op, std::vector<Check>& out) {
    // A zero step is an unfinished form for every kind; for a face it would
    // also make the pass layout meaningless, so the rule is unconditional.
    if (!(op.stepDownMm > 0.0)) {
        add(out, "stepDownMm", "Шаг врезания должен быть больше нуля: иначе проходов не будет.");
    } else if (descendsAxially(op.geometry) && op.stepDownMm > op.depthMm && op.depthMm > 0.0) {
        add(out, "stepDownMm", "Шаг врезания " + fmt(op.stepDownMm) + " мм больше глубины " +
                                   fmt(op.depthMm) + " мм: проход будет один, но проверьте задание.");
    }
}

void checkSafeZ(const Program& program, std::vector<Check>& out) {
    if (!(program.safeZ > stockTop(program.stock))) {
        add(out, "safeZ", "Безопасный Z " + fmt(program.safeZ) + " мм не выше верха заготовки " +
                              fmt(stockTop(program.stock)) + " мм.");
    }
}

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

void checkGeometry(const Program& program, const Operation& op, std::vector<Check>& out) {
    if (std::holds_alternative<FaceOp>(op.geometry)) {
        checkFaceGeometry(program, op, out);
        return;
    }
    add(out, "geometry", "Операция этого типа пока не планируется: проверка геометрии появится вместе с ней.");
}

} // namespace

bool validWcs(const std::string& text) {
    return text == "G54" || text == "G55" || text == "G56" || text == "G57" || text == "G58" ||
           text == "G59";
}

std::vector<Check> validate(const Program& program) {
    std::vector<Check> out;
    if (!validWcs(program.wcs)) {
        add(out, "wcs", "Смещение " + program.wcs + " не найдено: выберите G54..G59.");
    }
    if (program.operations.empty()) {
        add(out, "operations", "В программе нет ни одной операции.");
        return out;
    }
    for (const Operation& op : program.operations) {
        if (!op.enabled) { continue; }
        checkTool(op, out);
        checkCutting(op, out);
        checkDepth(program, op, out);
        checkStep(op, out);
        checkGeometry(program, op, out);
    }
    checkSafeZ(program, out);
    return out;
}

} // namespace qp
