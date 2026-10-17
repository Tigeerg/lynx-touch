// cpp/geometry_probe.cpp - пробник совпадения геометрий Qt Quick 3D (этап 3 плана
// переноса) с питоновским эталоном modules/gcode-viewer/geometry.py.
//
// Пробник не входит в плагин: отдельная цель CMake. На каждый случай он печатает
// состояние геометрии и sha256 буфера вершин, а сам буфер пишет файлом
// <случай>.bin:
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
//         geometry_probe --self [<файл.ngc>]
//         geometry_probe --manifest <oracle_manifest.json> [<файл.ngc>]
//         geometry_probe --check [<файл.ngc>]
// Код возврата: 0 - построено и записано (или дайджесты сошлись с оракулом);
//               1 - вход, файл данных или манифест недоступны; 2 - аргументы не
//               разобраны.
//
// Два набора входных данных:
//   * <каталог-входа> <каталог-дампа> - вход пишет питоновская проверка
//     (modules/gcode-viewer/test_geometry_core.py), поэтому обе стороны считают на
//     одних числах, а не на «похожих»;
//   * --self и --manifest/--check - вход пробник строит сам, без Python: траектория
//     читается из УП тем же путём, что рантайм (gvcore: parseGcode +
//     prepareOrdered), остальное - те же числа, что и в питоновской проверке.
//
// Режим --manifest/--check сверяет sha256 буферов (записи geom.*) с
// oracle_manifest.json без Python и без каталога-дампа: печатает строку на запись
// и "RESULT: OK" / "RESULT: FAIL (n)". Путь к УП по умолчанию берётся от корня
// репозитория (путь пробника). Режим --self печатает то же состояние с дайджестами,
// но не пишет дамп: им сверяют свой вход с питоновским вручную.
//
// Оговорка о входе сверки: профиль инструмента (случай tool) пробник строит
// gvcore::toolProfile по тем же метаданным UI_TOOL, что и питоновский
// mb._tool_profile, а траекторию - по самой УП. Совпадение входов доказывает
// равенство дайджестов geom.*: сверка ловит и расхождение геометрии, и расхождение
// входа (см. test_geometry_core.py - он строит вход питоновской стороной).

#include "manifest_check.h"
#include "sha256.h"

#include "core/gcode_parser.h"
#include "core/path_prep.h"
#include "core/tooling.h"
#include "qt/gv_geometry.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

// Входные данные всех случаев. Файловый режим наполняет его из каталога-входа,
// режим --self - из УП и тех же чисел, что у питоновской проверки.
struct Inputs {
    QByteArray tpRaw;                      // упакованный буфер подачи (stride 20)
    int tpNv = 0;                          // число вершин буфера подачи
    bool tpHasBounds = false;              // габарит сцены задан
    gv::Aabb tpBounds;                     // габарит сцены
    std::vector<float> hlrSegs32;          // звенья контура, 6 float32 на звено
    double hlrRef[3] = {0.0, 0.0, 0.0};    // опорная точка кадра модели
    std::vector<double> grid;              // size, step, level, cx, cz, feather
    std::vector<double> gridFlat;          // то же с нулевым растворением
    std::vector<double> axis;              // length, headLength, headWidth, shaftWidth
    std::vector<double> axisShort;         // голова длиннее указателя
    std::vector<double> meshVerts;         // float64, кратно 9 (треугольники)
    std::vector<double> toolProfile;       // пары (radius, height)
    int toolSegments = 48;                 // деления тела вращения
    std::vector<double> toolLowProfile;    // короткий профиль (3 деления)
    int toolLowSegments = 3;
};

// Разобранный случай: имя и байты буфера вершин (запись манифеста geom.<имя>).
using CaseBuffers = std::vector<std::pair<std::string, QByteArray>>;

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

