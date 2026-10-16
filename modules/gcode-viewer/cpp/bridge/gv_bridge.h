// bridge/gv_bridge.h - C-ABI над Qt-free слоями ядра C++ (этап 5 плана
// переноса): сюда смотрит питоновский шим modules/gcode-viewer/gv_cpp.py через
// ctypes.
//
// Правила ABI (их держит и проверка test_cpp_bridge.py):
//   * наружу только C-типы: int, size_t, double*, float*, const char*, void*;
//     ни один C++-класс, шаблон или std::string через границу не проходит;
//   * выходные массивы - через out-параметры; память выделяет библиотека
//     (malloc) и освобождает её же gv_free; *out обнуляется при любой неудаче;
//   * код возврата: 0 - успех, отрицательное число - ошибка (текст последней
//     ошибки отдаёт gv_last_error, он же попадает в лог Python);
//   * исключения наружу не выходят: всё ловится внутри и превращается в код.
//
// Qt в этой библиотеке нет вообще; OCCT есть только там, где без него не
// обойтись (чтение STEP), и его DLL подгружает шим.
#pragma once

#include <stddef.h>

#if defined(_WIN32)
#  define GV_API __declspec(dllexport)
#else
#  define GV_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Версия ABI: меняется при несовместимой правке контракта ниже.
#define GV_ABI_VERSION 1

// Коды возврата.
#define GV_OK 0
#define GV_ERR 1          // ошибка вычисления (текст - в gv_last_error)
#define GV_ERR_ARG 2      // негодный аргумент
#define GV_ERR_MEM 3      // не удалось выделить память
#define GV_ERR_STEP 4     // чтение STEP не прошло

// Версия ABI и последняя ошибка этого потока (строка в UTF-8, живёт до
// следующего вызова gv_last_error в этом же потоке).
GV_API int gv_abi_version(void);
GV_API const char *gv_last_error(void);

// Освобождение памяти, выданной библиотекой в out-параметрах. Вызов с NULL и с
// указателем, выданным не этой библиотекой, запрещён.
GV_API void gv_free(void *mem);

// Crease-aware нормали кожи: verts - суп вершин float64 (F*3,3), vert_count
// кратно 9; crease_deg - порог сглаживания в градусах. Результат - (F,3,3)
// float32 в *out_normals, число элементов (F*9) в *out_count.
GV_API int gv_smooth_face_normals(const double *verts, size_t vert_count,
                                  double crease_deg,
                                  float **out_normals, size_t *out_count);

// Модель STEP целиком: суп вершин float64 (F*9), нормали float32 (F*9) и рёбра
// формы до склейки float64 (N*6, пары a,b подряд). Путь - UTF-8 (так его
// принимает OCCT), deflection - линейный допуск тесселяции, мм.
// Пустой суп - успех с нулевыми длинами (как у эталона normals = None).
GV_API int gv_load_step_model(const char *utf8_path, double deflection,
                              double **out_verts, size_t *out_vert_count,
                              float **out_normals, size_t *out_normal_count,
                              double **out_segments, size_t *out_segment_count);

// ---------------------------------------------------------------------------
// Аналитические поверхности вращения STEP (этап 5 плана переноса, часть 2).
//
// Раскладка дескрипторов та же, что у пробника cpp/tangents_probe.cpp, поэтому
// сверка моста и пробника с питоновским эталоном общая:
//   * kinds - по одному int на поверхность в порядке обхода граней
//     TopExp_Explorer: 0 - цилиндр, 1 - конус;
//   * cylinders - по 17 чисел float64 на цилиндр: o(3), d(3), e1(3), e2(3),
//     r, t0, t1, a0, a1; полный круг вместо дуги - NaN в a0 и a1;
//   * cones - по 9 чисел float64 на конус: a(3), d(3), psi (рад), s0, s1.
// Цилиндры и конусы идут отдельными массивами, а порядок списка восстанавливают
// kinds: цилиндры берутся из cylinders по порядку, конусы - из cones.
typedef struct GvSurfaces {
    int *kinds;
    size_t surface_count;
    double *cylinders;
    size_t cylinder_count;
    double *cones;
    size_t cone_count;
} GvSurfaces;

// Поверхности одного файла (чтение STEP внутри). Пустой список - успех с
// нулевыми длинами.
GV_API int gv_load_step_surfaces(const char *utf8_path, double deflection,
                                 GvSurfaces *out);

// Модель STEP и поверхности за одно чтение файла: второй проход ради
// поверхностей стоил бы ещё около 0.8 с на модель. Раскладка выходов та же, что
// у gv_load_step_model и gv_load_step_surfaces.
GV_API int gv_load_step_all(const char *utf8_path, double deflection,
                            double **out_verts, size_t *out_vert_count,
                            float **out_normals, size_t *out_normal_count,
                            double **out_segments, size_t *out_segment_count,
                            GvSurfaces *out_surfaces);

// Освобождение массивов GvSurfaces (каждый указатель структуры - по отдельности).
GV_API void gv_free_surfaces(GvSurfaces *surfaces);

// ---------------------------------------------------------------------------
// Топология силуэта и силуэт меша (этап 5 плана переноса, часть 2).
//
// Поля - те же, что у MeshTopology в gv_types.py: ea/eb (K,3) float32 - концы
// кандидатных силуэтных рёбер, f0/f1 (K,) int - смежные грани (f0 < f1),
// n (F,3) float32 - нормали граней, d (F,) float32 - предвычисленное n*cen.
typedef struct GvMeshTopology {
    float *ea;
    float *eb;
    int *f0;
    int *f1;
    float *n;
    float *d;
    size_t edge_count;      // K - кандидатных рёбер
    size_t face_count;      // F - граней
} GvMeshTopology;

