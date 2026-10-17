// occt/step_surfaces.h - аналитические поверхности вращения STEP: цилиндры и
// конусы.
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
//
// shapeAlreadyMeshed=true пропускает тесселяцию: форма уже размещена с теми же
// параметрами (путь loadStepBundleAll: тесселяцию кожи сделал loadStepModel).
// Повторный BRepMesh_IncrementalMesh на размещённой форме ничего не делает, но
// шаг лишний; параметры у обоих вызовов одни, поэтому пропуск результат не
// меняет. По умолчанию (false) тесселяция идёт - путь по файлу форму не размещал.
StepSurfaceList loadStepSurfaces(const TopoDS_Shape& shape, double deflection = 0.05,
                                 bool shapeAlreadyMeshed = false);

// То же по пути: чтение STEP внутри. Путь - UTF-8, ошибка чтения -
// std::runtime_error.
StepSurfaceList loadStepSurfaces(const std::string& utf8Path, double deflection = 0.05);

} // namespace gv
