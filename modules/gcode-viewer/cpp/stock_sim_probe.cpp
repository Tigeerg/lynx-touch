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
// 2. Замер. Время одного прохода длиной 50 мм и время всего Test_model.ngc
// (все ходы, включая быстрые, инструмент этапа, габарит из UI_STOCK) при узле
// 0.5 и 0.2 мм - это предложения пресетов из DESIGN.md. Печатаются строки
// "phase <имя> <мс>", как у bench_probe; разбор УП в замер не входит.
//
// Запуск: stock_sim_probe [файл.ngc]
//   без аргумента берётся data/cad-testdata/Test_model.ngc от текущего каталога
//   или от каталога сборки.
// Код возврата: 0 - все случаи сошлись; 1 - есть расхождение или УП не найдена.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    //    инструмент. Проход 4 мм по X, узел 0.5, шаг 0.25.
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
    //    сфера. Профиль инструмента - 12 отрезков (core/tooling.cpp), поэтому
    //    допуск = сагитта хорды 7.5 градуса, усиленная на 1.5 у стенки, где
    //    уклон сферы наибольший в срезанной зоне (срез не глубже 1 мм -> не
    //    круче 48 градусов, 1/cos = 1.5).
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
    //    долина с прямыми стенками. Профиль CHAMFER с TAPER=45 даёт ровно это.
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
    //    одном узле.
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
    //    (r = 0.5), проход по X на y = 0.3 от x = 0.5 до 3.5. Колонка (1, 0)
    //    отстоит от линии на 0.3 < r и попадает ровно на положение шага 0.5 мм;
    //    при шаге в целый узел положения 0.5, 1.5, 2.5, 3.5 дают 0.583 > r, и
    //    колонка осталась бы на верху.
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
            std::printf("program cell_mm=%.1f nodes=%dx%d cells=%lld moves=%d "
                        "positions=%lld length_mm=%.1f reset_ms=%.3f sweep_ms=%.3f total_ms=%.3f\n",
                        cell, timing.countX, timing.countY,
                        (long long)timing.countX * timing.countY, timing.moves,
                        timing.positions, timing.lengthMm, timing.resetMs,
                        timing.sweepMs, timing.resetMs + timing.sweepMs);
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
