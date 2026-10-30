// core/validate-common.cpp - the rules every operation shares (8).
//
// Tool, cutting data, depth, step and safe Z: the checks that do not look at
// the shape of the operation. The form runs them after every keystroke, before
// a path exists; the planner is allowed to assume a valid document.
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

#include "core/feeds.h"
#include "core/limits.h"
#include "core/validate_internal.h"

namespace qp {
namespace valdetail {

std::string fmt(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", value);
    return std::string(buf);
}

void add(std::vector<Check>& out, const std::string& field, const std::string& reason) {
    out.push_back(Check{field, reason});
}

namespace {

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

} // namespace

void checkTool(const Operation& op, std::vector<Check>& out) {
    if (op.tool.number <= 0) {
        add(out, "tool.number", "Инструмент не выбран: укажите номер в таблице инструмента.");
    }
    // The H row of G43 is a row of the stand's length table: a negative number
    // does not exist, and a zero is the model default ("the tool number") and
    // is written as the tool number (PLAN-QUICKPROG, 7.3).
    if (op.tool.hNumber < 0) {
        add(out, "tool.hNumber", "Номер коррекции H " + std::to_string(op.tool.hNumber) +
                                     " отрицательный: в таблице коррекций нет такого номера.");
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
    // cutting feed. A drill has one feed by construction (`drillFeed`), so the
    // rule is not applied to it: a difference there is the model's own
    // normalisation, not a mistake of the operator.
    if (!isDrill(op.geometry)) {
        if (op.cutting.plungeFeedMmMin <= 0.0) {
            add(out, "cutting.plungeFeedMmMin", "Подача врезания должна быть больше нуля.");
        } else if (op.cutting.plungeFeedMmMin > op.cutting.feedMmMin && op.cutting.feedMmMin > 0.0) {
            add(out, "cutting.plungeFeedMmMin", "Подача врезания не должна быть выше рабочей подачи.");
        }
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

void checkStep(const Operation& op, std::vector<Check>& out) {
    // The step is a field of every kind (the document has no per-kind subset),
    // but a drill descends in one cycle and never splits the hole into passes.
    // A zero step is the normal value of a drill and is not checked there; a
    // negative step is a mistake of any kind.
    if (isDrill(op.geometry) && op.stepDownMm >= 0.0) { return; }
    if (!(op.stepDownMm > 0.0)) {
        add(out, "stepDownMm", "Шаг врезания должен быть больше нуля: иначе проходов не будет.");
    } else if (op.stepDownMm > op.depthMm && op.depthMm > 0.0) {
        add(out, "stepDownMm", "Шаг врезания " + fmt(op.stepDownMm) + " мм больше глубины " +
                                   fmt(op.depthMm) + " мм: проход будет один, но проверьте задание.");
    }
}

namespace {

// A G64 word: the field name ("g64P") and the letter the operator reads.
void checkTolerance(std::vector<Check>& out, const char* field, double value) {
    const std::string letter(1, field[3]);
    if (!(value > 0.0)) {
        add(out, field, "Допуск G64 " + letter + " = " + fmt(value) +
                            " мм: допуск должен быть больше нуля.");
    } else if (value > 1.0 + 1e-9) {
        add(out, field, "Допуск G64 " + letter + " = " + fmt(value) +
                            " мм больше 1 мм: сглаживание уведёт фрезу от контура.");
    }
}

} // namespace

void checkProgramPost(const Program& program, std::vector<Check>& out) {
    // Both G64 numbers are a path tolerance in millimetres: positive and not
    // over a millimetre, else the control either cuts every corner or reads the
    // word as a mistake (PLAN-QUICKPROG, 7.3).
    checkTolerance(out, "g64P", program.g64P);
    checkTolerance(out, "g64Q", program.g64Q);
}

void checkSafeZ(const Program& program, std::vector<Check>& out) {
    if (!(program.safeZ > stockTop(program.stock))) {
        add(out, "safeZ", "Безопасный Z " + fmt(program.safeZ) + " мм не выше верха заготовки " +
                              fmt(stockTop(program.stock)) + " мм.");
    }
}

} // namespace valdetail
} // namespace qp
