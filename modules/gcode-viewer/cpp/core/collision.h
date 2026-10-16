// core/collision.h - коллизии оправки с заготовкой и сверка инструмента со
// стойкой (зеркало collision.py).
//
// Шаг 5.4в плана переноса. Скан идёт вдоль выборки пути с шагом `collisionStep`
// (мм): на каждом шаге секции оправки (коллизия их берёт из core/tooling)
// проверяются на пересечение с габаритом заготовки. Порог глубины 0.05 мм
// отсекает касание грани: без него резание по верхней плоскости заготовки
// давало бы ложные срабатывания.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "core/path_prep.h"     // gv::StageRange
#include "gv_types.h"

namespace gv {

// Шаг выборки вдоль пути при скане коллизий, мм (CNC_COLLISION_STEP).
double collisionStep();

// Истина, когда круг (cx, cy, r) перекрывает прямоугольник XY. Касание
// считается попаданием (`<=`), и граница важна: она решает, будет ли найдена
// коллизия у самой стенки заготовки.
bool circleRectHit(double cx, double cy, double r,
                   double xmin, double xmax, double ymin, double ymax);

// Найденный диапазон коллизии: длина пути до представительной точки, глубина
// проникновения (мм) и сама точка в координатах сцены; `scene_hb` - низ оправки
// в той же точке (маркер в сцене садится на него).
struct CollisionRange {
    double d = 0.0;
    double depth = 0.0;
    std::array<double, 3> scene{0.0, 0.0, 0.0};
    std::array<double, 3> scene_hb{0.0, 0.0, 0.0};
};

// Скан коллизий оправки о заготовку вдоль пути.
//
// `pts`/`cum` - выборка пути в координатах сцены (X*3 на точку) и накопленная
// длина; `stage_starts` - начало каждого этапа по длине пути; `stage_len[i]` -
// вылет инструмента этапа i; `stock` - (xmin, xmax, ymin, ymax, zmin, zmax)
// в машинных координатах. Диапазоны, идущие подряд, сливаются в один.
std::vector<CollisionRange> scanCollisions(const std::vector<double>& pts,
                                           const std::vector<double>& cum,
                                           double total,
                                           const std::vector<double>& stage_starts,
                                           const std::vector<double>& stage_len,
                                           const std::array<double, 6>& stock,
                                           double step);

// Измеренный инструмент из таблицы стойки: вылет (длина коррекции, мм), имя и
// признак "включён". Ключ - номер инструмента.
struct MachineTool {
    double length = 0.0;
    std::string name;
    bool enabled = true;
};

// Пределы осей стойки: `present` - карта была непустой (у эталона пустой dict
// от непустого отличает именно истинность).
struct MachineLimits {
    bool present = false;
    double z_max = 0.0;
};

// Строка отчёта сверки: номер инструмента, подпись, длина из УП и минимальный Z
// обработки, измеренная длина стойки и их разница (поля `has_*` отвечают за
// None у эталона), состояние и сообщение оператору.
struct ToolCheckRow {
    int number = 0;
    std::string name;
    double nc_length = 0.0;
    double depth = 0.0;
    bool has_machine_length = false;
    double machine_length = 0.0;
    bool has_delta = false;
    double delta = 0.0;
    std::string status;                  // ok / missing / short / limit / unknown
    std::string message;
};

// Сверка каждого инструмента УП с измеренной таблицей стойки: есть ли номер в
// таблице и не короче ли вылет стойки того, что предполагает программа.
// Пустая таблица - стойка не подключена, сверять нечего.
std::vector<ToolCheckRow> toolCheckReport(const std::vector<ToolInfo>& stage_tools,
                                          const std::vector<StageRange>& stages,
                                          const std::vector<double>& pts,
                                          const std::vector<double>& cum,
                                          double total,
                                          const std::map<int, MachineTool>& machine_tools,
                                          const MachineLimits& limits);

} // namespace gv
