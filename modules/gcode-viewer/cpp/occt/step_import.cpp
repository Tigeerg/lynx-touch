#include "occt/step_import.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include "core/normals.h"

// The traversal order and the order of operations are part of the contract with
// the reference (occt/step_import.cpp): the result is checked against it bit for
// bit, so the file is compiled with /fp:precise (MSVC) and -ffp-contract=off
// (GCC/Clang).

namespace gv {
namespace {

// numpy pairwise summation (pairwise_sum): a block of 128, eight accumulators,
// recursion over a half aligned to 8.
double pairwiseSum(const double* a, std::size_t n) {
    if (n < 8) {
        double res = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            res += a[i];
        }
        return res;
    }
    if (n <= 128) {
        double r[8];
        for (std::size_t k = 0; k < 8; ++k) {
            r[k] = a[k];
        }
        std::size_t i = 8;
        for (; i < n - (n % 8); i += 8) {
            for (std::size_t k = 0; k < 8; ++k) {
                r[k] += a[i + k];
            }
        }
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i) {
            res += a[i];
        }
        return res;
    }
    std::size_t n2 = n / 2;
    n2 -= n2 % 8;
    return pairwiseSum(a, n2) + pairwiseSum(a + n2, n - n2);
}

// Sum of a contiguous array the way numpy (1.26.4) computes it: the iterator
// buffer is 8192 elements, inside the buffer it is pairwise summation, and the
// buffers are added in sequence. The numbers were measured against numpy: on
// 136194 elements this scheme matches bit for bit, a single pairwise reduction
// over the whole array does not.
double bufferedSum(const std::vector<double>& a) {
    constexpr std::size_t BLOCK = 8192;
    double res = 0.0;
    for (std::size_t off = 0; off < a.size(); off += BLOCK) {
        const std::size_t n = std::min(BLOCK, a.size() - off);
        res += pairwiseSum(a.data() + off, n);
    }
    return res;
}

// Vector area of a face and its centroid: the same products and the same
// summation order as numpy (cross and the mean of three points).
void faceCrossAndCenter(const double* t, double cr[3], double ctr[3]) {
    const double d0[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
    const double d1[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
    cr[0] = d0[1] * d1[2] - d0[2] * d1[1];
    cr[1] = d0[2] * d1[0] - d0[0] * d1[2];
    cr[2] = d0[0] * d1[1] - d0[1] * d1[0];
    for (int d = 0; d < 3; ++d) {
        ctr[d] = ((t[d] + t[3 + d]) + t[6 + d]) / 3.0;
    }
}

} // namespace

TopoDS_Shape readStepShape(const std::string& utf8Path) {
    STEPControl_Reader reader;
    if (reader.ReadFile(utf8Path.c_str()) != IFSelect_ReturnStatus::IFSelect_RetDone) {
        throw std::runtime_error("STEP read failed: " + utf8Path);
    }
    reader.TransferRoots();
    return reader.OneShape();
}

std::vector<double> tessellateSoup(const TopoDS_Shape& shape, double deflection,
                                   bool shapeAlreadyMeshed) {
    if (!shapeAlreadyMeshed) {
        BRepMesh_IncrementalMesh mesher(shape, deflection, false, deflection * 2.0, true);
        mesher.Perform();
    }

    std::vector<double> pts;            // nodes of all faces in a row, as in the reference
    std::vector<double> soup;
    std::vector<double> nodes;          // node buffer of the current face

    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        try {
            const TopoDS_Face face = TopoDS::Face(exp.Current());
            TopLoc_Location loc;
            const Handle(Poly_Triangulation)& tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull() || tri->NbTriangles() <= 0) {
                continue;
            }
            const gp_Trsf trsf = loc.Transformation();
            const bool rev = face.Orientation() == TopAbs_REVERSED;

            const std::size_t base = pts.size() / 3;
            nodes.clear();
            nodes.reserve(std::size_t(tri->NbNodes()) * 3);
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                const gp_Pnt p = tri->Node(i).Transformed(trsf);
                nodes.push_back(p.X());
                nodes.push_back(p.Y());
                nodes.push_back(p.Z());
            }
            pts.insert(pts.end(), nodes.begin(), nodes.end());

            for (int i = 1; i <= tri->NbTriangles(); ++i) {
                const Poly_Triangle& t = tri->Triangle(i);
                const int a = t.Value(1);
                int b = t.Value(2);
                int c = t.Value(3);
                if (rev) {
                    std::swap(b, c);
                }
                const int corner[3] = {a, b, c};
                for (int k = 0; k < 3; ++k) {
                    const std::size_t n = (base + std::size_t(corner[k] - 1)) * 3;
                    soup.push_back(pts[n + 0]);
                    soup.push_back(pts[n + 1]);
                    soup.push_back(pts[n + 2]);
                }
            }
        } catch (const Standard_Failure&) {
            // a face that failed to parse is skipped - as in the reference
        }
    }
    return soup;
}

