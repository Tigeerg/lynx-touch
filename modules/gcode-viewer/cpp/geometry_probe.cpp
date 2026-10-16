// cpp/geometry_probe.cpp - пробник совпадения геометрий Qt Quick 3D (этап 3 плана
// переноса) с питоновским эталоном modules/gcode-viewer/geometry.py.
//
// Пробник не входит в плагин: отдельная цель CMake. Входные данные он не
// придумывает - их пишет питоновская проверка (modules/gcode-viewer/test_geometry_core.py)
// в каталог-аргумент, поэтому обе стороны строят геометрию на одних и тех же
// числах, а не на «похожих». На каждый случай пробник печатает состояние геометрии
// и sha256 буфера вершин, а сам буфер пишет файлом <случай>.bin:
//
//   <случай>.vertexCount: N          число вершин
//   <случай>.triangleCount: N        число треугольников
//   <случай>.stride: N               шаг вершины, байт
//   <случай>.primitiveType: N        Lines=2, Triangles=5
//   <случай>.attributeCount: N       число атрибутов
//   <случай>.attributeK: <semantic> <offset> <componentType>   атрибут K
//   <случай>.boundsMin: 0x... 0x... 0x...   габарит побитно (float32, три слова)
//   <случай>.boundsMax: 0x... 0x... 0x...
//   <случай>.bytes: N                длина буфера вершин, байт
//   <случай>.sha256: H               sha256 буфера вершин
//
// Случаи - те же, что перебирает питоновская проверка: траектория с габаритом, без
// габарита и пустая; контур лентами с опорной точкой, без неё и пустой; сетка с
// растворением и с нулевым растворением; указатель оси и указатель короче головы;
// меш, меш после reset и тело вращения по профилю (48 делений и 3 деления).
//
// Запуск: geometry_probe <каталог-входа> <каталог-дампа>
// Код возврата: 0 - построено и записано; 1 - входной файл или каталог недоступны.

#include "qt/gv_geometry.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Входные файлы пишет питоновская сторона: float64 little-endian (порядок байтов
// тот же, что у буферов на GPU: x86 - little-endian).
bool readBytes(const QString &path, QByteArray *out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *out = file.readAll();
    return true;
}

bool readDoubles(const QString &path, std::vector<double> *out)
{
    QByteArray raw;
    if (!readBytes(path, &raw))
        return false;
    if (raw.size() % 8 != 0)
        return false;
    const int count = int(raw.size()) / 8;
    out->clear();
    out->reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        double value = 0.0;
        std::memcpy(&value, raw.constData() + std::size_t(i) * 8, 8);
        out->push_back(value);
    }
    return true;
}

double at(const std::vector<double> &v, std::size_t i)
{
    return i < v.size() ? v[i] : 0.0;
}

// Побитовая запись float32: питоновская сторона сравнивает не «примерно равные»
// числа, а те же самые биты.
unsigned int bits(float value)
{
    unsigned int raw = 0;
    std::memcpy(&raw, &value, 4);
    return raw;
}

QString hex32(float value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", bits(value));
    return QString::fromLatin1(text);
}

