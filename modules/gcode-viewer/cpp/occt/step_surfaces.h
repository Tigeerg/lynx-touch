// occt/step_surfaces.h - analytic STEP surfaces of revolution: cylinders and
// cones.
//
// The layer reads OCCT directly: the reference goes through the same
// BRepAdaptor_Surface, BRep_Tool and TopExp_Explorer via the OCP Python bindings
// and pays for it with marshalling on every face vertex. Qt is not needed, as for
// the whole STEP layer.
#pragma once

#include <string>

#include <TopoDS_Shape.hxx>

#include "core/tangents.h"     // gv::StepSurfaceList - surface descriptors

namespace gv {

// Analytic surfaces of revolution of the shape: one descriptor per face lying on
// a cylinder or cone (holes, fillets, chamfers, counterbores). The order is the
// TopExp_Explorer face traversal order: it shows in the tangent family order, so
// it must not be reordered. Unparsed faces are skipped - as the reference
// swallows exceptions. An empty list for a shape without cylinders and cones.
//
// shapeAlreadyMeshed=true skips tessellation: the shape is already meshed with
// the same parameters (the loadStepBundleAll path: loadStepModel did the skin
// tessellation). A repeated BRepMesh_IncrementalMesh on a meshed shape does
// nothing, but the step is redundant; both calls have the same parameters, so
// skipping does not change the result. By default (false) tessellation runs - the
// file path did not mesh the shape.
StepSurfaceList loadStepSurfaces(const TopoDS_Shape& shape, double deflection = 0.05,
                                 bool shapeAlreadyMeshed = false);

// Same, over a path: reads STEP inside. The path is UTF-8, a read error is
// std::runtime_error.
StepSurfaceList loadStepSurfaces(const std::string& utf8Path, double deflection = 0.05);

} // namespace gv
