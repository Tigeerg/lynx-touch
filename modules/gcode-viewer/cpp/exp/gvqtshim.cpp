// cpp/exp/gvqtshim.cpp - реализация C-ABI шима эксперимента (этап 5.3).
//
// Здесь проверяется не алгоритм, а загрузка: Qt-объекты (настоящий
// gvgeom::ToolpathGeometry этапа 3) создаются в куче C++-библиотеки, собранной
// против C:\Qt\6.12.0\msvc2022_64, и отдаются интерпретатору PySide6 как void*.
//
// Список живых объектов (gvq_alive) - часть эксперимента, а не удобство: он
// показывает, забрал ли shiboken6 владение указателем и удалил ли объект.
// Указатель сравнивается как значение, разыменования нет.
//
// Вторая часть файла - сборка буферов геометрий этапа 3 по заказу живого
// вьювера (этап 5.3а): gvq_make_* отдают настоящие gvgeom-геометрии, оболочка
// gv_qtgeom.py читает из них буфер и габарит и освобождает объект.

#include "exp/gvqtshim.h"

#include "qt/gv_geometry.h"

#include <QQuick3DGeometry>
#include <QString>
#include <QVariant>
#include <QVector3D>

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace {

// Живые объекты, выданные наружу. Ключ - адрес объекта как QQuick3DGeometry*.
std::mutex g_lock;
std::set<void *> g_live;

void remember(void *base)
{
    std::lock_guard<std::mutex> guard(g_lock);
    g_live.insert(base);
}

void forget(void *base)
{
    std::lock_guard<std::mutex> guard(g_lock);
    g_live.erase(base);
}

// Тестовая геометрия: пустой объект без наполнения. Питоновская сторона
// вызывает на нём setStride/setVertexData/addAttribute сама.
class TestGeometry : public QQuick3DGeometry
{
public:
    TestGeometry()
    {
        remember(static_cast<QQuick3DGeometry *>(this));
    }
    ~TestGeometry() override
    {
        forget(static_cast<QQuick3DGeometry *>(this));
    }
};

// Настоящая геометрия этапа 3 под тем же учётом живости.
class ShimToolpath : public gvgeom::ToolpathGeometry
{
public:
    ShimToolpath()
    {
        remember(static_cast<QQuick3DGeometry *>(this));
    }
    ~ShimToolpath() override
    {
        forget(static_cast<QQuick3DGeometry *>(this));
    }
};

} // namespace

