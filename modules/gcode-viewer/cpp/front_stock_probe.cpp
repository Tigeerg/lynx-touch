// cpp/front_stock_probe.cpp - пробник хроники реза (PLAN-STOCK-TIMEFIELD.md,
// этап 1). Отдельная цель CMake над gvcore, как stock_sim_probe.
//
// 1. Точность. Хроника строится точным съёмом (core/stock_sim, sweepExact),
// поэтому поле на конец программы обязано совпасть с аналитикой: плоская
// (постоянная глубина по всему следу, старой полосы cell / 4 нет), шар
// (истинная сфера, а не 12 хорд) и конус (V-бита). Случай front_match сверяет
// поле хроники с полем simulateStock: у них один и тот же точный обход.
//
// 2. Хроника. Поле на момент t - порог по метке времени: узел, углублённый
// позже t, нетронут. Случай front_rewind сверяет поле-порог с обходом с начала
// до той же длины пути (дайджест) и меряет обе цены: перемотка - смена порога,
// обход с начала - полный пересчёт. Случай front_determinism держит побитовый
// дайджест двух прогонов.
//
// 3. Цена метки. На узел хранится одна метка времени (последнее углубление).
// Случай front_repeated_cut считает узлы, углублённые больше одного раза, - на
// них ранние моменты показывают итоговую глубину с последнего реза, а не
// промежуточную. Числа считаются на синтетической двухпроходной программе и,
// если найдена, на data/cad-testdata/Test_model.ngc.
//
// Запуск: front_stock_probe [файл.ngc]
// Код возврата: 0 - все случаи сошлись; 1 - есть расхождение.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "core/gcode_parser.h"
#include "core/path_prep.h"
#include "core/stock_front.h"
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

volatile std::size_t g_sink = 0;

