// cpp/stepledges_probe.cpp — пробник совпадения рёбер STEP-модели с питоновским
// эталоном occt/step_import.cpp::load_step_model.
//
// Пробник не входит в плагин: отдельная цель CMake. Он читает STEP штатным путём
// плагина (свойство source -> STEPControl_Reader -> setShape) и печатает:
//
// segments: N число отрезков в буфере (эталон Test_model.STEP — 2426)
// raw_segments: M число отрезков до склейки (эталон — 4884)
// sha256_order: H sha256 байтов буфера: float32 little-endian, порядок сборки
// sha256_set: H sha256 тех же 24-байтовых кусков, отсортированных по байтам
//
// Хэши печатаются как улика и считаются по сырым байтам: побайтовое совпадение с
// эталоном недостижимо (кластеры склейки в эталоне упорядочены неустойчивым
// numpy.argsort, а у 16 отрезков из 2426 numpy оставляет в младших битах double
// остаток 1e-15 там, где плагин даёт нуль). Сверку ведёт
//: он сравнивает число отрезков и
// набор точек с точностью 1e-4 мм. Третий аргумент — необязательный дамп буфера
// сырыми float32 для этого сравнения (numpy.fromfile).
//
// Запуск: stepledges_probe.exe <файл.step> [дамп.bin] (числа и хэши рёбер)
// stepledges_probe.exe --repeat <N> <файл.step> (замер смены свойства)
// stepledges_probe.exe --qml <import-path> (видит ли модуль движок QML)
// (нужны DLL OCCT и Qt в PATH)

#include "stepledgesgeometry.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace {

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

// Отпечаток набора кусков: сортировка кусков по байтам делает хэш независимым от
// порядка обхода. Python сравнивает байты так же (sorted по bytes).
QByteArray setDigest(const QByteArray &bytes, int chunkSize)
{
    const int count = int(bytes.size()) / chunkSize;
    std::vector<std::array<unsigned char, 24>> chunks;
    chunks.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        const char *src = bytes.constData() + std::size_t(i) * chunkSize;
        std::array<unsigned char, 24> chunk{};
        for (int k = 0; k < chunkSize; ++k)
            chunk[std::size_t(k)] = static_cast<unsigned char>(src[k]);
        chunks.push_back(chunk);
    }
    std::sort(chunks.begin(), chunks.end());
    QByteArray joined;
    joined.reserve(count * chunkSize);
    for (const auto &chunk : chunks) {
        for (int k = 0; k < chunkSize; ++k)
            joined.append(char(chunk[std::size_t(k)]));
    }
    return sha256(joined);
}

} // namespace

// Проверка, что движок QML действительно видит модуль: import CNC.Edges 1.0 и
// создание элемента StepEdges. Импорт-путь — каталог сборки (build/edges).
int runQmlImport(const QString &importPath)
{
    QQmlEngine engine;
    engine.addImportPath(importPath);
    QQmlComponent component(&engine);
    component.setData("import CNC.Edges 1.0\nStepEdges { hideFormEdges: true }\n",
                      QUrl("qrc:/probe.qml"));
    if (component.isError()) {
        std::fprintf(stderr, "qml: FAIL %s\n",
                     component.errorString().toUtf8().constData());
        return 1;
    }
    std::printf("qml_import: CNC.Edges StepEdges OK\n");
    return 0;
}

// Замер цены смены свойства на уже открытой модели: файл читается один раз
// (setSource), затем N раз меняется deflection (0.05 -> 0.06 ->...), так что
// видно, перечитывает ли StepEdgesGeometry STEP на каждое изменение свойства.
// Чётное N возвращает 0.05 в конце - числа рёбер остаются эталонными.
int runRepeat(const QString &path, int repeats)
{
    StepEdgesGeometry geometry;

    QElapsedTimer read;
    read.start();
    geometry.setSource(path);
    const qint64 readMs = read.elapsed();
    const int firstSegments = geometry.segmentCount();

    QElapsedTimer changes;
    changes.start();
    for (int i = 0; i < repeats; ++i) {
        geometry.setDeflection(i % 2 == 0 ? 0.06 : 0.05);
    }
    const qint64 totalMs = changes.elapsed();

    std::printf("source_read_ms: %lld\n", static_cast<long long>(readMs));
    std::printf("repeats: %d\n", repeats);
    std::printf("changes_total_ms: %lld\n", static_cast<long long>(totalMs));
    std::printf("per_change_ms: %.3f\n",
                repeats > 0 ? double(totalMs) / repeats : 0.0);
    std::printf("first_segments: %d\n", firstSegments);
    std::printf("segments: %d\n", geometry.segmentCount());
    std::printf("raw_segments: %d\n", geometry.rawSegmentCount());
    return geometry.segmentCount() == firstSegments ? 0 : 1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() >= 3 && args.at(1) == QStringLiteral("--qml"))
        return runQmlImport(args.at(2));
    if (args.size() >= 4 && args.at(1) == QStringLiteral("--repeat"))
        return runRepeat(args.at(3), args.at(2).toInt());
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: stepledges_probe <file.step> [dump.bin]\n"
                             "       stepledges_probe --repeat <N> <file.step>\n"
                             "       stepledges_probe --qml <import-path>\n");
        return 2;
    }

    StepEdgesGeometry geometry;
    geometry.setSource(args.at(1));

    const QVector<float> points = geometry.segmentPoints();
    const int segments = geometry.segmentCount();
    const int rawSegments = geometry.rawSegmentCount();

    std::printf("segments: %d\n", segments);
    std::printf("raw_segments: %d\n", rawSegments);
    if (segments <= 0 || points.size() != segments * 6) {
        std::fprintf(stderr, "probe: буфер не заполнен (точек %d)\n", int(points.size()));
        return 1;
    }

    // 24 байта на отрезок: две точки по три float32, порядок сборки.
    const QByteArray bytes(reinterpret_cast<const char *>(points.constData()),
                           int(points.size() * sizeof(float)));
    std::printf("sha256_order: %s\n", sha256(bytes).constData());
    std::printf("sha256_set: %s\n", setDigest(bytes, 24).constData());

    // Необязательный третий аргумент: дамп буфера сырыми float32 для разбора
    // расхождений с эталоном (питоновская сторона читает его numpy.fromfile).
    if (args.size() >= 3) {
        QFile dump(args.at(2));
        if (!dump.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "probe: не удалось записать %s\n",
                         args.at(2).toUtf8().constData());
            return 1;
        }
        dump.write(bytes);
        dump.close();
    }
    return 0;
}
