// core/collision.cpp - скан коллизий оправки с заготовкой и сверка инструмента
// со стойкой (зеркало collision.py).
//
// Выражения повторяют питоновские дословно: скан сравнивается с эталоном до
// последнего разряда (test_controller_cpp.py), поэтому порядок суммирования и
// деления здесь часть результата, а не оформление.
#include "core/collision.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/tooling.h"
#include "core/ui_tags.h"

namespace gv {

namespace {

// Тексты сообщений оператору - байт в байт как у эталона (UTF-8, escape'ами:
// слой ядра пишет русские строки так же, как ui_tags). Комментарий рядом -
// читаемый текст.
const char *const MSG_MISSING =
    "\x54\x25\x64\x20\xD0\xBD\xD0\xB5\xD1\x82\x20\xD0\xB2\x20\xD1\x82\xD0\xB0\xD0\xB1\xD0\xBB"
    "\xD0\xB8\xD1\x86\xD0\xB5\x20\xD1\x81\xD1\x82\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB8";   // "T%d нет в таблице стойки"
const char *const MSG_DISABLED =
    "\x54\x25\x64\x20\xD0\xBE\xD1\x82\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD1\x91\xD0\xBD\x20"
    "\xD0\xB2\x20\xD1\x81\xD1\x82\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB5";                    // "T%d отключён в стойке"
const char *const MSG_SHORT_NC =
    "\xD0\x92\xD1\x8B\xD0\xBB\xD0\xB5\xD1\x82\x20\x54\x25\x64\x20\x28\x25\x2E\x31\x66\x29\x20"
    "\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8C\xD1\x88\xD0\xB5\x20\xD0\xB7\xD0\xB0\xD0\xB4\xD0\xB0"
    "\xD0\xBD\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\x20\xD0\xB2\x20\xD0\xA3\xD0\x9F\x20"
    "\x28\x25\x2E\x31\x66\x29";                     // "Вылет T%d (%.1f) меньше заданного в УП (%.1f)"
const char *const MSG_SHORT_DEPTH =
    "\xD0\x92\xD1\x8B\xD0\xBB\xD0\xB5\xD1\x82\x20\x54\x25\x64\x20\x28\x25\x2E\x31\x66\x29\x20"
    "\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8C\xD1\x88\xD0\xB5\x20\xD0\xB3\xD0\xBB\xD1\x83\xD0\xB1"
    "\xD0\xB8\xD0\xBD\xD1\x8B\x20\xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82"
    "\xD0\xBA\xD0\xB8\x20\x28\x25\x2E\x31\x66\x29"; // "Вылет T%d (%.1f) меньше глубины обработки (%.1f)"
const char *const MSG_LIMIT =
    "\x54\x25\x64\x20\xD0\xB4\xD0\xBB\xD0\xB8\xD0\xBD\xD0\xBD\xD0\xB5\xD0\xB5\x20\xD0\xBD"
    "\xD0\xB0\x20\x25\x2E\x31\x66\x20\xD0\xBC\xD0\xBC\x20\xE2\x80\x94\x20\xD0\xB2\xD1\x8B"
    "\xD1\x85\xD0\xBE\xD0\xB4\x20\xD0\xB7\xD0\xB0\x20\xD0\xBB\xD0\xB8\xD0\xBC\xD0\xB8\xD1\x82"
    "\x20\xD0\xBE\xD1\x81\xD0\xB8\x20\x5A";         // "T%d длиннее на %.1f мм — выход за лимит оси Z"

// Строка сообщения из шаблона: snprintf считает по C-локали, но разделитель
// дробной части приводится к точке - оператор видит тот же текст, что в Python.
std::string formatMessage(const char *pattern, int number, double first, bool has_second,
                          double second)
{
    char buf[256];
    if (has_second) {
        std::snprintf(buf, sizeof(buf), pattern, number, first, second);
    } else {
        std::snprintf(buf, sizeof(buf), pattern, number, first);
    }
    for (char& c : buf) {
        if (c == ',') {
            c = '.';
        }
    }
    return std::string(buf);
}

std::string messageOfNumber(const char *pattern, int number)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), pattern, number);
    return std::string(buf);
}

// Индекс этапа, которому принадлежит длина пути d: последний этап с началом <= d.
int stageAt(const std::vector<double>& starts, double d)
{
    int lo = 0;
    int hi = int(starts.size()) - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (starts[std::size_t(mid)] <= d) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

} // namespace

