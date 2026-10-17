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
};

// Значение переменной окружения: пустая строка или отсутствие - значение по
// умолчанию, иначе первый разобранный strtod (то же чтение, что envDouble в
// core/gcode_parser.cpp).
inline double qualityEnvMm(const char *name, double fallback)
{
    const char *raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') {
        return fallback;
    }
    return std::strtod(raw, nullptr);
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
    const double value = std::strtod(raw, &end);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return 1.0;
    }
    return value;
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
    } else {
        q.level = "med";                    // пустое или неизвестное имя
    }

    q.decimateTolMm = qualityEnvMm("CNC_DECIMATE_TOL", 0.5);
    q.arcDecimateTolMm = qualityEnvMm("CNC_ARC_DECIMATE_TOL", 0.1);
    q.collisionStepMm = qualityEnvCollisionStepMm();
    return q;
}

// Уровень из окружения CNC_QUALITY (пустое или неизвестное значение - med).
inline QualityPreset presetFromEnv()
{
    return presetFor(std::getenv("CNC_QUALITY"));
}

} // namespace gv
