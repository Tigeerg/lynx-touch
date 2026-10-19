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
//
// Живая публикация (StockWalk): поле и курсор обхода живут между вызовами, съём
// до текущего pathDist продолжается с последнего целого хода, а не считается
// заново; назад по пути - сброс и обход с начала. Инструмент с поднутрением
// (лоллипоп, ласточка, Т-слот) картой не выражается: обход отказывается целиком,
// поля не публикуется (DESIGN.md, "Симуляция съёма материала").
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include "core/path_prep.h"
#include "gv_types.h"

namespace gv {

// Участок режущего профиля по радиусу: отрезок в плоскости (r, h) или дуга
// h(r) = centerHMm - sqrt(arcRadiusMm^2 - (r - centerRMm)^2), взятая нижней
// половиной. Так записаны примитивы инструмента: шар (центр (0, R), радиус R),
// скругление нижнего угла (центр (r - cr, cr), радиус cr), фаска и конус
// (отрезок). Радиус - параметр, h - высота над остриём, мм.
struct StockProfilePiece {
    enum class Kind { Line, Arc };
    Kind kind = Kind::Line;
    double r0Mm = 0.0;                 // начало участка по радиусу
    double r1Mm = 0.0;                 // конец участка по радиусу, r1 > r0
    double h0Mm = 0.0;                 // Line: высота на r0
    double h1Mm = 0.0;                 // Line: высота на r1
    double centerRMm = 0.0;            // Arc: центр дуги по радиусу
    double centerHMm = 0.0;            // Arc: центр дуги по высоте
    double arcRadiusMm = 0.0;          // Arc: радиус дуги, мм

    // h(r) на участке, мм. Значение вне [r0Mm, r1Mm] не определено.
    double heightAt(double radiusMm) const;
    // dh/dr на участке, мм на мм: у отрезка - наклон, у дуги - касательная.
    double slopeAt(double radiusMm) const;
};

// Нижняя огибающая режущей части аналитическими примитивами: то, чем считается
// точный съём (StockField::sweepExact). Участки идут по возрастанию радиуса,
// h непрерывна и не убывает. Пустой профиль - инструмент не режет.
//
// Таблица radiusMm/heightMm у StockCutter остаётся эталоном и входом старой
// выборки по положениям; точный съём читает отсюда: у шара тут истинная сфера,
// а не 12 хорд (stock_sim_probe, случаи ball_plunge_exact и
// inclined_ball_quadrature).
struct StockProfile {
    std::vector<StockProfilePiece> pieces;
    double maxRadiusMm = 0.0;          // последний радиус огибающей, мм

    bool empty() const { return pieces.empty(); }
    // h(r), мм; +infinity вне maxRadiusMm.
    double heightAt(double radiusAtMm) const;
};

// Аналитическая огибающая по метаданным UI_TOOL: примитивы вместо хорд.
// Вылет обрезает участки так же, как обрезает таблицу (stockCutterFromProfile).
StockProfile stockProfileFromTool(const ToolInfo& info);

// Аналитическая огибающая по готовому профилю (r, y), мм: каждый интервал -
// отрезок. Так точный съём воспроизводит таблицу, если примитивы неизвестны
// (проверка строит профиль сама, не через UI_TOOL).
StockProfile stockProfileFromPoints(const std::vector<ProfilePoint>& profile,
                                    double exposedMm);

// Профиль резания радиальной таблицей h(r), мм: на расстоянии r от оси острие
// режет на высоте tipZ + h(r), всё выше в этой колонке снимается. Таблица -
// нижняя огибающая профиля до вылета, считается один раз на инструмент этапа.
struct StockCutter {
    std::vector<double> radiusMm;      // узлы таблицы по возрастанию
    std::vector<double> heightMm;      // высота над остриём в узле
    StockProfile profile;              // аналитическая огибающая: точный съём
    double maxRadiusMm = 0.0;          // шире не режет
    double exposedMm = 0.0;            // вылет инструмента, мм
    int number = 0;                    // номер UI_TOOL: им называется отказ
    bool undercut = false;             // шире кверху: картой не выражается

    // h(r), мм; +infinity вне maxRadiusMm.
    double heightAt(double radiusAtMm) const;
};

// Инструмент из UI_TOOL: профиль gv::toolProfile, режет до toolExposedLength.
// Тип с поднутрением (лоллипоп, ласточка, Т-слот) помечается undercut: профиль
// такой формы toolProfile строит как прямую фрезу, и съём срезал бы материал,
// который на станке остаётся.
StockCutter stockCutter(const ToolInfo& info);

// Таблица по готовому профилю (r, y), мм. Нужна пробнику: проверка строит
// профиль сама, не через UI_TOOL.
StockCutter stockCutterFromProfile(const std::vector<ProfilePoint>& profile,
                                   double exposedMm);

// Инструменты этапов программы: UI_TOOL сменяет предыдущий, этап без тега несёт
// предыдущий (на станке инструмент меняется только по M6); до первого тега
// действует `initialTool`. Этапов нет - один инструмент `initialTool`.
std::vector<StockCutter> stockCutterPlan(const PathData& data, const ToolInfo& initialTool);

// Этап, чей инструмент карта высот не выражает, или -1: у него поднутрение
// (профиль шире кверху), а карта по колонке срезала бы материал, который на
// станке остаётся. Программа считается целиком: один такой этап делает ложью
// всё поле, а не только свой участок.
int stockUndercutStage(const std::vector<StockCutter>& cutters);

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