// Состояние одной геометрии: печать в stdout, дамп буфера файлом и сбор байтов
// для сверки с манифестом.
void report(const QString &name, QQuick3DGeometry *geo, int vertexCount, int triangleCount,
            const QString &dumpDir, bool printReport, CaseBuffers *collect)
{
    const QByteArray data = geo->vertexData();
    const QByteArray nameUtf8 = name.toUtf8();
    const char *prefix = nameUtf8.constData();
    if (printReport) {
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
                    gv::sha256Hex(std::vector<unsigned char>(data.begin(), data.end())).c_str());
    }
    if (!dumpDir.isEmpty()) {
        QFile dump(QDir(dumpDir).filePath(name + QStringLiteral(".bin")));
        if (!dump.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "probe: не удалось записать дамп %s\n", prefix);
            return;
        }
        dump.write(data);
        dump.close();
    }
    if (collect != nullptr) {
        collect->emplace_back(name.toStdString(), data);
    }
}

// Все 14 случаев на одном входе: тот же порядок, что у питоновской проверки.
void runCases(const Inputs &in, const QString &dumpDir, bool printReport, CaseBuffers *collect)
{
    // ---- ToolpathGeometry: stride 20, контракт toolpath.vert/.frag ----
    gv::Aabb tpBox = in.tpBounds;
    gvgeom::ToolpathGeometry toolpath;
    toolpath.setPacked(in.tpRaw, in.tpHasBounds ? &tpBox : nullptr, in.tpNv);
    report(QStringLiteral("toolpath"), &toolpath, toolpath.vertexCount(), 0, dumpDir, printReport,
           collect);

    gvgeom::ToolpathGeometry toolpathNoBounds;
    toolpathNoBounds.setPacked(in.tpRaw, nullptr, in.tpNv);
    report(QStringLiteral("toolpath_nobounds"), &toolpathNoBounds, toolpathNoBounds.vertexCount(), 0,
           dumpDir, printReport, collect);

    gvgeom::ToolpathGeometry toolpathEmpty;
    toolpathEmpty.setPacked(QByteArray(), nullptr, 0);
    report(QStringLiteral("toolpath_empty"), &toolpathEmpty, toolpathEmpty.vertexCount(), 0, dumpDir,
           printReport, collect);

    // ---- HLRLineGeometry: stride 48, контракт stepledges.vert ----
    const int hlrCount = int(in.hlrSegs32.size()) / 6;

    gvgeom::HLRLineGeometry hlr;
    hlr.setReference(in.hlrRef);
    hlr.setSegments(in.hlrSegs32.data(), hlrCount);
    report(QStringLiteral("hlr"), &hlr, hlr.vertexCount(), 0, dumpDir, printReport, collect);

    gvgeom::HLRLineGeometry hlrNoRef;
    hlrNoRef.setSegments(in.hlrSegs32.data(), hlrCount);
    report(QStringLiteral("hlr_noref"), &hlrNoRef, hlrNoRef.vertexCount(), 0, dumpDir, printReport,
           collect);

    gvgeom::HLRLineGeometry hlrEmpty;
    hlrEmpty.setSegments(nullptr, 0);
    report(QStringLiteral("hlr_empty"), &hlrEmpty, hlrEmpty.vertexCount(), 0, dumpDir, printReport,
           collect);

    // ---- GridGeometry ----
    gvgeom::GridGeometry gridGeo;
    gridGeo.setSize(at(in.grid, 0));
    gridGeo.setStep(at(in.grid, 1));
    gridGeo.setLevel(at(in.grid, 2));
    gridGeo.setCenterX(at(in.grid, 3));
    gridGeo.setCenterZ(at(in.grid, 4));
    gridGeo.setFeather(at(in.grid, 5));
    const int gridStride = gvgeom::GridGeometry::STRIDE;
    report(QStringLiteral("grid"), &gridGeo, int(gridGeo.vertexData().size()) / gridStride, 0,
           dumpDir, printReport, collect);

    gvgeom::GridGeometry gridFlatGeo;
    gridFlatGeo.setSize(at(in.gridFlat, 0));
    gridFlatGeo.setStep(at(in.gridFlat, 1));
    gridFlatGeo.setLevel(at(in.gridFlat, 2));
    gridFlatGeo.setCenterX(at(in.gridFlat, 3));
    gridFlatGeo.setCenterZ(at(in.gridFlat, 4));
    gridFlatGeo.setFeather(at(in.gridFlat, 5));
    report(QStringLiteral("grid_flat"), &gridFlatGeo,
           int(gridFlatGeo.vertexData().size()) / gridStride, 0, dumpDir, printReport, collect);

    // ---- AxisArrowGeometry ----
    gvgeom::AxisArrowGeometry axisGeo;
    axisGeo.setLength(at(in.axis, 0));
    axisGeo.setHeadLength(at(in.axis, 1));
    axisGeo.setHeadWidth(at(in.axis, 2));
    axisGeo.setShaftWidth(at(in.axis, 3));
    const int axisStride = gvgeom::AxisArrowGeometry::STRIDE;
    report(QStringLiteral("axis"), &axisGeo, int(axisGeo.vertexData().size()) / axisStride, 0,
           dumpDir, printReport, collect);

    gvgeom::AxisArrowGeometry axisShortGeo;
    axisShortGeo.setLength(at(in.axisShort, 0));
    axisShortGeo.setHeadLength(at(in.axisShort, 1));
    axisShortGeo.setHeadWidth(at(in.axisShort, 2));
    axisShortGeo.setShaftWidth(at(in.axisShort, 3));
    report(QStringLiteral("axis_short"), &axisShortGeo,
           int(axisShortGeo.vertexData().size()) / axisStride, 0, dumpDir, printReport, collect);

    // ---- MeshGeometry ----
    gvgeom::MeshGeometry mesh;
    mesh.setTriangles(in.meshVerts.data(), int(in.meshVerts.size()));
    report(QStringLiteral("mesh"), &mesh, mesh.triangleCount() * 3, mesh.triangleCount(), dumpDir,
           printReport, collect);

    gvgeom::MeshGeometry meshEmpty;
    meshEmpty.setTriangles(in.meshVerts.data(), int(in.meshVerts.size()));
    meshEmpty.reset();
    report(QStringLiteral("mesh_empty"), &meshEmpty, 0, meshEmpty.triangleCount(), dumpDir,
           printReport, collect);

    // ---- ToolGeometry ----
    gvgeom::ToolGeometry tool;
    tool.setProfile(in.toolProfile.data(), int(in.toolProfile.size()) / 2, in.toolSegments);
    report(QStringLiteral("tool"), &tool, tool.triangleCount() * 3, tool.triangleCount(), dumpDir,
           printReport, collect);

    gvgeom::ToolGeometry toolLow;
    toolLow.setProfile(in.toolLowProfile.data(), int(in.toolLowProfile.size()) / 2,
                       in.toolLowSegments);
    report(QStringLiteral("tool_low"), &toolLow, toolLow.triangleCount() * 3, toolLow.triangleCount(),
           dumpDir, printReport, collect);
}

