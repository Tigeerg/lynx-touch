// bridge/gv_bridge.cpp - реализация C-ABI (см. gv_bridge.h). Слой тонкий: вся
// работа лежит в gvcore (нормали) и gvstep (STEP), здесь только буферы, коды
// возврата и текст ошибки.
//
// Память: каждый выходной буфер выделяется std::malloc и освобождается gv_free.
// Исключения ядра (std::runtime_error из чтения STEP и любые другие) не выходят
// за границу ABI: они превращаются в код возврата и текст в gv_last_error.
#include "bridge/gv_bridge.h"

#include "core/normals.h"
#include "occt/step_bundle.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <vector>

namespace {

// Текст ошибки на поток: OCCT и питоновский воркер читают модель из разных
// потоков, общая строка на всех перепутала бы сообщения.
thread_local std::string g_lastError;

void setError(const std::string &text)
{
    g_lastError = text;
}

// Копия вектора в память библиотеки. Пустой вектор - NULL: длину вызывающий
// знает из out-параметра, а нулевое выделение malloc может вернуть чем угодно.
template <typename T>
T *copyToLibrary(const std::vector<T> &src)
{
    if (src.empty())
        return nullptr;
    T *mem = static_cast<T *>(std::malloc(src.size() * sizeof(T)));
    if (mem == nullptr)
        return nullptr;
    std::memcpy(mem, src.data(), src.size() * sizeof(T));
    return mem;
}

const char *safeWhat(const std::exception &exc)
{
    return exc.what() != nullptr ? exc.what() : "неизвестная ошибка";
}

} // namespace

extern "C" {

int gv_abi_version(void)
{
    return GV_ABI_VERSION;
}

const char *gv_last_error(void)
{
    return g_lastError.c_str();
}

void gv_free(void *mem)
{
    std::free(mem);
}

int gv_smooth_face_normals(const double *verts, size_t vert_count, double crease_deg,
                           float **out_normals, size_t *out_count)
{
    if (out_normals != nullptr)
        *out_normals = nullptr;
    if (out_count != nullptr)
        *out_count = 0;
    if (verts == nullptr || out_normals == nullptr || out_count == nullptr) {
        setError("gv_smooth_face_normals: пустой указатель на вход или выход");
        return GV_ERR_ARG;
    }
    if (vert_count == 0 || vert_count % 9 != 0) {
        setError("gv_smooth_face_normals: вершин " + std::to_string(vert_count)
                 + ", ожидалось кратно 9 (суп по три вершины на треугольник)");
        return GV_ERR_ARG;
    }
    if (!(crease_deg > 0.0) || crease_deg >= 180.0) {
        setError("gv_smooth_face_normals: порог сглаживания вне (0, 180) градусов");
        return GV_ERR_ARG;
    }
    try {
        const std::vector<double> soup(verts, verts + vert_count);
        const std::vector<float> normals = gv::smoothFaceNormals(soup, crease_deg);
        float *mem = copyToLibrary(normals);
        if (mem == nullptr && !normals.empty()) {
            setError("gv_smooth_face_normals: не удалось выделить память");
            return GV_ERR_MEM;
        }
        *out_normals = mem;
        *out_count = normals.size();
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_smooth_face_normals: ") + safeWhat(exc));
        return GV_ERR;
    } catch (...) {
        setError("gv_smooth_face_normals: неизвестное исключение");
        return GV_ERR;
    }
}

int gv_load_step_model(const char *utf8_path, double deflection,
                       double **out_verts, size_t *out_vert_count,
                       float **out_normals, size_t *out_normal_count,
                       double **out_segments, size_t *out_segment_count)
{
    double *verts = nullptr;
    float *normals = nullptr;
    double *segments = nullptr;
    if (out_verts != nullptr)
        *out_verts = nullptr;
    if (out_vert_count != nullptr)
        *out_vert_count = 0;
    if (out_normals != nullptr)
        *out_normals = nullptr;
    if (out_normal_count != nullptr)
        *out_normal_count = 0;
    if (out_segments != nullptr)
        *out_segments = nullptr;
    if (out_segment_count != nullptr)
        *out_segment_count = 0;
    if (utf8_path == nullptr || out_verts == nullptr || out_vert_count == nullptr
            || out_normals == nullptr || out_normal_count == nullptr
            || out_segments == nullptr || out_segment_count == nullptr) {
        setError("gv_load_step_model: пустой указатель на путь или на выход");
        return GV_ERR_ARG;
    }
    if (!(deflection > 0.0)) {
        setError("gv_load_step_model: допуск тесселяции должен быть больше нуля");
        return GV_ERR_ARG;
    }

    try {
        const gv::StepBundle bundle = gv::loadStepBundle(utf8_path, deflection);
        verts = copyToLibrary(bundle.verts);
        normals = copyToLibrary(bundle.normals);
        segments = copyToLibrary(bundle.segments);
        if ((verts == nullptr && !bundle.verts.empty())
                || (normals == nullptr && !bundle.normals.empty())
                || (segments == nullptr && !bundle.segments.empty())) {
            std::free(verts);
            std::free(normals);
            std::free(segments);
            setError("gv_load_step_model: не удалось выделить память");
            return GV_ERR_MEM;
        }
        *out_verts = verts;
        *out_vert_count = bundle.verts.size();
        *out_normals = normals;
        *out_normal_count = bundle.normals.size();
        *out_segments = segments;
        *out_segment_count = bundle.segments.size();
        return GV_OK;
    } catch (const std::exception &exc) {
        setError(std::string("gv_load_step_model: ") + safeWhat(exc));
        return GV_ERR_STEP;
    } catch (...) {
        setError("gv_load_step_model: неизвестное исключение");
        return GV_ERR_STEP;
    }
}

} // extern "C"