double median(std::vector<double> v)
{
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void printPhase(const char* name, double ms)
{
    std::printf("phase %-26s %9.3f\n", name, ms);
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
    std::printf("case %-16s checked=%6d max_dev_mm=%.3e tol_mm=%.1e %s\n",
                name, checked, maxDevMm, tolMm, stat.ok ? "OK" : "FAIL");
}

std::uint64_t digest(const std::vector<float>& values)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (float value : values) {
        unsigned char bytes[sizeof(float)];
        std::memcpy(bytes, &value, sizeof(float));
        for (unsigned char byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

double segmentDistanceMm(double px, double py, double ax, double ay, double bx, double by)
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

double ballHeightMm(double radiusMm, double rMm, double tipZMm)
{
    if (radiusMm >= rMm) {
        return 0.0;
    }
    return std::min(0.0, tipZMm + rMm - std::sqrt(rMm * rMm - radiusMm * radiusMm));
}

double coneHeightMm(double radiusMm, double tipZMm)
{
    return std::min(0.0, tipZMm + radiusMm);
}

struct ToolSpec {
    const char* type = "FLAT";
    double diameterMm = 0.0;
    double taperDeg = 0.0;
};

gv::ToolInfo makeTool(const ToolSpec& spec)
{
    gv::ToolInfo info;
    info.present = true;
    info.type = spec.type;
    info.diameter = spec.diameterMm;
    info.taper_deg = spec.taperDeg;
    info.flute_length = 10.0;
    info.shoulder_length = 10.0;
    info.body_length = 20.0;
    info.overall_length = 20.0;
    info.shank_diameter = spec.diameterMm;
    return info;
}

std::string readText(const std::string& utf8Path)
{
    std::ifstream fh(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!fh) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());
}

gv::StockBounds boxBounds()
{
    gv::StockBounds bounds;
    bounds.xMinMm = -5.0;
    bounds.xMaxMm = 5.0;
    bounds.yMinMm = -5.0;
    bounds.yMaxMm = 5.0;
    bounds.zMinMm = -10.0;
    bounds.zMaxMm = 0.0;
    return bounds;
}

} // namespace

int main(int argc, char** argv)
{
    int failures = 0;
    const double cell = 0.5;

    // 1. Плоская фреза: рез постоянен по всему следу, без полосы cell / 4.
    {
        const std::string program =
            "G21 G90\n"
            "G0 X-2 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X2 F100\n"
            "G0 Z5\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        gv::StockChronicle chronicle;
        const bool built = chronicle.build(data, boxBounds(), cell, makeTool(ToolSpec{"FLAT", 6.0}));

        const double r = 3.0;
        const double tipZ = -1.0;
        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < chronicle.field().countY(); ++iy) {
            for (int ix = 0; ix < chronicle.field().countX(); ++ix) {
                const double x = chronicle.field().nodeXMm(ix);
                const double y = chronicle.field().nodeYMm(iy);
                const double d = segmentDistanceMm(x, y, -2.0, 0.0, 2.0, 0.0);
                if (std::fabs(d - r) <= 1e-9) {
                    continue;
                }
                const double analytic = (d <= r) ? tipZ : 0.0;
                maxDev = std::max(maxDev, std::fabs(chronicle.heightAt(data.total, ix, iy) - analytic));
                ++checked;
            }
        }
        reportCase("front_flat", checked, maxDev, 1e-6, built);
    }

    // 2. Шар: истинная сфера, а не 12 хорд таблицы.
    {
        const std::string program =
            "G21 G90\n"
            "G0 X0 Y0 Z5\n"
            "G1 Z-1 F100\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        gv::StockChronicle chronicle;
        const bool built = chronicle.build(data, boxBounds(), cell, makeTool(ToolSpec{"BALL", 6.0}));
        const double r = 3.0;
        const double tipZ = -1.0;
        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < chronicle.field().countY(); ++iy) {
            for (int ix = 0; ix < chronicle.field().countX(); ++ix) {
                const double s = std::hypot(chronicle.field().nodeXMm(ix),
                                            chronicle.field().nodeYMm(iy));
                maxDev = std::max(maxDev, std::fabs(chronicle.heightAt(data.total, ix, iy)
                                                    - ballHeightMm(s, r, tipZ)));
                ++checked;
            }
        }
        reportCase("front_ball", checked, maxDev, 1e-6, built);
    }

    // 3. Конус (V-бита): долина с прямыми стенками.
    {
        const std::string program =
            "G21 G90\n"
            "G0 X0 Y0 Z5\n"
            "G1 Z-1 F100\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        gv::StockChronicle chronicle;
        const bool built = chronicle.build(data, boxBounds(), cell,
                                           makeTool(ToolSpec{"CHAMFER", 6.0, 45.0}));
        const double tipZ = -1.0;
        int checked = 0;
        double maxDev = 0.0;
        for (int iy = 0; iy < chronicle.field().countY(); ++iy) {
            for (int ix = 0; ix < chronicle.field().countX(); ++ix) {
                const double s = std::hypot(chronicle.field().nodeXMm(ix),
                                            chronicle.field().nodeYMm(iy));
                maxDev = std::max(maxDev, std::fabs(chronicle.heightAt(data.total, ix, iy)
                                                    - coneHeightMm(s, tipZ)));
                ++checked;
            }
        }
        reportCase("front_cone", checked, maxDev, 1e-6, built);
    }

    // 4. Поле хроники на конец программы - то же поле, что у simulateStock:
    // оба идут одним точным обходом.
    {
        const std::string program =
            "G21 G90\n"
            "G0 X-3 Y1 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X3 F100\n"
            "G1 Y-1 F100\n"
            "G1 X-3 F100\n"
            "G0 Z5\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        const gv::ToolInfo tool = makeTool(ToolSpec{"FLAT", 6.0});
        gv::StockChronicle chronicle;
        const bool built = chronicle.build(data, boxBounds(), cell, tool);
        const gv::StockRunResult run = gv::simulateStock(data, boxBounds(), cell, tool);
        const std::uint64_t digestChronicle = digest(chronicle.viewDepthMm(data.total));
        const std::uint64_t digestRun = digest(run.field.depthMm());
        std::printf("note front_match digest_chronicle=%016llx digest_run=%016llx\n",
                    static_cast<unsigned long long>(digestChronicle),
                    static_cast<unsigned long long>(digestRun));
        reportCase("front_match", run.field.countX(), 0.0, 0.0,
                   built && digestChronicle == digestRun);
    }

    // 5. Перемотка как порог на программе, где каждый узел режется один раз: поле
    // на момент t обязано совпасть с обходом с начала до той же длины пути.
    // Меряются обе цены: смена порога и пересчёт.
    {
        const std::string program =
            "G21 G90\n"
            "G0 X-2 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X2 F100\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        const gv::ToolInfo tool = makeTool(ToolSpec{"FLAT", 6.0});

        gv::StockChronicle full;
        const bool builtFull = full.build(data, boxBounds(), cell, tool);
        const double rewindTarget = data.total * 0.5;
        gv::StockChronicle prefix;
        const bool builtPrefix = prefix.build(data, boxBounds(), cell, tool, rewindTarget);

        const std::uint64_t digestView = digest(full.viewDepthMm(rewindTarget));
        const std::uint64_t digestPrefix = digest(prefix.field().depthMm());

        std::vector<double> viewMs;
        std::vector<double> rebuildMs;
        for (int rep = 0; rep < 9; ++rep) {
            Clock::time_point t0 = Clock::now();
            g_sink += full.viewDepthMm(rewindTarget).size();
            viewMs.push_back(msSince(t0));
            gv::StockChronicle again;
            t0 = Clock::now();
            again.build(data, boxBounds(), cell, tool, rewindTarget);
            rebuildMs.push_back(msSince(t0));
        }
        std::printf("note front_rewind target_mm=%.1f total_mm=%.1f nodes=%d "
                    "view_median_ms=%.4f rebuild_median_ms=%.3f repeated_cut_nodes=%lld\n",
                    rewindTarget, data.total, full.field().countX() * full.field().countY(),
                    median(viewMs), median(rebuildMs), full.repeatedCutNodes());
        printPhase("front_rewind_view", median(viewMs));
        printPhase("front_rewind_rebuild", median(rebuildMs));
        const bool ok = builtFull && builtPrefix && digestView == digestPrefix
                && full.repeatedCutNodes() == 0;
        reportCase("front_rewind", 2, 0.0, 0.0, ok);
    }

    // 5б. Граница одномерной метки: программа в два прохода (черновая на -1,
    // чистовая на -2). Узел, углублённый до t снова после t, в поле-пороге
    // показывается нетронутым (метка ушла за порог), а обход с начала знает его
    // промежуточную глубину. Расхождение названо числом: узлы без второго реза
    // обязаны совпасть, узлы со вторым - разойтись, и все разошедшиеся обязаны
    // быть узлами со вторым резом. Точный проигрыш требует журнала вкладов
    // (PLAN-STOCK.md 2.4), в этап 1 не входит.
    {
        const std::string program =
            "G21 G90\n"
            "G0 X-2 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X2 F100\n"
            "G1 Z-2 F100\n"
            "G1 X-2 F100\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        const gv::ToolInfo tool = makeTool(ToolSpec{"FLAT", 6.0});
        gv::StockChronicle full;
        const bool builtFull = full.build(data, boxBounds(), cell, tool);
        // Порог берётся перед последним ходом: обход с начала применяет все ходы,
        // кроме последнего, а поле-порог гасит узлы, углублённые последним ходом,
        // - то есть ровно узлы со вторым резом.
        const double rewindTarget = std::max(0.0, data.total - 1.0);
        gv::StockChronicle prefix;
        const bool builtPrefix = prefix.build(data, boxBounds(), cell, tool, rewindTarget);

        const std::vector<float>& view = full.viewDepthMm(rewindTarget);
        const std::vector<float>& events = full.eventMm();
        int agree = 0;
        int differ = 0;
        int differAtSecondCut = 0;
        for (std::size_t i = 0; i < view.size(); ++i) {
            const float expected = prefix.field().depthMm()[i];
            if (view[i] == expected) {
                ++agree;
                continue;
            }
            ++differ;
            if (view[i] == 0.0f && expected > 0.0f && events[i] > float(rewindTarget)) {
                ++differAtSecondCut;
            }
        }
        std::printf("note front_rewind_shared target_mm=%.1f repeated_cut_nodes=%lld "
                    "agree=%d differ=%d differ_at_second_cut=%d nodes=%d\n",
                    rewindTarget, full.repeatedCutNodes(), agree, differ,
                    differAtSecondCut, int(view.size()));
        const bool ok = builtFull && builtPrefix && differ > 0
                && differ == differAtSecondCut
                && differ <= int(full.repeatedCutNodes());
        reportCase("front_rewind_shared", agree + differ, 0.0, 0.0, ok);
    }

    // 6. Детерминизм: два прогона одной программы дают одинаковую хронику
    // (и глубины, и метки времени).
    {
        const std::string program =
            "G21 G90\n"
            "G0 X-2 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X2 F100\n"
            "G1 Z-2 F100\n"
            "G1 X-2 F100\n"
            "G0 Z5\n";
        const gv::ParseResult parsed = gv::parseGcode(program);
        const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
        const gv::ToolInfo tool = makeTool(ToolSpec{"FLAT", 6.0});
        gv::StockChronicle first;
        gv::StockChronicle second;
        first.build(data, boxBounds(), cell, tool);
        second.build(data, boxBounds(), cell, tool);
        const std::uint64_t digestFirst = digest(first.eventMm());
        const std::uint64_t digestSecond = digest(second.eventMm());
        std::printf("note front_determinism digest_a=%016llx digest_b=%016llx\n",
                    static_cast<unsigned long long>(digestFirst),
                    static_cast<unsigned long long>(digestSecond));
        reportCase("front_determinism", int(first.eventMm().size()), 0.0, 0.0,
                   digestFirst == digestSecond
                       && digest(first.field().depthMm()) == digest(second.field().depthMm()));
    }

    // 7. Цена метки времени на реальной программе: доля узлов, углублённых
    // больше одного раза. Она и есть граница "поле на момент t точное".
    {
        std::string ngcPath;
        if (argc >= 2) {
            ngcPath = argv[1];
        }
        std::vector<std::string> candidates;
        if (!ngcPath.empty()) {
            candidates.push_back(ngcPath);
        } else {
            candidates.push_back("data/cad-testdata/Test_model.ngc");
            candidates.push_back("../data/cad-testdata/Test_model.ngc");
            candidates.push_back("../../data/cad-testdata/Test_model.ngc");
        }
        for (const std::string& candidate : candidates) {
            if (!readText(candidate).empty()) {
                ngcPath = candidate;
                break;
            }
        }
        const std::string text = ngcPath.empty() ? std::string() : readText(ngcPath);
        if (text.empty()) {
            std::printf("note front_repeated_cut program not found: %s\n",
                        candidates.front().c_str());
        } else {
            const gv::ParseResult parsed = gv::parseGcode(text);
            const gv::PathData data = gv::prepareOrdered(parsed.ordered, parsed.stages);
            gv::StockBounds bounds;
            std::array<double, 6> stock{};
            if (gv::parseUiStock(text)) {
                stock = *gv::parseUiStock(text);
                bounds.xMinMm = stock[0];
                bounds.xMaxMm = stock[1];
                bounds.yMinMm = stock[2];
                bounds.yMaxMm = stock[3];
                bounds.zMinMm = stock[4];
                bounds.zMaxMm = stock[5];
            }
            const gv::ToolInfo initial = gv::parseUiTool(text);
            const double programCell = 0.5;
            gv::StockChronicle chronicle;
            const bool built = chronicle.build(data, bounds, programCell, initial);
            long long cut = 0;
            const std::vector<float>& events = chronicle.eventMm();
            for (float e : events) {
                if (e > 0.0f) {
                    ++cut;
                }
            }
            const long long repeated = chronicle.repeatedCutNodes();
            const double share = cut > 0 ? double(repeated) / double(cut) : 0.0;
            std::printf("note front_repeated_cut moves=%d nodes=%d cut_nodes=%lld "
                        "repeated=%lld share=%.3f build_ms=%.1f length_mm=%.1f\n",
                        chronicle.moves(), chronicle.field().countX() * chronicle.field().countY(),
                        cut, repeated, share, chronicle.buildMs(), chronicle.lengthMm());
            printPhase("front_build_0_5", chronicle.buildMs());

            // Цена перемотки на реальной программе: смена порога против обхода
            // с начала до той же длины пути (нынешний пересчёт префикса).
            const double rewindTarget = data.total * 0.5;
            std::vector<double> viewMs;
            std::vector<double> rebuildMs;
            for (int rep = 0; rep < 9; ++rep) {
                Clock::time_point t0 = Clock::now();
                g_sink += chronicle.viewDepthMm(rewindTarget).size();
                viewMs.push_back(msSince(t0));
                gv::StockChronicle again;
                t0 = Clock::now();
                again.build(data, bounds, programCell, initial, rewindTarget);
                rebuildMs.push_back(msSince(t0));
            }
            std::printf("note front_rewind_program target_mm=%.1f view_median_ms=%.4f "
                        "rebuild_median_ms=%.3f speedup=%.0fx\n",
                        rewindTarget, median(viewMs), median(rebuildMs),
                        median(viewMs) > 0.0 ? median(rebuildMs) / median(viewMs) : 0.0);
            printPhase("front_rewind_view_0_5", median(viewMs));
            printPhase("front_rewind_rebuild_0_5", median(rebuildMs));

            reportCase("front_repeated_cut", int(cut), 0.0, 0.0, built && cut > 0);
        }
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