    // Точный съём вдоль отрезка A -> B (PLAN-STOCK.md, 3.2): H(P) = минимум по t
    // из [0, 1] от (z(t) + h(r(t))), h - аналитическая огибающая cutter.profile,
    // r(t) - расстояние от узла до точки отрезка в XY. Обходится ровно капсула
    // (bbox отрезка плюс maxRadiusMm), каждый узел - один раз на ход: нет ни
    // повторного обхода узла, ни ошибки дискретизации шага cell / 2.
    void sweepExact(const StockCutter& cutter,
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

// Обход программы с полем и курсором: съём монотонен, поэтому обход продолжается
// с последнего целого хода, и тик живой публикации платит только за новый участок
// пути. Назад по пути (цель позади курсора) поле сбрасывается и обход идёт с
// начала. Объект не потокобезопасен: поле и курсор трогает один поток за раз.
class StockWalk {
public:
    // Начать обход с нуля: поле под габарит и узел. Ложь - поля не будет:
    // инструмент этапа картой не выражается (refusedStage) или проходить
    // нечего (ходов меньше двух). Поле тогда пустое.
    bool start(const PathData& data, const StockBounds& bounds, double cellMm,
               std::vector<StockCutter> cutters);

    bool started() const { return started_; }
    int refusedStage() const { return refused_stage_; }
    int refusedTool() const { return refused_tool_; }
    // Длина пути, съём до которой уже в поле, мм.
    double cursorMm() const { return cursor_mm_; }

    // Продолжить обход до длины пути targetMm. Ходы берутся целиком: ход,
    // кончающийся позже цели, ждёт следующего вызова. `cancelled(pathDist)` -
    // как в simulateStock, ложь возврата - обход прерван токеном и неполон.
    // `progress(pathDist)` - после целого хода, как в simulateStock.
    bool advanceTo(double targetMm,
                   const std::function<bool(double pathDist)>& cancelled = {},
                   const std::function<void(double pathDist)>& progress = {});

    StockField& field() { return field_; }
    const StockField& field() const { return field_; }
    int moves() const { return moves_; }
    double lengthMm() const { return length_mm_; }
    // Время последнего сброса поля (аллокация) и обхода после него, мс: по ним
    // меряется перемотка, а сумма - цена полного прохода.
    double lastResetMs() const { return last_reset_ms_; }
    double advanceMs() const { return advance_ms_; }

private:
    // Поле с нуля, курсор в начало программы.
    void restart();

    const PathData* data_ = nullptr;
    std::vector<StockCutter> cutters_;
    std::vector<double> starts_;        // начало этапа по длине пути, мм
    StockBounds bounds_;
    double cellMm_ = 0.5;
    StockField field_;
    std::size_t next_ = 1;              // индекс точки конца следующего хода
    std::size_t stage_ = 0;
    double cursor_mm_ = 0.0;
    int moves_ = 0;
    double length_mm_ = 0.0;
    double last_reset_ms_ = 0.0;
    double advance_ms_ = 0.0;
    bool started_ = false;
    int refused_stage_ = -1;
    int refused_tool_ = 0;
};

// Верхняя сетка поля для рисования: вершина на узел поля, texcoord - центр его
// текселя. Формат 20 Б на вершину (позиция 3f + texcoord 2f), как у буферов
// траектории; позиции - машинные мм, в узле стоит верх заготовки (zMaxMm):
// высоту вычитает вершинный шейдер, читая поле как текстуру R32F, - поэтому
// сетка одна и та же, что считается, и второго уровня сэмплирования нет.
// Последний узел по X и по Y растянут до габарита: сетка узлов кончается на
// (число узлов - 1) * узел, и без растяжения верх не накрыл бы коробку до её
// стенки (разрыв не больше узла). Индексы - два треугольника на ячейку.
//
// subdiv > 1 дробит каждую клетку поля на subdiv x subdiv подклеток: узлы
// подклеток лежат на сетке узла поля (шаг узел/subdiv), texcoord - та же доля
// клетки, а countX/countY - числа УЖЕ НАРИСОВАННОЙ сетки. Это нужно гладкому
// виду: вершинный шейдер берёт высоту билинейно по узлам поля, и чем чаще
// вершины, тем мельче ступенька затенения (stocksurface.vert). Поле, его байты
// и его сверки дробление не трогает - меняется только рисуемый меш.
struct StockSurfaceMesh {
    std::vector<float> vertices;       // 5 float на вершину
    std::vector<std::uint32_t> indices;
    int countX = 0;
    int countY = 0;

    int triangleCount() const { return int(indices.size() / 3); }
};

// Меш верха поля, машинные мм. Пуст, если узлов меньше двух по любой оси:
// рисовать нечего. subdiv = 1 (умолчание) - прежняя сетка по узлам поля,
// байт в байт та же: её сверяет случай stocksurface пробника stock_sim_probe.
StockSurfaceMesh stockSurfaceMesh(const StockBounds& bounds, double cellMm,
                                  int countX, int countY, int subdiv = 1);

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
// токеном, поле неполное и публиковать его нельзя. `refused` - инструмент
// программы картой высот не выражается (поднутрение): поля нет вовсе,
// `refusedStage` называет этап (с нуля), `refusedTool` - номер UI_TOOL.
struct StockRunResult {
    StockField field;
    int moves = 0;
    double lengthMm = 0.0;
    double resetMs = 0.0;
    double computeMs = 0.0;
    bool cancelled = false;
    bool refused = false;
    int refusedStage = -1;
    int refusedTool = 0;
};

// Поле по готовой траектории: все ходы, включая быстрые, инструмент этапа из
// stage.info (этап без UI_TOOL несёт предыдущий, `initialTool` - до первого
// тега). `pts` в координатах сцены, как в PathData: обратно в машинные
// (x, -z, y) точки переводятся здесь, чтобы съём шёл по машинной сетке.
//
// Обход идёт StockWalk до конца программы; тот же объект с целью меньше полной
// длины даёт поле до текущего pathDist (живая публикация, см. StockWalk).
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