void orientShellOutwardInPlace(std::vector<double>& soup) {
    const std::size_t faces = soup.size() / 9;
    if (faces == 0) {
        return;
    }

    // Model centroid: naive accumulation by columns in ascending row number (3F
    // rows of 3 coordinates) - this is how numpy computes mean(axis=0) along a
    // strided axis. Verified on Test_model: 3 of 3 columns matched bit for bit,
    // the pairwise scheme on this axis did not.
    double cent[3] = {0.0, 0.0, 0.0};
    for (std::size_t row = 0; row < faces * 3; ++row) {
        for (int d = 0; d < 3; ++d) {
            cent[d] += soup[row * 3 + std::size_t(d)];
        }
    }
    for (int d = 0; d < 3; ++d) {
        cent[d] /= static_cast<double>(faces * 3);
    }

    std::vector<double> prod(faces * 3, 0.0);            // cr * (ctr - cent), (F,3)
    for (std::size_t f = 0; f < faces; ++f) {
        double cr[3];
        double ctr[3];
        faceCrossAndCenter(&soup[f * 9], cr, ctr);
        for (int d = 0; d < 3; ++d) {
            prod[f * 3 + std::size_t(d)] = cr[d] * (ctr[d] - cent[d]);
        }
    }
    if (bufferedSum(prod) >= 0.0) {
        return;                                          // the shell looks outward
    }

    // Flip in place: a face swaps its first and third corners (in place of the
    // copy p0 <- p2, p2 <- p0). The values are the same as the per-frame flip.
    for (std::size_t f = 0; f < faces; ++f) {
        const std::size_t p = f * 9;
        for (int d = 0; d < 3; ++d) {
            std::swap(soup[p + std::size_t(d)], soup[p + 6 + std::size_t(d)]);
        }
    }
}

std::vector<double> orientShellOutward(const std::vector<double>& soup) {
    if (soup.size() / 9 == 0) {
        return {};
    }
    std::vector<double> out = soup;
    orientShellOutwardInPlace(out);
    return out;
}

StepModel loadStepModel(const TopoDS_Shape& shape, double deflection, double creaseDeg) {
    StepModel out;
    // The soup is built directly in out.verts and flipped in place: separate
    // materialisations for tessellation, orientation and normals are no longer
    // needed.
    out.verts = tessellateSoup(shape, deflection);
    out.tri_count = static_cast<int>(out.verts.size() / 9);
    orientShellOutwardInPlace(out.verts);
    if (!out.verts.empty()) {
        out.normals = smoothFaceNormals(out.verts, creaseDeg);
    }
    return out;
}

StepModel loadStepModel(const std::string& utf8Path, double deflection, double creaseDeg) {
    return loadStepModel(readStepShape(utf8Path), deflection, creaseDeg);
}

} // namespace gv
