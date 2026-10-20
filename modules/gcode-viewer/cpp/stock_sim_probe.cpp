// cpp/stock_sim_probe.cpp - пробник поля высот остатка заготовки.
//
// Пробник не входит ни в плагин, ни в ядро: отдельная цель CMake над gvcore,
// как stl_probe. Он делает две вещи.
//
// 1. Аналитика. Поле режется синтетической заготовкой и сверяется с формулой,
// посчитанной здесь же по геометрии инструмента и пути, - не кодом поля. Случаи:
// плоская концевая по плоскому верху (глубина постоянна везде, где прошёл
// инструмент), шаровая (круглая долина - истинная сфера), конус/V-бита (конус
// долины), рез выше заготовки (поле не меняется) и колонка, которую ловит только
// шаг в половину узла (шаг в целый узел её пропускает - это и есть довод за
// правило шага). У плоского прохода узел у самой стенки следа может остаться
// нетронутым: шаг гарантирует только узлы ближе r - шаг/2, внешняя полоса
// шириной шаг/2 - цена дискретизации, а не ошибка. Поэтому сравниваются узлы
// вне этой полосы, а узел внутри неё, но вне радиуса, обязан остаться на верху.
//
// Там же рисуемая часть: случай stocksurface строит меш верха поля
// (stockSurfaceMesh) и повторяет вершинный шейдер (texcoord -> узел поля, высота
// = верх заготовки минус глубина, кадр сцены - поворот узла), случай stockshell
// проверяет форму оболочки коробки: стенки и дно без верхней грани, а случай
// stocksurface_fine - рисование гладкого вида: дроблённую сетку (подузел на
// сетке узла, texcoord - его доля клетки), билинейную высоту подузла и правило
// уступа (на плоской клетке у кромки нормаль плоская, на клетке-стенке - наклон
// стенки). Случай stockshell_trim проверяет обрезку верха стенки по полю
// (правило stockshell.frag): на кромке габарита есть и снятые, и нетронутые узлы,
// углы кромки нетронуты, и обрезка опускает верх стенки ровно на глубину поля.
// Случай holder_rings - кольца обводки тела вращения (profileRingPoints):
// высоты колец оправки и отсутствие колец на звеньях дуг шара и скругления.
// Поле во всех случаях одно и то же: рисование его не трогает.
//
// 1б. Точный съём (PLAN-STOCK.md, 3.2). Те же формы, но огибающая считается
// замкнуто по капсуле хода, а не выборкой положений: случаи flat_sweep_exact,
// ball_plunge_exact, cone_plunge_exact и near_miss_exact держат точность против
// аналитики, inclined_ball_quadrature - против квадратуры по t высокой точности.
// Прежние случаи (flat_sweep, ball_plunge, cone_plunge, near_miss_step) остаются
// сверкой старой выборки по таблице: их числа не трогаются.
//
// 1в. Точная граница реза (PLAN-STOCK.md, 1.5). Случаи boundary_wall и
// boundary_ball мерят ошибку границы в МИЛЛИМЕТРАХ против аналитики (капсула
// радиуса 3 и круг радиуса s05) на узлах 0.5 и 0.2 мм и сравнивают три подхода:
// marching squares по полю (было), поле в 5-8 раз мельче и точную границу
// (stockCutBoundary). boundary_drawn - нулевой уровень знакового расстояния
// (им рисуется цвет и линия, stocksurface.frag). boundary_incremental держит
// совпадение границы с состоянием (StockBoundaryCache) и полной сборки после
// подрастания поля.
//
// 2. Замер. Время одного прохода длиной 50 мм и время всего Test_model.ngc
// (все ходы, включая быстрые, инструмент этапа, габарит из UI_STOCK) при узле
// 0.5 и 0.2 мм - это предложения пресетов из DESIGN.md. Печатаются строки
// "phase <имя> <мс>", как у bench_probe; разбор УП в замер не входит.
//
// Запуск: stock_sim_probe [файл.ngc]
// без аргумента берётся data/cad-testdata/Test_model.ngc от текущего каталога
// или от каталога сборки.
// Код возврата: 0 - все случаи сошлись; 1 - есть расхождение или УП не найдена.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "core/gcode_parser.h"
#include "core/path_prep.h"
#include "core/stock_sim.h"
#include "core/tooling.h"
#include "core/ui_tags.h"

