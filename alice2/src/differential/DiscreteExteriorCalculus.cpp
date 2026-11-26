#include "DiscreteExteriorCalculus.h"

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace alice2::differential {
namespace {
using Sparse = DiscreteExteriorCalculus::Sparse;
using Vector = DiscreteExteriorCalculus::Vector;
using Triplet = Eigen::Triplet<double>;

class Components {
public:
    explicit Components(int count) : parent_(count) { std::iota(parent_.begin(), parent_.end(), 0); }
    int root(int item) {
        while (item != parent_[item]) {
            parent_[item] = parent_[parent_[item]];
            item = parent_[item];
        }
        return item;
    }
    bool join(int a, int b) {
        a = root(a);
        b = root(b);
        if (a == b) return false;
        parent_[std::max(a, b)] = std::min(a, b);
        return true;
    }
private:
    std::vector<int> parent_;
};

Sparse sparse(int rows, int cols, const std::vector<Triplet>& values) {
    Sparse matrix(rows, cols);
    matrix.setFromTriplets(values.begin(), values.end());
    return matrix;
}

void checkVector(const Vector& vector, Eigen::Index size) {
    if (!size || vector.size() != size || !vector.allFinite())
        throw std::invalid_argument("Cochain has the wrong size or contains a non-finite value");
}

Eigen::Vector3d position(const MeshData& mesh, int index) {
    const auto& p = mesh.vertices[index].position;
    return {p.x, p.y, p.z};
}
} // namespace

void DiscreteExteriorCalculus::build(const MeshData& mesh) {
    // Validate and assemble in a temporary object so a failed import preserves the current mesh.
    DiscreteExteriorCalculus next;
    next.buildTopology(mesh);
    next.updateMetric(mesh);
    d0_.swap(next.d0_); d1_.swap(next.d1_);
    h0_.swap(next.h0_); h1_.swap(next.h1_); h2_.swap(next.h2_);
    stiffness_.swap(next.stiffness_);
    vertexMass_.swap(next.vertexMass_); faceMass_.swap(next.faceMass_);
    edges_.swap(next.edges_); faces_.swap(next.faces_);
    vertexComponents_.swap(next.vertexComponents_); vertexRoots_.swap(next.vertexRoots_);
    faceRows_.swap(next.faceRows_); generators_.swap(next.generators_);
    dualLinks_.swap(next.dualLinks_); midpointBasis_.swap(next.midpointBasis_);
    boundaryLoops_ = next.boundaryLoops_;
    faceRowCount_ = next.faceRowCount_;
    scalarReady_ = massReady_ = coexactReady_ = false;
}

void DiscreteExteriorCalculus::buildTopology(const MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.faces.empty() ||
        mesh.vertices.size() > std::numeric_limits<int>::max() ||
        mesh.faces.size() > std::numeric_limits<int>::max() / 3)
        throw std::invalid_argument("Expected a nonempty triangle mesh within the supported index range");
    const int nv = static_cast<int>(mesh.vertices.size()), nf = static_cast<int>(mesh.faces.size());
    struct Incident { int face = -1, side = -1; };
    std::vector<std::array<Incident, 2>> incident;
    std::map<Edge, int> ids;
    std::set<std::array<int, 3>> triangles;
    std::vector<Triplet> d0, d1;
    faces_.resize(nf);
    for (int f = 0; f < nf; ++f) {
        const auto& face = mesh.faces[f].vertices;
        if (face.size() != 3) throw std::invalid_argument("DEC requires triangulated faces");
        auto& local = faces_[f];
        std::copy(face.begin(), face.end(), local.vertices.begin());
        auto sorted = local.vertices;
        std::sort(sorted.begin(), sorted.end());
        if (sorted[0] < 0 || sorted[2] >= nv || sorted[0] == sorted[1] || sorted[1] == sorted[2])
            throw std::invalid_argument("Invalid triangle vertex indices");
        if (!triangles.insert(sorted).second) throw std::invalid_argument("Duplicate triangle");
        for (int k = 0; k < 3; ++k) {
            const int a = face[k], b = face[(k + 1) % 3];
            const Edge edge{std::min(a, b), std::max(a, b)};
            const auto [it, inserted] = ids.try_emplace(edge, static_cast<int>(edges_.size()));
            const int id = it->second, sign = a < b ? 1 : -1;
            if (inserted) {
                edges_.push_back(edge);
                incident.push_back({Incident{f, k}, Incident{}});
                d0.emplace_back(id, edge[0], -1);
                d0.emplace_back(id, edge[1], 1);
            } else {
                if (incident[id][1].face >= 0) throw std::invalid_argument("Nonmanifold edge");
                incident[id][1] = {f, k};
            }
            local.edges[k] = id;
            local.signs[k] = sign;
            d1.emplace_back(f, id, sign);
        }
    }
    const int ne = static_cast<int>(edges_.size());
    d0_ = sparse(ne, nv, d0);
    d1_ = sparse(nf, ne, d1);

    Components vertices(nv), fans(3 * nf), boundary(nv);
    std::vector<bool> primal(ne);
    std::vector<int> firstCorner(nv, -1), boundaryDegree(nv, 0);
    std::vector<std::vector<int>> across(nf);
    for (int e = 0; e < ne; ++e) {
        const auto [a, b] = edges_[e];
        primal[e] = vertices.join(a, b);
        const auto [left, right] = incident[e];
        if (right.face < 0) {
            boundary.join(a, b);
            ++boundaryDegree[a];
            ++boundaryDegree[b];
            continue;
        }
        across[left.face].push_back(e);
        across[right.face].push_back(e);
        for (const int v : edges_[e]) {
            const auto corner = [&](Incident side) {
                return 3 * side.face + (faces_[side.face].vertices[side.side] == v ? side.side : (side.side + 1) % 3);
            };
            fans.join(corner(left), corner(right));
        }
    }
    for (int f = 0; f < nf; ++f) {
        for (int k = 0; k < 3; ++k) {
            int& first = firstCorner[faces_[f].vertices[k]];
            const int root = fans.root(3 * f + k);
            if (first >= 0 && first != root) throw std::invalid_argument("Nonmanifold vertex fan");
            first = root;
        }
    }
    vertexComponents_.resize(nv);
    std::vector<int> hasBoundary;
    for (int v = 0; v < nv; ++v) {
        if (firstCorner[v] < 0) throw std::invalid_argument("Isolated vertex");
        const int root = vertices.root(v);
        if (root == v) {
            vertexComponents_[v] = static_cast<int>(vertexRoots_.size());
            vertexRoots_.push_back(v);
            hasBoundary.push_back(false);
        } else {
            vertexComponents_[v] = vertexComponents_[root];
        }
        if (boundaryDegree[v]) {
            if (boundaryDegree[v] != 2) throw std::invalid_argument("Nonmanifold boundary");
            hasBoundary[vertexComponents_[v]] = true;
            if (boundary.root(v) == v) ++boundaryLoops_;
        }
    }

    // Each closed component contributes one dependent circulation equation,
    // even when input triangle windings were independently reversed.
    std::vector<int> orientation(nf), faceRoots(hasBoundary.size(), -1);
    for (int f = 0; f < nf; ++f) {
        if (orientation[f]) continue;
        faceRoots[vertexComponents_[faces_[f].vertices[0]]] = f;
        std::vector<int> queue{f};
        orientation[f] = 1;
        for (size_t i = 0; i < queue.size(); ++i) {
            const int current = queue[i];
            for (int e : across[current]) {
                auto [a, b] = incident[e];
                if (a.face != current) std::swap(a, b);
                const int sign = -orientation[current] * faces_[current].signs[a.side] * faces_[b.face].signs[b.side];
                if (orientation[b.face] == sign) continue;
                if (orientation[b.face]) throw std::invalid_argument("Nonorientable triangle surface");
                orientation[b.face] = sign;
                queue.push_back(b.face);
            }
        }
    }
    faceRows_.resize(nf);
    for (int f = 0; f < nf; ++f) {
        const int component = vertexComponents_[faces_[f].vertices[0]];
        faceRows_[f] = !hasBoundary[component] && faceRoots[component] == f ? -1 : faceRowCount_++;
    }

    // A primal spanning tree fixes exact forms. A dual spanning tree using
    // the remaining edges eliminates face circulations from leaf to root.
    // Each boundary component of the surface shares its exterior dual node.
    std::vector<std::vector<std::pair<int, int>>> dual(nf + hasBoundary.size());
    for (int e = 0; e < ne; ++e) {
        if (primal[e]) continue;
        const auto [left, right] = incident[e];
        const int a = left.face;
        const int b = right.face >= 0 ? right.face : nf + vertexComponents_[edges_[e][0]];
        dual[a].emplace_back(b, e);
        dual[b].emplace_back(a, e);
    }
    std::vector<bool> seen(dual.size()), inDual(ne);
    for (int c = 0; c < componentCount(); ++c) {
        const int root = hasBoundary[c] ? nf + c : faceRoots[c];
        std::vector<int> queue{root};
        seen[root] = true;
        for (size_t i = 0; i < queue.size(); ++i) {
            const int parent = queue[i];
            for (const auto& [child, edge] : dual[parent]) {
                if (seen[child]) continue;
                seen[child] = inDual[edge] = true;
                queue.push_back(child);
                dualLinks_.push_back({child, edge});
            }
        }
    }
    for (int e = 0; e < ne; ++e)
        if (!primal[e] && !inDual[e]) generators_.push_back(e);
}

