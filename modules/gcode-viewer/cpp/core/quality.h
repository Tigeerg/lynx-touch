// core/quality.h - пресеты качества: уровень (low/med/high/safe) задаёт одним
// набором допуски чтения модели, силуэта, касательных и обводки.
//
// Уровень med повторяет числа, действовавшие до появления пресетов, поэтому
// пустое окружение ничего не меняет. Значения low и high - предложения: у тех
// допусков, чья цена видна замером bench_probe, в комментарии стоит измеренное
// время, у остальных - слово "предложение". Уровень safe берёт самые грубые из
// значений low.
//
// Числа разбора УП и скана коллизий читаются окружением (CNC_DECIMATE_TOL,
// CNC_ARC_DECIMATE_TOL, CNC_COLLISION_STEP) и уровень их не меняет: пресет их
// только сообщает тем же чтением, что core/gcode_parser и core/collision.
//
// Слой чистый, как и остальное ядро: ни Qt, ни OCCT.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdlib>

#include "text_util.h"

namespace gv {

// Набор допусков одной загрузки. Величины с размерностью носят её в имени:
// Mm - миллиметры, Mm2 - квадратные миллиметры, Deg - градусы.
struct QualityPreset {
    const char *level = "med";          // имя уровня: low, med, high или safe

    // STEP: линейный допуск тесселяции кожи и полилиний рёбер B-Rep (мм).
    double stepDeflectionMm = 0.05;
    double edgeDeflectionMm = 0.05;

    // Склейка почти совпадающих отрезков рёбер B-Rep (occt/step_edges): сетка
    // расстояния до перпендикуляра и зазор по оси (мм).
    double coalesceDistTolMm = 0.12;
    double coalesceGapTolMm = 0.5;

    // Силуэт меша (core/silhouette): сетка сварки вершин (мм), порог острой
    // кромки (градусы, круче - кромка не силуэтная) и порог площади вырожденной
    // грани (мм^2, ниже - нормаль грани численный мусор).
    double silhouetteWeldMm = 1e-4;
    double silhouetteMaxEdgeDeg = 20.0;
    double silhouetteMinFaceAreaMm2 = 1e-6;

    // Нормали кожи (core/normals): порог сглаживания соседних нормалей (градусы).
    double creaseDeg = 20.0;

    // Обводка меша (core/outline): порог двугранного угла (градусы) и допуск
    // упрощения цепочек по Дугласу-Пекеру (мм).
    double outlineFeatureEdgeDeg = 20.0;
    double outlineFeatureSimplifyMm = 0.15;

    // Касательные тел вращения (core/tangents): порог длины образующей конуса
    // (мм), число азимутов пробы вогнутости, порог дуги силуэта цилиндра
    // (градусы), минимальный пролёт образующей (мм) и перекрытие по оси, при
    // котором азимут генератора уходит из политики DROP (мм).
    double coneMinSlantMm = 2.5;
    int coneProbeAzimuths = 8;
    double silhouetteArcDeg = 85.0;
    double silhouetteMinSpanMm = 2.5;
    double dropOverlapMm = 2.0;

    // Разбор УП и скан коллизий: значения окружения, уровень их не меняет.
    double decimateTolMm = 0.5;
    double arcDecimateTolMm = 0.1;
    double collisionStepMm = 1.0;

    // Симуляция съёма материала (core/stock_sim): шаг сетки поля в мм.
    // Замер на Test_model.ngc (70 914 ходов, 137.8 м, габарит UI_STOCK):
    // 0.5 мм - 60 501 узел, reset 0.1 мс + съём 192.4 мс, поле 236 КиБ;
    // 0.2 мм - 376 251 узел, 2288.6 мс, поле 1.44 МиБ. 1.0 мм не измерен:
    // узел вдвое крупнее med, узлов примерно вчетверо меньше.
    // Повторный замер (Release, /O2 /Ob2, загрузка машины 46 %) дал 396-425 мс
    // и 7631-8393 мс - ровно вдвое медленнее, и обе пробы сошлись между собой.
    // Разница не в сборке, а в состоянии машины, поэтому в пресетах стоят числа
    // первого замера, а оба ряда записаны здесь.
    double stockSimCellMm = 0.5;
};

// Значение переменной окружения: пустая строка или отсутствие - значение по
// умолчанию, иначе разобранное gv::strtodC - то же чтение, что у envDouble в
// core/gcode_parser.cpp: разбор не зависит от локали процесса.
inline double qualityEnvMm(const char *name, double fallback)
{
    const char *raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') {
        return fallback;
    }
    return strtodC(raw, nullptr);
}