// Файловый вход: каталог пишет питоновская проверка test_geometry_core.py.
bool loadInputs(const QString &inDir, Inputs *out, std::string *error)
{
    const auto file = [&inDir](const char *name) {
        return QDir(inDir).filePath(QLatin1String(name));
    };
    const auto fail = [error](const char *what) {
        if (error != nullptr)
            *error = what;
        return false;
    };

    std::vector<double> tpBounds, hlrRef, hlrSegs, grid, gridFlat, axis, axisShort;
    std::vector<double> meshVerts, toolProfile, toolLowProfile, toolSegments, toolLowSegments;
    if (!readBytes(file("tp_raw.bin"), &out->tpRaw)) return fail("tp_raw.bin");
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

    out->tpNv = int(out->tpRaw.size()) / gvgeom::ToolpathGeometry::STRIDE;
    out->tpHasBounds = true;
    out->tpBounds.lo = {at(tpBounds, 0), at(tpBounds, 1), at(tpBounds, 2)};
    out->tpBounds.hi = {at(tpBounds, 3), at(tpBounds, 4), at(tpBounds, 5)};
    out->hlrSegs32.assign(hlrSegs.begin(), hlrSegs.end());
    out->hlrRef[0] = at(hlrRef, 0);
    out->hlrRef[1] = at(hlrRef, 1);
    out->hlrRef[2] = at(hlrRef, 2);
    out->grid = grid;
    out->gridFlat = gridFlat;
    out->axis = axis;
    out->axisShort = axisShort;
    out->meshVerts = meshVerts;
    out->toolProfile = toolProfile;
    out->toolSegments = int(at(toolSegments, 0));
    out->toolLowProfile = toolLowProfile;
    out->toolLowSegments = int(at(toolLowSegments, 0));
    return true;
}