void DiscreteExteriorCalculus::updateMetric(const MeshData& mesh) {
    if (faces_.empty() || mesh.vertices.size() != static_cast<size_t>(d0_.cols()) || mesh.faces.size() != faces_.size())
        throw std::invalid_argument("Metric update requires the original connectivity");
    Vector mv = Vector::Zero(d0_.cols()), mf(d1_.rows());
    std::vector<Triplet> mass, vertexDiagonal, faceDiagonal;
    mass.reserve(faces_.size() * 9);
    std::vector<Eigen::Matrix3d> basis(faces_.size());
    for (int f = 0; f < d1_.rows(); ++f) {
        const auto& face = faces_[f];
        if (!std::equal(face.vertices.begin(), face.vertices.end(), mesh.faces[f].vertices.begin(), mesh.faces[f].vertices.end()))
            throw std::invalid_argument("Metric update requires the original connectivity");
        const auto [a, b, c] = face.vertices;
        const Eigen::Vector3d p = position(mesh, a), q = position(mesh, b), r = position(mesh, c);
        const Eigen::Vector3d normal = (q - p).cross(r - p);
        const double twiceArea = normal.norm();
        const double maxEdge2 = std::max({(q - p).squaredNorm(), (r - q).squaredNorm(), (p - r).squaredNorm()});
        if (!std::isfinite(twiceArea) || twiceArea <= 1e-14 * maxEdge2)
            throw std::invalid_argument("Degenerate or non-finite triangle");
        const double area = twiceArea / 2;
        const Eigen::Vector3d unit = normal / twiceArea;
        const std::array<Eigen::Vector3d, 3> grad{
            unit.cross(r - q) / twiceArea, unit.cross(p - r) / twiceArea, unit.cross(q - p) / twiceArea};
        const auto whitney = [&](const Eigen::Vector3d& barycentric) {
            Eigen::Matrix3d values;
            for (int k = 0; k < 3; ++k) {
                const int j = (k + 1) % 3;
                values.col(k) = face.signs[k] * (barycentric[k] * grad[j] - barycentric[j] * grad[k]);
            }
            return values;
        };
        basis[f] = whitney(Eigen::Vector3d::Constant(1.0 / 3.0));
        Eigen::Matrix3d local = Eigen::Matrix3d::Zero();
        for (int k = 0; k < 3; ++k) {
            Eigen::Vector3d barycentric = Eigen::Vector3d::Constant(1.0 / 6.0);
            barycentric[k] = 2.0 / 3.0;
            const Eigen::Matrix3d sample = whitney(barycentric);
            local.noalias() += (area / 3.0) * sample.transpose() * sample;
        }
        for (int i = 0; i < 3; ++i) {
            mv[face.vertices[i]] += area / 3.0;
            for (int j = 0; j < 3; ++j) mass.emplace_back(face.edges[i], face.edges[j], local(i, j));
        }
        mf[f] = 1.0 / area;
        faceDiagonal.emplace_back(f, f, mf[f]);
    }
    for (int v = 0; v < mv.size(); ++v) vertexDiagonal.emplace_back(v, v, mv[v]);
    Sparse edgeMass = sparse(d0_.rows(), d0_.rows(), mass);
    Sparse stiffness = d0_.transpose() * (edgeMass * d0_);
    h0_ = sparse(d0_.cols(), d0_.cols(), vertexDiagonal);
    h2_ = sparse(d1_.rows(), d1_.rows(), faceDiagonal);
    h1_.swap(edgeMass);
    stiffness_.swap(stiffness);
    vertexMass_.swap(mv);
    faceMass_.swap(mf);
    midpointBasis_.swap(basis);
    scalarReady_ = massReady_ = coexactReady_ = false;
}