double collisionStep()
{
    // Читается один раз: в Python это значение на момент импорта модуля.
    static const double step = [] {
        const char* raw = std::getenv("CNC_COLLISION_STEP");
        if (raw == nullptr || *raw == '\0') {
            return 1.0;
        }
        char* end = nullptr;
        const double value = std::strtod(raw, &end);
        if (end == raw || (end != nullptr && *end != '\0')) {
            return 1.0;
        }
        return value;
    }();
    return step;
}

bool circleRectHit(double cx, double cy, double r,
                   double xmin, double xmax, double ymin, double ymax)
{
    const double dx = cx - std::min(std::max(cx, xmin), xmax);
    const double dy = cy - std::min(std::max(cy, ymin), ymax);
    return dx * dx + dy * dy <= r * r;
}

std::vector<CollisionRange> scanCollisions(const std::vector<double>& pts,
                                           const std::vector<double>& cum,
                                           double total,
                                           const std::vector<double>& stage_starts,
                                           const std::vector<double>& stage_len,
                                           const std::array<double, 6>& stock,
                                           double step)
{
    if (pts.empty() || cum.empty() || total <= 0.0) {
        return {};
    }
    const double xmin = stock[0];
    const double xmax = stock[1];
    const double ymin = stock[2];
    const double ymax = stock[3];
    const double zmin = stock[4];
    const double zmax = stock[5];
    const int n = int(cum.size());
    if (n < 2 || stage_starts.empty()) {
        return {};
    }
    std::vector<std::vector<HolderSection>> holders;
    holders.reserve(stage_len.size());
    for (double length : stage_len) {
        holders.push_back(holderProfile(length));
    }

    std::vector<CollisionRange> out;
    bool has_cur = false;
    CollisionRange cur;
    int idx = 1;                       // движущийся указатель по cum (d растёт)
    double d = 0.0;
    while (d <= total) {
        while (idx < n - 1 && cum[std::size_t(idx)] < d) {
            ++idx;
        }
        const double d0 = cum[std::size_t(idx - 1)];
        const double d1 = cum[std::size_t(idx)];
        const double t = d1 <= d0 ? 0.0 : (d - d0) / (d1 - d0);
        const double* p0 = &pts[std::size_t(idx - 1) * 3];
        const double* p1 = &pts[std::size_t(idx) * 3];
        const double sx = p0[0] + (p1[0] - p0[0]) * t;
        const double sy = p0[1] + (p1[1] - p0[1]) * t;
        const double sz = p0[2] + (p1[2] - p0[2]) * t;
        const double x = sx;           // машинный X
        const double y = -sz;          // машинный Y (сцена z = -машинный Y)
        const double z = sy;           // машинный Z (сцена y = машинный Z)
        const int si = stageAt(stage_starts, d);
        const double L = si < int(stage_len.size()) ? stage_len[std::size_t(si)] : 0.0;
        double worst = 0.0;
        if (si < int(holders.size())) {
            for (const HolderSection& sec : holders[std::size_t(si)]) {
                const double hb = z + sec.z0_mm;      // низ секции, машинный Z
                if (hb >= zmax) {                     // секция выше заготовки
                    continue;
                }
                const double ht = z + sec.z1_mm;
                if (ht <= zmin) {                     // секция ниже заготовки
                    continue;
                }
                if (!circleRectHit(x, y, sec.radius_mm, xmin, xmax, ymin, ymax)) {
                    continue;
                }
                const double pen = std::min(ht, zmax) - std::max(hb, zmin);
                if (pen > worst) {
                    worst = pen;
                }
            }
        }
        if (worst > 0.05) {
            if (!has_cur) {
                cur = CollisionRange{d, worst, {sx, sy, sz}, {sx, sy + L, sz}};
                has_cur = true;
            } else {
                cur.depth = std::max(cur.depth, worst);
            }
        } else if (has_cur) {
            out.push_back(cur);
            has_cur = false;
        }
        d += step;
    }
    if (has_cur) {
        out.push_back(cur);
    }
    return out;
}

