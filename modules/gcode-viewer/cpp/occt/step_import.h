// occt/step_import.h - импорт STEP: B-rep, тесселяция, ориентация оболочки.
//
// Зеркало modules/gcode-viewer/step_import.py на OCCT SDK напрямую: эталон
// ходит в те же OCCT-классы через питоновские биндинги OCP и платит за это
// маршалингом на каждой вершине и треугольнике.
//
// Рёбра STEP здесь не строятся: они уже перенесены плагином CNC.Edges
// (cpp/stepledgesgeometry.cpp, обход TopAbs_EDGE + GCPnts_UniformDeflection +
// склейка) и сверены с эталоном в test_stepledges_plugin.py. Дублировать этот
// путь значило бы завести вторую редакцию одного алгоритма.
#pragma once

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace gv {

// Модель STEP для кожи: суп вершин и crease-aware нормали.
// verts - (F*3, 3) float64 в порядке сборки (по три вершины на треугольник);
// normals - (F, 3, 3) float32, пусто, если супа нет (у эталона normals = None).
// segments (рёбра) отдаёт плагин CNC.Edges: см. cpp/stepledgesgeometry.cpp.
struct StepModel {
    std::vector<double> verts;
    std::vector<float> normals;
    int tri_count = 0;              // треугольников в супе (verts / 9)
};

// Чтение STEP один раз: форма нужна и сетке, и списку аналитических
// поверхностей (второй проход STEPControl_Reader стоит около 0.8 с).
// Путь - в UTF-8: так его принимает OCCT. Ошибка чтения - std::runtime_error.
TopoDS_Shape readStepShape(const std::string& utf8Path);

// Тесселяция формы BRepMesh_IncrementalMesh(deflection, угловой deflection * 2,
// параллельно) и сборка супа вершин с учётом локации грани: обход
// TopExp_Explorer(TopAbs_FACE), узлы грани подряд, треугольники - с разворотом
// при ориентации REVERSED. Порядок обхода и порядок вершин внутри треугольника
// - часть контракта с эталоном, менять нельзя.
std::vector<double> tessellateSoup(const TopoDS_Shape& shape, double deflection = 0.05);

// Разворот оболочки наружу, если площадь-взвешенная нормаль смотрит к центроиду:
// одно решение на модель. Возвращает (F,3,3) float64, тот же суп при
// положительной сумме. Локальный разворот по центроиду грани не годится:
// на вогнутых участках (карманах, отверстиях) он выворачивает их внутрь.
std::vector<double> orientShellOutward(const std::vector<double>& soup);

// load_step_model по сетке и нормалям: тесселяция, ориентация, нормали кожи.
StepModel loadStepModel(const TopoDS_Shape& shape, double deflection = 0.05);

// То же по пути к файлу: чтение STEP внутри.
StepModel loadStepModel(const std::string& utf8Path, double deflection = 0.05);

} // namespace gv