Vector DiscreteExteriorCalculus::derivative0(const Vector& scalar) const {
    checkVector(scalar, d0_.cols());
    return d0_ * scalar;
}

Vector DiscreteExteriorCalculus::derivative1(const Vector& form) const {
    checkVector(form, d0_.rows());
    return d1_ * form;
}

Vector DiscreteExteriorCalculus::codifferential1(const Vector& form) const {
    checkVector(form, d0_.rows());
    return (d0_.transpose() * (h1_ * form)).cwiseQuotient(vertexMass_);
}

Vector DiscreteExteriorCalculus::laplaceScalar(const Vector& scalar) const {
    checkVector(scalar, d0_.cols());
    return (stiffness_ * scalar).cwiseQuotient(vertexMass_);
}

Vector DiscreteExteriorCalculus::laplaceForm(const Vector& form) {
    const Vector curl = derivative1(form).cwiseProduct(faceMass_);
    return derivative0(codifferential1(form)) + solveMass(d1_.transpose() * curl);
}

double DiscreteExteriorCalculus::edgeNorm(const Vector& form) const {
    checkVector(form, d0_.rows());
    return std::sqrt(std::max(0.0, form.dot(h1_ * form)));
}

Vector DiscreteExteriorCalculus::solveScalar(const Vector& rhs, const std::vector<int>& fixedIds,
                                            const Vector& fixedValues, bool reuseGauge) const {
    checkVector(rhs, d0_.cols());
    if (fixedValues.size() != static_cast<Eigen::Index>(fixedIds.size()) || !fixedValues.allFinite())
        throw std::invalid_argument("Expected a finite value for each constrained vertex");
    std::vector<bool> fixed(rhs.size()), constrainedComponent(componentCount());
    Vector result = Vector::Zero(rhs.size());
    for (size_t i = 0; i < fixedIds.size(); ++i) {
        const int v = fixedIds[i];
        if (v < 0 || v >= rhs.size() || fixed[v]) throw std::invalid_argument("Invalid or duplicate scalar constraint");
        fixed[v] = constrainedComponent[vertexComponents_[v]] = true;
        result[v] = fixedValues[i];
    }
    for (int c = 0; c < componentCount(); ++c)
        if (!constrainedComponent[c]) fixed[vertexRoots_[c]] = true;

    std::vector<int> index(rhs.size(), -1);
    int count = 0;
    for (int v = 0; v < rhs.size(); ++v)
        if (!fixed[v]) index[v] = count++;
    if (!count) return result;

    Eigen::SimplicialLDLT<Sparse> localSolver;
    auto& solver = reuseGauge ? scalarSolver_ : localSolver;
    if (!reuseGauge || !scalarReady_) {
        std::vector<Triplet> entries;
        entries.reserve(stiffness_.nonZeros());
        for (int col = 0; col < stiffness_.outerSize(); ++col) {
            if (fixed[col]) continue;
            for (Sparse::InnerIterator entry(stiffness_, col); entry; ++entry)
                if (!fixed[entry.row()]) entries.emplace_back(index[entry.row()], index[col], entry.value());
        }
        solver.compute(sparse(count, count, entries));
        if (solver.info() != Eigen::Success || solver.vectorD().minCoeff() <= 0)
            throw std::runtime_error("Scalar stiffness factorisation failed");
        if (reuseGauge) scalarReady_ = true;
    }
    const Vector shifted = rhs - stiffness_ * result;
    Vector reduced(count);
    for (int v = 0; v < rhs.size(); ++v)
        if (!fixed[v]) reduced[index[v]] = shifted[v];
    const Vector solution = solver.solve(reduced);
    if (solver.info() != Eigen::Success || !solution.allFinite()) throw std::runtime_error("Scalar solve failed");
    for (int v = 0; v < rhs.size(); ++v)
        if (!fixed[v]) result[v] = solution[index[v]];
    return result;
}