extern "C" {

const char *gvq_qt_build_version(void)
{
    return QT_VERSION_STR;
}

const char *gvq_qt_runtime_version(void)
{
    return qVersion();
}

const char *gvq_qt_core_path(void)
{
#if defined(_WIN32)
    static QString path;
    path.clear();
    HMODULE handle = GetModuleHandleW(L"Qt6Core.dll");
    if (!handle)
        return "";
    wchar_t buf[MAX_PATH * 4];
    const DWORD len = GetModuleFileNameW(handle, buf, DWORD(sizeof(buf) / sizeof(buf[0])));
    if (!len)
        return "";
    path = QString::fromWCharArray(buf, int(len));
    return path.toUtf8().constData();
#else
    return "";
#endif
}

void *gvq_make_test_geometry(void)
{
    return static_cast<QQuick3DGeometry *>(new TestGeometry());
}

void *gvq_make_toolpath(void)
{
    return static_cast<QQuick3DGeometry *>(new ShimToolpath());
}

void gvq_toolpath_set_packed(void *obj, const unsigned char *raw, int raw_bytes,
                             const double *bounds_lo_hi, int nv)
{
    if (!obj)
        return;
    gvgeom::ToolpathGeometry *geo = dynamic_cast<gvgeom::ToolpathGeometry *>(
            static_cast<QQuick3DGeometry *>(obj));
    if (!geo)
        return;
    const QByteArray packed(reinterpret_cast<const char *>(raw), raw_bytes);
    if (bounds_lo_hi) {
        gv::Aabb box;
        box.lo = {bounds_lo_hi[0], bounds_lo_hi[1], bounds_lo_hi[2]};
        box.hi = {bounds_lo_hi[3], bounds_lo_hi[4], bounds_lo_hi[5]};
        geo->setPacked(packed, &box, nv);
    } else {
        geo->setPacked(packed, nullptr, nv);
    }
}

int gvq_toolpath_vertex_count(void *obj)
{
    if (!obj)
        return -1;
    gvgeom::ToolpathGeometry *geo = dynamic_cast<gvgeom::ToolpathGeometry *>(
            static_cast<QQuick3DGeometry *>(obj));
    return geo ? geo->vertexCount() : -1;
}

// ---------------------------------------------------------------------------
// Геометрии этапа 3 для живого вьювера (этап 5.3а)
// ---------------------------------------------------------------------------
// Объекты этих функций в список живых (g_live) не попадают: владеет ими
// питоновская оболочка, освобождает она же через gvq_free_object. Список живых
// остаётся за пробными объектами эксперимента.

void *gvq_build_toolpath(const unsigned char *raw, int raw_bytes,
                        const double *bounds_lo_hi, int has_bounds, int nv)
{
    auto *geo = new gvgeom::ToolpathGeometry();
    const QByteArray packed = (raw && raw_bytes > 0)
            ? QByteArray(reinterpret_cast<const char *>(raw), raw_bytes)
            : QByteArray();
    if (bounds_lo_hi && has_bounds) {
        gv::Aabb box;
        box.lo = {bounds_lo_hi[0], bounds_lo_hi[1], bounds_lo_hi[2]};
        box.hi = {bounds_lo_hi[3], bounds_lo_hi[4], bounds_lo_hi[5]};
        geo->setPacked(packed, &box, nv);
    } else {
        geo->setPacked(packed, nullptr, nv);
    }
    return static_cast<QQuick3DGeometry *>(geo);
}

void *gvq_build_hlr(const float *segs, int seg_count, const double *ref)
{
    auto *geo = new gvgeom::HLRLineGeometry();
    if (ref)
        geo->setReference(ref);
    geo->setSegments(segs, seg_count);
    return static_cast<QQuick3DGeometry *>(geo);
}

void *gvq_build_grid(const double *params)
{
    auto *geo = new gvgeom::GridGeometry();
    geo->setSize(params[0]);
    geo->setStep(params[1]);
    geo->setLevel(params[2]);
    geo->setCenterX(params[3]);
    geo->setCenterZ(params[4]);
    geo->setFeather(params[5]);
    return static_cast<QQuick3DGeometry *>(geo);
}

void *gvq_build_axis(const double *params)
{
    auto *geo = new gvgeom::AxisArrowGeometry();
    geo->setLength(params[0]);
    geo->setHeadLength(params[1]);
    geo->setHeadWidth(params[2]);
    geo->setShaftWidth(params[3]);
    return static_cast<QQuick3DGeometry *>(geo);
}

void *gvq_build_mesh(const double *verts, int vert_floats, const double *normals)
{
    auto *geo = new gvgeom::MeshGeometry();
    if (normals)
        geo->setTriangles(verts, vert_floats, normals);
    else
        geo->setTriangles(verts, vert_floats);
    return static_cast<QQuick3DGeometry *>(geo);
}

void *gvq_build_tool(const double *profile, int count, int segments)
{
    auto *geo = new gvgeom::ToolGeometry();
    geo->setProfile(profile, count, segments);
    return static_cast<QQuick3DGeometry *>(geo);
}

void gvq_mesh_reset(void *obj)
{
    if (!obj)
        return;
    if (auto *geo = dynamic_cast<gvgeom::MeshGeometry *>(
                static_cast<QQuick3DGeometry *>(obj)))
        geo->reset();
}

int gvq_vertex_copy(void *obj, unsigned char **data, int *len)
{
    if (!obj || !data || !len)
        return -1;
    *data = nullptr;
    *len = 0;
    const QByteArray bytes = static_cast<QQuick3DGeometry *>(obj)->vertexData();
    if (bytes.isEmpty())
        return 0;
    unsigned char *out = static_cast<unsigned char *>(std::malloc(std::size_t(bytes.size())));
    if (!out)
        return -2;
    std::memcpy(out, bytes.constData(), std::size_t(bytes.size()));
    *data = out;
    *len = int(bytes.size());
    return 1;
}

void gvq_buf_free(unsigned char *data)
{
    std::free(data);
}

int gvq_bounds_unpadded(void *obj, double *out_lo_hi)
{
    if (!obj || !out_lo_hi)
        return -1;
    QQuick3DGeometry *base = static_cast<QQuick3DGeometry *>(obj);
    if (auto *geo = dynamic_cast<gvgeom::ToolpathGeometry *>(base)) {
        const gvgeom::BoundsOrNone &b = geo->boundsOrNone();
        if (!b.present)
            return 0;
        for (int j = 0; j < 3; ++j) {
            out_lo_hi[j] = b.box.lo[j];
            out_lo_hi[3 + j] = b.box.hi[j];
        }
        return 1;
    }
    if (auto *geo = dynamic_cast<gvgeom::HLRLineGeometry *>(base)) {
        const gvgeom::BoundsOrNone &b = geo->boundsOrNone();
        if (!b.present)
            return 0;
        for (int j = 0; j < 3; ++j) {
            out_lo_hi[j] = b.box.lo[j];
            out_lo_hi[3 + j] = b.box.hi[j];
        }
        return 1;
    }
    if (auto *geo = dynamic_cast<gvgeom::MeshGeometry *>(base)) {
        for (int j = 0; j < 3; ++j) {
            out_lo_hi[j] = geo->lo()[j];
            out_lo_hi[3 + j] = geo->hi()[j];
        }
        return 1;
    }
    if (auto *geo = dynamic_cast<gvgeom::ToolGeometry *>(base)) {
        for (int j = 0; j < 3; ++j) {
            out_lo_hi[j] = geo->lo()[j];
            out_lo_hi[3 + j] = geo->hi()[j];
        }
        return 1;
    }
    return -1;                      // сетка и указатель оси габарита не хранят
}

int gvq_object_kind(void *obj)
{
    if (!obj)
        return 0;
    QQuick3DGeometry *base = static_cast<QQuick3DGeometry *>(obj);
    if (dynamic_cast<gvgeom::ToolpathGeometry *>(base))
        return 2;
    if (dynamic_cast<TestGeometry *>(base))
        return 1;
    return 0;
}

int gvq_alive(void *obj)
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_live.count(obj) ? 1 : 0;
}

int gvq_live_count(void)
{
    std::lock_guard<std::mutex> guard(g_lock);
    return int(g_live.size());
}

void gvq_free_object(void *obj)
{
    if (!obj)
        return;
    delete static_cast<QQuick3DGeometry *>(obj);
}

} // extern "C"