// Шаг скана коллизий из окружения: правило разбора то же, что у collisionStep
// (неразобранный хвост считается ошибкой и даёт значение по умолчанию).
inline double qualityEnvCollisionStepMm()
{
    const char *raw = std::getenv("CNC_COLLISION_STEP");
    if (raw == nullptr || *raw == '\0') {
        return 1.0;
    }
    char *end = nullptr;
    const double value = strtodC(raw, &end);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return 1.0;
    }
    return value;
}

// Шаг сетки симуляции съёма из окружения CNC_STOCK_CELL: им перекрывают шаг
// пресета, не меняя сам уровень. Пустое или неразбираемое значение - шаг пресета
// (0), разбор тот же, что у qualityEnvMm.
inline double qualityEnvStockCellMm()
{
    const double value = qualityEnvMm("CNC_STOCK_CELL", 0.0);
    return value > 0.0 ? value : 0.0;
}

// Дробление сетки знакового расстояния для рисования из окружения
// CNC_STOCK_CUT_REFINE: им меряют волну кромки цвета при другом дроблении, не
// вводя нового уровня (как CNC_STOCK_CELL меряет узел поля). Пустое или
// неразбираемое значение - 0, то есть значение по умолчанию (gv_toolpath).
inline int qualityEnvStockCutRefine()
{
    const int value = int(qualityEnvMm("CNC_STOCK_CUT_REFINE", 0.0));
    return value > 0 ? value : 0;
}

// Имя уровня без учёта регистра: у соседних флагов окружения (CNC_CPP,
// CNC_SILHOUETTE) значение тоже читается без регистра.
inline bool sameLevelName(const char *level, const char *name)
{
    std::size_t i = 0;
    for (; level[i] != '\0' && name[i] != '\0'; ++i) {
        const int a = std::tolower(static_cast<unsigned char>(level[i]));
        const int b = std::tolower(static_cast<unsigned char>(name[i]));
        if (a != b) {
            return false;
        }
    }
    return level[i] == '\0' && name[i] == '\0';
}

