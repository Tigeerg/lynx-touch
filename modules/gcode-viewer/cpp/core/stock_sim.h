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

    // Хроника реза (PLAN-STOCK-TIMEFIELD.md, опора 2): запись длины пути, на
    // которой узел последний раз углубился. Включают ДО reset: буфер длин
    // выделяется вместе с полем. Выключено - буфера нет, цена нулевая.
    void recordEvents(bool on);
    bool eventsRecorded() const { return event_mm_.size() == depth_.size(); }
    // Длина пути, которой помечается следующее углубление узла, мм. Съём
    // монотонен, поэтому метка только растёт (порядок программы).
    void setEventMm(double pathMm) { event_at_ = float(pathMm); }
    // Длина пути последнего углубления узла, мм; 0 - узел не тронут.
    const std::vector<float>& eventMm() const { return event_mm_; }
    // Сколько узлов углублено больше одного раза: цена одномерной метки
    // времени (хроника реза, PLAN-STOCK-TIMEFIELD.md, опора 2). Счётчик - байт
    // на узел рядом с меткой; 255 углублений и больше считаются одним значением.
    long long eventOverwrites() const { return event_overwrites_; }

private:
    StockBounds bounds_;
    double cellMm_ = 0.5;
    int nx_ = 0;
    int ny_ = 0;
    std::vector<float> depth_;
    std::vector<float> event_mm_;      // длина пути углубления, если включена
    std::vector<std::uint8_t> event_count_;   // байт на узел: сколько раз углублён
    bool record_events_ = false;
    float event_at_ = 0.0f;
    long long event_overwrites_ = 0;   // узлов, углублённых больше одного раза
};

// Обход программы с полем и курсором: съём монотонен, поэтому обход продолжается
// с последнего целого хода, и тик живой публикации платит только за новый участок
// пути. Назад по пути (цель позади курсора) поле сбрасывается и обход идёт с
// начала. Объект не потокобезопасен: поле и курсор трогает один поток за раз.
class StockWalk {
public:
    // Начать обход с нуля: поле под габарит и узел. Ложь - поля не будет:
    // инструмент этапа картой не выражается (refusedStage) или проходить
    // нечего (ходов меньше двух). Поле тогда пустое. recordEvents - вместе с
    // полем вести хронику реза (длину пути углубления на узел, см. StockField).
    bool start(const PathData& data, const StockBounds& bounds, double cellMm,
               std::vector<StockCutter> cutters, bool recordEvents = false);

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
    bool record_events_ = false;        // вести хронику реза вместе с полем
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

// Отрезок точного контура реза, машинные мм: граница, где глубина поля
// линейно пересекает порог cutMm (PLAN-STOCK.md, 3.1). Положение перехода -
// под-ячейковое, поэтому стенка кармана ложится ровной линией, а не ступенями
// узлов.
struct StockContourSegment {
    double x0Mm = 0.0;
    double y0Mm = 0.0;
    double x1Mm = 0.0;
    double y1Mm = 0.0;
};

// Контур реза (marching squares) по полю глубин. Седло разрешается знаком
// среднего по клетке; порядок рёбер фиксирован (низ, право, верх, лево) -
// контур детерминирован.
std::vector<StockContourSegment> stockCutContour(const StockBounds& bounds, double cellMm,
                                                 const std::vector<float>& depthMm,
                                                 int countX, int countY, double cutMm);

// Вертикальная стенка по контуру: верх на плоскости нетронутого верха (zMaxMm),
// низ - на высоте дна реза в точке контура (билинейно по узлам клетки). Верх
// сходится с верхом поля, низ - с дном, щели нет. Вершина - 4 float (позиция 3f
// + глубина дна для цвета), два треугольника на отрезок контура.
struct StockWallMesh {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;

