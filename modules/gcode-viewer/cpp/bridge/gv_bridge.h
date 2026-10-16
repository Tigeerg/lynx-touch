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

#ifdef __cplusplus
}
#endif
