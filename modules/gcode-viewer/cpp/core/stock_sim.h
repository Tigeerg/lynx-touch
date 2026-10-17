// core/stock_sim.h - поле высот остатка заготовки: сетка по машинным X/Y, в
// узле - верх материала в колонке (DESIGN.md, "Симуляция съёма материала").
//
// Слой чистый, как core/collision: ни Qt, ни OCCT. Шаг узла приходит аргументом:
// ядро CNC_QUALITY не читает, уровень качества ему передаёт вызывающий. Режет
// только нижняя огибающая профиля режущей части (gv::toolProfile) до вылета;
// оправка не режет никогда - иначе съём скрыл бы ту коллизию, которую ищет скан.
//
// Из DESIGN взято: узел как допуск пресета, съём - минимум по колонке вдоль пути,
// шаг вдоль пути - половина узла (предложение, отсюда правило "колонка между
// положениями не остаётся нетронутой"), хранение глубины от верха заготовки
// (предложение под R16F), идут все перемещения, включая быстрые.
// Моё: узел лежит в углу габарита, а не в центре ячейки; число узлов
// floor(протяжённость / узел) + 1 (при 200 мм и 0.5 мм - 401, как в DESIGN);
// шаг считается по 3D-длине отрезка, поэтому сдвиг по XY между соседними
// положениями не больше шага; глубина обрезана толщиной заготовки; h(r) -
// линейная интерполяция по вершинам профиля (у шара их 12, отсюда расхождение с
// истинной сферой около 0.006 мм при r = 3).
#pragma once

#include <array>
#include <functional>
#include <limits>
#include <vector>

#include "core/path_prep.h"
#include "gv_types.h"

namespace gv {

// Профиль резания радиальной таблицей h(r), мм: на расстоянии r от оси острие
// режет на высоте tipZ + h(r), всё выше в этой колонке снимается. Таблица -
// нижняя огибающая профиля до вылета, считается один раз на инструмент этапа.
struct StockCutter {
    std::vector<double> radiusMm;      // узлы таблицы по возрастанию
    std::vector<double> heightMm;      // высота над остриём в узле
    double maxRadiusMm = 0.0;          // шире не режет
    double exposedMm = 0.0;            // вылет инструмента, мм
    bool undercut = false;             // шире кверху: картой не выражается

    // h(r), мм; +infinity вне maxRadiusMm.
    double heightAt(double radiusAtMm) const;
};

// Инструмент из UI_TOOL: профиль gv::toolProfile, режет до toolExposedLength.
StockCutter stockCutter(const ToolInfo& info);

// Таблица по готовому профилю (r, y), мм. Нужна пробнику: проверка строит
// профиль сама, не через UI_TOOL.
StockCutter stockCutterFromProfile(const std::vector<ProfilePoint>& profile,
                                   double exposedMm);

// Габарит заготовки в машинных координатах, мм.
struct StockBounds {
    double xMinMm = 0.0;
    double xMaxMm = 0.0;
    double yMinMm = 0.0;
    double yMaxMm = 0.0;
    double zMinMm = 0.0;
    double zMaxMm = 0.0;
};

// Поле высот. Глубина хранится от верха заготовки (float32, как пойдёт в
// текстуру R32F): 0 - верх нетронут. Индекс узла - iy * countX() + ix, строка
// по Y, как в текстуре.
class StockField {
public:
    void reset(const StockBounds& bounds, double cellMm);

    bool empty() const { return nx_ <= 0 || ny_ <= 0; }
    double cellMm() const { return cellMm_; }
    const StockBounds& bounds() const { return bounds_; }
    int countX() const { return nx_; }
    int countY() const { return ny_; }

    double nodeXMm(int ix) const { return bounds_.xMinMm + double(ix) * cellMm_; }
    double nodeYMm(int iy) const { return bounds_.yMinMm + double(iy) * cellMm_; }

    // Верх материала в узле, машинный Z, мм.
    double heightMm(int ix, int iy) const;
    void setHeightMm(int ix, int iy, double zMm);

    // Съём в одном положении: острие в (xMm, yMm, tipZMm).
    void plunge(const StockCutter& cutter, double xMm, double yMm, double tipZMm);

    // Съём вдоль отрезка A -> B в машинных координатах, мм. Шаг - половина узла
    // по 3D-длине, поэтому сдвиг по XY между положениями не больше шага.
    void sweep(const StockCutter& cutter,
               const std::array<double, 3>& aMm,
               const std::array<double, 3>& bMm);

    // Глубины для текстуры: countX * countY float32, глубина от zMaxMm вниз.
    const std::vector<float>& depthMm() const { return depth_; }

private:
    StockBounds bounds_;
    double cellMm_ = 0.5;
    int nx_ = 0;
    int ny_ = 0;
    std::vector<float> depth_;
};

// Итог прохода по траектории: поле и его цена. `cancelled` - проход прерван
// токеном, поле неполное и публиковать его нельзя.
struct StockRunResult {
    StockField field;
    int moves = 0;
    double lengthMm = 0.0;
    double resetMs = 0.0;
    double computeMs = 0.0;
    bool cancelled = false;
};

// Поле по готовой траектории: все ходы, включая быстрые, инструмент этапа из
// stage.info (этап без UI_TOOL несёт предыдущий, `initialTool` - до первого
// тега). `pts` в координатах сцены, как в PathData: обратно в машинные
// (x, -z, y) точки переводятся здесь, чтобы съём шёл по машинной сетке.
//
// `cancelled(pathDist)` зовётся перед каждым ходом; истина - проход обрывается
// без результата. `progress(pathDist)` - после хода, по той же накопленной длине
// пути, что у скана коллизий. Оба колбэка необязательны. Поток один: порядок
// ходов - порядок программы, внутри съёма нет потоков и перестановок, поэтому
// одни и те же путь, инструмент и узел дают бит-в-бит то же поле.
StockRunResult simulateStock(const PathData& data, const StockBounds& bounds, double cellMm,
                             const ToolInfo& initialTool,
                             const std::function<bool(double pathDist)>& cancelled = {},
                             const std::function<void(double pathDist)>& progress = {});

} // namespace gv