// Состояние одной геометрии: печать в stdout и дамп буфера файлом.
void report(const QString &name, QQuick3DGeometry *geo, int vertexCount, int triangleCount,
            const QString &dumpDir)
{
    const QByteArray data = geo->vertexData();
    const QByteArray nameUtf8 = name.toUtf8();
    const char *prefix = nameUtf8.constData();
    std::printf("%s.vertexCount: %d\n", prefix, vertexCount);
    std::printf("%s.triangleCount: %d\n", prefix, triangleCount);
    std::printf("%s.stride: %d\n", prefix, geo->stride());
    std::printf("%s.primitiveType: %d\n", prefix, int(geo->primitiveType()));
    std::printf("%s.attributeCount: %d\n", prefix, geo->attributeCount());
    for (int i = 0; i < geo->attributeCount(); ++i) {
        const QQuick3DGeometry::Attribute attr = geo->attribute(i);
        std::printf("%s.attribute%d: %d %d %d\n", prefix, i, int(attr.semantic), attr.offset,
                    int(attr.componentType));
    }
    const QVector3D lo = geo->boundsMin();
    const QVector3D hi = geo->boundsMax();
    std::printf("%s.boundsMin: %s %s %s\n", prefix, hex32(lo.x()).toUtf8().constData(),
                hex32(lo.y()).toUtf8().constData(), hex32(lo.z()).toUtf8().constData());
    std::printf("%s.boundsMax: %s %s %s\n", prefix, hex32(hi.x()).toUtf8().constData(),
                hex32(hi.y()).toUtf8().constData(), hex32(hi.z()).toUtf8().constData());
    std::printf("%s.bytes: %d\n", prefix, int(data.size()));
    std::printf("%s.sha256: %s\n", prefix,
                QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().constData());

    QFile dump(QDir(dumpDir).filePath(name + QStringLiteral(".bin")));
    if (!dump.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "probe: не удалось записать дамп %s\n", prefix);
        return;
    }
    dump.write(data);
    dump.close();
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QStringList args = QGuiApplication::arguments();
    if (args.size() < 3) {
        std::fprintf(stderr, "usage: geometry_probe <input-dir> <dump-dir>\n");
        return 2;
    }
    const QString inDir = args.at(1);
    const QString dumpDir = args.at(2);
    QDir().mkpath(dumpDir);

    std::vector<double> tpBounds, hlrRef, hlrSegs, grid, gridFlat, axis, axisShort;
    std::vector<double> meshVerts, toolProfile, toolLowProfile, toolSegments, toolLowSegments;
    QByteArray tpRaw;
    const auto fail = [](const char *what) {
        std::fprintf(stderr, "probe: не прочитан вход %s\n", what);
        return 1;
    };
    const auto file = [&inDir](const char *name) { return QDir(inDir).filePath(QLatin1String(name)); };
    if (!readBytes(file("tp_raw.bin"), &tpRaw)) return fail("tp_raw.bin");
    if (!readDoubles(file("tp_bounds.f64"), &tpBounds)) return fail("tp_bounds.f64");
    if (!readDoubles(file("hlr_ref.f64"), &hlrRef)) return fail("hlr_ref.f64");
    if (!readDoubles(file("hlr_segs.f64"), &hlrSegs)) return fail("hlr_segs.f64");
    if (!readDoubles(file("grid_params.f64"), &grid)) return fail("grid_params.f64");
    if (!readDoubles(file("grid_flat_params.f64"), &gridFlat)) return fail("grid_flat_params.f64");
    if (!readDoubles(file("axis_params.f64"), &axis)) return fail("axis_params.f64");
    if (!readDoubles(file("axis_short_params.f64"), &axisShort)) return fail("axis_short_params.f64");
    if (!readDoubles(file("mesh_verts.f64"), &meshVerts)) return fail("mesh_verts.f64");
    if (!readDoubles(file("tool_profile.f64"), &toolProfile)) return fail("tool_profile.f64");
    if (!readDoubles(file("tool_low_profile.f64"), &toolLowProfile)) return fail("tool_low_profile.f64");
    if (!readDoubles(file("tool_segments.f64"), &toolSegments)) return fail("tool_segments.f64");
    if (!readDoubles(file("tool_low_segments.f64"), &toolLowSegments)) return fail("tool_low_segments.f64");

    // ---- ToolpathGeometry: stride 20, контракт toolpath.vert/.frag ----
    const int tpNv = int(tpRaw.size()) / gvgeom::ToolpathGeometry::STRIDE;
    gv::Aabb tpBox;
    tpBox.lo = {at(tpBounds, 0), at(tpBounds, 1), at(tpBounds, 2)};
    tpBox.hi = {at(tpBounds, 3), at(tpBounds, 4), at(tpBounds, 5)};

    gvgeom::ToolpathGeometry toolpath;
    toolpath.setPacked(tpRaw, &tpBox, tpNv);
    report(QStringLiteral("toolpath"), &toolpath, toolpath.vertexCount(), 0, dumpDir);

    gvgeom::ToolpathGeometry toolpathNoBounds;
    toolpathNoBounds.setPacked(tpRaw, nullptr, tpNv);
    report(QStringLiteral("toolpath_nobounds"), &toolpathNoBounds, toolpathNoBounds.vertexCount(), 0,
           dumpDir);

    gvgeom::ToolpathGeometry toolpathEmpty;
    toolpathEmpty.setPacked(QByteArray(), nullptr, 0);
    report(QStringLiteral("toolpath_empty"), &toolpathEmpty, toolpathEmpty.vertexCount(), 0, dumpDir);

    // ---- HLRLineGeometry: stride 48, контракт stepledges.vert ----
    const int hlrCount = int(hlrSegs.size()) / 6;
    std::vector<float> hlrSegs32(std::size_t(hlrCount) * 6);
    for (std::size_t i = 0; i < hlrSegs32.size(); ++i)
        hlrSegs32[i] = float(hlrSegs[i]);
    double hlrRef3[3] = {at(hlrRef, 0), at(hlrRef, 1), at(hlrRef, 2)};

    gvgeom::HLRLineGeometry hlr;
    hlr.setReference(hlrRef3);
    hlr.setSegments(hlrSegs32.data(), hlrCount);
    report(QStringLiteral("hlr"), &hlr, hlr.vertexCount(), 0, dumpDir);

    gvgeom::HLRLineGeometry hlrNoRef;
    hlrNoRef.setSegments(hlrSegs32.data(), hlrCount);
    report(QStringLiteral("hlr_noref"), &hlrNoRef, hlrNoRef.vertexCount(), 0, dumpDir);

    gvgeom::HLRLineGeometry hlrEmpty;
    hlrEmpty.setSegments(nullptr, 0);
    report(QStringLiteral("hlr_empty"), &hlrEmpty, hlrEmpty.vertexCount(), 0, dumpDir);

    // ---- GridGeometry ----
    gvgeom::GridGeometry gridGeo;
    gridGeo.setSize(at(grid, 0));
    gridGeo.setStep(at(grid, 1));
    gridGeo.setLevel(at(grid, 2));
    gridGeo.setCenterX(at(grid, 3));
    gridGeo.setCenterZ(at(grid, 4));
    gridGeo.setFeather(at(grid, 5));
    const int gridStride = gvgeom::GridGeometry::STRIDE;
    report(QStringLiteral("grid"), &gridGeo, int(gridGeo.vertexData().size()) / gridStride, 0, dumpDir);

    gvgeom::GridGeometry gridFlatGeo;
    gridFlatGeo.setSize(at(gridFlat, 0));
    gridFlatGeo.setStep(at(gridFlat, 1));
    gridFlatGeo.setLevel(at(gridFlat, 2));
    gridFlatGeo.setCenterX(at(gridFlat, 3));
    gridFlatGeo.setCenterZ(at(gridFlat, 4));
    gridFlatGeo.setFeather(at(gridFlat, 5));
    report(QStringLiteral("grid_flat"), &gridFlatGeo,
           int(gridFlatGeo.vertexData().size()) / gridStride, 0, dumpDir);

    // ---- AxisArrowGeometry ----
    gvgeom::AxisArrowGeometry axisGeo;
    axisGeo.setLength(at(axis, 0));
    axisGeo.setHeadLength(at(axis, 1));
    axisGeo.setHeadWidth(at(axis, 2));
    axisGeo.setShaftWidth(at(axis, 3));
    const int axisStride = gvgeom::AxisArrowGeometry::STRIDE;
    report(QStringLiteral("axis"), &axisGeo, int(axisGeo.vertexData().size()) / axisStride, 0, dumpDir);

    gvgeom::AxisArrowGeometry axisShortGeo;
    axisShortGeo.setLength(at(axisShort, 0));
    axisShortGeo.setHeadLength(at(axisShort, 1));
    axisShortGeo.setHeadWidth(at(axisShort, 2));
    axisShortGeo.setShaftWidth(at(axisShort, 3));
    report(QStringLiteral("axis_short"), &axisShortGeo,
           int(axisShortGeo.vertexData().size()) / axisStride, 0, dumpDir);

    // ---- MeshGeometry ----
    gvgeom::MeshGeometry mesh;
    mesh.setTriangles(meshVerts.data(), int(meshVerts.size()));
    report(QStringLiteral("mesh"), &mesh, mesh.triangleCount() * 3, mesh.triangleCount(), dumpDir);

    gvgeom::MeshGeometry meshEmpty;
    meshEmpty.setTriangles(meshVerts.data(), int(meshVerts.size()));
    meshEmpty.reset();
    report(QStringLiteral("mesh_empty"), &meshEmpty, 0, meshEmpty.triangleCount(), dumpDir);

    // ---- ToolGeometry ----
    gvgeom::ToolGeometry tool;
    tool.setProfile(toolProfile.data(), int(toolProfile.size()) / 2,
                    int(at(toolSegments, 0)));
    report(QStringLiteral("tool"), &tool, tool.triangleCount() * 3, tool.triangleCount(), dumpDir);

    gvgeom::ToolGeometry toolLow;
    toolLow.setProfile(toolLowProfile.data(), int(toolLowProfile.size()) / 2,
                       int(at(toolLowSegments, 0)));
    report(QStringLiteral("tool_low"), &toolLow, toolLow.triangleCount() * 3, toolLow.triangleCount(),
           dumpDir);

    return 0;
}