Vector DiscreteExteriorCalculus::harmonicField(const std::vector<int>& fixedIds, const Vector& fixedValues) const {
    return solveScalar(Vector::Zero(d0_.cols()), fixedIds, fixedValues, fixedIds.empty());
}

Vector DiscreteExteriorCalculus::integrate(const Vector& form) {
    checkVector(form, d0_.rows());
    return solveScalar(d0_.transpose() * (h1_ * form), {}, {}, true);
}

Vector DiscreteExteriorCalculus::solveMass(const Vector& rhs) {
    if (!massReady_) {
        massSolver_.compute(h1_);
        if (massSolver_.info() != Eigen::Success || massSolver_.vectorD().minCoeff() <= 0)
            throw std::runtime_error("Whitney mass factorisation failed");
        massReady_ = true;
    }
    const Vector solution = massSolver_.solve(rhs);
    if (massSolver_.info() != Eigen::Success || !solution.allFinite()) throw std::runtime_error("Whitney mass solve failed");
    return solution;
}

Vector DiscreteExteriorCalculus::solveCoexact(const Vector& circulation) {
    const int ne = static_cast<int>(d0_.rows());
    if (circulation.isZero(0)) return Vector::Zero(ne);
    Vector rhs = Vector::Zero(ne + faceRowCount_);
    for (int f = 0; f < circulation.size(); ++f)
        if (faceRows_[f] >= 0) rhs[ne + faceRows_[f]] = circulation[f];

    if (!coexactReady_) {
        std::vector<Triplet> entries;
        entries.reserve(h1_.nonZeros() + 6 * faceRowCount_);
        for (int col = 0; col < h1_.outerSize(); ++col)
            for (Sparse::InnerIterator entry(h1_, col); entry; ++entry)
                entries.emplace_back(entry.row(), col, entry.value());
        for (int f = 0; f < d1_.rows(); ++f) {
            if (faceRows_[f] < 0) continue;
            for (int k = 0; k < 3; ++k) {
                const int edge = faces_[f].edges[k], row = ne + faceRows_[f];
                entries.emplace_back(row, edge, faces_[f].signs[k]);
                entries.emplace_back(edge, row, faces_[f].signs[k]);
            }
        }
        coexactSolver_.compute(sparse(ne + faceRowCount_, ne + faceRowCount_, entries));
        if (coexactSolver_.info() != Eigen::Success) throw std::runtime_error("Coexact factorisation failed");
        coexactReady_ = true;
    }
    const Vector solution = coexactSolver_.solve(rhs);
    if (coexactSolver_.info() != Eigen::Success || !solution.allFinite()) throw std::runtime_error("Coexact solve failed");
    return solution.head(ne);
}

