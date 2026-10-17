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

// Порядок обхода и порядок операций - часть контракта с эталоном
// (step_import.py): результат сверяется с ним побитово, поэтому файл
// компилируется с /fp:precise (MSVC) и -ffp-contract=off (GCC/Clang).

namespace gv {
namespace {

// Парное суммирование numpy (pairwise_sum): блок 128, восемь аккумуляторов,
// рекурсия по половине, выровненной на 8.
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

// Сумма непрерывного массива так, как её считает numpy (1.26.4): буфер
// итератора - 8192 элемента, внутри буфера парное суммирование, буферы
// складываются подряд. Числа измерены против numpy: на 136194 элементах эта
// схема совпадает бит-в-бит, одиночная парная редукция по всему массиву - нет.
double bufferedSum(const std::vector<double>& a) {
    constexpr std::size_t BLOCK = 8192;
    double res = 0.0;
    for (std::size_t off = 0; off < a.size(); off += BLOCK) {
        const std::size_t n = std::min(BLOCK, a.size() - off);
        res += pairwiseSum(a.data() + off, n);
    }
    return res;
}

// Векторная площадь грани и её центроид: те же произведения и тот же порядок
// слагаемых, что у numpy (cross и среднее трёх точек).
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

    std::vector<double> pts;            // узлы всех граней подряд, как в эталоне
    std::vector<double> soup;
    std::vector<double> nodes;          // буфер узлов текущей грани

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
            // грань, которую не удалось разобрать, пропускается - как в эталоне
        }
    }
    return soup;
}

void orientShellOutwardInPlace(std::vector<double>& soup) {
    const std::size_t faces = soup.size() / 9;
    if (faces == 0) {
        return;
    }

    // Центроид модели: наивная аккумуляция по столбцам в возрастании номера
    // строки (3F строк по 3 координаты) - так numpy считает mean(axis=0) по оси
    // с шагом. Проверено на Test_model: 3 из 3 столбцов совпали бит-в-бит,
    // парная схема на этой оси - нет.
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
        return;                                          // оболочка смотрит наружу
    }

    // Разворот на месте: у грани меняются местами первый и третий углы (на месте
    // копии p0 <- p2, p2 <- p0). Значения те же, что у покадрового разворота.
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
    // Суп строится прямо в out.verts и разворачивается на месте: отдельные
    // материализации под тесселяцию, ориентацию и нормали больше не нужны.
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
