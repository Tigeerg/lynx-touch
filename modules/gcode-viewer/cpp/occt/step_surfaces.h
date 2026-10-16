// occt/step_surfaces.h - аналитические поверхности вращения STEP: цилиндры и
// конусы (зеркало modules/gcode-viewer/step_surfaces.py).
//
// Слой читает OCCT напрямую: эталон ходит в те же BRepAdaptor_Surface,
// BRep_Tool и TopExp_Explorer через питоновские биндинги OCP и платит за это
// маршалингом на каждой вершине грани. Qt не нужен, как и всему слою STEP.
#pragma once

#include <string>

#include <TopoDS_Shape.hxx>

#include "core/tangents.h"     // gv::StepSurfaceList - дескрипторы поверхностей

namespace gv {

// Аналитические поверхности вращения формы: по одному дескриптору на грань,
// лежащую на цилиндре или конусе (отверстия, галтели, фаски, зенковки). Порядок
// - порядок обхода граней TopExp_Explorer: он виден в порядке семейств
// касательных, поэтому его нельзя переставлять. Неразобранные грани
// пропускаются - как глотает исключения эталон. Пустой список у формы без
// цилиндров и конусов.
StepSurfaceList loadStepSurfaces(const TopoDS_Shape& shape, double deflection = 0.05);

// То же по пути: чтение STEP внутри. Путь - UTF-8, ошибка чтения -
// std::runtime_error.
StepSurfaceList loadStepSurfaces(const std::string& utf8Path, double deflection = 0.05);

} // namespace gv
