// cpp/stepledges_probe.cpp - probe of the match between the edges of a STEP model
// and the Python oracle occt/step_import.cpp::load_step_model.
//
// The probe is not part of the plugin: a separate CMake target. It reads STEP along
// the plugin's regular path (the source property -> STEPControl_Reader -> setShape)
// and prints:
//
// segments: N the number of segments in the buffer (oracle Test_model.STEP - 2426)
// raw_segments: M the number of segments before coalescing (oracle - 4884)
// sha256_order: H sha256 of the buffer bytes: float32 little-endian, assembly order
// sha256_set: H sha256 of the same 24-byte chunks sorted by bytes
//
// The hashes are printed as evidence and are computed over the raw bytes: a
// byte-for-byte match with the oracle is unreachable (the coalescing clusters in the
// oracle are ordered by an unstable numpy.argsort, and for 16 segments out of 2426
// numpy leaves a double remainder of 1e-15 in the low bits where the plugin gives
// zero). The comparison is driven by
//: it compares the number of segments and
// the set of points to a precision of 1e-4 mm. The third argument is an optional
// dump of the buffer as raw float32 for that comparison (numpy.fromfile).
//
// Usage: stepledges_probe.exe <file.step> [dump.bin] (edge numbers and hashes)
// stepledges_probe.exe --repeat <N> <file.step> (property change measurement)
// stepledges_probe.exe --qml <import-path> (does the QML engine see the module)
// (OCCT and Qt DLLs must be in PATH)

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

// Digest of a set of chunks: sorting the chunks by bytes makes the hash independent
// of the traversal order. Python compares the bytes the same way (sorted over bytes).
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

// Check that the QML engine really sees the module: import CNC.Edges 1.0 and
// creating a StepEdges item. The import path is the build directory (build/edges).
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

// Measuring the cost of a property change on an already opened model: the file is
// read once (setSource), then deflection is changed N times (0.05 -> 0.06 ->...), so
// it is visible whether StepEdgesGeometry re-reads STEP on every property change.
// An even N returns 0.05 at the end - the edge numbers stay at the oracle values.
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

    // 24 bytes per segment: two points of three float32 each, assembly order.
    const QByteArray bytes(reinterpret_cast<const char *>(points.constData()),
                           int(points.size() * sizeof(float)));
    std::printf("sha256_order: %s\n", sha256(bytes).constData());
    std::printf("sha256_set: %s\n", setDigest(bytes, 24).constData());

    // Optional third argument: a dump of the buffer as raw float32 for analysing
    // discrepancies with the oracle (the Python side reads it with numpy.fromfile).
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