namespace {

constexpr double PI = 3.14159265358979323846;

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Общий сток: не даёт оптимизатору выбросить посчитанное.
volatile std::size_t g_sink = 0;

double median(std::vector<double> v)
{
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::string toUtf8(const std::wstring& w)
{
#ifdef _WIN32
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
#else
    return std::string(w.begin(), w.end());
#endif
}

std::vector<std::string> probeArgs(int argc, char** argv)
{
#ifdef _WIN32
    int count = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) {
        out.push_back(toUtf8(wargv[i]));
    }
    LocalFree(wargv);
    return out;
#else
    std::vector<std::string> out;
    for (int i = 0; i < argc; ++i) {
        out.push_back(argv[i]);
    }
    return out;
#endif
}

void printPhase(const char* name, double ms)
{
    std::printf("phase %-26s %9.3f\n", name, ms);
}

// Инструмент для синтетических случаев: те поля UI_TOOL, что нужны профилю.
struct ToolSpec {
    const char* type = "FLAT";
    double diameterMm = 0.0;
    double taperDeg = 0.0;
    double fluteMm = 10.0;
    double bodyMm = 20.0;
    double cornerMm = 0.0;             // радиус скругления нижнего угла
};

gv::ToolInfo makeTool(const ToolSpec& spec)
{
    gv::ToolInfo info;
    info.present = true;
    info.type = spec.type;
    info.diameter = spec.diameterMm;
    info.taper_deg = spec.taperDeg;
    info.flute_length = spec.fluteMm;
    info.shoulder_length = spec.fluteMm;
    info.body_length = spec.bodyMm;
    info.overall_length = spec.bodyMm;
    info.shank_diameter = spec.diameterMm;
    info.corner_radius = spec.cornerMm;
    return info;
}

// Расстояние от точки до отрезка в плоскости XY, мм.
double segmentDistanceMm(double px, double py,
                         double ax, double ay, double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double l2 = dx * dx + dy * dy;
    double t = 0.0;
    if (l2 > 0.0) {
        t = std::min(1.0, std::max(0.0, ((px - ax) * dx + (py - ay) * dy) / l2));
    }
    return std::hypot(px - (ax + dx * t), py - (ay + dy * t));
}

// Расстояние от точки до набора отрезков границы, мм: им меряется, стоит ли
// вершина кромки на полилинии границы реза.
double contourDistanceMm(double px, double py,
                         const std::vector<gv::StockContourSegment>& segments)
{
    double best = std::numeric_limits<double>::max();
    for (const gv::StockContourSegment& s : segments) {
        best = std::min(best, segmentDistanceMm(px, py, s.x0Mm, s.y0Mm, s.x1Mm, s.y1Mm));
    }
    return best;
}

// Дайджест поля (FNV-1a 64 по сырым float32): прогоны одного пути, инструмента
// и узла обязаны дать одно и то же поле бит в бит. Порядок узлов - порядок
// depthMm (строка по Y), он же порядок буфера R32F.
std::uint64_t fieldDigest(const std::vector<float>& depth)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (float value : depth) {
        unsigned char bytes[sizeof(float)];
        std::memcpy(bytes, &value, sizeof(float));
        for (unsigned char byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

// Истинная сфера радиуса r с остриём в tipZ: верх материала в колонке.
double ballHeightMm(double radiusMm, double rMm, double tipZMm)
{
    if (radiusMm >= rMm) {
        return 0.0;                    // вне инструмента верх не тронут
    }
    const double cut = tipZMm + rMm - std::sqrt(rMm * rMm - radiusMm * radiusMm);
    return std::min(0.0, cut);
}

// Конус с образующей под 45 градусов (включённый угол 90): рез на высоте tipZ + r.
double coneHeightMm(double radiusMm, double tipZMm)
{
    return std::min(0.0, tipZMm + radiusMm);
}

struct CaseStat {
    const char* name = "";
    int checked = 0;
    double maxDevMm = 0.0;
    double tolMm = 0.0;
    bool ok = false;
};

std::vector<CaseStat> g_cases;

void reportCase(const char* name, int checked, double maxDevMm, double tolMm, bool extraOk = true)
{
    CaseStat stat;
    stat.name = name;
    stat.checked = checked;
    stat.maxDevMm = maxDevMm;
    stat.tolMm = tolMm;
    stat.ok = extraOk && maxDevMm <= tolMm;
    g_cases.push_back(stat);
    std::printf("case %-12s checked=%6d max_dev_mm=%.3e tol_mm=%.1e %s\n",
                name, checked, maxDevMm, tolMm, stat.ok ? "OK" : "FAIL");
}

std::string readText(const std::string& utf8Path)
{
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!fh) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());
}

bool fileExists(const std::string& utf8Path)
{
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    return bool(fh);
}

// Ход программы с номером инструмента этапа.
struct Move {
    std::array<double, 3> aMm{0.0, 0.0, 0.0};
    std::array<double, 3> bMm{0.0, 0.0, 0.0};
    int cutter = 0;
};

struct ProgramTiming {
    int moves = 0;
    long long positions = 0;
    double lengthMm = 0.0;
    double resetMs = 0.0;
    double sweepMs = 0.0;
    int countX = 0;
    int countY = 0;
};

ProgramTiming runProgram(const std::vector<Move>& moves,
                         const std::vector<gv::StockCutter>& cutters,
                         const gv::StockBounds& bounds,
                         double cellMm)
{
    ProgramTiming out;
    const double step = cellMm * 0.5;
    for (const Move& move : moves) {
        const double dx = move.bMm[0] - move.aMm[0];
        const double dy = move.bMm[1] - move.aMm[1];
        const double dz = move.bMm[2] - move.aMm[2];
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        out.lengthMm += length;
        // Число положений считает пробник по своему правилу, а не ядро: это
        // проверка цены, а не повтор внутренней арифметики поля. Правило то же:
        // отрезок короче шага даёт два положения (начало и конец).
        const long long steps = length > step ? (long long)std::ceil(length / step) : 1;
        out.positions += steps + 1;
    }
    out.moves = int(moves.size());

    gv::StockField field;
    Clock::time_point t0 = Clock::now();
    field.reset(bounds, cellMm);
    out.resetMs = msSince(t0);
    out.countX = field.countX();
    out.countY = field.countY();

    t0 = Clock::now();
    for (const Move& move : moves) {
        const gv::StockCutter& cutter = cutters[std::size_t(move.cutter)];
        if (cutter.maxRadiusMm <= 0.0) {
            continue;
        }
        field.sweep(cutter, move.aMm, move.bMm);
    }
    out.sweepMs = msSince(t0);
    g_sink += field.depthMm().size();
    return out;
}

// Тот же проход точным съёмом: цена поля после этапа 2 (PLAN-STOCK.md, 3.2).
// Позиций нет - каждый узел капсулы трогается один раз на ход, поэтому число
// обращений равно числу ходов, а не сумме положений шага.
ProgramTiming runProgramExact(const std::vector<Move>& moves,
                              const std::vector<gv::StockCutter>& cutters,
                              const gv::StockBounds& bounds,
                              double cellMm)
{
    ProgramTiming out;
    for (const Move& move : moves) {
        const double dx = move.bMm[0] - move.aMm[0];
        const double dy = move.bMm[1] - move.aMm[1];
        const double dz = move.bMm[2] - move.aMm[2];
        out.lengthMm += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    out.moves = int(moves.size());
    out.positions = moves.size();          // одно обращение к ходу, не к положению

    gv::StockField field;
    Clock::time_point t0 = Clock::now();
    field.reset(bounds, cellMm);
    out.resetMs = msSince(t0);
    out.countX = field.countX();
    out.countY = field.countY();

    t0 = Clock::now();
    for (const Move& move : moves) {
        const gv::StockCutter& cutter = cutters[std::size_t(move.cutter)];
        if (cutter.maxRadiusMm <= 0.0) {
            continue;
        }
        field.sweepExact(cutter, move.aMm, move.bMm);
    }
    out.sweepMs = msSince(t0);
    g_sink += field.depthMm().size();
    return out;
}

// Поле и рисование гладкого вида: зеркало верхнего слоя stocksurface.vert
// (steppedDepth). Правило обязано совпадать с шейдером: билинейно по четырём
// узлам клетки, а у настоящего уступа - уровень своей стороны по знаку точного
// расстояния до границы реза. Разбор правила и его чисел - в шейдере; здесь те
// же числа: STEP_CELLS 0.5 (порог размаха), площадки - 0.25 перепада на сход с
// уровнем и 0.75 на расхождение между площадками, cutMm - порог съёма.
struct DrawnField {
    const gv::StockBounds* bounds = nullptr;
    double cellMm = 0.5;
    int countX = 0;
    int countY = 0;
    const std::vector<float>* depth = nullptr;
    const std::vector<float>* signedMm = nullptr;   // знаковое расстояние до границы
    double cutMm = 0.05;

    // Узел поля, ix и iy прижаты к сетке - как прижимает шейдер выборку текселя.
    double node(int ix, int iy) const
    {
        ix = std::max(0, std::min(countX - 1, ix));
        iy = std::max(0, std::min(countY - 1, iy));
        return double((*depth)[std::size_t(iy) * std::size_t(countX) + std::size_t(ix)]);
    }

    // Координата точки в узлах поля: узел ix стоит в (ix + 0.5) / count.
    double nodeCoord(double uv, int count) const
    {
        return uv * double(count) - 0.5;
    }

    // Билинейная реконструкция по четырём узлам клетки, мм - прежнее правило.
    double bilinear(double u, double v) const
    {
        const double p = nodeCoord(u, countX);
        const double q = nodeCoord(v, countY);
        const double fx = p - std::floor(p);
        const double fy = q - std::floor(q);
        const int ix = int(std::floor(p));
        const int iy = int(std::floor(q));
        const double a = node(ix, iy) * (1.0 - fx) + node(ix + 1, iy) * fx;
        const double b = node(ix, iy + 1) * (1.0 - fx) + node(ix + 1, iy + 1) * fx;
        return a * (1.0 - fy) + b * fy;
    }

    // Знаковое расстояние до границы билинейно по её узлам: нулевой уровень
    // между узлами линеен, поэтому сторона точки берётся тем же правилом, что
    // цвет и линия во фрагменте (cutSample).
    double signedAt(double u, double v) const
    {
        if (signedMm == nullptr) {
            return 0.0;
        }
        const int n = countX;
        const auto at = [&](int ix, int iy) {
            ix = std::max(0, std::min(countX - 1, ix));
            iy = std::max(0, std::min(countY - 1, iy));
            return double((*signedMm)[std::size_t(iy) * std::size_t(n) + std::size_t(ix)]);
        };
        const double p = nodeCoord(u, countX);
        const double q = nodeCoord(v, countY);
        const double fx = p - std::floor(p);
        const double fy = q - std::floor(q);
        const int ix = int(std::floor(p));
        const int iy = int(std::floor(q));
        const double a = at(ix, iy) * (1.0 - fx) + at(ix + 1, iy) * fx;
        const double b = at(ix, iy + 1) * (1.0 - fx) + at(ix + 1, iy + 1) * fx;
        return a * (1.0 - fy) + b * fy;
    }

    // Глубина нарисованного верха в точке гладкого вида, мм: зеркало
    // steppedDepth (stocksurface.vert).
    double step(double u, double v) const
    {
        const double p = nodeCoord(u, countX);
        const double q = nodeCoord(v, countY);
        const double fx = p - std::floor(p);
        const double fy = q - std::floor(q);
        const int ix = int(std::floor(p));
        const int iy = int(std::floor(q));
        const double d00 = node(ix, iy);
        const double d10 = node(ix + 1, iy);
        const double d01 = node(ix, iy + 1);
        const double d11 = node(ix + 1, iy + 1);
        const double depth = bilinear(u, v);

        const double shallow = std::min(std::min(d00, d10), std::min(d01, d11));
        const double deep = std::max(std::max(d00, d10), std::max(d01, d11));
        const double rise = deep - shallow;
        if (rise <= cellMm * 0.5) {
            return depth;                        // размаха на уступ нет
        }
        // Главная ось уступа и площадки за клеткой по обе её стороны.
        const bool alongX = std::fabs(d10 - d00) + std::fabs(d11 - d01)
                >= std::fabs(d01 - d00) + std::fabs(d11 - d10);
        const int ax = alongX ? 1 : 0;
        const int ay = alongX ? 0 : 1;
        const int sx = alongX ? 0 : 1;
        const int sy = alongX ? 1 : 0;
        const double aheadA = node(ix + 2 * ax, iy + 2 * ay);
        const double aheadB = node(ix + 2 * ax + sx, iy + 2 * ay + sy);
        const double behindA = node(ix - ax, iy - ay);
        const double behindB = node(ix - ax + sx, iy - ay + sy);
        const double tol = 0.25 * rise;
        const auto atLevel = [&](double value) {
            return std::min(std::fabs(value - shallow), std::fabs(value - deep)) <= tol;
        };
        bool kink = atLevel(aheadA) && atLevel(aheadB)
                && atLevel(behindA) && atLevel(behindB);
        kink = kink && std::fabs(0.5 * (aheadA + aheadB) - 0.5 * (behindA + behindB))
                               >= 0.75 * rise;
        if (!kink) {
            return depth;                        // склон или канавка в клетку
        }
        const double mid = 0.5 * (shallow + deep);
        const bool split = ((d00 > cutMm) == (d00 > mid)) && ((d10 > cutMm) == (d10 > mid))
                && ((d01 > cutMm) == (d01 > mid)) && ((d11 > cutMm) == (d11 > mid));
        if (!split) {
            return depth > mid ? deep : shallow;
        }
        return signedAt(u, v) < 0.0 ? deep : shallow;
    }
};

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args = probeArgs(argc, argv);
    int failures = 0;

    // ---- аналитические случаи ----------------------------------------------
    // 1. Плоская концевая D6 по плоскому верху: рез -1 мм везде, где прошёл
    // инструмент. Проход 4 мм по X, узел 0.5, шаг 0.25.
    {
        const double cell = 0.5;
        const double r = 3.0;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        field.sweep(cutter, {-2.0, 0.0, tipZ}, {2.0, 0.0, tipZ});

        const double halfStep = cell * 0.25;   // шаг = cell/2, половина шага
        int checked = 0;
        double maxDev = 0.0;
        bool deeper = false;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double x = field.nodeXMm(ix);
                const double y = field.nodeYMm(iy);
                const double d = segmentDistanceMm(x, y, -2.0, 0.0, 2.0, 0.0);
                const double analytic = (d <= r) ? tipZ : 0.0;
                const double height = field.heightMm(ix, iy);
                if (height < analytic - 1e-6) {
                    deeper = true;             // поле ниже истинного съёма
                }
                // Вне полосы шаг/2 у стенки следа значение обязано быть точным.
                if (d <= r - halfStep || d >= r + halfStep) {
                    maxDev = std::max(maxDev, std::fabs(height - analytic));
                    ++checked;
                }
            }
        }
        reportCase("flat_sweep", checked, maxDev, 1e-5, !deeper);
    }

    // 2. Шаровая D6 по плоскому верху: долина радиуса 3 мм, профиль - истинная
    // сфера. Профиль инструмента - 12 отрезков (core/tooling.cpp), поэтому
    // допуск = сагитта хорды 7.5 градуса, усиленная на 1.5 у стенки, где
    // уклон сферы наибольший в срезанной зоне (срез не глубже 1 мм -> не
    // круче 48 градусов, 1/cos = 1.5).
    {
        const double cell = 0.5;
        const double r = 3.0;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"BALL", 6.0}));
        field.plunge(cutter, 0.0, 0.0, tipZ);

        const double angularStep = (PI / 2.0) / 12.0;
        const double sagitta = r * (1.0 - std::cos(angularStep / 2.0));
        const double tol = sagitta * 1.5 + 1e-5;
        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double s = std::hypot(field.nodeXMm(ix), field.nodeYMm(iy));
                maxDev = std::max(maxDev, std::fabs(field.heightMm(ix, iy) - ballHeightMm(s, r, tipZ)));
                ++checked;
            }
        }
        std::printf("note ball_sagitta_mm=%.6f\n", sagitta);
        reportCase("ball_plunge", checked, maxDev, tol);
    }

    // 3. Конус (V-бита, включённый угол 90) по плоскому верху: рез tipZ + r,
    // долина с прямыми стенками. Профиль CHAMFER с TAPER=45 даёт ровно это.
    {
        const double cell = 0.5;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"CHAMFER", 6.0, 45.0}));
        field.plunge(cutter, 0.0, 0.0, tipZ);

        int checked = 0;
        double maxDev = 0.0;
        bool deeper = false;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double s = std::hypot(field.nodeXMm(ix), field.nodeYMm(iy));
                const double analytic = coneHeightMm(s, tipZ);
                const double height = field.heightMm(ix, iy);
                if (height < analytic - 1e-6) {
                    deeper = true;
                }
                maxDev = std::max(maxDev, std::fabs(height - analytic));
                ++checked;
            }
        }
        reportCase("cone_plunge", checked, maxDev, 1e-5, !deeper);
    }

    // 4. Рез выше заготовки: острие на +2 мм, шар D6. Поле не меняется ни в
    // одном узле.
    {
        const double cell = 0.5;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"BALL", 6.0}));
        field.plunge(cutter, 0.3, 0.2, 2.0);

        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                maxDev = std::max(maxDev, std::fabs(field.heightMm(ix, iy)));
                ++checked;
            }
        }
        reportCase("no_reach", checked, maxDev, 1e-9);
    }

    // 5. Колонка, которую ловит только шаг в половину узла. Узел 1 мм, фреза D1
    // (r = 0.5), проход по X на y = 0.3 от x = 0.5 до 3.5. Колонка (1, 0)
    // отстоит от линии на 0.3 < r и попадает ровно на положение шага 0.5 мм;
    // при шаге в целый узел положения 0.5, 1.5, 2.5, 3.5 дают 0.583 > r, и
    // колонка осталась бы на верху.
    {
        const double cell = 1.0;
        const double r = 0.5;
        const double tipZ = -0.2;
        gv::StockBounds bounds;
        bounds.xMinMm = -4.0;
        bounds.xMaxMm = 8.0;
        bounds.yMinMm = -4.0;
        bounds.yMaxMm = 4.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 1.0}));
        field.sweep(cutter, {0.5, 0.3, tipZ}, {3.5, 0.3, tipZ});

        const int ix = int((1.0 - bounds.xMinMm) / cell + 0.5);
        const int iy = int((0.0 - bounds.yMinMm) / cell + 0.5);
        const int iyOutside = int((-1.0 - bounds.yMinMm) / cell + 0.5);
        const double hitHeight = field.heightMm(ix, iy);
        const double outsideHeight = field.heightMm(ix, iyOutside);

        double fullCellDistance = 1e9;
        const double fullStep = cell;              // шаг, который дал бы узел
        for (int k = 0; k <= 3; ++k) {
            const double sx = 0.5 + fullStep * double(k);
            fullCellDistance = std::min(fullCellDistance, std::hypot(1.0 - sx, 0.0 - 0.3));
        }
        const bool fullCellWouldMiss = fullCellDistance > r;
        std::printf("note near_miss node=(1,0) dist_mm=0.3 full_cell_dist_mm=%.4f "
                    "tool_r_mm=%.1f full_cell_would_miss=%d\n",
                    fullCellDistance, r, fullCellWouldMiss ? 1 : 0);

        double maxDev = std::fabs(hitHeight - tipZ);
        maxDev = std::max(maxDev, std::fabs(outsideHeight));
        const bool outsideOk = std::fabs(outsideHeight) < 1e-6;
        reportCase("near_miss_step", 2, maxDev, 1e-5, fullCellWouldMiss && outsideOk);
    }

    // 5б. Точный съём (PLAN-STOCK.md, 3.2): огибающая хода по капсуле, без шага
    // cell / 2. Случай плоской фрезы: рез постоянен по всей площади капсулы,
    // включая полосу шириной до cell / 4 у стенки следа, которую выборка по
    // положениям вынужденно исключала (случай flat_sweep выше). Сверка - со всей
    // площадью сразу, без исключений, и с прежней выборкой: она обязана оставить
    // нетронутые узлы внутри радиуса - это и есть её цена.
    {
        const double cell = 0.5;
        const double r = 0.5;
        const double tipZ = -0.2;
        gv::StockBounds bounds;
        bounds.xMinMm = -3.0;
        bounds.xMaxMm = 7.0;
        bounds.yMinMm = -3.0;
        bounds.yMaxMm = 7.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 0.0;
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 1.0}));

        gv::StockField exact;
        exact.reset(bounds, cell);
        exact.sweepExact(cutter, {-1.0, 0.6, tipZ}, {5.0, 4.6, tipZ});

        gv::StockField sampled;
        sampled.reset(bounds, cell);
        sampled.sweep(cutter, {-1.0, 0.6, tipZ}, {5.0, 4.6, tipZ});

        int checked = 0;
        int stripNodes = 0;
        int deeper = 0;
        double maxDev = 0.0;
        double stripMaxMm = 0.0;
        for (int iy = 0; iy < exact.countY(); ++iy) {
            for (int ix = 0; ix < exact.countX(); ++ix) {
                const double x = exact.nodeXMm(ix);
                const double y = exact.nodeYMm(iy);
                const double d = segmentDistanceMm(x, y, -1.0, 0.6, 5.0, 4.6);
                const double analytic = (d <= r) ? tipZ : 0.0;
                const double height = exact.heightMm(ix, iy);
                if (height < analytic - 1e-6) {
                    ++deeper;
                }
                if (std::fabs(d - r) > 1e-9) {
                    maxDev = std::max(maxDev, std::fabs(height - analytic));
                    ++checked;
                }
                // Узел внутри радиуса, но не пойманный выборкой по положениям.
                if (d <= r - 1e-6 && sampled.heightMm(ix, iy) > analytic + 1e-6) {
                    ++stripNodes;
                    stripMaxMm = std::max(stripMaxMm, d);
                }
            }
        }
        std::printf("note flat_sweep_exact nodes=%dx%d strip_nodes=%d strip_edge_mm=%.4f "
                    "tool_r_mm=%.1f cell_mm=%.1f\n",
                    exact.countX(), exact.countY(), stripNodes, stripMaxMm, r, cell);
        reportCase("flat_sweep_exact", checked, maxDev, 1e-6, deeper == 0 && stripNodes > 0);
    }

    // 5в. Истинная сфера: аналитическая огибающая шара (stockProfileFromTool)
    // вместо 12 хорд таблицы. Рез D6 сверяется с точной формулой сферы, допуск -
    // машинный: сагитта хорд (случай ball_plunge, 6.4e-3 мм) уходит целиком.
    {
        const double cell = 0.5;
        const double r = 3.0;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"BALL", 6.0}));
        field.sweepExact(cutter, {0.0, 0.0, tipZ}, {0.0, 0.0, tipZ});

        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double s = std::hypot(field.nodeXMm(ix), field.nodeYMm(iy));
                maxDev = std::max(maxDev, std::fabs(field.heightMm(ix, iy)
                                                    - ballHeightMm(s, r, tipZ)));
                ++checked;
            }
        }
        reportCase("ball_plunge_exact", checked, maxDev, 1e-6);
    }

    // 5г. Конус точным съёмом: участок-отрезок огибающей, минимум берётся
    // замкнутой формой. Случай держит аналитический профиль фаски.
    {
        const double cell = 0.5;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"CHAMFER", 6.0, 45.0}));
        field.sweepExact(cutter, {0.0, 0.0, tipZ}, {0.0, 0.0, tipZ});

        int checked = 0;
        double maxDev = 0.0;
        bool deeper = false;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double s = std::hypot(field.nodeXMm(ix), field.nodeYMm(iy));
                const double analytic = coneHeightMm(s, tipZ);
                const double height = field.heightMm(ix, iy);
                if (height < analytic - 1e-6) {
                    deeper = true;
                }
                maxDev = std::max(maxDev, std::fabs(height - analytic));
                ++checked;
            }
        }
        reportCase("cone_plunge_exact", checked, maxDev, 1e-6, !deeper);
    }

    // 5д. Точный край вместо "поймали шагом": узел, стоящий от линии реза ближе
    // радиуса, снят независимо от узла сетки и шага; узел дальше радиуса не
    // тронут. Сверка - с квадратурой по t высокой точности: минимум
    // z(t) + h(r(t)) ищется перебором 2001 точек и уточняется золотым сечением,
    // то есть независимо от замкнутой формы ядра (PLAN-STOCK.md, 3.2).
    {
        const double r = 3.0;                       // шар D6: истинная сфера
        const std::array<double, 3> a{-2.0, 0.0, -1.0};
        const std::array<double, 3> b{2.0, 1.0, -2.0};   // наклонный ход
        gv::StockBounds bounds;
        bounds.xMinMm = -6.0;
        bounds.xMaxMm = 6.0;
        bounds.yMinMm = -6.0;
        bounds.yMaxMm = 6.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"BALL", 6.0}));
        gv::StockField field;
        field.reset(bounds, 0.5);
        field.sweepExact(cutter, a, b);

        // Квадратура по t: r(t) - расстояние до точки отрезка в XY, z(t) линейна.
        const auto quadrature = [&](double x, double y) {
            double best = std::numeric_limits<double>::infinity();
            const int samples = 2001;
            int bestIndex = 0;
            for (int i = 0; i <= samples; ++i) {
                const double t = double(i) / double(samples);
                const double px = a[0] + (b[0] - a[0]) * t;
                const double py = a[1] + (b[1] - a[1]) * t;
                const double pz = a[2] + (b[2] - a[2]) * t;
                const double s = std::hypot(x - px, y - py);
                if (s >= r) {
                    continue;                       // вне инструмента здесь не режет
                }
                const double h = pz + r - std::sqrt(r * r - s * s);
                if (h < best) {
                    best = h;
                    bestIndex = i;
                }
            }
            if (!std::isfinite(best)) {
                return bounds.zMaxMm;               // нетронутый верх
            }
            double lo = std::max(0.0, double(bestIndex - 1) / double(samples));
            double hi = std::min(1.0, double(bestIndex + 1) / double(samples));
            const double invPhi = 0.618033988749894848;
            double u = hi - (hi - lo) * invPhi;
            double v = lo + (hi - lo) * invPhi;
            const auto at = [&](double t) {
                const double px = a[0] + (b[0] - a[0]) * t;
                const double py = a[1] + (b[1] - a[1]) * t;
                const double pz = a[2] + (b[2] - a[2]) * t;
                const double s = std::hypot(x - px, y - py);
                return pz + r - std::sqrt(std::max(1e-18, r * r - s * s));
            };
            double fu = at(u);
            double fv = at(v);
            for (int i = 0; i < 200; ++i) {
                if (fu < fv) {
                    hi = v;
                    v = u;
                    fv = fu;
                    u = hi - (hi - lo) * invPhi;
                    fu = at(u);
                } else {
                    lo = u;
                    u = v;
                    fu = fv;
                    v = lo + (hi - lo) * invPhi;
                    fv = at(v);
                }
            }
            return std::min(bounds.zMaxMm, std::min(fu, fv));
        };

        int checked = 0;
        double maxDev = 0.0;
        double worstX = 0.0;
        double worstY = 0.0;
        double worstWant = 0.0;
        double worstGot = 0.0;
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double x = field.nodeXMm(ix);
                const double y = field.nodeYMm(iy);
                const double want = quadrature(x, y);
                const double got = field.heightMm(ix, iy);
                if (std::fabs(got - want) > maxDev) {
                    maxDev = std::fabs(got - want);
                    worstX = x;
                    worstY = y;
                    worstWant = want;
                    worstGot = got;
                }
                ++checked;
            }
        }
        std::printf("note inclined_ball worst=(%.2f,%.2f) got=%.6f want=%.6f\n",
                    worstX, worstY, worstGot, worstWant);
        reportCase("inclined_ball_quadrature", checked, maxDev, 1e-6);
    }

    // 6. Привязка поля к траектории. Короткая программа разбирается тем же
    // путём, что у вьювера (parseGcode + prepareOrdered), и проходит через
    // simulateStock - тот же проход по PathData, что зовёт фоновый рабочий
    // backend'а. Сверка - с аналитикой плоского прохода, как в случае 1;
    // дайджест поля обязан совпасть у двух прогонов (детерминизм).
    {
        const std::string program =
            "G21 G90\n"
            "G0 X-2 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X2 F100\n"
            "G0 Z5\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        const gv::ToolInfo tool = makeTool(ToolSpec{"FLAT", 6.0});
        const double cell = 0.5;
        const gv::StockRunResult first =
            gv::simulateStock(data, bounds, cell, tool);
        const gv::StockRunResult second =
            gv::simulateStock(data, bounds, cell, tool);

        const double r = 3.0;
        const double tipZ = -1.0;
        const double halfStep = cell * 0.25;
        int checked = 0;
        double maxDev = 0.0;
        bool deeper = false;
        for (int iy = 0; iy < first.field.countY(); ++iy) {
            for (int ix = 0; ix < first.field.countX(); ++ix) {
                const double x = first.field.nodeXMm(ix);
                const double y = first.field.nodeYMm(iy);
                const double d = segmentDistanceMm(x, y, -2.0, 0.0, 2.0, 0.0);
                const double analytic = (d <= r) ? tipZ : 0.0;
                const double height = first.field.heightMm(ix, iy);
                if (height < analytic - 1e-6) {
                    deeper = true;
                }
                if (d <= r - halfStep || d >= r + halfStep) {
                    maxDev = std::max(maxDev, std::fabs(height - analytic));
                    ++checked;
                }
            }
        }
        const std::uint64_t digest_a = fieldDigest(first.field.depthMm());
        const std::uint64_t digest_b = fieldDigest(second.field.depthMm());
        std::printf("note stock_bind moves=%d length_mm=%.1f digest=%016llx twice=%d\n",
                    first.moves, first.lengthMm,
                    static_cast<unsigned long long>(digest_a),
                    digest_a == digest_b ? 1 : 0);
        reportCase("stock_bind", checked, maxDev, 1e-5,
                   !deeper && digest_a == digest_b && first.moves > 0);
    }

    // 7. Рисование верха: меш поля плюс высота из поля. Случай повторяет
    // рисование целиком, кроме самой выборки на GPU: сетка строится по узлам
    // поля (stockSurfaceMesh), texcoord вершины переводится в узел (так же
    // читает поле nearest-выборка stocksurface.vert), высота берётся
    // вычитанием глубины из верха заготовки, а точка переводится в кадр сцены
    // тем же поворотом узла: машинные (x, y, z) -> (x, z, -y). Сверка - с
    // аналитикой плоского врезания, посчитанной здесь же: дно реза обязано
    // встать на высоту острия инструмента, а нетронутые узлы - на верх
    // заготовки. Верх заготовки взят не в нуле (z = 5): так проверка ловит и
    // подмену оси, и потерю слагаемого zMax.
    {
        const double cell = 0.5;
        const double r = 3.0;
        const double tipX = -1.5;
        const double tipY = -2.5;
        const double tipZ = -1.0;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 5.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        field.plunge(cutter, tipX, tipY, tipZ);

        const int nx = field.countX();
        const int ny = field.countY();
        const gv::StockSurfaceMesh mesh = gv::stockSurfaceMesh(bounds, cell, nx, ny);
        // Допуск: и глубина, и позиция вершины хранятся float32; на 100 мм шаг
        // float32 около 7.6e-6 мм, отсюда 1e-5 мм. Узел ровно на границе радиуса
        // (|d - r| < 1e-6 мм) в сверку не идёт: там разошлись бы последние биты
        // разного порядка сложения, а не съём.
        const double tol = 1e-5;
        bool geometryOk = mesh.countX == nx && mesh.countY == ny
                && int(mesh.vertices.size()) == 5 * nx * ny
                && int(mesh.indices.size()) == 6 * (nx - 1) * (ny - 1);
        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                const std::size_t v = std::size_t(iy) * std::size_t(nx) + std::size_t(ix);
                const float* vertex = &mesh.vertices[v * 5];
                // Texcoord - центр текселя этого узла: выборка nearest возвращает
                // ровно его.
                const int tx = int(double(vertex[3]) * double(nx));
                const int ty = int(double(vertex[4]) * double(ny));
                const double x = field.nodeXMm(ix);
                const double y = field.nodeYMm(iy);
                geometryOk = geometryOk && tx == ix && ty == iy
                        && std::fabs(double(vertex[0]) - std::min(x, bounds.xMaxMm)) < 1e-4
                        && std::fabs(double(vertex[1]) - std::min(y, bounds.yMaxMm)) < 1e-4
                        && double(vertex[2]) == bounds.zMaxMm;
                // Высота, как её считает вершинный шейдер: z = верх - глубина.
                const double drawnZ = double(vertex[2] - field.depthMm()[v]);
                const double d = std::hypot(x - tipX, y - tipY);
                const double analytic = (d <= r) ? tipZ : bounds.zMaxMm;
                if (std::fabs(d - r) > 1e-6) {
                    maxDev = std::max(maxDev, std::fabs(drawnZ - analytic));
                    ++checked;
                }
            }
        }
        // Узел под остриём: его дно в кадре сцены (x, z, -y) обязано совпасть с
        // остриём (X и Z сцены - с точностью до половины узла, ось сцены Y -
        // точно: острие режет на своей высоте).
        const int ixTip = int(std::lround((tipX - bounds.xMinMm) / cell));
        const int iyTip = int(std::lround((tipY - bounds.yMinMm) / cell));
        const std::size_t vTip = std::size_t(iyTip) * std::size_t(nx) + std::size_t(ixTip);
        const double nodeTipX = field.nodeXMm(ixTip);
        const double nodeTipY = field.nodeYMm(iyTip);
        const double drawnTipZ = double(mesh.vertices[vTip * 5 + 2] - field.depthMm()[vTip]);
        const double planeDev = std::fabs(drawnTipZ - tipZ);
        const double posDev = std::max(std::fabs(nodeTipX - tipX), std::fabs(nodeTipY - tipY));
        std::printf("note stocksurface node=(%.2f,%.2f) uv=(%.4f,%.4f) tip=(%.2f,%.2f) "
                    "scene=(%.2f,%.2f,%.2f) tip_scene=(%.2f,%.2f,%.2f) plane_dev_mm=%.2e "
                    "pos_dev_mm=%.3f node_mm=%.2f\n",
                    nodeTipX, nodeTipY, double(mesh.vertices[vTip * 5 + 3]),
                    double(mesh.vertices[vTip * 5 + 4]), tipX, tipY,
                    nodeTipX, drawnTipZ, -nodeTipY, tipX, tipZ, -tipY,
                    planeDev, posDev, cell);
        reportCase("stocksurface", checked, maxDev, tol,
                   geometryOk && planeDev <= tol && posDev <= cell * 0.5 + 1e-6);
    }

    // 7б. Гладкий вид: дроблённая сетка рисования и правило уступа. Поле при
    // этом не трогается - случай 7 сверяет его по узлам (и только его числа
    // держат манифест), а здесь проверяется то, что видно на экране: подузлы
    // стоят на сетке узла поля, texcoord - их доля клетки, высота подузла
    // билинейна (её считает вершинный шейдер, stocksurface.vert), а нормаль на
    // клетке со уступом берётся наклоном самой стенки, а не полусуммой с плоским
    // верхом. Проверка красна до правки: полусумма даёт на плоской клетке у
    // кромки полклетки наклона стенки - ту самую полосу, что видна глазом.
    {
        const double cell = 0.5;
        const int subdiv = 2;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 5.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{ "FLAT", 6.0 }));
        field.plunge(cutter, -1.5, -2.5, -1.0);

        const int nx = field.countX();
        const int ny = field.countY();
        const gv::StockSurfaceMesh mesh = gv::stockSurfaceMesh(bounds, cell, nx, ny, subdiv);
        const int drawnX = (nx - 1) * subdiv + 1;
        const int drawnY = (ny - 1) * subdiv + 1;
        bool geometryOk = mesh.countX == drawnX && mesh.countY == drawnY
                && int(mesh.vertices.size()) == 5 * drawnX * drawnY
                && int(mesh.indices.size()) == 6 * (drawnX - 1) * (drawnY - 1);
        int checked = 0;
        double maxDev = 0.0;
        for (int jy = 0; jy < drawnY; ++jy) {
            for (int jx = 0; jx < drawnX; ++jx) {
                const float* vertex = &mesh.vertices[(std::size_t(jy) * std::size_t(drawnX)
                                                      + std::size_t(jx)) * 5];
                // Подузел - доля клетки поля: он обязан лечь на сетку узла
                // (шаг клетка / subdiv), а texcoord - в ту же долю клетки.
                const double tx = double(jx) / double(subdiv);
                const double ty = double(jy) / double(subdiv);
                const double x = bounds.xMinMm + tx * cell;
                const double y = bounds.yMinMm + ty * cell;
                const double u = (tx + 0.5) / double(nx);
                const double v = (ty + 0.5) / double(ny);
                geometryOk = geometryOk
                        && std::fabs(double(vertex[0]) - std::min(x, bounds.xMaxMm)) < 1e-4
                        && std::fabs(double(vertex[1]) - std::min(y, bounds.yMaxMm)) < 1e-4
                        && std::fabs(double(vertex[3]) - u) < 1e-6
                        && std::fabs(double(vertex[4]) - v) < 1e-6;
                ++checked;
            }
        }

        // Ожидаемая высота нарисованной поверхности в подузле: билинейная по
        // узлам клетки - то же, что считает fieldSample в stocksurface.vert.
        const std::vector<float>& depth = field.depthMm();
        const auto nodeDepth = [&depth, nx, ny](int ix, int iy) {
            ix = std::max(0, std::min(nx - 1, ix));
            iy = std::max(0, std::min(ny - 1, iy));
            return double(depth[std::size_t(iy) * std::size_t(nx) + std::size_t(ix)]);
        };
        const auto bilinearDepth = [&nodeDepth, nx, ny](double u, double v) {
            const double p = u * double(nx) - 0.5;
            const double q = v * double(ny) - 0.5;
            const double bx = std::floor(p);
            const double by = std::floor(q);
            const double fx = p - bx;
            const double fy = q - by;
            const int ix = int(bx);
            const int iy = int(by);
            const double d0 = nodeDepth(ix, iy) * (1.0 - fx) + nodeDepth(ix + 1, iy) * fx;
            const double d1 = nodeDepth(ix, iy + 1) * (1.0 - fx) + nodeDepth(ix + 1, iy + 1) * fx;
            return d0 * (1.0 - fy) + d1 * fy;
        };
        // Подузел в середине клетки поля обязан дать полусумму её узлов по
        // дробной оси и узел - по целой: это и есть билинейная высота, которой
        // шейдер опускает вершину.
        int bilinearChecked = 0;
        double bilinearDev = 0.0;
        for (int iy = 0; iy + 1 < ny; ++iy) {
            for (int ix = 0; ix + 1 < nx; ++ix) {
                const double u = (double(ix) + 1.0) / double(nx);
                const double v = (double(iy) + 0.5) / double(ny);
                const double expect = 0.5 * (nodeDepth(ix, iy) + nodeDepth(ix + 1, iy));
                bilinearDev = std::max(bilinearDev, std::fabs(bilinearDepth(u, v) - expect));
                ++bilinearChecked;
                const double u2 = (double(ix) + 0.5) / double(nx);
                const double v2 = (double(iy) + 1.0) / double(ny);
                const double expect2 = 0.5 * (nodeDepth(ix, iy) + nodeDepth(ix, iy + 1));
                bilinearDev = std::max(bilinearDev, std::fabs(bilinearDepth(u2, v2) - expect2));
                ++bilinearChecked;
            }
        }

        // Наклон по X в подузле, как его считает ШЕЙДЕР: разности соседей на
        // клетку ловят уступ, на клетке со уступом наклон - собственный градиент
        // клетки, иначе - центральная разность через клетку (2 * cell - плечо;
        // список соседей и правило повторяет stocksurface.frag). Здесь повторена
        // только ось X и только те точки, где ответ различает правила.
        const auto shaderSlopeX = [&nodeDepth, nx, ny, cell](double u, double v) {
            const double stepMm = cell;                   // STEP_CELLS = 1
            const double du = 1.0 / double(nx);
            const auto sample = [&nodeDepth, nx, ny](double uu, double vv) {
                const double p = uu * double(nx) - 0.5;
                const double q = vv * double(ny) - 0.5;
                const double bx = std::floor(p);
                const double by = std::floor(q);
                const double fx = p - bx;
                const double fy = q - by;
                const int ix = int(bx);
                const int iy = int(by);
                const double a = nodeDepth(ix, iy) * (1.0 - fx) + nodeDepth(ix + 1, iy) * fx;
                const double b = nodeDepth(ix, iy + 1) * (1.0 - fx) + nodeDepth(ix + 1, iy + 1) * fx;
                return a * (1.0 - fy) + b * fy;
            };
            const double depth = sample(u, v);
            const double r = sample(u + du, v);
            const double l = sample(u - du, v);
            // Аналитический градиент клетки точки и правило axisSlope шейдера:
            // уступ (склон внутри клетки) берёт градиент своей клетки, ровный
            // участок - разность соседей на клетку (плечо 2 * cell).
            const double p = u * double(nx) - 0.5;
            const double q = v * double(ny) - 0.5;
            const double fx = p - std::floor(p);
            const double fy = q - std::floor(q);
            const int ix = int(std::floor(p));
            const int iy = int(std::floor(q));
            const double g = ((nodeDepth(ix + 1, iy) - nodeDepth(ix, iy)) * (1.0 - fy)
                            + (nodeDepth(ix + 1, iy + 1) - nodeDepth(ix, iy + 1)) * fy) / cell;
            const bool jr = std::fabs(r - depth) > stepMm;
            const bool jl = std::fabs(depth - l) > stepMm;
            if (jr || jl)
                return g;
            return (r - l) / (2.0 * cell);
        };

        // Клетка со уступом по X: справа на много клеток глубже, слева плоско.
        // Ряд по Y берётся тот, где стенка прямая (соседние узлы равны): тогда
        // ответ не зависит от доли внутри клетки по Y и сверка точная.
        int wallCells = 0;
        double ruleDev = 0.0;
        for (int iy = 0; iy + 1 < ny; ++iy) {
            for (int ix = 1; ix + 1 < nx; ++ix) {
                const double step = nodeDepth(ix + 1, iy) - nodeDepth(ix, iy);
                if (std::fabs(step) < 2.0 * cell)          // не уступ, а склон
                    continue;
                if (std::fabs(nodeDepth(ix, iy) - nodeDepth(ix - 1, iy)) > 1e-6)
                    continue;                              // слева не плоско
                if (nodeDepth(ix, iy) != nodeDepth(ix, iy + 1)
                        || nodeDepth(ix + 1, iy) != nodeDepth(ix + 1, iy + 1))
                    continue;                              // стенка не прямая по Y
                // Плоская клетка слева: наклон обязан быть нулевым - полосы у
                // кромки нет. Полусумма здесь давала бы полклетки наклона.
                const double flat = shaderSlopeX(double(ix) / double(nx),
                                                 (double(iy) + 0.5) / double(ny));
                // Клетка-стенка: наклон обязан быть наклоном самой стенки.
                const double wall = shaderSlopeX(double(ix + 1) / double(nx),
                                                 (double(iy) + 0.5) / double(ny));
                ruleDev = std::max(ruleDev, std::fabs(flat));
                ruleDev = std::max(ruleDev, std::fabs(wall - step / cell));
                ++wallCells;
            }
        }
        std::printf("note stocksurface_fine subdiv=%d drawn=%dx%d cells=%d bilinear=%d "
                    "wall_cells=%d rule_dev_mm/mm=%.3e\n",
                    subdiv, drawnX, drawnY, checked, bilinearChecked, wallCells, ruleDev);
        reportCase("stocksurface_fine", checked + bilinearChecked + wallCells,
                   std::max(maxDev, std::max(bilinearDev, ruleDev)), 1e-6,
                   geometryOk && wallCells > 0);
    }

    // 7г. Наклон, который считает ФРАГМЕНТ (PLAN-STOCK.md, этап 1.1): пункт и
    // уступ живут в stocksurface.frag, там же снят компромисс NORMAL_SPAN -
    // разность идёт на клетку (плечо 2 * cell), а не через 2 * NORMAL_SPAN (4
    // клетки, 2 мм при узле 0.5 мм). Мера - подъём в две клетки: поле 0 -> 0.5 ->
    // 1.0 мм глубины, свой наклон 1.0 мм/мм. Прежняя разность через 4 клетки
    // брала только концы подъёма и давала 0.5 - половину; разность на клетку
    // берёт ровно 0 и 1.0, то есть весь подъём. Случай красный до правки.
    {
        const double cell = 0.5;
        gv::StockBounds bounds;
        bounds.xMinMm = 0.0;
        bounds.xMaxMm = 10.0;
        bounds.yMinMm = 0.0;
        bounds.yMaxMm = 10.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        for (int iy = 0; iy < field.countY(); ++iy) {
            for (int ix = 0; ix < field.countX(); ++ix) {
                const double depth = (ix <= 10) ? 0.0 : (ix == 11 ? 0.5 : 1.0);
                field.setHeightMm(ix, iy, bounds.zMaxMm - depth);
            }
        }

        const int nx = field.countX();
        const int ny = field.countY();
        const std::vector<float>& depth = field.depthMm();
        const auto nodeDepth = [&depth, nx, ny](int ix, int iy) {
            ix = std::max(0, std::min(nx - 1, ix));
            iy = std::max(0, std::min(ny - 1, iy));
            return double(depth[std::size_t(iy) * std::size_t(nx) + std::size_t(ix)]);
        };
        const auto sample = [&nodeDepth, nx, ny](double u, double v) {
            const double p = u * double(nx) - 0.5;
            const double q = v * double(ny) - 0.5;
            const double bx = std::floor(p);
            const double by = std::floor(q);
            const double fx = p - bx;
            const double fy = q - by;
            const int ix = int(bx);
            const int iy = int(by);
            const double a = nodeDepth(ix, iy) * (1.0 - fx) + nodeDepth(ix + 1, iy) * fx;
            const double b = nodeDepth(ix, iy + 1) * (1.0 - fx) + nodeDepth(ix + 1, iy + 1) * fx;
            return a * (1.0 - fy) + b * fy;
        };
        const auto shaderSlopeX = [&nodeDepth, nx, ny, cell](double u, double v) {
            const double stepMm = cell;                   // STEP_CELLS = 1
            const double du = 1.0 / double(nx);
            const auto at = [&nodeDepth, nx, ny](double uu, double vv) {
                const double p = uu * double(nx) - 0.5;
                const double q = vv * double(ny) - 0.5;
                const double bx = std::floor(p);
                const double by = std::floor(q);
                const double fx = p - bx;
                const double fy = q - by;
                const int ix = int(bx);
                const int iy = int(by);
                const double a = nodeDepth(ix, iy) * (1.0 - fx) + nodeDepth(ix + 1, iy) * fx;
                const double b = nodeDepth(ix, iy + 1) * (1.0 - fx) + nodeDepth(ix + 1, iy + 1) * fx;
                return a * (1.0 - fy) + b * fy;
            };
            const double depth0 = at(u, v);
            const double r = at(u + du, v);
            const double l = at(u - du, v);
            const double p = u * double(nx) - 0.5;
            const double q = v * double(ny) - 0.5;
            const double fx = p - std::floor(p);
            const double fy = q - std::floor(q);
            const int ix = int(std::floor(p));
            const int iy = int(std::floor(q));
            const double g = ((nodeDepth(ix + 1, iy) - nodeDepth(ix, iy)) * (1.0 - fy)
                            + (nodeDepth(ix + 1, iy + 1) - nodeDepth(ix, iy + 1)) * fy) / cell;
            const bool jr = std::fabs(r - depth0) > stepMm;
            const bool jl = std::fabs(depth0 - l) > stepMm;
            if (jr || jl)
                return g;
            return (r - l) / (2.0 * cell);
        };
        // Прежнее правило (плечо 4 клетки) - только для числа в примечании.
        const auto oldSpanSlopeX = [&sample, nx, cell](double u, double v) {
            const double du = 1.0 / double(nx);
            return (sample(u + 2.0 * du, v) - sample(u - 2.0 * du, v)) / (4.0 * cell);
        };

        // Середина подъёма: узел 11 (глубина 0.5 мм), середина ряда по Y.
        const double u = 11.5 / double(nx);
        const double v = 0.5;
        const double want = 1.0 / (2.0 * cell);           // 1.0 мм на 1.0 мм
        const double got = shaderSlopeX(u, v);
        const double oldGot = oldSpanSlopeX(u, v);
        // Точки внутри подъёма: узел 11 каждого ряда, кроме крайних.
        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 1; iy + 1 < ny; ++iy) {
            const double vv = (double(iy) + 0.5) / double(ny);
            maxDev = std::max(maxDev, std::fabs(shaderSlopeX(u, vv) - want));
            ++checked;
        }
        std::printf("note stocksurface_shade_frag u=%.4f slope=%.4f want=%.4f "
                    "old_span_slope=%.4f cell_mm=%.2f\n",
                    u, got, want, oldGot, cell);
        reportCase("stocksurface_shade_frag", checked, std::max(maxDev, 0.0), 1e-12,
                   checked > 0 && std::fabs(got - want) <= 1e-12
                       && std::fabs(oldGot - want) > 0.4);
    }

    // 7д. Плоский карман концевой фрезой: дно плоское, стенка отвесная. Случай
    // держит то, что видит оператор на кадре, - наклон, которым ШЕЙДЕР красит
    // точку (stocksurface.frag: fieldSample, порог уступа STEP_CELLS, нормаль), -
    // против поля съёма. Строится прямой ход концевой D10 на глубину d при узле
    // 0.5 мм, стенка капсулы ложится ровно между двумя узлами: поле - чистая
    // ступень 0 -> d, какую и оставляет плоская фреза. Мера - наклон по Y в трёх
    // местах: на плоском дне (обязан быть нулём - дно параллельно верху
    // заготовки), в клетке-стенке (обязан быть d/клетка - стенка отвесна
    // настолько, насколько это даёт узел) и на нетронутом верху (ноль). Плюс
    // сверка рисования с полем: билинейная реконструкция в узле обязана вернуть
    // сам узел - ею шейдер опускает вершину.
    //
    // Случай красный до правки на мелком окне (d между 0.5 и 1.0 мм): порог
    // уступа стоял в целую клетку и сравнивался с выборкой соседа НА КЛЕТКУ, а
    // та стоит на середине соседней клетки и видит ПОЛОВИНУ перепада уступа.
    // При d меньше двух клеток порог не срабатывал: дно у стенки получало
    // наклон 0.4 мм/мм (22 градуса) вместо нуля, а стенка - половину своей
    // крутизны (0.8 вместо 1.6). На кадре это и читается конусом: дно светлеет
    // к лампе, стенка выходит наклонной.
    //
    // Совпадение с шейдером. Зеркало здесь повторяет stocksurface.frag, включая
    // порог STEP_CELLS = 0.5; случаи выше (stocksurface_fine,
    // stocksurface_shade_frag) держат старые числа и к порогу нечувствительны:
    // там перепад уступа не меньше двух клеток, и оба порога отвечают одинаково.
    {
        const double cell = 0.5;
        const double r = 5.0;                        // концевая D10
        const double depths[2] = { 4.0, 0.8 };       // глубокий и мелкий уступ
        for (double depth : depths) {
            gv::StockBounds bounds;
            bounds.xMinMm = -10.0;
            bounds.xMaxMm = 10.0;
            bounds.yMinMm = -5.0;
            bounds.yMaxMm = 5.0;
            bounds.zMinMm = -5.0;
            bounds.zMaxMm = 0.0;
            gv::StockField field;
            field.reset(bounds, cell);
            const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{ "FLAT", 10.0 }));
            // Ход вдоль X на y = 0.75: стенка капсулы (радиус 5) ложится на
            // середину между узлами -4.5 и -4.0 - поле выходит ступенью.
            field.sweepExact(cutter, { -8.0, 0.75, -depth }, { 8.0, 0.75, -depth });

            const int nx = field.countX();
            const int ny = field.countY();
            const std::vector<float>& fieldDepth = field.depthMm();
            const auto nodeDepth = [&fieldDepth, nx, ny](int ix, int iy) {
                ix = std::max(0, std::min(nx - 1, ix));
                iy = std::max(0, std::min(ny - 1, iy));
                return double(fieldDepth[std::size_t(iy) * std::size_t(nx) + std::size_t(ix)]);
            };
            // fieldSample из stocksurface.vert/frag: билинейно по четырём узлам
            // клетки точки, выборка прижата к центрам крайних текселей.
            const auto sample = [&nodeDepth, nx, ny](double u, double v) {
                const double p = u * double(nx) - 0.5;
                const double q = v * double(ny) - 0.5;
                const double bx = std::floor(p);
                const double by = std::floor(q);
                const double fx = p - bx;
                const double fy = q - by;
                const int ix = int(bx);
                const int iy = int(by);
                const double a = nodeDepth(ix, iy) * (1.0 - fx) + nodeDepth(ix + 1, iy) * fx;
                const double b = nodeDepth(ix, iy + 1) * (1.0 - fx) + nodeDepth(ix + 1, iy + 1) * fx;
                return a * (1.0 - fy) + b * fy;
            };
            // Наклон по Y, как его считает stocksurface.frag: соседи на клетку
            // ловят уступ, на клетке со уступом берётся аналитический градиент
            // самой клетки, иначе - центральная разность на две клетки.
            const double stepMm = cell * 0.5;        // STEP_CELLS = 0.5, как в шейдере
            const auto slopeY = [&nodeDepth, &sample, nx, ny, cell, stepMm](double u, double v) {
                const double dv = 1.0 / double(ny);
                const double here = sample(u, v);
                const double yr = sample(u, v + dv);
                const double yl = sample(u, v - dv);
                const double p = u * double(nx) - 0.5;
                const double q = v * double(ny) - 0.5;
                const double fx = p - std::floor(p);
                const double fy = q - std::floor(q);
                const int ix = int(std::floor(p));
                const int iy = int(std::floor(q));
                const double g = ((nodeDepth(ix, iy + 1) - nodeDepth(ix, iy)) * (1.0 - fx)
                                + (nodeDepth(ix + 1, iy + 1) - nodeDepth(ix + 1, iy)) * fx) / cell;
                if (std::fabs(yr - here) > stepMm || std::fabs(here - yl) > stepMm)
                    return g;
                return (yr - yl) / (2.0 * cell);
            };

            // Точки: середина клетки нетронутого верха (узлы 0 и 1), клетки-стенки
            // (узлы 1 и 2) и плоского дна у стенки (узлы 2 и 3); по X - середина
            // клетки у x = 0, где капсула однородна по длине.
            const double u = 20.0 / double(nx);
            const double topSlope = slopeY(u, 1.0 / double(ny));
            const double wallSlope = slopeY(u, 2.0 / double(ny));
            const double floorSlope = slopeY(u, 3.0 / double(ny));
            const double wallWant = depth / cell;
            // Нормаль стенки в машинных осях: (0, wall, 1) - доля z и есть
            // вертикальность: ноль - нормаль горизонтальна, стенка отвесна.
            const double wallNormalZ =
                cell / std::sqrt(cell * cell + depth * depth);

            double drawnDev = 0.0;
            int drawnChecked = 0;
            for (int iy = 0; iy < ny; ++iy) {
                for (int ix = 0; ix < nx; ++ix) {
                    const double uu = (double(ix) + 0.5) / double(nx);
                    const double vv = (double(iy) + 0.5) / double(ny);
                    drawnDev = std::max(drawnDev, std::fabs(sample(uu, vv) - nodeDepth(ix, iy)));
                    ++drawnChecked;
                }
            }
            const double slopeDev = std::max(std::fabs(floorSlope),
                                             std::max(std::fabs(topSlope),
                                                      std::fabs(wallSlope - wallWant)));
            std::printf("note flat_pocket_drawn depth_mm=%.2f cell_mm=%.2f floor_slope=%.3f "
                        "wall_slope=%.4f wall_want=%.4f top_slope=%.3f wall_normal_z=%.3f "
                        "wall_tilt_deg=%.1f wall_band_mm=%.2f drawn_dev_mm=%.2e nodes=%d\n",
                        depth, cell, floorSlope, wallSlope, wallWant, topSlope, wallNormalZ,
                        std::atan2(cell, depth) * 180.0 / PI, cell, drawnDev, drawnChecked);
            reportCase("flat_pocket_drawn", drawnChecked, slopeDev, 1e-6,
                       drawnDev <= 1e-5 && std::fabs(floorSlope) <= 1e-6
                           && std::fabs(topSlope) <= 1e-6
                           && std::fabs(wallSlope - wallWant) <= 1e-6);
        }
    }

    // 7е. Кромка ОСТАВЛЕННОГО материала: верх ступает, а не наклоняется. Случай
    // держит то, что видит оператор на кадре, - кромку острова внутри кармана, -
    // против материала: плоская фреза оставляет два ровных уровня (нетронутый верх
    // 0 и дно d) и отвесную кромку между ними, поэтому нарисованный верх обязан
    // стоять на СВОЁМ уровне, а наклонная полоса между уровнями - уйти. Прежняя
    // билинейная реконструкция клала полосу шириной в КЛЕТКУ поля (0.5 мм при узле
    // 0.5), и кромка читалась валом под atan(клетка / уступ): 32.0 градуса при
    // уступе 0.8 мм и 7.1 при 4.0 (числа - строки note ниже). Отсюда мера случая:
    // отклонение рисованного от БЛИЖАЙШЕГО уровня (у прежнего правила - полперепада,
    // 0.4 мм на уступе 0.8) и ширина полосы перехода с её наклоном от отвеса.
    // Поле и его байты не трогаются: правило живёт в рисовании (stocksurface.vert,
    // steppedDepth), случай повторяет его зеркалом (DrawnField).
    {
        const double cell = 0.5;
        const int subdiv = 2;                        // дробление гладкого вида
        const double cutMm = 0.05;
        const double r = 5.0;                        // концевая D10
        const double yPath = -7.7;                   // нижняя сторона хода
        const double yRim = yPath + r;               // кромка острова: -2.7
        const double depths[2] = { 4.0, 0.8 };
        for (double depth : depths) {
            gv::StockBounds bounds;
            bounds.xMinMm = -10.0;
            bounds.xMaxMm = 10.0;
            bounds.yMinMm = -10.0;
            bounds.yMaxMm = 10.0;
            bounds.zMinMm = -5.0;
            bounds.zMaxMm = 0.0;
            // Ход по прямоугольнику вокруг острова: след фрезы - полоса шириной
            // 2r вокруг хода, поэтому остров - прямоугольник, сжатый на r внутрь:
            // [-2.7, 2.7] x [-2.7, 3.3] с прямыми углами. Кромка y = -2.7 ложится
            // между подузлами -2.75 и -2.5 (узел -3.0 снят, узел -2.5 нетронут) -
            // так же, как у настоящего кармана с островом.
            const std::string program =
                "G21 G90\n"
                "G0 X0 Y-14 Z5\n"
                "G1 Z-" + std::to_string(depth) + " F100\n"
                "G1 X0 Y-7.7\n"
                "G1 X7.7\n"
                "G1 Y8.3\n"
                "G1 X-7.7\n"
                "G1 Y-7.7\n"
                "G1 X0\n"
                "G0 Z5\n"
                "M30\n";
            const gv::ParseResult parsed = gv::parseGcode(program);
            const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
            const gv::ToolInfo tool = makeTool(ToolSpec{ "FLAT", 10.0 });
            const gv::StockRunResult run = gv::simulateStock(data, bounds, cell, tool);
            const gv::StockField& field = run.field;
            const int nx = field.countX();
            const int ny = field.countY();
            const std::vector<gv::StockCutter> cutters = gv::stockCutterPlan(data, tool);
            const gv::StockBoundaryResult boundary = gv::stockCutBoundary(
                data, bounds, cell, cutters, field.depthMm(), nx, ny, cutMm);
            const std::vector<float> signedMm = gv::stockCutSignedDistance(
                bounds, cell, field.depthMm(), nx, ny, boundary.segments, cutMm, cell * 3.0);
            DrawnField drawn;
            drawn.bounds = &bounds;
            drawn.cellMm = cell;
            drawn.countX = nx;
            drawn.countY = ny;
            drawn.depth = &field.depthMm();
            drawn.signedMm = &signedMm;
            drawn.cutMm = cutMm;

            // Колонка через нижнюю кромку: x = 0 - это узел 20 (доля поля
            // 20.5 / nx), где ход однороден по длине и кромка параллельна X.
            // Подузлы рисованной сетки идут через клетка / subdiv (0.25 мм).
            const double u = 20.5 / double(nx);
            const int drawnY = (ny - 1) * subdiv + 1;
            // Уровни случая - по узлам самой колонки: узел 20 по оси x, узел 20 -
            // нетронутый верх острова (0), узел 14 - дно кармана под кромкой.
            const double shallowLevel = drawn.node(20, 20);
            const double deepLevel = drawn.node(20, 14);
            const double levels[2] = { shallowLevel, deepLevel };
            const auto nearestLevel = [&](double value) {
                return std::min(std::fabs(value - levels[0]), std::fabs(value - levels[1]));
            };
            const double windowMm = 1.0;             // окно вокруг кромки, мм
            const double tol = 1e-6;
            double levelNew = 0.0;
            double levelOld = 0.0;
            int rampedNew = 0;
            int rampedOld = 0;
            double plateauNew = 0.0;
            double plateauOld = 0.0;
            double lastDeepNew = -1e9;
            double firstShallowNew = 1e9;
            double lastDeepOld = -1e9;
            double firstShallowOld = 1e9;
            for (int k = 0; k + 1 < drawnY; ++k) {
                const double y0 = bounds.yMinMm + (double(k) / double(subdiv)) * cell;
                const double y1 = bounds.yMinMm + (double(k + 1) / double(subdiv)) * cell;
                const double v0 = (double(k) / double(subdiv) + 0.5) / double(ny);
                const double v1 = (double(k + 1) / double(subdiv) + 0.5) / double(ny);
                if (std::fabs(y0 - yRim) > windowMm)
                    continue;
                const double hereNew = drawn.step(u, v0);
                const double hereOld = drawn.bilinear(u, v0);
                levelNew = std::max(levelNew, nearestLevel(hereNew));
                levelOld = std::max(levelOld, nearestLevel(hereOld));
                if (nearestLevel(hereNew) > tol)
                    ++rampedNew;
                if (nearestLevel(hereOld) > tol)
                    ++rampedOld;
                // Полоса перехода: от последнего подузла на дне (ниже кромки) до
                // первого на нетронутом верху (выше неё).
                if (nearestLevel(hereNew) <= tol) {
                    if (std::fabs(hereNew - deepLevel) <= tol)
                        lastDeepNew = std::max(lastDeepNew, y0);
                    if (std::fabs(hereNew - shallowLevel) <= tol)
                        firstShallowNew = std::min(firstShallowNew, y0);
                }
                if (nearestLevel(hereOld) <= tol) {
                    if (std::fabs(hereOld - deepLevel) <= tol)
                        lastDeepOld = std::max(lastDeepOld, y0);
                    if (std::fabs(hereOld - shallowLevel) <= tol)
                        firstShallowOld = std::min(firstShallowOld, y0);
                }
            }
            const double bandNew = firstShallowNew - lastDeepNew;
            const double bandOld = firstShallowOld - lastDeepOld;
            // Ровные площадки: наклон пары подузлов ВНЕ полосы перехода - и на
            // дне, и на нетронутом верху. У склона он не ноль, у уступа - ноль.
            for (int k = 0; k + 1 < drawnY; ++k) {
                const double y0 = bounds.yMinMm + (double(k) / double(subdiv)) * cell;
                const double y1 = bounds.yMinMm + (double(k + 1) / double(subdiv)) * cell;
                const double v0 = (double(k) / double(subdiv) + 0.5) / double(ny);
                const double v1 = (double(k + 1) / double(subdiv) + 0.5) / double(ny);
                if (std::fabs(y0 - yRim) > windowMm)
                    continue;
                if (y0 <= firstShallowNew && y1 >= lastDeepNew)
                    continue;                       // пара через полосу перехода
                if (y0 <= firstShallowOld && y1 >= lastDeepOld)
                    continue;
                plateauNew = std::max(plateauNew,
                                      std::fabs(drawn.step(u, v1) - drawn.step(u, v0)) / (y1 - y0));
                plateauOld = std::max(plateauOld,
                                      std::fabs(drawn.bilinear(u, v1) - drawn.bilinear(u, v0)) / (y1 - y0));
            }
            const bool rimInsideNew = lastDeepNew < yRim && yRim < firstShallowNew;
            const bool rimInsideOld = lastDeepOld < yRim && yRim < firstShallowOld;
            const double tiltNew = std::atan2(bandNew, deepLevel - shallowLevel) * 180.0 / PI;
            const double tiltOld = std::atan2(bandOld, deepLevel - shallowLevel) * 180.0 / PI;
            // Слоистая поверхность (3.1/1.3): те же две половины, разрезанные по
            // ТОЧНОЙ границе реза (stockLayerSurface). Кромка обеих половин стоит
            // НА границе, поэтому полоса перехода между уровнями - это расстояние
            // между кромками, и оно ноль: наклонной грани у кромки нет вовсе.
            // Высоты читаются зеркалом шейдера по ЕЁ texcoord (DrawnField::step) -
            // им же проверяется сдвиг texcoord граничной вершины: без сдвига знак
            // знакового расстояния на самой границе - монетка, и низ кромки встал
            // бы на уровень верха.
            const gv::StockLayerSurface layers = gv::stockLayerSurface(
                    bounds, cell, field.depthMm(), nx, ny, cutMm, boundary.segments);
            const gv::StockWallMesh layerWall = gv::stockCutWallFromLayer(
                    bounds, cell, field.depthMm(), nx, ny, cutMm, boundary.segments);
            double layerTopMin = 1e9;         // кромка верха в колонке у x = 0
            double layerFloorMax = -1e9;      // кромка дна там же
            double layerLevelDev = 0.0;       // высота против уровня своей половины
            double layerSideDev = 0.0;        // зеркало шейдера против правила слоя
            double layerEdgeDev = 0.0;        // кромка против полилинии границы
            double layerWallDev = 0.0;        // низ стенки против кромки дна
            int layerShifted = 0;             // граничных вершин со сдвинутым texcoord
            int layerRamp = 0;                // треугольников с разными высотами
            for (std::size_t i = 0; i < layers.vertices.size() / 5; ++i) {
                const double x = double(layers.vertices[i * 5]);
                const double y = double(layers.vertices[i * 5 + 1]);
                const double u = double(layers.vertices[i * 5 + 3]);
                const double v = double(layers.vertices[i * 5 + 4]);
                if (std::fabs(x) > 0.05 || std::fabs(y - yRim) > windowMm) {
                    continue;
                }
                const bool floor = i >= std::size_t(layers.topVertexCount);
                const double drawnMm = drawn.step(u, v);
                const gv::StockLayer layer =
                        floor ? gv::StockLayer::Floor : gv::StockLayer::Top;
                layerSideDev = std::max(layerSideDev,
                                        std::fabs(drawnMm - gv::stockLayerDepthMm(
                                                bounds, cell, field.depthMm(), nx, ny,
                                                cutMm, x, y, layer)));
                layerLevelDev = std::max(layerLevelDev,
                                         std::fabs(drawnMm - (floor ? deepLevel : shallowLevel)));
                if (std::fabs(drawn.signedAt(u, v)) <= 0.03) {
                    layerEdgeDev = std::max(layerEdgeDev,
                                            contourDistanceMm(x, y, boundary.segments));
                }
                const double uOfPos = ((x - bounds.xMinMm) / cell + 0.5) / double(nx);
                const double vOfPos = ((y - bounds.yMinMm) / cell + 0.5) / double(ny);
                if (std::fabs(u - uOfPos) > 1e-12 || std::fabs(v - vOfPos) > 1e-12) {
                    ++layerShifted;
                }
                if (floor) {
                    layerFloorMax = std::max(layerFloorMax, y);
                } else {
                    layerTopMin = std::min(layerTopMin, y);
                }
            }
            // Треугольник в окне с РАЗНЫМИ высотами вершин - это и есть наклонная
            // грань перехода: у половины с уступом все её вершины стоят на её
            // уровне. Прежняя билинейная полоса давала по треугольнику на
            // подклетку, ступенчатая - один на клетку.
            for (std::size_t t = 0; t + 2 < layers.indices.size(); t += 3) {
                double heights[3] = { 0.0, 0.0, 0.0 };
                bool inWindow = true;
                for (int k = 0; k < 3; ++k) {
                    const std::size_t iv = std::size_t(layers.indices[t + std::size_t(k)]);
                    const double x = double(layers.vertices[iv * 5]);
                    const double y = double(layers.vertices[iv * 5 + 1]);
                    if (std::fabs(x) > 0.05 || std::fabs(y - yRim) > windowMm) {
                        inWindow = false;
                        break;
                    }
                    heights[k] = drawn.step(double(layers.vertices[iv * 5 + 3]),
                                            double(layers.vertices[iv * 5 + 4]));
                }
                if (!inWindow) {
                    continue;
                }
                const double lo = std::min(heights[0], std::min(heights[1], heights[2]));
                const double hi = std::max(heights[0], std::max(heights[1], heights[2]));
                if (hi - lo > tol) {
                    ++layerRamp;
                }
            }
            // Низ стенки в окне обязан лечь на кромку дна (уровень дна): у уступа
            // билинейная глубина в точке контура вдвое мельче кромки.
            for (std::size_t i = 0; i < layerWall.vertices.size() / 4; ++i) {
                const float* p = &layerWall.vertices[i * 4];
                if (std::fabs(double(p[2]) - double(bounds.zMaxMm)) < 1e-9) {
                    continue;                    // верх квада стенки
                }
                if (std::fabs(double(p[0])) > 0.05
                        || std::fabs(double(p[1]) - yRim) > windowMm) {
                    continue;
                }
                layerWallDev = std::max(layerWallDev, std::fabs(double(p[3]) - deepLevel));
            }
            const double layerBand = layerFloorMax - layerTopMin;
            const double layerTilt =
                    std::atan2(std::fabs(layerBand), deepLevel - shallowLevel) * 180.0 / PI;
            std::printf("note island_rim_drawn depth_mm=%.2f cell_mm=%.2f subdiv=%d rim_mm=%.2f "
                        "level_dev_new=%.2e level_dev_old=%.2e ramped_new=%d ramped_old=%d "
                        "band_new=%.2f band_old=%.2f tilt_new_deg=%.1f tilt_old_deg=%.1f "
                        "plateau_new=%.1e plateau_old=%.1e wall_segments=%d "
                        "layer_band=%.2f layer_tilt_deg=%.2f layer_level_mm=%.1e "
                        "layer_side_mm=%.1e layer_edge_mm=%.1e layer_wall_mm=%.1e "
                        "layer_ramp=%d layer_shifted=%d layer_floor_tri=%d\n",
                        depth, cell, subdiv, yRim, levelNew, levelOld, rampedNew, rampedOld,
                        bandNew, bandOld, tiltNew, tiltOld, plateauNew, plateauOld,
                        int(boundary.segments.size()), layerBand, layerTilt, layerLevelDev,
                        layerSideDev, layerEdgeDev, layerWallDev, layerRamp, layerShifted,
                        layers.floorTriangles());
            // Сверка: половины сходятся ровно на точной границе (полоса 0 и наклон
            // 0), высота кромки стоит на уровне СВОЕЙ половины, зеркало шейдера
            // совпадает с правилом слоя, texcoord граничной вершины назвал сторону
            // (layer_shifted), низ стенки лёг на кромку дна, а наклонных
            // треугольников у кромки не осталось. Прежние правила обязаны быть
            // красными здесь же - иначе случай слеп: у билинейного рисованного
            // подузла кромки полперепада (0.4 мм на уступе 0.8), у ступенчатого -
            // полоса 0.25 мм и 17.4 градуса.
            reportCase("island_rim_drawn", drawnY, layerBand, tol,
                       layerTilt <= tol && layerLevelDev <= tol && layerSideDev <= 1e-6
                           && layerEdgeDev <= 1e-6 && layerWallDev <= 1e-6
                           && layerRamp == 0 && layerShifted > 0
                           && layerTopMin <= yRim + tol && layerFloorMax >= yRim - tol
                           && rampedNew == 0 && rimInsideNew && plateauNew <= tol
                           && std::fabs(bandNew - cell / double(subdiv)) <= tol
                           && tiltNew > 1.0
                           && levelOld > 0.4 * (deepLevel - shallowLevel)
                           && rampedOld > 0 && tiltOld > tiltNew
                           && boundary.segments.size() > 0);
        }
    }

    // 7ж. Ровный СКЛОН ступенью не наклоняется: конус (V-бита 90 градусов) врезан
    // в плоский верх, стенка долины - 45 градусов, перепад на клетку равен самой
    // клетке (0.5 мм при узле 0.5) - размах тот же, что у уступа. Отличает склон
    // кинк: отсчёты через клетку у него равны, площадок по сторонам нет, поэтому
    // рисованный верх обязан остаться билинейным. Случай красный, если ступень
    // лезет на склон: полперепада (0.25 мм) высоты на подузле.
    {
        const double cell = 0.5;
        const double cutMm = 0.05;
        const std::string program =
            "G21 G90\n"
            "G0 X0 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G0 Z5\n"
            "M30\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        const gv::ToolInfo tool = makeTool(ToolSpec{ "CHAMFER", 6.0, 45.0 });
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 0.0;
        const gv::StockRunResult run = gv::simulateStock(data, bounds, cell, tool);
        const gv::StockField& field = run.field;
        const int nx = field.countX();
        const int ny = field.countY();
        const std::vector<gv::StockCutter> cutters = gv::stockCutterPlan(data, tool);
        const gv::StockBoundaryResult boundary = gv::stockCutBoundary(
            data, bounds, cell, cutters, field.depthMm(), nx, ny, cutMm);
        const std::vector<float> signedMm = gv::stockCutSignedDistance(
            bounds, cell, field.depthMm(), nx, ny, boundary.segments, cutMm, cell * 3.0);
        DrawnField drawn;
        drawn.bounds = &bounds;
        drawn.cellMm = cell;
        drawn.countX = nx;
        drawn.countY = ny;
        drawn.depth = &field.depthMm();
        drawn.signedMm = &signedMm;
        drawn.cutMm = cutMm;

        // Точки у самой границы реза по узлам и по подузлам: там, где склон крут
        // (перепад на клетку 0.5), ступень и лезла бы на него.
        int checked = 0;
        double maxDev = 0.0;
        double snapWouldBe = 0.0;
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                const double x = field.nodeXMm(ix);
                const double y = field.nodeYMm(iy);
                const double radius = std::hypot(x, y);
                if (radius < 0.5 || radius > 1.6) {
                    continue;               // кольцо вокруг кромки (глубина 0.25..0.9)
                }
                for (int k = 0; k < 2; ++k) {
                    for (int j = 0; j < 2; ++j) {
                        const double uu = (double(ix) + 0.25 + 0.5 * double(k)) / double(nx);
                        const double vv = (double(iy) + 0.25 + 0.5 * double(j)) / double(ny);
                        const double drawnMm = drawn.step(uu, vv);
                        const double bilinearMm = drawn.bilinear(uu, vv);
                        maxDev = std::max(maxDev, std::fabs(drawnMm - bilinearMm));
                        // Цена подмены: размах клетки - это и есть то, что ступень
                        // поставила бы на склон.
                        const double shallow = std::min({ drawn.node(ix, iy), drawn.node(ix + 1, iy),
                                                          drawn.node(ix, iy + 1), drawn.node(ix + 1, iy + 1) });
                        const double deep = std::max({ drawn.node(ix, iy), drawn.node(ix + 1, iy),
                                                       drawn.node(ix, iy + 1), drawn.node(ix + 1, iy + 1) });
                        snapWouldBe = std::max(snapWouldBe, 0.5 * (deep - shallow));
                        ++checked;
                    }
                }
            }
        }
        std::printf("note steep_slope_drawn cell_mm=%.2f checked=%d dev_mm=%.2e "
                    "snap_would_be_mm=%.3f boundary_segments=%d\n",
                    cell, checked, maxDev, snapWouldBe, int(boundary.segments.size()));
        reportCase("steep_slope_drawn", checked, maxDev, 1e-9,
                   checked > 0 && snapWouldBe > 0.05 && boundary.segments.size() > 0);
    }

    // 7з. Слоистая поверхность (PLAN-STOCK.md, 3.1; этап 1.3 плана): верх
    // нетронутого материала и дно реза, разрезанные по ТОЧНОЙ границе реза.
    // Случай держит четыре вещи разом.
    //   - Половины делят габарит без щели и нахлёста: сумма площадей их
    //     треугольников равна площади заготовки (полилиния границы у половин
    //     общая, поэтому равенство точное).
    //   - Кромка половин стоит НА полилинии границы (не дальше 1e-9 мм), а сама
    //     половина не заходит на чужую сторону дальше волоска (сдвиг texcoord
    //     0.02 мм плюс ошибка нулевого уровня 0.011 мм - случай boundary_drawn).
    //   - Высота вершины, прочитанная зеркалом шейдера по ЕЁ texcoord
    //     (DrawnField::step), равна правилу её половины (stockLayerDepthMm): без
    //     сдвига texcoord у граничной вершины знак расстояния - монетка, и низ
    //     кромки встал бы на уровень верха.
    //   - Низ стенки совпадает с кромкой дна: щели между стенкой и дном нет.
    // Плюс склон ступенью не портится: там, где правило не ступает, рисованное
    // совпадает с билинейным полем (то же держит steep_slope_drawn).
    {
        const double cell = 0.5;
        const double cutMm = 0.05;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{ "BALL", 6.0 }));
        field.sweepExact(cutter, { 0.0, 0.0, -1.0 }, { 0.0, 0.0, -1.0 });
        const int nx = field.countX();
        const int ny = field.countY();
        const std::vector<gv::StockContourSegment> contour =
                gv::stockCutContour(bounds, cell, field.depthMm(), nx, ny, cutMm);
        const std::vector<float> signedMm = gv::stockCutSignedDistance(
                bounds, cell, field.depthMm(), nx, ny, contour, cutMm, cell * 3.0);
        DrawnField drawn;
        drawn.bounds = &bounds;
        drawn.cellMm = cell;
        drawn.countX = nx;
        drawn.countY = ny;
        drawn.depth = &field.depthMm();
        drawn.signedMm = &signedMm;
        drawn.cutMm = cutMm;

        const gv::StockLayerSurface layers = gv::stockLayerSurface(
                bounds, cell, field.depthMm(), nx, ny, cutMm, contour);
        const gv::StockWallMesh wall = gv::stockCutWallFromLayer(
                bounds, cell, field.depthMm(), nx, ny, cutMm, contour);

        // Сумма площадей половин: треугольники - в машинных мм, площадь габарита
        // известна точно.
        double areaMm2 = 0.0;
        for (std::size_t t = 0; t + 2 < layers.indices.size(); t += 3) {
            const std::size_t i0 = std::size_t(layers.indices[t]) * 5;
            const std::size_t i1 = std::size_t(layers.indices[t + 1]) * 5;
            const std::size_t i2 = std::size_t(layers.indices[t + 2]) * 5;
            const double ax = double(layers.vertices[i0]);
            const double ay = double(layers.vertices[i0 + 1]);
            const double bx = double(layers.vertices[i1]);
            const double by = double(layers.vertices[i1 + 1]);
            const double cx = double(layers.vertices[i2]);
            const double cy = double(layers.vertices[i2 + 1]);
            areaMm2 += 0.5 * std::fabs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
        }
        const double wantAreaMm2 = (bounds.xMaxMm - bounds.xMinMm) * (bounds.yMaxMm - bounds.yMinMm);

        // (б, в) Кромка, сторона и высота каждой вершины по её же texcoord.
        const double hairMm = 0.05;              // десятая клетки: поле само гуляет
        double edgeWorst = 0.0;                  // кромка на полилинии, мм
        double sideWorst = 0.0;                  // заход на чужую сторону, мм: у
                                                 // ступающей вершины его держит
                                                 // зеркало шейдера (ruleWorst),
                                                 // здесь - только мера гуляния
                                                 // самого поля (см. hairMm)
        double ruleWorst = 0.0;                  // зеркало шейдера против правила слоя
        double fieldWorst = 0.0;                 // против билинейного поля (где не ступень)
        int boundaryVertices = 0;
        int shiftedVertices = 0;
        for (std::size_t i = 0; i < layers.vertices.size() / 5; ++i) {
            const double x = double(layers.vertices[i * 5]);
            const double y = double(layers.vertices[i * 5 + 1]);
            const double u = double(layers.vertices[i * 5 + 3]);
            const double v = double(layers.vertices[i * 5 + 4]);
            const bool floor = i >= std::size_t(layers.topVertexCount);
            const double signedAt = drawn.signedAt(u, v);
            const double drawnMm = drawn.step(u, v);
            const double bilinearMm = drawn.bilinear(u, v);
            const gv::StockLayer layer = floor ? gv::StockLayer::Floor : gv::StockLayer::Top;
            const double wantMm = gv::stockLayerDepthMm(bounds, cell, field.depthMm(), nx, ny,
                                                       cutMm, x, y, layer);
            ruleWorst = std::max(ruleWorst, std::fabs(drawnMm - wantMm));
            if (std::fabs(signedAt) <= 0.02) {
                ++boundaryVertices;
                edgeWorst = std::max(edgeWorst, contourDistanceMm(x, y, contour));
                const double uOfPos = ((x - bounds.xMinMm) / cell + 0.5) / double(nx);
                const double vOfPos = ((y - bounds.yMinMm) / cell + 0.5) / double(ny);
                if (std::fabs(u - uOfPos) > 1e-12 || std::fabs(v - vOfPos) > 1e-12) {
                    ++shiftedVertices;
                }
            }
            // Сторона важна там, где правило ступает: у склона уровень от знака не
            // зависит, и «заход» на чужую сторону там ничего не значит.
            if (std::fabs(wantMm - bilinearMm) > 1e-9) {
                sideWorst = std::max(sideWorst,
                                     floor ? std::max(signedAt, 0.0) : std::max(-signedAt, 0.0));
            } else {
                fieldWorst = std::max(fieldWorst, std::fabs(drawnMm - bilinearMm));
            }
        }

        // (г) Низ стенки на кромке дна: у каждой нижней вершины квада стенки
        // (глубина - четвёртое число) обязана найтись вершина кромки дна с той же
        // высотой - иначе между стенкой и дном щель во весь уступ.
        double slitWorst = 0.0;
        int wallBottoms = 0;
        for (std::size_t i = 0; i < wall.vertices.size() / 4; ++i) {
            const float* p = &wall.vertices[i * 4];
            if (std::fabs(double(p[2]) - double(bounds.zMaxMm)) < 1e-9) {
                continue;                        // верх квада стенки
            }
            const double wx = double(p[0]);
            const double wy = double(p[1]);
            const double wd = double(p[3]);
            ++wallBottoms;
            double best = std::numeric_limits<double>::max();
            for (std::size_t k = std::size_t(layers.topVertexCount);
                 k < layers.vertices.size() / 5; ++k) {
                const double fx = double(layers.vertices[k * 5]);
                const double fy = double(layers.vertices[k * 5 + 1]);
                if (std::fabs(fx - wx) > 1e-9 || std::fabs(fy - wy) > 1e-9) {
                    continue;
                }
                const double fu = double(layers.vertices[k * 5 + 3]);
                const double fv = double(layers.vertices[k * 5 + 4]);
                best = std::min(best, std::fabs(drawn.step(fu, fv) - wd));
            }
            slitWorst = std::max(slitWorst, best);
        }

        // (д) Пустая граница: нетронутая заготовка целиком уходит верху.
        const gv::StockLayerSurface plainLayers = gv::stockLayerSurface(
                bounds, cell, std::vector<float>(std::size_t(nx) * std::size_t(ny), 0.0f),
                nx, ny, cutMm, {});
        const int plainTriangles = (nx - 1) * (ny - 1) * 2;

        std::printf("note layer_surface cell_mm=%.2f cut_mm=%.2f vertices=%zu top_vertices=%d "
                    "top_tri=%d floor_tri=%d area_mm2=%.3f want_area_mm2=%.3f boundary_vertices=%d "
                    "shifted=%d edge_mm=%.2e side_mm=%.2e rule_mm=%.2e field_mm=%.2e "
                    "wall_bottoms=%d slit_mm=%.2e wall_segments=%zu plain_top_tri=%d plain_floor_tri=%d\n",
                    cell, cutMm, layers.vertices.size() / 5, layers.topVertexCount,
                    layers.topTriangles(), layers.floorTriangles(), areaMm2, wantAreaMm2,
                    boundaryVertices, shiftedVertices, edgeWorst, sideWorst, ruleWorst, fieldWorst,
                    wallBottoms, slitWorst, contour.size(), plainLayers.topTriangles(),
                    plainLayers.floorTriangles());
        reportCase("layer_surface", int(layers.vertices.size() / 5), slitWorst, 1e-6,
                   std::fabs(areaMm2 - wantAreaMm2) <= 1e-6 * wantAreaMm2
                       && layers.topTriangles() > 0 && layers.floorTriangles() > 0
                       && boundaryVertices > 0 && shiftedVertices > 0
                       && edgeWorst <= 1e-6 && sideWorst <= hairMm
                       && ruleWorst <= 1e-6 && fieldWorst <= 1e-6
                       && wallBottoms >= 4 && slitWorst <= 1e-6
                       && plainLayers.topTriangles() == plainTriangles
                       && plainLayers.floorTriangles() == 0);
    }

    // 7в. Точный контур реза и стенка по нему (PLAN-STOCK.md, 3.1). Marching
    // squares даёт границу под-ячейково: сверка endpoints со шаром, глубина
    // которого пересекает порог cutMm на радиусе s05. Стенка обязана: держать
    // верх на плоскости нетронутого верха, низ - на дне реза (то есть ровно на
    // cutMm ниже верха), и давать два треугольника на отрезок - стык верх,
    // стенка и дно идёт по одному контуру, без щели.
    {
        const double cell = 0.5;
        const double r = 3.0;
        const double tipZ = -1.0;
        const double cutMm = 0.05;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"BALL", 6.0}));
        field.sweepExact(cutter, {0.0, 0.0, tipZ}, {0.0, 0.0, tipZ});

        // Аналитическая граница среза шара по порогу: глубина
        // zMax - (tipZ + r - sqrt(r^2 - s^2)) равна cutMm на радиусе s05.
        const double chord = cutMm + r + tipZ - bounds.zMaxMm;
        const double s05 = std::sqrt(std::max(0.0, r * r - chord * chord));

        const std::vector<gv::StockContourSegment> contour = gv::stockCutContour(
                bounds, cell, field.depthMm(), field.countX(), field.countY(), cutMm);
        const gv::StockWallMesh wall = gv::stockCutWall(
                bounds, cell, field.depthMm(), field.countX(), field.countY(), cutMm);

        int checked = 0;
        double maxDev = 0.0;
        for (const gv::StockContourSegment& segment : contour) {
            const std::array<std::array<double, 2>, 2> ends{{
                {segment.x0Mm, segment.y0Mm}, {segment.x1Mm, segment.y1Mm}}};
            for (const std::array<double, 2>& p : ends) {
                maxDev = std::max(maxDev, std::fabs(std::hypot(p[0], p[1]) - s05));
                ++checked;
            }
        }
        bool wallOk = wall.triangleCount() == 2 * int(contour.size())
                && int(wall.vertices.size()) == 4 * 4 * int(contour.size())
                && !contour.empty();
        for (int v = 0; v + 3 < int(wall.vertices.size()); v += 4) {
            const double z = wall.vertices[v + 2];
            const double depth = wall.vertices[v + 3];
            const bool onTop = std::fabs(z - bounds.zMaxMm) < 1e-9;
            const bool onFloor = std::fabs(z - (bounds.zMaxMm - depth)) < 1e-9;
            wallOk = wallOk && (onTop || onFloor) && std::fabs(depth - cutMm) < 1e-6;
        }
        std::printf("note stocksurface_exact segments=%d s05_mm=%.4f wall_tris=%d "
                    "wall_verts=%d max_dev_mm=%.4f tol_mm=%.4f\n",
                    int(contour.size()), s05, wall.triangleCount(),
                    int(wall.vertices.size() / 4), maxDev, cell * 0.5);
        reportCase("stocksurface_exact", checked, maxDev, cell * 0.5,
                   wallOk && !contour.empty());
    }

    // 7г. Точная граница реза (PLAN-STOCK.md, 1.5): ОТДЕЛЬНОЕ представление,
    // независимое от узла поля. Мера - ошибка границы в МИЛЛИМЕТРАХ против
    // аналитики, на двух узлах поля (0.5 - умолчание med, 0.2 - план). Одной
    // мерой меряются три подхода:
    //   coarse - marching squares по полю (как было: граница = порог поля),
    //   fine   - поле мельче (0.1 и 0.0625 мм) и marching squares по нему,
    //   exact  - stockCutBoundary (переход ребра уточняется по точной глубине хода).
    // Стенка - плоская фреза по прямой (граница - капсула радиуса 3 вокруг
    // отрезка), шар - врезание шара (граница - круг радиуса s05 из аналитики
    // профиля, той же, что у случая stocksurface_exact). Ещё меряется НАРИСОВАННАЯ
    // граница: нулевой уровень знакового расстояния (stockCutSignedDistance) -
    // им фрагмент рисует цвет и линию (stocksurface.frag).
    {
        const double cutMm = 0.05;
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 0.0;
        const double tipZ = -1.0;
        const double r = 3.0;
        const double chord = cutMm + r + tipZ - bounds.zMaxMm;
        const double s05 = std::sqrt(std::max(0.0, r * r - chord * chord));

        const std::string wallProgram =
            "G21 G90\n"
            "G0 X-2 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X2 F100\n"
            "G0 Z5\n";
        const std::string ballProgram =
            "G21 G90\n"
            "G0 X0 Y0 Z5\n"
            "G1 Z-1 F100\n";
        const gv::ParseResult wallParsed = gv::parseGcode(wallProgram);
        const gv::PathData wallData = gv::prepareOrdered(wallParsed.ordered, wallParsed.stages);
        const gv::ParseResult ballParsed = gv::parseGcode(ballProgram);
        const gv::PathData ballData = gv::prepareOrdered(ballParsed.ordered, ballParsed.stages);
        const gv::ToolInfo wallTool = makeTool(ToolSpec{"FLAT", 6.0});
        const gv::ToolInfo ballTool = makeTool(ToolSpec{"BALL", 6.0});

        // Расстояние точки границы до аналитической границы реза, мм.
        const auto wallDev = [](double x, double y) {
            return std::fabs(segmentDistanceMm(x, y, -2.0, 0.0, 2.0, 0.0) - 3.0);
        };
        const auto ballDev = [s05](double x, double y) {
            return std::fabs(std::hypot(x, y) - s05);
        };
        const auto measureEnds = [](const std::vector<gv::StockContourSegment>& segments,
                                    auto dev) {
            double worst = 0.0;
            int checked = 0;
            for (const gv::StockContourSegment& s : segments) {
                worst = std::max(worst, dev(s.x0Mm, s.y0Mm));
                worst = std::max(worst, dev(s.x1Mm, s.y1Mm));
                checked += 2;
            }
            return std::pair<int, double>(checked, worst);
        };
        // Нулевой уровень знакового расстояния на рёбрах клеток поля - то, что
        // видно на экране: цвет и линия идут по нему (stocksurface.frag).
        const auto measureDrawn = [](const gv::StockBounds& b, double cell,
                                     const std::vector<float>& sd, int nx, int ny,
                                     double cut, auto dev) {
            double worst = 0.0;
            int checked = 0;
            const auto at = [&](int ix, int iy) {
                return double(sd[std::size_t(iy) * std::size_t(nx) + std::size_t(ix)]);
            };
            for (int iy = 0; iy < ny; ++iy) {
                for (int ix = 0; ix < nx; ++ix) {
                    const double x0 = b.xMinMm + ix * cell;
                    const double y0 = b.yMinMm + iy * cell;
                    const double f = at(ix, iy);
                    if (ix + 1 < nx) {
                        const double g = at(ix + 1, iy);
                        if ((f < 0.0) != (g < 0.0)) {
                            const double t = f / (f - g);
                            worst = std::max(worst, dev(x0 + t * cell, y0));
                            ++checked;
                        }
                    }
                    if (iy + 1 < ny) {
                        const double g = at(ix, iy + 1);
                        if ((f < 0.0) != (g < 0.0)) {
                            const double t = f / (f - g);
                            worst = std::max(worst, dev(x0, y0 + t * cell));
                            ++checked;
                        }
                    }
                }
            }
            (void)cut;
            return std::pair<int, double>(checked, worst);
        };

        const double cells[2] = {0.5, 0.2};
        int exactChecked = 0;
        int drawnChecked = 0;
        double wallExactWorst = 0.0;
        double ballExactWorst = 0.0;
        double drawnWorst = 0.0;
        bool shapesOk = true;
        for (double cell : cells) {
            for (int shape = 0; shape < 2; ++shape) {
                const gv::PathData& data = shape == 0 ? wallData : ballData;
                const gv::ToolInfo& tool = shape == 0 ? wallTool : ballTool;
                const std::function<double(double, double)> dev = shape == 0
                        ? std::function<double(double, double)>(wallDev)
                        : std::function<double(double, double)>(ballDev);
                const char* name = shape == 0 ? "wall" : "ball";

                const gv::StockRunResult run = gv::simulateStock(data, bounds, cell, tool);
                const gv::StockField& field = run.field;
                const std::vector<gv::StockCutter> cutters = gv::stockCutterPlan(data, tool);
                const gv::StockBoundaryResult boundary = gv::stockCutBoundary(
                    data, bounds, cell, cutters, field.depthMm(),
                    field.countX(), field.countY(), cutMm);
                const std::vector<gv::StockContourSegment> coarse = gv::stockCutContour(
                    bounds, cell, field.depthMm(), field.countX(), field.countY(), cutMm);
                const auto coarseStat = measureEnds(coarse, dev);
                const auto exactStat = measureEnds(boundary.segments, dev);

                // Поле мельче: 0.1 мм (пример плана) и 0.0625 мм (узел/8 при 0.5).
                double fine01 = 0.0;
                double fine006 = 0.0;
                {
                    const gv::StockRunResult f1 = gv::simulateStock(data, bounds, 0.1, tool);
                    fine01 = measureEnds(gv::stockCutContour(bounds, 0.1, f1.field.depthMm(),
                                                             f1.field.countX(), f1.field.countY(),
                                                             cutMm), dev).second;
                    const gv::StockRunResult f2 = gv::simulateStock(data, bounds, 0.0625, tool);
                    fine006 = measureEnds(gv::stockCutContour(bounds, 0.0625, f2.field.depthMm(),
                                                              f2.field.countX(), f2.field.countY(),
                                                              cutMm), dev).second;
                }
                const std::vector<float> sd = gv::stockCutSignedDistance(
                    bounds, cell, field.depthMm(), field.countX(), field.countY(),
                    boundary.segments, cutMm, cell * 3.0);
                const auto drawnStat = measureDrawn(bounds, cell, sd, field.countX(),
                                                    field.countY(), cutMm, dev);

                std::printf("note boundary_%s cell=%.2f coarse=%.4f fine_0.1=%.4f "
                            "fine_0.0625=%.4f exact=%.6f drawn=%.4f segs=%d coarse_segs=%d "
                            "exact_ms=%.1f evals=%lld\n",
                            name, cell, coarseStat.second, fine01, fine006, exactStat.second,
                            drawnStat.second, int(boundary.segments.size()), int(coarse.size()),
                            boundary.buildMs, boundary.evaluations);
                // Число звеньев уточнённой границы: переход между рёбрами клетки
                // раздроблен на BOUNDARY_SUBDIV_PER_CELL звеньев, поэтому прежнее
                // равенство coarse.size() сменилось на произведение. Это следствие
                // дробления, а не ослабление допуска: переходы (концы цепочек) те
                // же, и они по-прежнему мерятся против аналитики.
                shapesOk = shapesOk && !boundary.segments.empty()
                        && boundary.segments.size()
                                == coarse.size() * std::size_t(gv::BOUNDARY_SUBDIV_PER_CELL);
                exactChecked += coarseStat.first + exactStat.first;
                drawnChecked += drawnStat.first;
                drawnWorst = std::max(drawnWorst, drawnStat.second);
                if (shape == 0) {
                    wallExactWorst = std::max(wallExactWorst, exactStat.second);
                } else {
                    ballExactWorst = std::max(ballExactWorst, exactStat.second);
                }
            }
        }
        // Точная граница: допуск 1e-3 мм - на два порядка строже цели 0.05-0.1 мм
        // и на два-три порядка лучше прежней (coarse). Нарисованная (нулевой
        // уровень знакового расстояния) - в пределах цели 0.05 мм.
        reportCase("boundary_wall", exactChecked, wallExactWorst, 1e-3, shapesOk);
        reportCase("boundary_ball", exactChecked, ballExactWorst, 1e-3, shapesOk);
        reportCase("boundary_drawn", drawnChecked, drawnWorst, 0.05, drawnChecked > 0);

        // 7д. Цепочка границы идёт по УРОВНЮ реза, а не по хорде
        // (stockCutBoundary, subdivPerCell): между переходами клетки кладутся
        // подточки, спущенные на уровень вдоль нормали клетки. Меры: излом на
        // вершине цепочки в градусах (было - по клетке), отклонение вершин от
        // аналитической границы (уровень шара - круг s05) и от ПРЕЖНЕЙ цепочки
        // (хорды), число подточек на клетку. Прежняя цепочка (subdivPerCell = 1)
        // меряется здесь же: без неё случай слеп.
        {
            const double cell = 0.5;
            const double cutMm = 0.05;
            const double r = 3.0;
            const double tipZ = -1.0;
            gv::StockBounds bounds;
            bounds.xMinMm = -5.0;
            bounds.xMaxMm = 5.0;
            bounds.yMinMm = -5.0;
            bounds.yMaxMm = 5.0;
            bounds.zMinMm = -5.0;
            bounds.zMaxMm = 0.0;
            const double chord = cutMm + r + tipZ - bounds.zMaxMm;
            const double s05 = std::sqrt(std::max(0.0, r * r - chord * chord));

            const std::string program =
                "G21 G90\n"
                "G0 X0 Y0 Z5\n"
                "G1 Z-1 F100\n";
            const gv::ParseResult parsed = gv::parseGcode(program);
            const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
            const gv::ToolInfo tool = makeTool(ToolSpec{ "BALL", 6.0 });
            const gv::StockRunResult run = gv::simulateStock(data, bounds, cell, tool);
            const gv::StockField& field = run.field;
            const int nx = field.countX();
            const int ny = field.countY();
            const std::vector<gv::StockCutter> cutters = gv::stockCutterPlan(data, tool);
            const gv::StockBoundaryResult coarse = gv::stockCutBoundary(
                data, bounds, cell, cutters, field.depthMm(), nx, ny, cutMm, nullptr, 24, 1);
            const gv::StockBoundaryResult fine = gv::stockCutBoundary(
                data, bounds, cell, cutters, field.depthMm(), nx, ny, cutMm);

            // Излом на вершине цепочки: звенья склеиваются по общим концам
            // (шаг CHAIN_JOIN_MM), у каждой внутренней вершины берётся угол между
            // соседними звеньями. Цепочка клетки без дробления - хорда, и излом
            // на её конце - это перелом на клетке; у дроблённой цепочки вершины
            // стоят на самом уровне.
            const auto turnStats = [](const std::vector<gv::StockContourSegment>& segs,
                                      std::vector<double>& turns, int& vertices) {
                constexpr double JOIN_MM = 1e-6;
                std::vector<char> used(segs.size(), 0);
                for (std::size_t s = 0; s < segs.size(); ++s) {
                    if (used[s] != 0) {
                        continue;
                    }
                    std::vector<std::array<double, 2>> pts{ { segs[s].x0Mm, segs[s].y0Mm },
                                                            { segs[s].x1Mm, segs[s].y1Mm } };
                    used[s] = 1;
                    for (bool grew = true; grew;) {
                        grew = false;
                        for (std::size_t k = 0; k < segs.size(); ++k) {
                            if (used[k] != 0) {
                                continue;
                            }
                            const std::array<double, 2> ends[2] = {
                                { segs[k].x0Mm, segs[k].y0Mm }, { segs[k].x1Mm, segs[k].y1Mm } };
                            for (int e = 0; e < 2; ++e) {
                                if (std::hypot(pts.back()[0] - ends[e][0],
                                               pts.back()[1] - ends[e][1]) <= JOIN_MM) {
                                    pts.push_back(ends[1 - e]);
                                    used[k] = 1;
                                    grew = true;
                                    break;
                                }
                            }
                        }
                    }
                    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
                        const double ax = pts[i][0] - pts[i - 1][0];
                        const double ay = pts[i][1] - pts[i - 1][1];
                        const double bx = pts[i + 1][0] - pts[i][0];
                        const double by = pts[i + 1][1] - pts[i][1];
                        const double la = std::hypot(ax, ay);
                        const double lb = std::hypot(bx, by);
                        if (la <= 0.0 || lb <= 0.0) {
                            continue;
                        }
                        const double cosA = (ax * bx + ay * by) / (la * lb);
                        turns.push_back(std::acos(std::max(-1.0, std::min(1.0, cosA)))
                                        * 180.0 / PI);
                        ++vertices;
                    }
                }
            };
            std::vector<double> turnsCoarse;
            std::vector<double> turnsFine;
            int vertCoarse = 0;
            int vertFine = 0;
            turnStats(coarse.segments, turnsCoarse, vertCoarse);
            turnStats(fine.segments, turnsFine, vertFine);

            // Отклонение вершин уточнённой цепочки от аналитической границы и от
            // прежней цепочки (хорд): первое - точность уровня, второе - насколько
            // уточнение сдвинуло линию.
            double levelWorst = 0.0;
            double chordWorst = 0.0;
            for (const gv::StockContourSegment& s : fine.segments) {
                const std::array<std::array<double, 2>, 2> ends{ {
                    { s.x0Mm, s.y0Mm }, { s.x1Mm, s.y1Mm } } };
                for (const std::array<double, 2>& p : ends) {
                    levelWorst = std::max(levelWorst, std::fabs(std::hypot(p[0], p[1]) - s05));
                    double best = std::numeric_limits<double>::max();
                    for (const gv::StockContourSegment& c : coarse.segments) {
                        best = std::min(best, segmentDistanceMm(p[0], p[1], c.x0Mm, c.y0Mm,
                                                               c.x1Mm, c.y1Mm));
                    }
                    chordWorst = std::max(chordWorst, best);
                }
            }
            // Точки на клетку: у дроблённой цепочки звеньев в subdiv раз больше,
            // подточек на переход - subdiv - 1. unprojected - подточки, которым
            // уровня рядом не нашлось: у гладкого среза их нет вовсе.
            const double perCell = double(fine.segments.size())
                    / double(coarse.segments.size());
            std::printf("note boundary_refined cell_mm=%.2f subdiv=%d per_cell=%.2f "
                        "turn_coarse_deg=%.3f turn_fine_deg=%.3f median_coarse_deg=%.3f "
                        "median_fine_deg=%.3f checked_coarse=%d checked_fine=%d level_mm=%.2e "
                        "chord_mm=%.2e unprojected=%d\n",
                        cell, gv::BOUNDARY_SUBDIV_PER_CELL, perCell,
                        turnsCoarse.empty() ? 0.0 : *std::max_element(turnsCoarse.begin(),
                                                                      turnsCoarse.end()),
                        turnsFine.empty() ? 0.0 : *std::max_element(turnsFine.begin(),
                                                                    turnsFine.end()),
                        median(turnsCoarse), median(turnsFine), vertCoarse, vertFine,
                        levelWorst, chordWorst, fine.unprojected);
            // Сверка: уточнённая цепочка стоит на уровне шара (не дальше 1e-6 мм,
            // как и точная граница), излом вершины упал в разы (было - излом
            // клетки), сдвиг от прежней цепочки - сагитта хорды (десятые доли
            // клетки, а не миллиметры), звеньев на клетку ровно subdiv, а подточек,
            // оставшихся на хорде, у гладкого среза нет.
            reportCase("boundary_refined", vertFine, levelWorst, 1e-6,
                       levelWorst <= 1e-6 && chordWorst <= 0.1 && fine.unprojected == 0
                           && std::fabs(perCell - double(gv::BOUNDARY_SUBDIV_PER_CELL)) <= 1e-9
                           && !turnsFine.empty() && !turnsCoarse.empty()
                           && *std::max_element(turnsFine.begin(), turnsFine.end())
                                  * 2.0
                                  < *std::max_element(turnsCoarse.begin(), turnsCoarse.end())
                           && *std::max_element(turnsFine.begin(), turnsFine.end()) < 5.0
                           && *std::max_element(turnsCoarse.begin(), turnsCoarse.end()) > 8.0
                           && vertFine > 0);
        }

        // 7е. Кромка ЦВЕТА идёт по уточнённой цепочке реза, а не по хорде клетки
        // (stocksurface.frag, ядро stockCutChainDistance). Мера - боковой размах
        // кромки цвета вдоль аналитической границы реза: по M углам ищется радиус,
        // где знаковое расстояние пересекает ноль, и берётся размах (r - R). Срез
        // шара даёт круг s05, срез плоской фрезы - круг радиуса инструмента
        // (уступ). Поле, его байты и его сверки не трогаются: мера стоит на
        // ОТДЕЛЬНОМ представлении для рисования, а прежнее правило (билинейная
        // хорда, refine=1) меряется здесь же - без него случай слеп.
        {
            const double cell = 0.5;
            const double cutMm = 0.05;
            const double r = 3.0;
            const double tipZ = -1.0;
            gv::StockBounds bnd;
            bnd.xMinMm = -5.0;
            bnd.xMaxMm = 5.0;
            bnd.yMinMm = -5.0;
            bnd.yMaxMm = 5.0;
            bnd.zMinMm = -5.0;
            bnd.zMaxMm = 0.0;
            const double chord = cutMm + r + tipZ - bnd.zMaxMm;
            const double s05 = std::sqrt(std::max(0.0, r * r - chord * chord));

            // Билинейная выборка поля на его сетке (узел (0,0) в xMin, шаг h).
            const auto sampleAt = [](const std::vector<float>& f, int nx, int ny,
                                     const gv::StockBounds& b, double h, double x, double y) {
                const double px = (x - b.xMinMm) / h;
                const double py = (y - b.yMinMm) / h;
                const int ix0 = std::max(0, std::min(nx - 2, int(std::floor(px))));
                const int iy0 = std::max(0, std::min(ny - 2, int(std::floor(py))));
                const double fx = std::max(0.0, std::min(1.0, px - double(ix0)));
                const double fy = std::max(0.0, std::min(1.0, py - double(iy0)));
                const auto at = [&](int ix, int iy) {
                    return double(f[std::size_t(iy) * std::size_t(nx) + std::size_t(ix)]);
                };
                const double a = at(ix0, iy0) * (1.0 - fx) + at(ix0 + 1, iy0) * fx;
                const double c = at(ix0, iy0 + 1) * (1.0 - fx) + at(ix0 + 1, iy0 + 1) * fx;
                return a * (1.0 - fy) + c * fy;
            };
            // Размах (r - R) кромки цвета вдоль круга радиуса R, мм. Вдоль каждого
            // луча снаружи внутрь ищется смена знака (снаружи нетронуто, внутри
            // снято); положение уточняется долей. maxAbs - наибольшее |r - R|.
            const auto wobble = [&sampleAt, &bnd](const std::vector<float>& f, int nx, int ny,
                                                  double h, double R, double* maxAbsOut) {
                constexpr int M = 180;
                constexpr int STEPS = 240;
                const double span = 0.6;
                double lo = 1e9;
                double hi = -1e9;
                double maxAbs = 0.0;
                for (int m = 0; m < M; ++m) {
                    const double a = (2.0 * PI * double(m)) / double(M);
                    const double ca = std::cos(a);
                    const double sa = std::sin(a);
                    double prev = 0.0;
                    double rZero = R;
                    for (int k = 0; k <= STEPS; ++k) {
                        const double rad = R + span - 2.0 * span * (double(k) / double(STEPS));
                        const double val = sampleAt(f, nx, ny, bnd, h, rad * ca, rad * sa);
                        if (k > 0 && (prev > 0.0) != (val > 0.0)) {
                            const double t = prev / (prev - val);
                            const double prevRad =
                                R + span - 2.0 * span * (double(k - 1) / double(STEPS));
                            rZero = prevRad + (rad - prevRad) * t;
                            break;
                        }
                        prev = val;
                    }
                    const double e = rZero - R;
                    lo = std::min(lo, e);
                    hi = std::max(hi, e);
                    maxAbs = std::max(maxAbs, std::fabs(e));
                }
                if (maxAbsOut != nullptr) {
                    *maxAbsOut = maxAbs;
                }
                return hi - lo;
            };

            const std::string program =
                "G21 G90\n"
                "G0 X0 Y0 Z5\n"
                "G1 Z-1 F100\n";
            const gv::ParseResult parsed = gv::parseGcode(program);
            const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);

            double coarseWob = 0.0;
            double fineWob = 0.0;
            double coarseMax = 0.0;
            double fineMax = 0.0;
            int fineNodesX = 0;
            int fineNodesY = 0;
            bool haveFine = false;
            for (int shape = 0; shape < 2; ++shape) {
                const gv::ToolInfo tool = shape == 0
                        ? makeTool(ToolSpec{ "BALL", 6.0 })
                        : makeTool(ToolSpec{ "FLAT", 6.0 });
                const double R = shape == 0 ? s05 : r;
                const gv::StockRunResult run = gv::simulateStock(data, bnd, cell, tool);
                const gv::StockField& field = run.field;
                const int nx = field.countX();
                const int ny = field.countY();
                const std::vector<gv::StockCutter> cutters = gv::stockCutterPlan(data, tool);
                const gv::StockBoundaryResult boundary = gv::stockCutBoundary(
                    data, bnd, cell, cutters, field.depthMm(), nx, ny, cutMm);
                const std::vector<float> coarse = gv::stockCutSignedDistance(
                    bnd, cell, field.depthMm(), nx, ny, boundary.segments, cutMm, cell * 3.0);
                const std::vector<float> fine = gv::stockCutChainDistance(
                    bnd, cell, coarse, nx, ny, boundary.segments, cell * 1.0, 2,
                    &fineNodesX, &fineNodesY);
                double cMax = 0.0;
                double fMax = 0.0;
                const double cWob = wobble(coarse, nx, ny, cell, R, &cMax);
                const double fWob = wobble(fine, fineNodesX, fineNodesY, cell / 2.0, R, &fMax);
                coarseWob = std::max(coarseWob, cWob);
                fineWob = std::max(fineWob, fWob);
                coarseMax = std::max(coarseMax, cMax);
                fineMax = std::max(fineMax, fMax);
                haveFine = haveFine || !fine.empty();
                std::printf("note cut_colour_edge_%s cell_mm=%.2f refine=2 R_mm=%.4f "
                            "coarse_wobble_mm=%.4f coarse_max_mm=%.4f fine_wobble_mm=%.4f "
                            "fine_max_mm=%.4f nodes=%dx%d\n",
                            shape == 0 ? "ball" : "flat", cell, R, cWob, cMax, fWob, fMax,
                            fineNodesX, fineNodesY);
            }
            // Кромка цвета: размах у мелкой сетки меньше допуска (0.006 мм - втрое
            // меньше прежнего размаха и в 200 раз меньше пикселя кадра 0.33 мм), а
            // прежнее правило (билинейная хорда) обязано быть шире втрое - иначе
            // случай слеп. Замер: 0.0146 -> 0.0038 мм (шар), 0.0104 -> 0.0028 (фреза).
            std::printf("note cut_colour_edge cell_mm=%.2f coarse_wobble_mm=%.4f "
                        "fine_wobble_mm=%.4f coarse_max_mm=%.4f fine_max_mm=%.4f\n",
                        cell, coarseWob, fineWob, coarseMax, fineMax);
            reportCase("cut_colour_edge", 2 * 180, fineWob, 6e-3,
                       fineWob <= 6e-3 && coarseWob >= 3.0 * fineWob && coarseWob > 6e-3
                           && haveFine);
        }

        // Инкрементальный тик: граница с состоянием (кэш) после подрастания поля
        // обязана совпасть с полной сборкой отрезок в отрезок, а цена - упасть.
        // Поле монотонно (глубина узла только растёт), поэтому тик пересчитывает
        // лишь клетки с изменившимися узлами.
        {
            const double cell = 0.5;
            const gv::StockRunResult run = gv::simulateStock(wallData, bounds, cell, wallTool);
            const std::vector<gv::StockCutter> cutters = gv::stockCutterPlan(wallData, wallTool);
            const int gx = run.field.countX();
            std::vector<float> grown = run.field.depthMm();
            for (int iy = 8; iy <= 14; ++iy) {
                for (int ix = 8; ix <= 14; ++ix) {
                    grown[std::size_t(iy) * std::size_t(gx) + std::size_t(ix)] =
                        std::max(grown[std::size_t(iy) * std::size_t(gx) + std::size_t(ix)], 1.0f);
                }
            }
            gv::StockBoundaryCache cache;
            const gv::StockBoundaryResult first = gv::stockCutBoundary(
                wallData, bounds, cell, cutters, run.field.depthMm(), gx, run.field.countY(),
                cutMm, &cache);
            const Clock::time_point tInc = Clock::now();
            const gv::StockBoundaryResult inc = gv::stockCutBoundary(
                wallData, bounds, cell, cutters, grown, gx, run.field.countY(), cutMm, &cache);
            const double incMs = msSince(tInc);
            const gv::StockBoundaryResult full = gv::stockCutBoundary(
                wallData, bounds, cell, cutters, grown, gx, run.field.countY(), cutMm, nullptr);
            bool same = inc.segments.size() == full.segments.size();
            double maxDelta = 0.0;
            for (std::size_t i = 0; i < inc.segments.size() && i < full.segments.size(); ++i) {
                maxDelta = std::max(maxDelta, std::fabs(inc.segments[i].x0Mm - full.segments[i].x0Mm));
                maxDelta = std::max(maxDelta, std::fabs(inc.segments[i].y0Mm - full.segments[i].y0Mm));
                maxDelta = std::max(maxDelta, std::fabs(inc.segments[i].x1Mm - full.segments[i].x1Mm));
                maxDelta = std::max(maxDelta, std::fabs(inc.segments[i].y1Mm - full.segments[i].y1Mm));
            }
            std::printf("note boundary_incremental first_segments=%d inc_segments=%d "
                        "full_segments=%d inc_ms=%.4f full_ms=%.4f max_delta_mm=%.3e\n",
                        int(first.segments.size()), int(inc.segments.size()),
                        int(full.segments.size()), incMs, full.buildMs, maxDelta);
            reportCase("boundary_incremental", int(inc.segments.size()), maxDelta, 1e-12,
                       same && !inc.segments.empty() && !full.segments.empty());
        }
    }

    // 8. Оболочка коробки: пять прямоугольников без верхней грани. Проверяется
    // форма, а не буфер: вершин 30, каждая - угол габарита, увеличенного на
    // зазоры; у каждого треугольника одна координата постоянна (грани
    // плоские); дно есть, верхней грани нет - иначе она спорила бы с полем за
    // одну плоскость, а микро-сдвиг на верху соврал бы о глубине реза.
    {
        gv::StockBounds bounds;
        bounds.xMinMm = -5.0;
        bounds.xMaxMm = 5.0;
        bounds.yMinMm = -5.0;
        bounds.yMaxMm = 5.0;
        bounds.zMinMm = -5.0;
        bounds.zMaxMm = 5.0;
        const double gapSide = 0.7;
        const double gapTop = 0.5;
        const double gapBottom = 0.5;
        const std::vector<float> vertices = gv::stockShellVertices(
                bounds, 0.5, 21, 21, gapSide, gapTop, gapBottom);
        const double x0 = bounds.xMinMm - gapSide;
        const double x1 = bounds.xMaxMm + gapSide;
        const double y0 = bounds.yMinMm - gapSide;
        const double y1 = bounds.yMaxMm + gapSide;
        const double z0 = bounds.zMinMm - gapBottom;
        const double z1 = bounds.zMaxMm + gapTop;

        const auto atCorner = [x0, x1, y0, y1, z0, z1](double value, double a, double b) {
            return std::fabs(value - a) < 1e-6 || std::fabs(value - b) < 1e-6;
        };
        bool shapeOk = int(vertices.size()) == 30 * 3;
        double maxDev = 0.0;
        int bottomFaces = 0;
        int topFaces = 0;
        int checked = 0;
        for (std::size_t i = 0; i + 2 < vertices.size() / 3; i += 3) {
            const float* a = &vertices[i * 3];
            const float* b = &vertices[(i + 1) * 3];
            const float* c = &vertices[(i + 2) * 3];
            ++checked;
            for (const float* p : {a, b, c}) {
                const double px = double(p[0]);
                const double py = double(p[1]);
                const double pz = double(p[2]);
                // Отклонение от ближайшего угла габарита: ноль, если вершина
                // стоит точно на нём.
                const double dx = std::min(std::fabs(px - x0), std::fabs(px - x1));
                const double dy = std::min(std::fabs(py - y0), std::fabs(py - y1));
                const double dz = std::min(std::fabs(pz - z0), std::fabs(pz - z1));
                maxDev = std::max(maxDev, std::max(dx, std::max(dy, dz)));
                shapeOk = shapeOk
                        && atCorner(px, x0, x1) && atCorner(py, y0, y1) && atCorner(pz, z0, z1);
            }
            const bool flat = (a[0] == b[0] && b[0] == c[0])
                    || (a[1] == b[1] && b[1] == c[1])
                    || (a[2] == b[2] && b[2] == c[2]);
            shapeOk = shapeOk && flat;
            // Грань целиком на верху или на дне: верхней быть не должно, дно - два
            // треугольника.
            const bool onBottom = a[2] == float(z0) && b[2] == float(z0) && c[2] == float(z0);
            const bool onTop = a[2] == float(z1) && b[2] == float(z1) && c[2] == float(z1);
            if (onBottom) {
                ++bottomFaces;
            }
            if (onTop) {
                ++topFaces;
            }
        }
        std::printf("note stockshell verts=%zu tris=%d box=%.1f..%.1f x %.1f..%.1f x %.1f..%.1f "
                    "bottom=%d top=%d\n",
                    vertices.size() / 3, checked, x0, x1, y0, y1, z0, z1,
                    bottomFaces, topFaces);
        reportCase("stockshell", checked, maxDev, 1e-6,
                   shapeOk && bottomFaces == 2 && topFaces == 0);
    }

    // 8б. Обрезка верха стенки по полю: правило stockshell.frag. Стенка стоит на
    // габарите, её верх - на верху заготовки, а узел поля на кромке опускается
    // там, где инструмент снял металл через кромку (поле режется и за габаритом
    // пути). Сверяется, что (1) на кромке есть и снятые, и нетронутые узлы, (2)
    // правило обрезки опускает верх стенки ровно на глубину поля плюс зазор
    // верха, (3) углы кромки при этом остаются нетронутыми - то есть линейная
    // интерполяция между углами (обрезка вершиной) верх стенки в середине реза не
    // опустила бы, и обрезка обязана быть попиксельной.
    {
        const double cell = 0.5;
        const double gapTop = 0.5;                 // как shellGapTopMm в QML
        gv::StockBounds bounds;
        bounds.xMinMm = -10.0;
        bounds.xMaxMm = 10.0;
        bounds.yMinMm = -10.0;
        bounds.yMaxMm = 10.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        // Рез вдоль кромки y = yMax: фреза D6 идёт по самой стенке, глубина 2 мм.
        field.sweep(cutter, {-5.0, bounds.yMaxMm, -2.0}, {5.0, bounds.yMaxMm, -2.0});

        const int iyEdge = field.countY() - 1;     // узел на кромке yMax
        const int ixMid = int((0.0 - bounds.xMinMm) / cell + 0.5);
        int cutNodes = 0;
        int untouchedNodes = 0;
        double maxDepth = 0.0;
        for (int ix = 0; ix < field.countX(); ++ix) {
            const double depth = bounds.zMaxMm - field.heightMm(ix, iyEdge);
            if (depth > 0.05) {
                ++cutNodes;
                maxDepth = std::max(maxDepth, depth);
            } else {
                ++untouchedNodes;
            }
        }
        const double cornerLeft = bounds.zMaxMm - field.heightMm(0, iyEdge);
        const double cornerRight = bounds.zMaxMm - field.heightMm(field.countX() - 1, iyEdge);
        const double midDepth = bounds.zMaxMm - field.heightMm(ixMid, iyEdge);
        // Правило шейдера: верх стенки = верх заготовки минус глубина поля. У
        // нетронутого узла это ровно верх (щели нет), у снятого стенка уходит
        // вниз на глубину, а необрезанная стояла бы выше на зазор плюс глубину.
        const double trimGapMm = gapTop + midDepth;
        const bool ok = cutNodes > 0 && untouchedNodes > 0
                && cornerLeft == 0.0 && cornerRight == 0.0
                && midDepth >= 1.5
                && (cornerLeft + cornerRight) / 2.0 < 0.5 * midDepth;
        std::printf("note stockshell_trim edge_nodes=%d cut=%d untouched=%d "
                    "max_depth_mm=%.2f mid_depth_mm=%.2f corners=(%.2f,%.2f) "
                    "trim_gap_mm=%.2f\n",
                    field.countX(), cutNodes, untouchedNodes, maxDepth, midDepth,
                    cornerLeft, cornerRight, trimGapMm);
        reportCase("stockshell_trim", field.countX(), 0.0, 0.0, ok);
    }

    // 8г. Обрезка верха стенки берёт глубину ТОЙ ЖЕ выборкой, что нарисованный
    // верх (stockshell.frag, edgeDepth): билинейно по узлам клетки. Прежнее
    // правило брало ближайший узел - ступеньку по узлам, - и на кромке со снятым
    // металлом верх стенки расходился с кромкой нарисованного верха на половину
    // перепада соседних узлов. Случай держит обе половины правила: у ближайшего
    // узла расхождение есть, у билинейной выборки его нет.
    {
        const double cell = 0.5;
        gv::StockBounds bounds;
        bounds.xMinMm = -10.0;
        bounds.xMaxMm = 10.0;
        bounds.yMinMm = -10.0;
        bounds.yMaxMm = 10.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        gv::StockField field;
        field.reset(bounds, cell);
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        // Кромка y = yMax снимается на разную глубину по x: у ближайшего узла
        // обрезка идёт ступенькой, у билинейной выборки - линейно между узлами.
        field.sweep(cutter, {-8.0, bounds.yMaxMm, -3.0}, {-1.0, bounds.yMaxMm, -3.0});
        field.sweep(cutter, { 1.0, bounds.yMaxMm, -1.0}, { 8.0, bounds.yMaxMm, -1.0});

        const int iyEdge = field.countY() - 1;
        const int nx = field.countX();
        // Правило шейдера (stockshell.frag, edgeDepth): глубина в точке кромки
        // берётся билинейно по четырём узлам клетки - на габарите это прямая
        // между соседними узлами кромки.
        const auto trimDepth = [&](double xMm) {
            const double hi = double(nx - 1);
            const double q = std::min(std::max((xMm - bounds.xMinMm) / cell, 0.0), hi);
            const int i0 = int(std::floor(q));
            const int i1 = std::min(i0 + 1, nx - 1);
            const double f = q - double(i0);
            const double d0 = bounds.zMaxMm - field.heightMm(i0, iyEdge);
            const double d1 = bounds.zMaxMm - field.heightMm(i1, iyEdge);
            return d0 + (d1 - d0) * f;
        };
        // Край нарисованного верха на кромке - прямая между соседними узлами
        // (core/stock_sim, stockSurfaceMesh: последний ряд прижат к габариту, а
        // между узлами меш линеен).
        double worstNearestMm = 0.0;
        double worstBilinearMm = 0.0;
        for (int ix = 0; ix + 1 < nx; ++ix) {
            const double left = bounds.zMaxMm - field.heightMm(ix, iyEdge);
            const double right = bounds.zMaxMm - field.heightMm(ix + 1, iyEdge);
            const double drawn = 0.5 * (left + right);
            const double xMid = bounds.xMinMm + (double(ix) + 0.5) * cell;
            worstNearestMm = std::max(worstNearestMm, std::abs(left - drawn));
            worstBilinearMm = std::max(worstBilinearMm, std::abs(trimDepth(xMid) - drawn));
        }
        const bool ok = worstNearestMm >= 0.25 && worstBilinearMm <= 1e-6;
        std::printf("note stockshell_trim_interp nearest_dev_mm=%.2f "
                    "bilinear_dev_mm=%.6f\n",
                    worstNearestMm, worstBilinearMm);
        reportCase("stockshell_trim_interp", nx - 1, worstBilinearMm, 1e-6, ok);
    }

    // 8в. Кольца обводки оправки и резца: порог перепада profileRingPoints.
    // Кольцо рисуется на настоящем перепаде профиля, но не на звене дуги - иначе
    // каркас тела вращения превращается в клетку. Порог обязан лежать между
    // шагом дуги и самой пологой фаской: проверяются оба края - кольца оправки с
    // высотами, кольца на дугах шара и скругления (их быть не должно).
    {
        const std::vector<gv::ProfilePoint> holder = gv::holderSolidProfile();
        const std::vector<gv::ProfilePoint> holderRings = gv::profileRingPoints(holder);
        const double expectY[] = {0.0, 3.0, 16.0, 20.0, 22.0, 24.0, 28.0, 72.0};
        const int expectCount = int(sizeof(expectY) / sizeof(expectY[0]));
        bool heightsOk = int(holderRings.size()) == expectCount;
        double maxDev = 0.0;
        for (int i = 0; i < int(holderRings.size()); ++i) {
            const double y = holderRings[std::size_t(i)].height_mm;
            if (i < expectCount) {
                maxDev = std::max(maxDev, std::fabs(y - expectY[i]));
                heightsOk = heightsOk && std::fabs(y - expectY[i]) < 1e-9;
            }
        }

        // Дуга шара: 12 отрезков на 90 градусов, 7.5 на звено. Кольца на дуге -
        // клетка, поэтому на ней их быть не должно; у полюса дуги кольца нет и
        // по другой причине: хорда там почти вертикальна (3.8 градуса до
        // цилиндра), а не 90. Настоящее ребро у шара одно - шейка.
        const std::vector<gv::ProfilePoint> ball = gv::toolProfile(makeTool(ToolSpec{"BALL", 6.0}));
        const std::vector<gv::ProfilePoint> ballRings = gv::profileRingPoints(ball);
        // Дуга скругления нижнего угла: 8 отрезков на 90, 11.25 на звено - самый
        // близкий к порогу шаг, им порог и проверяется снизу: порог ниже 11.25
        // дал бы восемь колец на дуге, и число колец это покажет.
        const std::vector<gv::ProfilePoint> bull =
                gv::toolProfile(makeTool(ToolSpec{"FLAT", 6.0, 0.0, 10.0, 20.0, 1.0}));
        const std::vector<gv::ProfilePoint> bullRings = gv::profileRingPoints(bull);

        std::printf("note holder_rings count=%d", int(holderRings.size()));
        for (const gv::ProfilePoint& p : holderRings) {
            std::printf(" (%.2f,%.2f)", p.radius_mm, p.height_mm);
        }
        std::printf(" ball=%d bull=%d\n", int(ballRings.size()), int(bullRings.size()));

        const bool ok = heightsOk
                && ballRings.size() == 1
                && bullRings.size() == 1;
        reportCase("holder_rings", expectCount + 2, maxDev, 1e-9, ok);
    }

    // 9. Поднутрение: карта высот его не выражает, и обход обязан отказаться, а
    // не нарисовать срез, которого на станке не будет. Проверяются оба входа:
    // готовый профиль (лоллипоп: радиус растёт, падает и снова растёт - такую
    // форму UI_TOOL не описывает) и тип UI_TOOL LOLLIPOP, для которого профиль
    // строится как прямая фреза и форма уже потеряна. Поля в обоих случаях нет:
    // не пустое, а не созданное вовсе.
    {
        const std::string program =
            "(UI_TOOL: T=7 TYPE=LOLLIPOP D=10. CR=0. TIPD=0. FL=5. SL=5. BL=20. OL=20. NF=2)\n"
            "G21 G90\n"
            "G0 X0 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X10 F100\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        gv::StockBounds bounds;
        bounds.xMinMm = -20.0;
        bounds.xMaxMm = 20.0;
        bounds.yMinMm = -20.0;
        bounds.yMaxMm = 20.0;
        bounds.zMinMm = -10.0;
        bounds.zMaxMm = 0.0;
        const double cell = 0.5;

        // Готовый профиль лоллипопа: рост радиуса, шейка, снова рост.
        const std::vector<gv::ProfilePoint> profile{
            {0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}, {3.0, 3.0}, {1.5, 5.0}, {4.0, 7.0}};
        const gv::StockCutter cutter = gv::stockCutterFromProfile(profile, 8.0);
        gv::StockWalk walk;
        const bool profileStarted = walk.start(data, bounds, cell, {cutter});
        const bool profileEmpty = walk.field().empty();
        const bool profileRefused = walk.refusedStage() == 0;

        // Тип UI_TOOL: отказ идёт по типу, до профиля.
        const gv::StockRunResult byTag =
            gv::simulateStock(data, bounds, cell, gv::ToolInfo{});
        const gv::StockCutter byType = gv::stockCutter(parsed.stages.front().info);
        std::printf("note stock_undercut profile_undercut=%d stage=%d field_nodes=%d "
                    "type_undercut=%d tag_refused=%d tag_stage=%d tag_tool=%d tag_nodes=%d\n",
                    cutter.undercut ? 1 : 0, walk.refusedStage(),
                    walk.field().countX() * walk.field().countY(),
                    byType.undercut ? 1 : 0, byTag.refused ? 1 : 0, byTag.refusedStage,
                    byTag.refusedTool, byTag.field.countX() * byTag.field.countY());
        const bool ok = cutter.undercut && !profileStarted && profileEmpty && profileRefused
                && byType.undercut && byTag.refused && byTag.refusedStage == 0
                && byTag.refusedTool == 7 && byTag.field.empty();
        reportCase("stock_undercut", 8, 0.0, 0.0, ok);
    }

    // ---- замер: один проход ------------------------------------------------
    {
        const double cell = 0.5;
        const double r = 3.0;
        const gv::StockBounds bounds{-100.0, 100.0, -100.0, 100.0, -10.0, 0.0};
        const gv::StockCutter cutter = gv::stockCutter(makeTool(ToolSpec{"FLAT", 6.0}));
        gv::StockField field;
        std::vector<double> times;
        const int reps = 15;
        for (int rep = 0; rep < reps; ++rep) {
            field.reset(bounds, cell);
            const Clock::time_point t0 = Clock::now();
            field.sweep(cutter, {-25.0, 0.0, -1.0}, {25.0, 0.0, -1.0});
            times.push_back(msSince(t0));
        }
        g_sink += field.depthMm().size();
        printPhase("stock_sim_segment", median(times));
        std::printf("segment cell_mm=%.1f tool_r_mm=%.1f length_mm=50.0 positions=201 reps=%d\n",
                    cell, r, reps);
    }

    // ---- замер: весь Test_model.ngc ---------------------------------------
    std::string ngcPath;
    if (args.size() >= 2) {
        ngcPath = args[1];
    }
    std::vector<std::string> candidates;
    if (!ngcPath.empty()) {
        candidates.push_back(ngcPath);
    } else {
        candidates.push_back("data/cad-testdata/Test_model.ngc");
        // Второй путь - от каталога сборки (<репо>/build/edges): три уровня вверх.
#ifdef _WIN32
        wchar_t buf[4096];
        const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
        if (n > 0 && n < static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]))) {
            std::filesystem::path dir(std::wstring(buf, static_cast<std::size_t>(n)));
            dir = dir.parent_path().parent_path().parent_path();
            candidates.push_back(toUtf8((dir / L"data" / L"cad-testdata" / L"Test_model.ngc").wstring()));
        }