    int triangleCount() const { return int(indices.size() / 3); }
};

StockWallMesh stockCutWall(const StockBounds& bounds, double cellMm,
                           const std::vector<float>& depthMm, int countX, int countY,
                           double cutMm);

// Точная граница реза - ОТДЕЛЬНОЕ представление, независимое от карты высот
// (PLAN-STOCK.md, этап 1.5). Полилиния нулевого уровня (глубина - cutMm):
// marching squares по знакам узлов поля выбирает рёбра-кандидаты, а положение
// перехода на ребре уточняется делением отрезка по ТОЧНОЙ аналитической
// глубине хода (та же формула, что у узла поля), поэтому ошибка не зависит от
// узла. Знак в узле берётся из поля: в узле глубина поля точна, поэтому узел
// задаёт лишь выбор ребра, а не положение границы. Поле, его байты и его
// сверки не трогаются - граница считается по ходам заново.
//
// Полилиния идёт по УРОВНЮ реза, а не по хорде между переходами: переходы
// стоят на уровне точно, но хорда между ними - касательная дуги уровня, и её
// направление скачет от клетки к клетке, поэтому и линия, и кромка реза шли
// изломом в каждую клетку (замер, случай boundary_refined: излом вершины 13.2
// градуса, медиана 9.8, при узле 0.5 у круга радиуса 2.19). Между
// переходами одной клетки кладутся подточки (subdivPerCell - 1 штука, доли
// вдоль хорды), каждая спускается на уровень по ТОЧНОЙ глубине хода вдоль
// градиента билинейной реконструкции поля (он перпендикулярен уровню, поэтому
// спуск трансверсален), поэтому цепочка повторяет саму дугу уровня, а излом
// вершины падает в subdiv раз. Отрезков у клетки становится subdiv вместо
// одного; переходы не двигаются, поэтому топология (выбор рёбер) та же.
// Подточка, которой уровня рядом не нашлось (уровень дальше клетки от хорды -
// клетка, где хорда перескакивает через нетронутый участок), остаётся на хорде
// и считается в unprojected: это цена, названная числом, а не спрятанная.
struct StockBoundaryResult {
    std::vector<StockContourSegment> segments;   // отрезки границы, машинные мм
    int crossingEdges = 0;                        // рёбер с переходом порога
    int unprojected = 0;                          // подточек, оставшихся на хорде
    long long evaluations = 0;                    // обращений к точной глубине
    double buildMs = 0.0;
};

// Состояние точной границы реза для живого тика: раскладка ходов по плиткам и
// отрезки ПО КЛЕТКАМ поля. Контроллер держит его в сессии и передаёт в
// stockCutBoundary, поэтому первый вызов (полный проход) строит всё, а тик
// пересчитывает только клетки, чьи узлы изменились (съём монотонен: глубина
// узла только растёт), - цена тика пропорциональна новому участку границы, а не
// всей границе. Члены - внутренние; наружу торчит только StockBoundaryResult.
// Пустой указатель - сборка без состояния (вся граница каждый раз).
struct StockBoundaryCache {
    StockBounds bounds;                                        // геометрия сборки
    int countX = 0;
    int countY = 0;
    double cellMm = 0.0;
    int tileCountX = 0;                                        // раскладка ходов
    int tileCountY = 0;
    double tileMm = 0.0;
    std::vector<float> depth;                                  // поле на момент сборки
    std::vector<std::vector<StockContourSegment>> cellSegments;  // (countX-1)*(countY-1)
    std::vector<std::array<double, 3>> moveA;                  // ходы: начало, машинные мм
    std::vector<std::array<double, 3>> moveB;                  // ходы: конец
    std::vector<int> moveCutter;                               // ход -> инструмент этапа
    std::vector<std::vector<int>> buckets;                     // плитка -> номера ходов
};

// Дробление перехода границы на клетку (умолчание stockCutBoundary): шесть
// звеньев на переход - это звено 0.083 мм при узле 0.5, меньше пикселя кадра
// 1024x600 с дистанции 341 мм (0.33 мм на пиксель). Цена - точные обращения к
// глубине хода на подточку (замер в stock_sim_probe, случай boundary_refined).
constexpr int BOUNDARY_SUBDIV_PER_CELL = 6;

StockBoundaryResult stockCutBoundary(const PathData& data, const StockBounds& bounds,
                                     double cellMm, const std::vector<StockCutter>& cutters,
                                     const std::vector<float>& depthMm, int countX, int countY,
                                     double cutMm, StockBoundaryCache* cache = nullptr,
                                     int refineSteps = 24,
                                     int subdivPerCell = BOUNDARY_SUBDIV_PER_CELL);

// Знаковое расстояние до точной границы в узлах поля, мм: отрицательно там, где
// глубина выше порога (снято). Это представление границы для рисования:
// нулевой уровень между узлами линеен, поэтому цвет и линия идут по границе, а
// не по ступеням узла. Магнитуда - расстояние до ближайшего отрезка полилинии
// (не дальше radiusMm), знак - по глубине узла (она в узле точна).
std::vector<float> stockCutSignedDistance(const StockBounds& bounds, double cellMm,
                                          const std::vector<float>& depthMm,
                                          int countX, int countY,
                                          const std::vector<StockContourSegment>& segments,
                                          double cutMm, double radiusMm);

// Знаковое расстояние до УТОЧНЁННОЙ цепочки реза на сетке, в refine раз более
// частой, чем поле (PLAN-STOCK.md, этап 1.4-рисование): нулевой уровень такой
// сетки повторяет САМУ цепочку, а не хорду клетки. Цвет поверхности (вид «точный
// контур», stocksurface.frag) берёт сторону по мелкой сетке, поэтому кромка цвета
// совпадает с кромкой геометрии. Зачем мелкая сетка: билинейная интерполяция
// знакового расстояния в клетке поля идёт по хорде - у круга радиуса 2.19 мм и
// узла 0.5 мм волна нулевого уровня 0.014 мм (случай cut_colour_edge), - а мелкая
// сетка повторяет дугу с ошибкой порядка (клетка/refine)^2 / (8 * радиус).
//
// Знак берётся билинейно из поля знакового расстояния (stockCutSignedDistance):
// у него нулевой уровень уже назван стороной цепочки, а мелкая сетка только
// уточняет МАГНИТУДУ. Магнитуда - точное расстояние до ближайшего звена цепочки,
// не дальше plateauMm; за плато значение - знак * plateauMm (там сторона точки
// уже не при чём). Между двумя узлами мелкой сетки разных знаков нулевой уровень
// стоит на весе расстояний, поэтому ложится на саму цепочку, а не на узел.
//
// Цена (Test_model, узел 0.5, refine=2, плато 1 клетка): узлов мелкой сетки
// 601x401 = 964 КиБ против 242 КиБ у поля; сборка - перебор узлов окрестности
// каждого звена (замер в stock_sim_probe, случай cut_colour_edge). Уточнённая
// цепочка приходит снаружи (stockCutBoundary); поле, его байты и его сверки не
// трогаются - это отдельное представление для рисования.
std::vector<float> stockCutChainDistance(const StockBounds& bounds, double cellMm,
                                         const std::vector<float>& signedMm,
                                         int countX, int countY,
                                         const std::vector<StockContourSegment>& segments,
                                         double plateauMm, int refine,
                                         int* outCountX, int* outCountY);

// Стенка по готовой полилинии границы: буфер тот же, что у stockCutWall
// (4 float на вершину), но контур приходит извне - точной границей. Низ - по
// глубине поля в точке контура (билинейно), верх - плоскость нетронутого верха.
StockWallMesh stockCutWallFromSegments(const StockBounds& bounds, double cellMm,
                                       const std::vector<float>& depthMm, int countX, int countY,
                                       const std::vector<StockContourSegment>& segments);

// Половина нарисованного остатка (PLAN-STOCK.md, 3.1/1.3). Верх - плоскость
// нетронутого материала там, где глубины нет; дно - снятый металл.
enum class StockLayer { Top, Floor };

// Слоистая поверхность остатка: нарисованный остаток разрезан по ТОЧНОЙ границе
// реза на две половины - верх и дно. Границей служит та же полилиния, что у
// стенки (stockCutBoundary), поэтому половины сходятся ровно на ней: полоса
// перехода между уровнями исчезает, а отвесную грань между ними несёт слой
// стенки. Прежний верх (stockSurfaceMesh) держал переход ПРАВИЛОМ внутри клетки
// (stocksurface.vert, steppedDepth): на уступе 0.8 мм при узле 0.5 мм полоса
// перехода шириной в подклетку 0.25 мм давала наклон 17.4 градуса от отвеса
// (случай island_rim_drawn пробника stock_sim_probe).
//
// Какая половина где: сторону называет знак глубины узлов поля (глубина выше
// порога - дно). Клетка, через которую граница не проходит, уходит своей
// половине двумя треугольниками; клетку с границей режут ЦЕПОЧКИ границы (см.
// stockCutBoundary): подряд идущие отрезки, у которых конец одного совпадает с
// началом другого. Цепочка - не хорда: у уточнённой границы внутри клетки есть
// узлы, и половина берёт СВОЮ сторону цепочки, а не отрезанный хордой кусок:
// вершины схода ложатся ровно на полилинию границы, поэтому полоса перехода
// нулевая, а куски половин не налезают друг на друга (сумма площадей половин
// равна площади клетки - случай layer_surface). При одном звене в цепочке
// половина - прежний кусок клетки, отрезанный хордой.
//
// Сторона вершины, стоящей НА границе, в шейдере берётся знаком знакового
// расстояния (stocksurface.vert, steppedDepth): ровно на нулевом уровне знак -
// монетка, а нулевой уровень билинейной реконструкции лежит от полилинии на
// 0.011 мм (случай boundary_drawn). Поэтому у вершины, где правило шейдера
// СТУПАЕТ (у уступа - там знак и выбирает уровень), texcoord уводится на 4 %
// клетки внутрь своей половины (0.02 мм при узле 0.5) - вдвое больше этой
// ошибки; позиция остаётся ровно на границе, поэтому полоса перехода нулевая, а
// не 0.02 мм. У склона правило билинейно и от знака не зависит: там texcoord не
// двигается, и половины сходятся вершина в вершину.
struct StockLayerSurface {
    std::vector<float> vertices;      // 5 float на вершину: позиция 3f, texcoord 2f
    std::vector<std::uint32_t> indices;
    int topVertexCount = 0;           // первые topVertexCount вершин - верх
    int topIndexCount = 0;            // первые topIndexCount индексов - верх
    int boundaryVertices = 0;         // вершин, стоящих на точной границе
    int shiftedUvVertices = 0;        // из них со сдвинутым texcoord (см. выше)