HodgeResult DiscreteExteriorCalculus::decompose(const Vector& form) {
    HodgeResult result;
    result.potential = integrate(form);
    result.exact = derivative0(result.potential);
    result.coexact = solveCoexact(derivative1(form));
    result.harmonic = form - result.exact - result.coexact;
    return result;
}

Eigen::MatrixXd DiscreteExteriorCalculus::harmonicBasis() {
    Eigen::MatrixXd basis(d0_.rows(), harmonicModeCount());
    for (int i = 0; i < basis.cols(); ++i) {
        Vector closed = Vector::Zero(d0_.rows());
        closed[generators_[i]] = 1;
        for (auto link = dualLinks_.rbegin(); link != dualLinks_.rend(); ++link) {
            const auto& face = faces_[link->child];
            double residual = 0;
            for (int k = 0; k < 3; ++k) residual += face.signs[k] * closed[face.edges[k]];
            closed[link->edge] -= residual * d1_.coeff(link->child, link->edge);
        }
        Vector harmonic = closed - derivative0(integrate(closed));
        for (int j = 0; j < i; ++j) harmonic -= basis.col(j).dot(h1_ * harmonic) * basis.col(j);
        const double norm = edgeNorm(harmonic);
        if (norm <= 0) throw std::runtime_error("Harmonic basis lost rank");
        basis.col(i) = harmonic / norm;
    }
    return basis;
}

Vector DiscreteExteriorCalculus::projectFaceField(const Eigen::MatrixXd& vectors) {
    if (!d0_.rows() || vectors.rows() != d1_.rows() || vectors.cols() != 3 || !vectors.allFinite())
        throw std::invalid_argument("Expected one finite 3D vector per triangle");
    Vector rhs = Vector::Zero(d0_.rows());
    for (int f = 0; f < d1_.rows(); ++f) {
        const Eigen::Vector3d local = midpointBasis_[f].transpose() * vectors.row(f).transpose() / faceMass_[f];
        for (int k = 0; k < 3; ++k) rhs[faces_[f].edges[k]] += local[k];
    }
    return solveMass(rhs);
}

Eigen::MatrixXd DiscreteExteriorCalculus::reconstructFaceField(const Vector& form) const {
    checkVector(form, d0_.rows());
    Eigen::MatrixXd result(d1_.rows(), 3);
    for (int f = 0; f < d1_.rows(); ++f) {
        const auto [a, b, c] = faces_[f].edges;
        result.row(f) = (midpointBasis_[f] * Eigen::Vector3d(form[a], form[b], form[c])).transpose();
    }
    return result;
}

} // namespace alice2::differential