// Топология по супу вершин float64 (F*9: по три вершины на треугольник).
// Пустой суп - успех с нулевыми длинами.
GV_API int gv_build_silhouette_topology(const double *soup, size_t soup_count,
                                        GvMeshTopology *out);

// Силуэтные рёбра на глазу eye_mm (raw-кадр модели): (K,2,3) float32 плоским
// рядом, K*6 элементов. Пустой силуэт - успех с нулевой длиной.
GV_API int gv_mesh_silhouette_segments(const GvMeshTopology *topology,
                                       const double eye_mm[3],
                                       float **out_segments, size_t *out_count);

// Освобождение массивов GvMeshTopology.
GV_API void gv_free_mesh_topology(GvMeshTopology *topology);

// ---------------------------------------------------------------------------
// Семейства касательных STEP (этап 5 плана переноса, часть 2).
//
// Поля - те же, что у CylinderFamilies/TangentFamilies в gv_types.py: O/D/E1/E2
// (N,3), R/T0/T1/RREF/A0/A1 (N,), FULL (N,) 0/1, DROP (N,8) с NaN в незанятых
// ячейках, ARC (N,64) 0/1, cones (K,9): a(3), d(3), psi, s0, s1.
typedef struct GvTangentFamilies {
    double *O;
    double *D;
    double *E1;
    double *E2;
    double *R;
    double *T0;
    double *T1;
    double *RREF;
    double *A0;
    double *A1;
    unsigned char *FULL;
    double *DROP;
    unsigned char *ARC;
    size_t count;              // N - логических цилиндров
    double *cones;
    size_t cone_count;
} GvTangentFamilies;

// Предвычисление семейств. Поверхности передаются той же раскладкой, что
// отдают gv_load_step_surfaces и gv_load_step_all; packed_raw - упакованная
// обводка STEP (float32, 5 чисел на вершину, две вершины на отрезок) или NULL;
// skin_soup (F*9 float64) и skin_normals (F,3,3 float32) - кожа для политики
// конусов, либо NULL (политика без кожи, как у эталона skin = None).
GV_API int gv_tangent_families(const GvSurfaces *surfaces,
                               const unsigned char *packed_raw, size_t packed_bytes,
                               int packed_nv,
                               const double *skin_soup, size_t skin_soup_count,
                               const float *skin_normals, size_t skin_normal_count,
                               GvTangentFamilies *out);

// Отрезки касательных на глазу: (M,2,3) float64 плоским рядом, M*6 элементов.
GV_API int gv_tangent_segments(const GvTangentFamilies *families,
                               const double eye_mm[3],
                               double **out_segments, size_t *out_count);

// Освобождение массивов GvTangentFamilies.
GV_API void gv_free_tangent_families(GvTangentFamilies *families);

// ---------------------------------------------------------------------------
// Парсер управляющей программы и подготовка траектории (этап 5, часть 2).
//
// Поля - те же, что у PathData в gv_types.py. Подписи и типы инструмента этапов
// едут двумя текстовыми блоками UTF-8: строку через границу ABI не передать.
// stages - по 16 чисел float64 на этап: start, end, has_tool и двенадцать
// числовых полей UI_TOOL (number, diameter, corner_radius, taper_deg,
// tip_angle_deg, tip_diameter, flute_length, shoulder_length, body_length,
// shank_diameter, overall_length, flutes); последняя ячейка пустая (запас).
typedef struct GvPathData {
    int seg_count;
    double last[3];
    double *pts;                    // (N*3) float64, координаты сцены (X, Z, -Y)
    double *cum;                    // (N) float64, накопленная длина, мм
    unsigned char *rapid;           // (N) 0/1 - флаг ИСХОДЯЩЕГО сегмента
    double *speeds;                 // (N) float64 - скорость ИСХОДЯЩЕГО сегмента
    size_t point_count;             // N
    double total;                   // полная длина пути, мм
    unsigned char *rapid_stream;    // float32, 5 чисел на вершину, 2 вершины на отрезок
    size_t rapid_bytes;
    int rapid_nv;
    unsigned char *feed_stream;
    size_t feed_bytes;
    int feed_nv;
    int has_bounds;
    double bounds[6];               // x0 y0 z0 x1 y1 z1 в координатах сцены
    double center[3];               // центр габарита в сцене
    double radius;                  // половина диагонали габарита
    double min_y;                   // нижняя точка пути в сцене
    double *stages;                 // (S*16)
    size_t stage_count;             // S
    char *stage_labels;             // подписи этапов UTF-8 подряд
    size_t *stage_label_at;         // (S+1) смещения подписей
    char *stage_types;              // типы инструмента этапов UTF-8 подряд
    size_t *stage_type_at;          // (S+1) смещения типов
} GvPathData;

// Разбор программы и подготовка траектории одним вызовом: эталон делает это
// парой parse_gcode + _prepare_ordered, а через границу дешевле передать готовый
// результат. Пустой путь - успех с нулевыми длинами.
GV_API int gv_prepare_path(const char *utf8_text, GvPathData *out);

// Освобождение массивов GvPathData.
GV_API void gv_free_path_data(GvPathData *data);

#ifdef __cplusplus
}
#endif