// Свой вход: траектория - из УП тем же путём, что рантайм (gvcore), остальное -
// те же числа, что пишет питоновская проверка (см. оговорку в шапке).
bool buildInputs(const std::string &ngcPath, Inputs *out, std::string *error)
{
    std::ifstream fh(std::filesystem::u8path(ngcPath), std::ios::binary);
    if (!fh) {
        if (error != nullptr)
            *error = "не открыт " + ngcPath;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(fh)), std::istreambuf_iterator<char>());
    const gv::ParseResult res = gv::parseGcode(text);
    const gv::PathData pd = gv::prepareOrdered(res.ordered, res.stages);
    out->tpRaw = QByteArray(reinterpret_cast<const char *>(pd.feed_stream.bytes.data()),
                            static_cast<int>(pd.feed_stream.bytes.size()));
    out->tpNv = pd.feed_stream.nv;
    out->tpHasBounds = pd.has_bounds;
    out->tpBounds = pd.bounds;

    // Контур: 7 обычных звеньев, вырожденное (ветка ln == 0) и вдоль оси Y.
    const auto addSeg = [out](double ax, double ay, double az, double bx, double by, double bz) {
        const float v[6] = {float(ax), float(ay), float(az), float(bx), float(by), float(bz)};
        out->hlrSegs32.insert(out->hlrSegs32.end(), v, v + 6);
    };
    for (int i = 0; i < 7; ++i)
        addSeg(i * 3.0, 0.0, i * 1.5, i * 3.0 + 10.0, 2.5 * i, i * 1.5);
    addSeg(5.0, 5.0, 5.0, 5.0, 5.0, 5.0);
    addSeg(0.0, 0.0, 0.0, 0.0, 10.0, 0.0);
    out->hlrRef[0] = 12.5;
    out->hlrRef[1] = -4.25;
    out->hlrRef[2] = 7.0;

    out->grid = {200.0, 10.0, -2.5, 3.25, -7.5, 0.45};
    out->gridFlat = {60.0, 7.0, 0.0, -1.5, 2.5, 0.0};
    out->axis = {12.0, 3.5, 2.0, 0.7};
    out->axisShort = {3.0, 4.0, 2.0, 0.7};

    // Меш: куб из 12 треугольников плюс вырожденный (ветка ln == 0 в нормалях).
    // Координаты не целые нарочно: на целых нормали совпали бы и при ошибке
    // округления.
    static const double kCube[8][3] = {{0.0, 0.0, 0.0},    {12.7, 0.0, 0.0},   {12.7, 12.7, 0.0},
                                      {0.0, 12.7, 0.0},   {0.0, 0.0, 6.3},    {12.7, 0.0, 6.3},
                                      {12.7, 12.7, 6.3},  {0.0, 12.7, 6.3}};
    static const int kFaces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1},
                                     {1, 5, 6}, {1, 6, 2}, {2, 6, 7}, {2, 7, 3}, {3, 7, 4}, {3, 4, 0}};
    for (const auto &face : kFaces) {
        for (int k = 0; k < 3; ++k) {
            out->meshVerts.push_back(kCube[face[k]][0]);
            out->meshVerts.push_back(kCube[face[k]][1]);
            out->meshVerts.push_back(kCube[face[k]][2]);
        }
    }
    for (int k = 0; k < 3; ++k) {
        out->meshVerts.push_back(1.1);
        out->meshVerts.push_back(2.2);
        out->meshVerts.push_back(3.3);
    }

    // Профиль инструмента: концевой фрез с радиусом скругления (BULL) - обе ветки
    // веера и четырёхугольники; те же метаданные UI_TOOL, что у проверки.
    gv::ToolInfo info;
    info.present = true;
    info.type = "BULL";
    info.diameter = 6.0;
    info.corner_radius = 1.0;
    info.flute_length = 20.0;
    info.shank_diameter = 6.0;
    info.body_length = 40.0;
    info.overall_length = 60.0;
    const std::vector<gv::ProfilePoint> prof = gv::toolProfile(info);
    for (const gv::ProfilePoint &p : prof) {
        out->toolProfile.push_back(p.radius_mm);
        out->toolProfile.push_back(p.height_mm);
    }
    out->toolSegments = 48;

    out->toolLowProfile = {0.0, 0.0, 3.0, 0.0, 3.0, 20.0, 0.0, 20.0};
    out->toolLowSegments = 3;
    return true;
}

