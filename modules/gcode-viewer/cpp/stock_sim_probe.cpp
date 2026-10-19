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
// стенки). Поле во всех трёх случаях одно и то же: рисование его не трогает.
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
#include <numeric>
#include <optional>
#include <string>
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

        // Наклон по X в подузле, как его считает шейдер: разности соседних узлов
        // ловят уступ, на клетке со уступом наклон - собственный градиент клетки,
        // иначе - центральная разность через 2 * NORMAL_SPAN клетки. Здесь
        // повторена только ось X и только те точки, где ответ различает правила.
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
            const double r1 = sample(u + du, v);
            const double l1 = sample(u - du, v);
            const double r2 = sample(u + 2.0 * du, v);
            const double l2 = sample(u - 2.0 * du, v);
            const double span = 4.0 * cell;               // 2 * NORMAL_SPAN клетки
            const bool jr = std::max(std::fabs(r1 - depth), std::fabs(r2 - r1)) > stepMm;
            const bool jl = std::max(std::fabs(depth - l1), std::fabs(l1 - l2)) > stepMm;
            if (!jr && !jl)
                return (r2 - l2) / span;
            const double p = u * double(nx) - 0.5;
            const double q = v * double(ny) - 0.5;
            const double fy = q - std::floor(q);
            const int ix = int(std::floor(p));
            const int iy = int(std::floor(q));
            const double g = (nodeDepth(ix + 1, iy) - nodeDepth(ix, iy)) * (1.0 - fy)
                           + (nodeDepth(ix + 1, iy + 1) - nodeDepth(ix, iy + 1)) * fy;
            return g / cell;
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
            const ProgramTiming timing = runProgram(moves, cutters, bounds, cell);
            const char* suffix = (cell < 0.35) ? "0_2" : "0_5";
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
