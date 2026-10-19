// core/stock_front.h - хроника реза: поле высот вместе с длиной пути, на которой
// каждый узел углубился (PLAN-STOCK-TIMEFIELD.md, опора 2). Слой чистый: ни Qt,
// ни OCCT.
//
// Съём монотонен: убранное не возвращается. Поэтому на каждый узел кроме глубины
// хранится длина пути последнего углубления - метка времени `tCut`. Поле на
// момент `t` - это порог по метке: узлы с tCut <= t со своей глубиной, остальные
// нетронуты. Перемотка тогда не пересчитывает путь (нынешний пересчёт префикса
// стоит 200-232 мс на узле 0,5 и до 4 с на узле 0,2 - DESIGN.md), а меняет одно
// число: O(1) на кадр.
//
// Границы представления названы, а не спрятаны. На узел хранится ОДНА метка
// времени - последнее углубление, рядом с итоговой глубиной. Пока узел режется
// один раз, поле на момент t точное. Если узел углубляют разные операции
// (черновая и чистовая), ранние моменты показывают узел уже с итоговой глубиной
// с момента последнего реза, а не промежуточную: точный проигрыш потребовал бы
// списка вкладов на узел (журнал вкладов, PLAN-STOCK.md 2.4). Долю узлов,
// углублённых больше одного раза, считает front_stock_probe (случай
// front_repeated_cut) - число, а не обещание.
#pragma once

#include <cstdint>
#include <vector>

#include "core/path_prep.h"
#include "core/stock_sim.h"

namespace gv {

// Хроника реза по программе: поле и метка времени на узел.
class StockChronicle {
public:
    // Обход программы точным съёмом (core/stock_sim, sweepExact) с записью
    // хроники. Ложь - программы нет или инструмент картой не выражается; в
    // втором случае причину называют refusedStage/refusedTool. targetMm -
    // остановиться на этой длине пути (отрицательное - вся программа).
    bool build(const PathData& data, const StockBounds& bounds, double cellMm,
               const ToolInfo& initialTool, double targetMm = -1.0);

    bool built() const { return built_; }
    const StockField& field() const { return field_; }
    // Метка времени узла, мм; 0 - узел не тронут.
    const std::vector<float>& eventMm() const { return field_.eventMm(); }

    // Глубина узла на момент tMm, мм: узел, углублённый позже tMm, нетронут.
    double depthAt(double tMm, int ix, int iy) const;
    // Верх материала в узле на момент tMm, машинный Z, мм.
    double heightAt(double tMm, int ix, int iy) const;

    // Всё поле на момент tMm как буфер глубин (R32F-совместимо), в порядке
    // depthMm поля. Пересчёта пути нет: чтение глубины и сравнение метки.
    const std::vector<float>& viewDepthMm(double tMm) const;

    int moves() const { return moves_; }
    double lengthMm() const { return length_mm_; }
    double buildMs() const { return build_ms_; }
    // Узлы, углублённые больше одного раза: цена одномерной метки времени.
    long long repeatedCutNodes() const { return repeated_cut_nodes_; }
    bool refused() const { return refused_; }
    int refusedStage() const { return refused_stage_; }
    int refusedTool() const { return refused_tool_; }

private:
    StockField field_;
    mutable std::vector<float> view_;
    int moves_ = 0;
    double length_mm_ = 0.0;
    double build_ms_ = 0.0;
    long long repeated_cut_nodes_ = -1;
    bool built_ = false;
    bool refused_ = false;
    int refused_stage_ = -1;
    int refused_tool_ = 0;
};

} // namespace gv