// УП по умолчанию от корня репозитория (путь пробника); пусто при неудаче.
std::string defaultNgcPath()
{
    const std::string root = gv::manifest::repoRoot();
    if (root.empty())
        return {};
    return root + "/data/cad-testdata/Test_model.ngc";
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QStringList qargs = QGuiApplication::arguments();
    std::vector<std::string> args;
    args.reserve(std::size_t(qargs.size()));
    for (const QString &a : qargs)
        args.push_back(a.toStdString());

    const bool manifestMode = args.size() >= 2 && (args[1] == "--manifest" || args[1] == "--check");
    const bool selfMode = args.size() >= 2 && args[1] == "--self";

    Inputs inputs;
    QString dumpDir;
    if (manifestMode || selfMode) {
        std::string ngcPath;
        std::string manifestPath;
        if (manifestMode) {
            if (!gv::manifest::resolveCheckArgs(args, "Test_model.ngc", manifestPath, ngcPath))
                return 2;
        } else {
            ngcPath = args.size() >= 3 ? args[2] : defaultNgcPath();
            if (ngcPath.empty()) {
                std::fprintf(stderr, "geometry_probe: корень репозитория от своего каталога не "
                                     "найден: укажите файл УП явно\n");
                return 1;
            }
        }
        std::string error;
        if (!buildInputs(ngcPath, &inputs, &error)) {
            std::fprintf(stderr, "geometry_probe: вход не построен (%s)\n", error.c_str());
            return 1;
        }
        if (manifestMode) {
            // Сверка с оракулом: буферы своих геометрий против oracle_manifest.json.
            CaseBuffers collect;
            runCases(inputs, QString(), false, &collect);
            gv::manifest::Manifest manifest;
            if (!manifest.load(manifestPath, error)) {
                std::fprintf(stderr, "geometry_probe: манифест не прочитан (%s): %s\n",
                             manifestPath.c_str(), error.c_str());
                return 1;
            }
            gv::manifest::Checker checker(manifest);
            for (const auto &entry : collect) {
                checker.check("geom." + entry.first, "uint8",
                              gv::sha256Hex(std::vector<unsigned char>(entry.second.begin(),
                                                                       entry.second.end())),
                              {static_cast<long long>(entry.second.size())});
            }
            checker.printResult();
            return checker.failures() == 0 ? 0 : 1;
        }
        runCases(inputs, QString(), true, nullptr);
        return 0;
    }

    if (args.size() < 3) {
        std::fprintf(stderr, "usage: geometry_probe <input-dir> <dump-dir>\n"
                             "       geometry_probe --self [<file.ngc>]\n"
                             "       geometry_probe --manifest <oracle_manifest.json> [<file.ngc>]\n"
                             "       geometry_probe --check [<file.ngc>]\n");
        return 1;
    }
    dumpDir = QString::fromStdString(args[2]);
    QDir().mkpath(dumpDir);
    std::string error;
    if (!loadInputs(QString::fromStdString(args[1]), &inputs, &error)) {
        std::fprintf(stderr, "probe: не прочитан вход %s\n", error.c_str());
        return 1;
    }
    runCases(inputs, dumpDir, true, nullptr);
    return 0;
}