#endif
    }
    for (const std::string& candidate : candidates) {
        if (fileExists(candidate)) {
            ngcPath = candidate;
            break;
        }
    }
    const std::string text = ngcPath.empty() ? std::string() : readText(ngcPath);
    if (text.empty()) {
        std::printf("program not found: %s\n",
                    candidates.empty() ? "(no path)" : candidates.front().c_str());
        ++failures;
    } else {
        const Clock::time_point tParse = Clock::now();
        const gv::ParseResult parsed = gv::parseGcode(text);
        const double parseMs = msSince(tParse);

        // Габарит заготовки - из УП, как у рантайма (parseUiStock). Без тега
        // берётся габарит пути: замер тогда грубее жизни, и это сказано.
        gv::StockBounds bounds;
        const std::optional<std::array<double, 6>> stock = gv::parseUiStock(text);
        if (stock) {
            bounds.xMinMm = (*stock)[0];
            bounds.xMaxMm = (*stock)[1];
            bounds.yMinMm = (*stock)[2];
            bounds.yMaxMm = (*stock)[3];
            bounds.zMinMm = (*stock)[4];
            bounds.zMaxMm = (*stock)[5];
        } else {
            // Тега нет: габарит по пути, верх на 1 мм выше самой верхней точки.
            bool first = true;
            for (const gv::PathPoint& p : parsed.ordered) {
                if (first) {
                    bounds.xMinMm = bounds.xMaxMm = p.point[0];
                    bounds.yMinMm = bounds.yMaxMm = p.point[1];
                    bounds.zMinMm = bounds.zMaxMm = p.point[2];
                    first = false;
                    continue;
                }
                bounds.xMinMm = std::min(bounds.xMinMm, p.point[0]);
                bounds.xMaxMm = std::max(bounds.xMaxMm, p.point[0]);
                bounds.yMinMm = std::min(bounds.yMinMm, p.point[1]);
                bounds.yMaxMm = std::max(bounds.yMaxMm, p.point[1]);
                bounds.zMinMm = std::min(bounds.zMinMm, p.point[2]);
                bounds.zMaxMm = std::max(bounds.zMaxMm, p.point[2]);
            }
            bounds.zMaxMm += 1.0;
        }

        // Инструмент этапа: у этапа без UI_TOOL остаётся предыдущий - на станке
        // инструмент меняется только по M6.
        std::vector<gv::StockCutter> cutters;
        gv::ToolInfo carried;
        for (const gv::Stage& stage : parsed.stages) {
            if (stage.info.present) {
                carried = stage.info;
            }
            cutters.push_back(gv::stockCutter(carried));
        }
        if (cutters.empty()) {
            cutters.push_back(gv::stockCutter(gv::ToolInfo{}));
        }

        std::vector<Move> moves;
        moves.reserve(parsed.ordered.size());
        std::size_t stageIndex = 0;
        for (std::size_t i = 1; i < parsed.ordered.size(); ++i) {
            while (stageIndex + 1 < parsed.stages.size()
                   && std::size_t(parsed.stages[stageIndex + 1].index) <= i) {
                ++stageIndex;
            }
            const int cutter = int(std::min(stageIndex, cutters.size() - 1));
            Move move;
            move.aMm = parsed.ordered[i - 1].point;
            move.bMm = parsed.ordered[i].point;
            move.cutter = cutter;
            moves.push_back(move);
        }

        const double cells[] = {0.5, 0.2};
        for (double cell : cells) {
            const ProgramTiming timing = runProgram(moves, cutters, bounds, cell);            const char* suffix = (cell < 0.35) ? "0_2" : "0_5";
            char name[64];
            std::snprintf(name, sizeof(name), "stock_sim_reset_%s", suffix);
            printPhase(name, timing.resetMs);
            std::snprintf(name, sizeof(name), "stock_sim_sweep_%s", suffix);
            printPhase(name, timing.sweepMs);
            std::printf("program cell_mm=%.1f nodes=%dx%d cells=%lld field_bytes=%lld "
                        "moves=%d positions=%lld length_mm=%.1f reset_ms=%.3f sweep_ms=%.3f "
                        "total_ms=%.3f\n",
                        cell, timing.countX, timing.countY,
                        (long long)timing.countX * timing.countY,
                        (long long)timing.countX * timing.countY * 4, timing.moves,
                        timing.positions, timing.lengthMm, timing.resetMs,
                        timing.sweepMs, timing.resetMs + timing.sweepMs);

            // Точный съём той же программы: цена и память поля после этапа 2.
            const ProgramTiming exact = runProgramExact(moves, cutters, bounds, cell);
            std::snprintf(name, sizeof(name), "stock_sim_reset_exact_%s", suffix);
            printPhase(name, exact.resetMs);
            std::snprintf(name, sizeof(name), "stock_sim_sweep_exact_%s", suffix);
            printPhase(name, exact.sweepMs);
            std::printf("program_exact cell_mm=%.1f nodes=%dx%d field_bytes=%lld "
                        "moves=%lld length_mm=%.1f reset_ms=%.3f sweep_ms=%.3f total_ms=%.3f "
                        "speedup=%.1fx positions_old=%lld\n",
                        cell, exact.countX, exact.countY,
                        (long long)exact.countX * exact.countY * 4, exact.positions,
                        exact.lengthMm, exact.resetMs, exact.sweepMs,
                        exact.resetMs + exact.sweepMs,
                        timing.sweepMs > 0.0 ? timing.sweepMs / std::max(1e-9, exact.sweepMs) : 0.0,
                        timing.positions);
        }

        // Цена точной границы реза и её знакового расстояния на обоих узлах поля:
        // время и байты. Поле - тем же simulateStock (его цена в строках выше).
        const gv::PathData pricePath = gv::prepareOrdered(parsed.ordered, parsed.stages);
        for (double cell : cells) {
            const gv::ToolInfo initial = gv::parseUiTool(text);
            const gv::StockRunResult run = gv::simulateStock(pricePath, bounds, cell, initial);
            const std::vector<gv::StockCutter> plan = gv::stockCutterPlan(pricePath, initial);
            const Clock::time_point tBoundary = Clock::now();
            const gv::StockBoundaryResult boundary = gv::stockCutBoundary(
                pricePath, bounds, cell, plan, run.field.depthMm(), run.field.countX(),
                run.field.countY(), 0.05);
            const double boundaryMs = msSince(tBoundary);
            const Clock::time_point tSd = Clock::now();
            const std::vector<float> sd = gv::stockCutSignedDistance(
                bounds, cell, run.field.depthMm(), run.field.countX(), run.field.countY(),
                boundary.segments, 0.05, cell * 3.0);
            const double sdMs = msSince(tSd);
            // Цена мелкой сетки знакового расстояния (в STOCK_CUT_REFINE раз чаще
            // поля): ею фрагмент рисует цвет и линию границы (ядро,
            // stockCutChainDistance). Время - полная сборка; живой тик платит
            // столько же, потому что сетка пересчитывается целиком.
            const Clock::time_point tFine = Clock::now();
            int fineNx = 0;
            int fineNy = 0;
            const std::vector<float> fine = gv::stockCutChainDistance(
                bounds, cell, sd, run.field.countX(), run.field.countY(), boundary.segments,
                cell * 1.0, 2, &fineNx, &fineNy);
            const double fineMs = msSince(tFine);
            char name[64];
            std::snprintf(name, sizeof(name), "stock_boundary_%s",
                          (cell < 0.35) ? "0_2" : "0_5");
            printPhase(name, boundaryMs);
            std::printf("note boundary_price cell=%.1f segments=%d evaluations=%lld "
                        "unprojected=%d boundary_ms=%.1f sd_ms=%.1f boundary_bytes=%lld sd_bytes=%lld "
                        "fine_ms=%.1f fine_bytes=%lld fine_nodes=%dx%d\n",
                        cell, int(boundary.segments.size()), boundary.evaluations,
                        boundary.unprojected, boundaryMs, sdMs,
                        (long long)boundary.segments.size() * 16, (long long)sd.size() * 4,
                        fineMs, (long long)fine.size() * 4, fineNx, fineNy);

            // Сколько уступов поля Test_model отдано порогу уступа: перепад
            // соседних узлов до клетка - настоящий склон (усреднение верно),
            // (клетка, 2 клетки] - окно, которое ловит только порог в полклетки
            // (stocksurface.frag, STEP_CELLS: порог сравнивается с половинным
            // перепадом), больше двух клеток - ловят оба порога. Число нужно,
            // чтобы видеть цену ошибки: пока окно не ловилось, дно и верх у
            // каждого такого уступа получали наклон полукрутизны стенки.
            {
                const int nx = run.field.countX();
                const int ny = run.field.countY();
                const std::vector<float>& d = run.field.depthMm();
                long long window = 0;
                long long both = 0;
                long long slopes = 0;
                long long windowSteps = 0;
                const auto countPair = [&](double delta) {
                    if (delta > 2.0 * cell) {
                        ++both;
                    } else if (delta > cell) {
                        ++window;
                    } else if (delta > 1e-6) {
                        ++slopes;
                    }
                };
                // Уступ в окне, стоящий один: соседи по обе стороны плоские -
                // это и есть стенка, которую старый порог отдавал склону.
                const auto countIsolatedX = [&](int ix, int iy, double delta) {
                    if (delta <= cell || delta > 2.0 * cell) {
                        return;
                    }
                    const double left = ix > 0
                        ? std::fabs(double(d[std::size_t(iy) * nx + ix])
                                    - double(d[std::size_t(iy) * nx + ix - 1])) : 0.0;
                    const double right = ix + 2 < nx
                        ? std::fabs(double(d[std::size_t(iy) * nx + ix + 2])
                                    - double(d[std::size_t(iy) * nx + ix + 1])) : 0.0;
                    if (left < 0.1 * cell && right < 0.1 * cell) {
                        ++windowSteps;
                    }
                };
                for (int iy = 0; iy < ny; ++iy) {
                    for (int ix = 0; ix + 1 < nx; ++ix) {
                        const double delta = std::fabs(double(d[std::size_t(iy) * nx + ix + 1])
                                                       - double(d[std::size_t(iy) * nx + ix]));
                        countPair(delta);
                        countIsolatedX(ix, iy, delta);
                    }
                }
                for (int iy = 0; iy + 1 < ny; ++iy) {
                    for (int ix = 0; ix < nx; ++ix) {
                        countPair(std::fabs(double(d[std::size_t(iy + 1) * nx + ix])
                                            - double(d[std::size_t(iy) * nx + ix])));
                    }
                }
                std::printf("note step_window cell_mm=%.1f step_le_cell=%lld step_cell_2cell=%lld "
                            "step_over_2cell=%lld window_single_steps=%lld\n",
                            cell, slopes, window, both, windowSteps);
            }
        }

        // ---- живой обход: тики 10 Гц, перемотка, дайджест -------------------
        // Поле, накопленное тиками, обязано совпасть бит в бит с полем одного
        // прохода до той же длины пути, а после перемотки - с проходом с начала
        // до цели перемотки. Тик - 100 мс машинного времени: цель растёт на путь,
        // который станок проходит на запрограммированной подаче (speedAt, мм/с),
        // как на проигрывании 1:1 в QML. Сверка побитовая (FNV-1a по float32):
        // здесь важен тот же порядок накопления минимумов, а допуск в мм скрыл
        // бы разошедшийся съём.
        {
            const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
            const gv::ToolInfo initial = gv::parseUiTool(text);
            const double cell = 0.5;
            const gv::StockRunResult full = gv::simulateStock(data, bounds, cell, initial);
            const std::uint64_t digestFull = fieldDigest(full.field.depthMm());

            // Цена живого тика: граница с состоянием от 5 % длины пути и два шага
            // (0.5 % и 0.1 %). Пересчитываются только клетки с изменившимися узлами.
            {
                const gv::ToolInfo initial = gv::parseUiTool(text);
                const std::vector<gv::StockCutter> plan = gv::stockCutterPlan(data, initial);
                gv::StockWalk walk;
                if (walk.start(data, bounds, cell, plan)) {
                    walk.advanceTo(data.total * 0.05);
                    gv::StockBoundaryCache cache;
                    const gv::StockBoundaryResult first = gv::stockCutBoundary(
                        data, bounds, cell, plan, walk.field().depthMm(),
                        walk.field().countX(), walk.field().countY(), 0.05, &cache);
                    // Два шага живого тика: 0.5 % и 0.1 % длины пути. Цена обязана
                    // падать вместе с шагом - платит только новый участок.
                    walk.advanceTo(data.total * 0.055);
                    const Clock::time_point tTick = Clock::now();
                    const gv::StockBoundaryResult tick = gv::stockCutBoundary(
                        data, bounds, cell, plan, walk.field().depthMm(),
                        walk.field().countX(), walk.field().countY(), 0.05, &cache);
                    const double tickMs = msSince(tTick);
                    walk.advanceTo(data.total * 0.056);
                    const Clock::time_point tTick2 = Clock::now();
                    const gv::StockBoundaryResult tick2 = gv::stockCutBoundary(
                        data, bounds, cell, plan, walk.field().depthMm(),
                        walk.field().countX(), walk.field().countY(), 0.05, &cache);
                    const double tick2Ms = msSince(tTick2);
                    std::printf("note boundary_tick first_segments=%d tick_segments=%d "
                                "tick_ms=%.3f tick_evals=%lld small_segments=%d small_ms=%.3f "
                                "small_evals=%lld\n",
                                int(first.segments.size()), int(tick.segments.size()),
                                tickMs, tick.evaluations, int(tick2.segments.size()), tick2Ms,
                                tick2.evaluations);
                    printPhase("stock_boundary_tick_0_5", tickMs);
                    printPhase("stock_boundary_tick_small_0_5", tick2Ms);
                }
            }

            // Что поле говорит на кромке габарита: сколько узлов периметра снято,
            // насколько глубоко и остались ли нетронутые. Это те числа, с
            // которыми на кадре спорит верх стенки: снятый узел кромки - верх
            // стенки ниже верхнего габарита, и без обрезки там читается щель.
            if (!full.field.empty()) {
                const gv::StockField& f = full.field;
                int edgeCut = 0;
                int edgeTotal = 0;
                double edgeMax = 0.0;
                double edgeX = 0.0;
                double edgeY = 0.0;
                const auto noteEdge = [&](int ix, int iy) {
                    ++edgeTotal;
                    const double depth = f.bounds().zMaxMm - f.heightMm(ix, iy);
                    if (depth > 0.05) {
                        ++edgeCut;
                        if (depth > edgeMax) {
                            edgeMax = depth;
                            edgeX = f.nodeXMm(ix);
                            edgeY = f.nodeYMm(iy);
                        }
                    }
                };
                for (int ix = 0; ix < f.countX(); ++ix) {
                    noteEdge(ix, 0);
                    noteEdge(ix, f.countY() - 1);
                }
                for (int iy = 1; iy + 1 < f.countY(); ++iy) {
                    noteEdge(0, iy);
                    noteEdge(f.countX() - 1, iy);
                }
                std::printf("note stock_edge nodes=%d cut=%d max_depth_mm=%.2f at=(%.1f,%.1f) "
                            "bounds_x=%.1f..%.1f y=%.1f..%.1f\n",
                            edgeTotal, edgeCut, edgeMax, edgeX, edgeY,
                            f.bounds().xMinMm, f.bounds().xMaxMm,
                            f.bounds().yMinMm, f.bounds().yMaxMm);
            }

            // Скорость исходящего сегмента под курсором: тот же выбор сегмента,
            // что у path.speedAt в QML (верхняя граница по накопленной длине).
            const std::vector<double>& cum = data.cum;
            const auto speedAt = [&cum, &data](double d) {
                const std::size_t n = cum.size();
                if (n < 2) {
                    return 0.0;
                }
                const std::size_t i = std::size_t(
                    std::upper_bound(cum.begin(), cum.end(), d) - cum.begin());
                const std::size_t idx = i == 0 ? 0 : std::min(i - 1, n - 2);
                return idx < data.speeds.size() ? data.speeds[idx] : 0.0;
            };

            gv::StockWalk walk;
            const bool started = walk.start(data, bounds, cell,
                                            gv::stockCutterPlan(data, initial));
            std::vector<double> tickMs;
            double target = 0.0;
            while (started && target < data.total) {
                double v = speedAt(target);
                if (!(v > 0.0)) {
                    v = 10.0;              // как QML: без подачи 10 мм/с
                }
                target = std::min(data.total, target + v * 0.1);
                const Clock::time_point t0 = Clock::now();
                walk.advanceTo(target);
                tickMs.push_back(msSince(t0));
            }
            const double cursorAfterTicks = walk.cursorMm();
            const std::uint64_t digestLive = fieldDigest(walk.field().depthMm());

            // Перемотка: цель позади курсора, обход сбрасывает поле и идёт с
            // начала. Меряется вместе со сбросом, трижды, на половине программы.
            const double rewindTarget = data.total * 0.5;
            std::vector<double> rewindMs;
            std::uint64_t digestRewind = 0;
            for (int rep = 0; rep < 3; ++rep) {
                walk.advanceTo(data.total);          // поле снова полное
                const Clock::time_point t0 = Clock::now();
                walk.advanceTo(rewindTarget);
                rewindMs.push_back(msSince(t0));
                digestRewind = fieldDigest(walk.field().depthMm());
            }
            // Поле с начала до цели перемотки - отдельным обходом: с ним и
            // сверяется поле после перемотки.
            gv::StockWalk prefix;
            const bool prefixStarted = prefix.start(data, bounds, cell,
                                                    gv::stockCutterPlan(data, initial));
            prefix.advanceTo(rewindTarget);
            const std::uint64_t digestPrefix = fieldDigest(prefix.field().depthMm());

            const std::size_t fieldBytes =
                walk.field().depthMm().size() * sizeof(float);
            const double tickMax = tickMs.empty()
                ? 0.0 : *std::max_element(tickMs.begin(), tickMs.end());
            const double tickTotal = std::accumulate(tickMs.begin(), tickMs.end(), 0.0);
            const std::size_t tickBusy = std::size_t(std::count_if(
                tickMs.begin(), tickMs.end(), [](double ms) { return ms > 0.0; }));
            std::printf("note stock_live ticks=%zu tick_mean_ms=%.4f tick_median_ms=%.4f "
                        "tick_max_ms=%.3f tick_busy=%zu total_ms=%.1f cursor_mm=%.1f "
                        "total_mm=%.1f rewind_target_mm=%.1f rewind_median_ms=%.3f "
                        "rewinds=%zu\n",
                        tickMs.size(), tickMs.empty() ? 0.0 : tickTotal / double(tickMs.size()),
                        median(tickMs), tickMax, tickBusy, tickTotal, cursorAfterTicks,
                        data.total, rewindTarget, median(rewindMs), rewindMs.size());
            // Память поля: ядро, копия QByteArray для текстуры и копия в GPU -
            // три буфера, как считает DESIGN.md.
            std::printf("note stock_live field_bytes=%zu peak_bytes=%zu nodes=%dx%d "
                        "digest_full=%016llx digest_live=%016llx digest_rewind=%016llx "
                        "digest_prefix=%016llx\n",
                        fieldBytes, fieldBytes * 3, walk.field().countX(),
                        walk.field().countY(),
                        static_cast<unsigned long long>(digestFull),
                        static_cast<unsigned long long>(digestLive),
                        static_cast<unsigned long long>(digestRewind),
                        static_cast<unsigned long long>(digestPrefix));
            printPhase("stock_live_tick_0_5",
                       tickTotal / double(std::max<std::size_t>(1, tickMs.size())));
            printPhase("stock_live_rewind_0_5", median(rewindMs));
            reportCase("stock_live", int(tickMs.size()), 0.0, 0.0,
                       started && prefixStarted && digestLive == digestFull
                           && digestRewind == digestPrefix && !tickMs.empty()
                           && walk.field().countX() == full.field.countX());
        }
        std::printf("program file=%s parse_ms=%.3f stages=%zu moves=%zu\n",
                    ngcPath.c_str(), parseMs, parsed.stages.size(), moves.size());
    }

    const int caseFailures = int(std::count_if(g_cases.begin(), g_cases.end(),
                                               [](const CaseStat& c) { return !c.ok; }));
    failures += caseFailures;
    if (failures == 0) {
        std::printf("RESULT: OK\n");
        return 0;
    }
    std::printf("RESULT: FAIL (%d)\n", failures);
    return 1;
}
