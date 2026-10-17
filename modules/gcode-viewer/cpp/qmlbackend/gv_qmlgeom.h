// cpp/qmlbackend/gv_qmlgeom.h - QML-типы геометрий модуля MockBackend.
//
// Шесть геометрий этапа 3 (qt/gv_geometry, цель gvqtgeom) уже собирают буферы,
// равные питоновскому qt/gv_geometry.cpp байт в байт, но это обычные классы C++:
// QML их не видит. Классы ниже - тонкие наследники: буфер, атрибуты, stride и
// порядок вершин остаются кодом этапа 3, а здесь добавлено ровно то, чего требует
// контракт QML - регистрация в URI MockBackend 1.0, свойства с сигналами
// (GcodeViewer3D.qml задаёт их у GridGeometry и AxisArrowGeometry) и счётчики
// (vertexCount, triangleCount), которые читают проверки.
//
// Форматы буферов - контракт QML и шейдеров, а не деталь реализации:
// ToolpathGeometry stride 20 = position 3f + texcoord0 2f (toolpath.vert/.frag);
// HLRLineGeometry stride 48 = position vec4, normal vec4, texcoord0 vec4 (stepledges.vert);
// GridGeometry stride 28 = position 3f + RGBA 4f;
// AxisArrowGeometry stride 12 = position 3f;
// MeshGeometry stride 24 = position 3f + normal 3f;
// ToolGeometry stride 24 = position 3f + normal 3f.
// Разбор и разбор форматов - в qt/gv_geometry.h; здесь они не дублируются.
//
// Сетку и указатель оси QML создаёт сам по именам из MockBackend (см.
// GcodeViewer3D.qml); остальные четыре приходят свойствами контроллера.
#pragma once

#include <QByteArray>
#include <QQuick3DObject>
#include <QtQml/qqmlregistration.h>

#include "qt/gv_geometry.h"

namespace gvqml {

// ---------------------------------------------------------------------------
// ToolpathGeometry: линии траектории (stride 20), параметр пути в TexCoord0.x
// ---------------------------------------------------------------------------
class ToolpathGeometry : public gvgeom::ToolpathGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)

public:
    explicit ToolpathGeometry(QQuick3DObject *parent = nullptr);

    // Установка готового буфера (собирается вне GUI-потока) и уведомление QML.
    void setPacked(const QByteArray &raw, const gv::Aabb *bounds, int nv);

signals:
    void changed();
};

// ---------------------------------------------------------------------------
// HLRLineGeometry: ленты контура (stride 48) с отжимом от опорной точки
// ---------------------------------------------------------------------------
class HLRLineGeometry : public gvgeom::HLRLineGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)

public:
    explicit HLRLineGeometry(QQuick3DObject *parent = nullptr);

    void setReference(const double *center);
    void setSegments(const float *segs, int count);

signals:
    void changed();
};

// ---------------------------------------------------------------------------
// GridGeometry: сетка пола в плоскости XZ с растворением к краю
// ---------------------------------------------------------------------------
class GridGeometry : public gvgeom::GridGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double size READ size WRITE setSize NOTIFY sizeChanged)
    Q_PROPERTY(double step READ step WRITE setStep NOTIFY sizeChanged)
    Q_PROPERTY(double level READ level WRITE setLevel NOTIFY sizeChanged)
    Q_PROPERTY(double centerX READ centerX WRITE setCenterX NOTIFY sizeChanged)
    Q_PROPERTY(double centerZ READ centerZ WRITE setCenterZ NOTIFY sizeChanged)
    Q_PROPERTY(double feather READ feather WRITE setFeather NOTIFY sizeChanged)

public:
    explicit GridGeometry(QQuick3DObject *parent = nullptr);

    // Сеттер перекрывает базовый: тот же пересчёт буфера плюс сигнал QML.
    void setSize(double v);
    void setStep(double v);
    void setLevel(double v);
    void setCenterX(double v);
    void setCenterZ(double v);
    void setFeather(double v);

signals:
    void sizeChanged();
};

// ---------------------------------------------------------------------------
// AxisArrowGeometry: плоский двусторонний указатель оси (stride 12)
// ---------------------------------------------------------------------------
class AxisArrowGeometry : public gvgeom::AxisArrowGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double length READ length WRITE setLength NOTIFY changed)
    Q_PROPERTY(double headLength READ headLength WRITE setHeadLength NOTIFY changed)
    Q_PROPERTY(double headWidth READ headWidth WRITE setHeadWidth NOTIFY changed)
    Q_PROPERTY(double shaftWidth READ shaftWidth WRITE setShaftWidth NOTIFY changed)

public:
    explicit AxisArrowGeometry(QQuick3DObject *parent = nullptr);

    void setLength(double v);
    void setHeadLength(double v);
    void setHeadWidth(double v);
    void setShaftWidth(double v);

signals:
    void changed();
};

// ---------------------------------------------------------------------------
// MeshGeometry: сетка кожи (stride 24), плоские нормали считает этап 3
// ---------------------------------------------------------------------------
class MeshGeometry : public gvgeom::MeshGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY changed)

public:
    explicit MeshGeometry(QQuick3DObject *parent = nullptr);

    void setTriangles(const double *verts, int nFloats);
    void setTriangles(const double *verts, int nFloats, const double *normals);
    void reset();

signals:
    void changed();
};

// ---------------------------------------------------------------------------
// ToolGeometry: тело вращения по профилю инструмента/оправки (stride 24)
// ---------------------------------------------------------------------------
class ToolGeometry : public gvgeom::ToolGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY changed)

public:
    explicit ToolGeometry(QQuick3DObject *parent = nullptr);

    void setProfile(const double *profile, int count, int segments = 0);

signals:
    void changed();
};

} // namespace gvqml