    int triangleCount() const { return int(indices.size() / 3); }
    int topTriangles() const { return topIndexCount / 3; }
    int floorTriangles() const { return (int(indices.size()) - topIndexCount) / 3; }
};

// Слоистая поверхность по полю и точной границе, машинные мм. Пусто, если поля
// нет; пустая граница допустима - тогда половина клетки называется знаком её
// узлов. subdiv не нужен: билинейная высота на подклетке - та же поверхность, а
// ступень уступа теперь режется геометрией, а не дроблением меша.
StockLayerSurface stockLayerSurface(const StockBounds& bounds, double cellMm,
                                    const std::vector<float>& depthMm, int countX, int countY,
                                    double cutMm,
                                    const std::vector<StockContourSegment>& segments);

// Глубина нарисованной половины в точке, мм: зеркало правила шейдера
// (stocksurface.vert, steppedDepth) с НАЗВАННОЙ стороной. У настоящего уступа
// (клетка-кинк с обеими площадками) половина стоит на своём уровне - самый
// мелкий узел клетки у верха, самый глубокий у дна; у склона и канавки в клетку
// правило билинейно, поэтому склон ступенью не портится. Стенке это правило
// нужно для низа: низ обязан лечь на кромку дна, иначе между стенкой и дном
// остаётся щель высотой в уступ (у уступа 0.8 мм билинейная глубина в точке
// контура - 0.32, а кромка дна - все 0.8).
double stockLayerDepthMm(const StockBounds& bounds, double cellMm,
                         const std::vector<float>& depthMm, int countX, int countY,
                         double cutMm, double xMm, double yMm, StockLayer layer);

// Стенка по точной границе с низом ПО СЛОЮ (stockLayerDepthMm, дно): тот же
// буфер, что у stockCutWallFromSegments (4 float на вершину), но низ - кромка
// отрезанного дна, а не билинейная глубина поля в точке контура.
StockWallMesh stockCutWallFromLayer(const StockBounds& bounds, double cellMm,
                                    const std::vector<float>& depthMm, int countX, int countY,
                                    double cutMm,
                                    const std::vector<StockContourSegment>& segments);

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
