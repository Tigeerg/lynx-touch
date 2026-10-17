// core/stock_sim.h - поле высот остатка заготовки: сетка по машинным X/Y, в
// узле - верх материала в колонке (DESIGN.md, "Симуляция съёма материала").
// Здесь же меш верха поля для рисования (stockSurfaceMesh): он строится по тем
// же узлам, а высоту вычитает вершинный шейдер из поля как текстуры.
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
#include <cstdint>
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
// текстуру R32F): 0 - верх нетронут. Индекс узла - iy * countX + ix, строка
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

// Верхняя сетка поля для рисования: вершина на узел поля, texcoord - центр его
// текселя. Формат 20 Б на вершину (позиция 3f + texcoord 2f), как у буферов
// траектории; позиции - машинные мм, в узле стоит верх заготовки (zMaxMm):
// высоту вычитает вершинный шейдер, читая поле как текстуру R32F, - поэтому
// сетка одна и та же, что считается, и второго уровня сэмплирования нет.
// Последний узел по X и по Y растянут до габарита: сетка узлов кончается на
// (число узлов - 1) * узел, и без растяжения верх не накрыл бы коробку до её
// стенки (разрыв не больше узла). Индексы - два треугольника на ячейку.
struct StockSurfaceMesh {
    std::vector<float> vertices;       // 5 float на вершину
    std::vector<std::uint32_t> indices;
    int countX = 0;
    int countY = 0;

    int triangleCount() const { return int(indices.size() / 3); }
};

// Меш верха поля, машинные мм. Пуст, если узлов меньше двух по любой оси:
// рисовать нечего.
StockSurfaceMesh stockSurfaceMesh(const StockBounds& bounds, double cellMm,
                                  int countX, int countY);

// Стенки и дно коробки заготовки: пять прямоугольников, верхней грани нет -
// её рисует поле. Позиции - машинные мм, коробка крупнее габарита на зазоры:
// ими разводят совпадающие грани заготовки и детали (прежние доли пикселя),
// физический габарит они не двигают. Край коробки - сам габарит, а не сетка
// узлов: последний узел поля растянут до габарита (см. stockSurfaceMesh),
// поэтому верх и стенки сходятся без щели.
std::vector<float> stockShellVertices(const StockBounds& bounds, double cellMm,
                                      int countX, int countY,
                                      double gapSideMm, double gapTopMm,
                                      double gapBottomMm);

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
