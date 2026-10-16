// cpp/exp/gvqtshim.h - C-ABI обычной SHARED-библиотеки с Qt-объектами (эксперимент
// этапа 5.3 плана PORT-CPP.md). Вопрос эксперимента: можно ли грузить C++-код,
// собранный против C:\Qt\6.12.0\msvc2022_64, внутрь процесса PySide6, который
// несёт свою Qt 6.12.0 в site-packages.
//
// Библиотека нарочно НЕ Qt-плагин (не MODULE, без метаданных QML): сначала
// проверяется самый простой путь - ctypes.CDLL + shiboken6.wrapInstance.
//
// Второе назначение той же библиотеки (этап 5.3а плана): сборка буферов
// геометрий этапа 3 для живого вьювера. Питоновская оболочка gv_qtgeom.py
// строительством не занимается - она только ставит готовые байты в геометрию,
// которой владеет контроллер (см. gvq_make_* в конце файла).
//
// Наружу только C-типы. Указатели на QQuick3DGeometry отдаются как void*:
// питоновская сторона оборачивает их shiboken6.wrapInstance(ptr, QQuick3DGeometry).
#pragma once

#if defined(_WIN32)
#  define GVQ_API __declspec(dllexport)
#else
#  define GVQ_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Версия Qt, ПРОТИВ которой собран этот код (QT_VERSION_STR, время компиляции).
GVQ_API const char *gvq_qt_build_version(void);

// Версия Qt, которая ФАКТИЧЕСКИ загружена в этом процессе (qVersion(), время
// исполнения). Расхождение с первой строкой - признак подмены DLL загрузчиком.
GVQ_API const char *gvq_qt_runtime_version(void);

// Полный путь загруженной Qt6Core.dll, как его видит сам C++ этого процесса.
// Пустой строкой, если модуль не найден или платформа не Windows.
GVQ_API const char *gvq_qt_core_path(void);

// Тестовая геометрия: пустой объект QQuick3DGeometry на куче. Питоновская сторона
// наполняет его сама (setStride, setVertexData, addAttribute) - это проверка
// того, что объект живой, а не только обёртка вокруг указателя.
GVQ_API void *gvq_make_test_geometry(void);

// Настоящая геометрия этапа 3: gvgeom::ToolpathGeometry (stride 20).
GVQ_API void *gvq_make_toolpath(void);

// Установка упакованного буфера: raw - уже собранные float32 (position 3f +
// texcoord 2f на вершину), bounds_lo_hi - шесть double (x0,y0,z0,x1,y1,z1) или
// NULL (нет габарита), nv - число вершин.
GVQ_API void gvq_toolpath_set_packed(void *obj, const unsigned char *raw, int raw_bytes,
                                     const double *bounds_lo_hi, int nv);

// Число вершин у gvgeom::ToolpathGeometry (через базу этого метода не видно).
GVQ_API int gvq_toolpath_vertex_count(void *obj);

// ---------------------------------------------------------------------------
// Геометрии этапа 3 для живого вьювера (этап 5.3а плана переноса)
// ---------------------------------------------------------------------------
// Каждая функция создаёт настоящую gvgeom-геометрию (ту же, что сверена с
// питоновским эталоном geometry.py), применяет вход и отдаёт указатель на её базу
// QQuick3DGeometry. Питоновская оболочка (modules/gcode-viewer/gv_qtgeom.py)
// читает буфер и габарит и ставит их в QQuick3DGeometry, которой владеет QML;
// сам объект освобождается gvq_free_object. Владение всё время у C++.

// Упакованные линии траектории: raw - готовые float32, bounds_lo_hi - шесть
// double (x0,y0,z0,x1,y1,z1), has_bounds=0 - габарита нет.
GVQ_API void *gvq_build_toolpath(const unsigned char *raw, int raw_bytes,
                                const double *bounds_lo_hi, int has_bounds, int nv);

// Ленты контура: segs - seg_count отрезков по 6 float32, ref - опорная точка
// (NULL - нет), от неё запекается радиальный отжим FLOAT_SCALE.
GVQ_API void *gvq_build_hlr(const float *segs, int seg_count, const double *ref);

// Сетка: params - size, step, level, centerX, centerZ, feather (шесть double).
GVQ_API void *gvq_build_grid(const double *params);

// Указатель оси: params - length, headLength, headWidth, shaftWidth (четыре double).
GVQ_API void *gvq_build_axis(const double *params);

// Сетка треугольников: verts - vert_floats значений float64 (кратно девяти),
// normals - столько же или NULL (плоские нормали считает геометрия).
GVQ_API void *gvq_build_mesh(const double *verts, int vert_floats, const double *normals);

// Сброс сетки: зеркало MeshGeometry::reset (в эталоне - reset() после
// set_triangles: пустой буфер, но шаг вершины остаётся от прошлого наполнения).
GVQ_API void gvq_mesh_reset(void *obj);

// Тело вращения по профилю: profile - count пар (radius_mm, height_mm) в float64,
// segments <= 0 - умолчание геометрии (48).
GVQ_API void *gvq_build_tool(const double *profile, int count, int segments);

// Копия буфера вершин: память выделяет библиотека, освобождает вызывающий
// (gvq_buf_free). Именно копия, а не указатель внутрь объекта: сетка и указатель
// оси буфер в поле не хранят, а оболочке он нужен уже после gvq_free_object.
// Код возврата: 1 - буфер скопирован, 0 - буфер пуст (data = NULL), -1 - плохой
// объект, -2 - нет памяти.
GVQ_API int gvq_vertex_copy(void *obj, unsigned char **data, int *len);

GVQ_API void gvq_buf_free(unsigned char *data);

// Габарит, как его хранит геометрия (то, что эталон держит в _bounds/_lo,_hi):
// 1 - габарит есть, 0 - нет, -1 - класс такого габарита не хранит (сетка и
// указатель оси). Шесть double на выходе: x0,y0,z0,x1,y1,z1.
GVQ_API int gvq_bounds_unpadded(void *obj, double *out_lo_hi);

// Тип объекта: 0 - неизвестный, 1 - тестовая геометрия, 2 - ToolpathGeometry.
GVQ_API int gvq_object_kind(void *obj);

// Жив ли ещё объект, выданный gvq_make_*: библиотека снимает с учёта объект в
// деструкторе. Нужен, чтобы отличить «shiboken6 забрал владение и удалил» от
// «владение осталось у C++». Чтения по указателю здесь нет - только поиск в
// списке живых, поэтому вызов безопасен и после удаления.
GVQ_API int gvq_alive(void *obj);

// Сколько объектов, выданных gvq_make_*, ещё живо.
GVQ_API int gvq_live_count(void);

// Освобождение объекта, выданного gvq_make_*. Повторный вызов и вызов после
// удаления питоновской стороной запрещён.
GVQ_API void gvq_free_object(void *obj);

#ifdef __cplusplus
}
#endif