// Пресет уровня: low, med, high или safe. Пустое или неизвестное имя даёт med -
// уровень, при котором числа те же, что были до пресетов. Три числа окружения
// (разбор УП и скан коллизий) подставляются здесь же.
inline QualityPreset presetFor(const char *level)
{
    QualityPreset q;
    const char *name = (level != nullptr && *level != '\0') ? level : "med";

    if (sameLevelName(name, "low")) {
        q.level = "low";
        // Отклонение тесселяции 0.2 мм: предложение; замер на Test_model - 7856
        // треугольников против 45398 у med (тесселяция 62.8 мс против 129.4 мс).
        q.stepDeflectionMm = 0.2;
        // Отклонение полилиний рёбер 0.2 мм: предложение; 1682 отрезка после
        // склейки против 2426 у med.
        q.edgeDeflectionMm = 0.2;
        // Склейка рёбер шире: предложение.
        q.coalesceDistTolMm = 0.2;
        q.coalesceGapTolMm = 0.5;
        // Сварка силуэта 0.001 мм: предложение.
        q.silhouetteWeldMm = 1e-3;
        q.silhouetteMaxEdgeDeg = 25.0;      // предложение
        q.silhouetteMinFaceAreaMm2 = 1e-5;  // предложение
        q.creaseDeg = 25.0;                 // предложение
        q.outlineFeatureEdgeDeg = 25.0;     // предложение
        q.outlineFeatureSimplifyMm = 0.3;   // предложение
        q.coneMinSlantMm = 3.0;             // предложение
        q.coneProbeAzimuths = 4;            // предложение
        q.silhouetteArcDeg = 70.0;          // предложение
        q.silhouetteMinSpanMm = 3.5;        // предложение
        q.dropOverlapMm = 3.0;              // предложение
        // Съём: 0.5 мм - 60 501 узел и 192.5 мс на Test_model.ngc (замер).
        q.stockSimCellMm = 0.5;
    } else if (sameLevelName(name, "high")) {
        q.level = "high";
        // Отклонение тесселяции 0.02 мм: предложение; замер на Test_model - 175808
        // треугольников (тесселяция 422.1 мс против 129.4 мс у med).
        q.stepDeflectionMm = 0.02;
        // Отклонение полилиний рёбер 0.02 мм: предложение; 3330 отрезков после
        // склейки против 2426 у med.
        q.edgeDeflectionMm = 0.02;
        q.coalesceDistTolMm = 0.08;         // предложение
        q.coalesceGapTolMm = 0.5;
        q.silhouetteWeldMm = 1e-5;          // предложение
        q.silhouetteMaxEdgeDeg = 15.0;      // предложение
        q.silhouetteMinFaceAreaMm2 = 1e-7;  // предложение
        q.creaseDeg = 15.0;                 // предложение
        q.outlineFeatureEdgeDeg = 15.0;     // предложение
        q.outlineFeatureSimplifyMm = 0.08;  // предложение
        q.coneMinSlantMm = 2.0;             // предложение
        q.coneProbeAzimuths = 12;           // предложение
        q.silhouetteArcDeg = 90.0;          // предложение
        q.silhouetteMinSpanMm = 2.0;        // предложение
        q.dropOverlapMm = 1.5;              // предложение
        // Съём: 0.2 мм - 376 251 узел и 2288.6 мс на Test_model.ngc (замер).
        q.stockSimCellMm = 0.2;
    } else if (sameLevelName(name, "safe")) {
        q.level = "safe";
        // Самые грубые значения: уровень для слабой машины и крупных моделей.
        q.stepDeflectionMm = 0.5;           // предложение; 4132 треугольника против 45398
        q.edgeDeflectionMm = 0.5;           // предложение; 1427 отрезков против 2426
        q.coalesceDistTolMm = 0.3;          // предложение
        q.coalesceGapTolMm = 0.5;
        q.silhouetteWeldMm = 1e-2;          // предложение
        q.silhouetteMaxEdgeDeg = 30.0;      // предложение
        q.silhouetteMinFaceAreaMm2 = 1e-4;  // предложение
        q.creaseDeg = 30.0;                 // предложение
        q.outlineFeatureEdgeDeg = 30.0;     // предложение
        q.outlineFeatureSimplifyMm = 0.5;   // предложение
        q.coneMinSlantMm = 4.0;             // предложение
        q.coneProbeAzimuths = 4;            // предложение
        q.silhouetteArcDeg = 60.0;          // предложение
        q.silhouetteMinSpanMm = 4.0;        // предложение
        q.dropOverlapMm = 4.0;              // предложение
        // Съём: 1.0 мм - не измерен; узел вдвое крупнее med.
        q.stockSimCellMm = 1.0;
    } else {
        q.level = "med";                    // пустое или неизвестное имя
    }

    q.decimateTolMm = qualityEnvMm("CNC_DECIMATE_TOL", 0.5);
    q.arcDecimateTolMm = qualityEnvMm("CNC_ARC_DECIMATE_TOL", 0.1);
    q.collisionStepMm = qualityEnvCollisionStepMm();
    // CNC_STOCK_CELL перекрывает шаг пресета: им меряют цену съёма при другом
    // узле, не вводя нового уровня.
    const double stock_cell = qualityEnvStockCellMm();
    if (stock_cell > 0.0) {
        q.stockSimCellMm = stock_cell;
    }
    return q;
}

// Уровень из окружения CNC_QUALITY (пустое или неизвестное значение - med).
inline QualityPreset presetFromEnv()
{
    return presetFor(std::getenv("CNC_QUALITY"));
}

} // namespace gv
