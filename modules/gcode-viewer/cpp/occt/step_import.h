// occt/step_import.h - STEP import: B-rep, tessellation, shell orientation.
//
// Works on the OCCT SDK directly: the reference goes through the same OCCT
// classes via the OCP Python bindings and pays for it with marshalling on every
// vertex and triangle.
//
// STEP edges are not built here: they were already ported by the CNC.Edges plugin
// (cpp/stepledgesgeometry.cpp, TopAbs_EDGE traversal + GCPnts_UniformDeflection +
// welding) and checked against the reference. Duplicating that path would mean
// keeping a second copy of one algorithm.
#pragma once

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace gv {

// STEP model for the skin: vertex soup and crease-aware normals.
// verts - (F*3, 3) float64 in assembly order (three vertices per triangle);
// normals - (F, 3, 3) float32, empty if there is no soup (the reference's
// normals = None). segments (edges) are provided by the CNC.Edges plugin: see
// cpp/stepledgesgeometry.cpp.
struct StepModel {
    std::vector<double> verts;
    std::vector<float> normals;
    int tri_count = 0;              // triangles in the soup (verts / 9)
};

// Read STEP once: the shape is needed both for the mesh and for the analytic
// surface list (a second STEPControl_Reader pass costs about 0.8 s). The path is
// UTF-8: that is how OCCT takes it. A read error is std::runtime_error.
TopoDS_Shape readStepShape(const std::string& utf8Path);

// Tessellate the shape with BRepMesh_IncrementalMesh(deflection, angular
// deflection * 2, parallel) and assemble the vertex soup accounting for the face
// location: TopExp_Explorer(TopAbs_FACE) traversal, face nodes in a row,
// triangles reversed on REVERSED orientation. The traversal order and the vertex
// order inside a triangle are part of the contract with the reference and must
// not change.
//
// shapeAlreadyMeshed=true skips BRepMesh: the shape was meshed above with the
// same call and the same parameters (as in loadStepSurfaces). The runtime does
// not use this; the flag is used to measure the split of tessellation into the
// mesher and soup assembly (bench_probe, step_mesh / step_soup).
std::vector<double> tessellateSoup(const TopoDS_Shape& shape, double deflection = 0.05,
                                   bool shapeAlreadyMeshed = false);

// Flip the shell outward if the area-weighted normal points to the centroid: one
// decision per model. Returns (F,3,3) float64, the same soup when the sum is
// positive. A local flip by the face centroid is not suitable: on concave areas
// (pockets, holes) it turns them inward.
std::vector<double> orientShellOutward(const std::vector<double>& soup);

// The same decision, but flipped in place: the soup from loadStepModel has no
// copy left - this call saves materialising the soup (about 3 MB on Test_model).
// The result is the same as orientShellOutward(soup), but soup is modified. An
// empty soup is left alone.
void orientShellOutwardInPlace(std::vector<double>& soup);

// load_step_model over the mesh and normals: tessellation, orientation, skin
// normals. creaseDeg - the normal smoothing threshold (core/normals), 20 degrees
// at med.
StepModel loadStepModel(const TopoDS_Shape& shape, double deflection = 0.05,
                        double creaseDeg = 20.0);

// Same, over a file path: reads STEP inside.
StepModel loadStepModel(const std::string& utf8Path, double deflection = 0.05,
                        double creaseDeg = 20.0);

} // namespace gv