std::vector<ToolCheckRow> toolCheckReport(const std::vector<ToolInfo>& stage_tools,
                                          const std::vector<StageRange>& stages,
                                          const std::vector<double>& pts,
                                          const std::vector<double>& cum,
                                          double total,
                                          const std::map<int, MachineTool>& machine_tools,
                                          const MachineLimits& limits)
{
    if (stages.empty() || pts.empty() || cum.empty() || total <= 0.0) {
        return {};
    }
    if (machine_tools.empty()) {
        return {};                     // стойка не подключена - сверять нечего
    }
    const int n = int(cum.size());
    std::vector<double> starts(stages.size());
    for (std::size_t i = 0; i < stages.size(); ++i) {
        starts[i] = stages[i].start * total;
    }
    std::vector<double> z_min(stages.size(), 0.0);
    std::vector<bool> has_z(stages.size(), false);
    bool has_zmax_nc = false;
    double zmax_nc = 0.0;
    int j = 0;
    for (int i = 0; i < n; ++i) {
        const double d = cum[std::size_t(i)];
        while (j + 1 < int(starts.size()) && starts[std::size_t(j + 1)] <= d) {
            ++j;
        }
        const double z = pts[std::size_t(i) * 3 + 1];      // сцена y = машинный Z
        if (!has_z[std::size_t(j)] || z < z_min[std::size_t(j)]) {
            z_min[std::size_t(j)] = z;
            has_z[std::size_t(j)] = true;
        }
        if (!has_zmax_nc || z > zmax_nc) {
            zmax_nc = z;
            has_zmax_nc = true;
        }
    }

    struct ToolEntry {
        int number = 0;
        std::string name;
        double nc_length = 0.0;
        double z_min = 0.0;
        bool has_z = false;
    };
    std::map<int, ToolEntry> by_tool;
    for (std::size_t k = 0; k < stages.size(); ++k) {
        const ToolInfo info = k < stage_tools.size() ? stage_tools[k] : ToolInfo{};
        const int num = info.present ? info.number : 0;
        const double nc_len = info.present ? info.overall_length : 0.0;
        const std::string name = info.present ? stageLabel(info) : stages[k].label;
        const bool has = has_z[k];
        const double z = z_min[k];
        auto it = by_tool.find(num);
        if (it == by_tool.end()) {
            ToolEntry entry;
            entry.number = num;
            entry.name = name;
            entry.nc_length = nc_len;
            entry.z_min = z;
            entry.has_z = has;
            by_tool.emplace(num, entry);
            continue;
        }
        ToolEntry& entry = it->second;
        if (has && (!entry.has_z || z < entry.z_min)) {
            entry.z_min = z;
            entry.has_z = true;
        }
        if (entry.nc_length == 0.0 && nc_len != 0.0) {
            entry.nc_length = nc_len;
        }
    }

    const double zmax_lim = limits.present ? limits.z_max : 0.0;
    std::vector<ToolCheckRow> report;
    report.reserve(by_tool.size());
    for (const auto& pair : by_tool) {          // ключи отсортированы номером
        const int num = pair.first;
        const ToolEntry& entry = pair.second;
        const double depth = entry.has_z ? std::fabs(entry.z_min) : 0.0;
        ToolCheckRow row;
        row.number = num;
        row.name = entry.name;
        row.nc_length = pyRound2(entry.nc_length);
        row.depth = pyRound2(depth);
        row.status = "unknown";
        const auto mt = machine_tools.find(num);
        if (mt == machine_tools.end()) {
            row.status = "missing";
            row.message = messageOfNumber(MSG_MISSING, num);
        } else {
            const double ml = mt->second.length;
            row.has_machine_length = true;
            row.machine_length = pyRound2(ml);
            row.has_delta = true;
            row.delta = pyRound2(ml - entry.nc_length);
            if (!mt->second.enabled) {
                row.status = "missing";
                row.message = messageOfNumber(MSG_DISABLED, num);
            } else if (ml + 1e-6 < entry.nc_length) {
                row.status = "short";
                row.message = formatMessage(MSG_SHORT_NC, num, ml, true, entry.nc_length);
            } else if (depth > 0.0 && ml + 1e-6 < depth) {
                row.status = "short";
                row.message = formatMessage(MSG_SHORT_DEPTH, num, ml, true, depth);
            } else {
                row.status = "ok";
                row.message = "OK";
                const double delta = ml - entry.nc_length;
                if (limits.present && delta > 1e-6 && has_zmax_nc
                        && zmax_nc + delta > zmax_lim + 1e-6) {
                    row.status = "limit";
                    row.message = formatMessage(MSG_LIMIT, num, delta, false, 0.0);
                }
            }
        }
        report.push_back(row);
    }
    return report;
}

} // namespace gv
