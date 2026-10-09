#include "ProjectedMixedFDM.h"

#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

namespace alice2::fdm {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void checkAssembly() {
    std::mt19937 random(17);
    std::uniform_real_distribution<double> values(-2, 2);
    for (int trial = 0; trial < 30; ++trial) {
        StructuralGraph g;
        Eigen::MatrixXd X(7, 3);
        for (int i = 0; i < 7; ++i) {
            Node node;
            for (int d = 0; d < 3; ++d) {
                X(i,d) = values(random);
                node.fixed[d] = (i == 0 || (i < 3 && (trial + i + d) % 3 == 0));
            }
            g.nodes.push_back(node);
        }
        for (int i = 1; i < 7; ++i) g.edges.push_back({i-1,i,i%2 ? MemberType::Tension : MemberType::Compression,1});
        g.edges.push_back({0,5,MemberType::Compression,1});
        g.edges.push_back({2,6,MemberType::Tension,1});
        Eigen::VectorXd q(g.edges.size());
        for (int e = 0; e < q.size(); ++e) q[e] = values(random);
        Eigen::MatrixXd LX = forceDensityMatrix(g,q) * X;
        Eigen::VectorXd packed(equilibriumMatrix(g,X).rows()); int row = 0;
        for (int i = 0; i < X.rows(); ++i) for (int d = 0; d < 3; ++d)
            if (!g.nodes[i].fixed[d]) packed[row++] = LX(i,d);
        require((equilibriumMatrix(g,X)*q - packed).norm() < 1e-10, "Aq differs from free(LX)");
        for (auto& e : g.edges) std::swap(e.i,e.j);
        require((equilibriumMatrix(g,X)*q - packed).norm() < 1e-10, "edge orientation changed forces");
    }
}

void checkSolve() {
    StructuralGraph g;
    for (Eigen::Vector3d x : {Eigen::Vector3d(-2,0,0), Eigen::Vector3d(2,0,0),
                              Eigen::Vector3d(0,0,1), Eigen::Vector3d(0,0,2)}) {
        Node n; n.x0 = x; g.nodes.push_back(n);
    }
    for (int i = 0; i < 2; ++i) for (bool& f : g.nodes[i].fixed) f = true;
    g.edges = {{0,2,MemberType::Compression,1}, {1,2,MemberType::Compression,1},
               {0,3,MemberType::Tension,2}, {1,3,MemberType::Tension,2},
               {2,3,MemberType::Tension,.5}};
    Eigen::MatrixXd known(4,3);
    for (int i = 0; i < 4; ++i) known.row(i) = g.nodes[i].x0.transpose();
    Eigen::VectorXd q(5); q << 1,1,-2,-2,-.5;
    Eigen::MatrixXd P = forceDensityMatrix(g,q)*known;
    for (int i = 2; i < 4; ++i) g.nodes[i].load = P.row(i).transpose();
    g.nodes[2].x0 += Eigen::Vector3d(.15,.1,.2);
    g.nodes[3].x0 += Eigen::Vector3d(-.1,-.05,.1);
    auto result = solveProjectedMixedFDM(g);
    require(result.equilibriumValidated && result.X.allFinite(), "mixed signed FDM equilibrium failed");
    require(result.history.back().forceResidual < 1e-7, "actual residual too large");
    require(result.history.size() > 1 &&
            result.history.back().shapeError < result.history.front().shapeError - 1e-8,
            "projected solve did not improve shape");
    for (int i = 0; i < q.size(); ++i)
        require((q[i] > 0) == (result.q[i] > 0) && std::abs(result.q[i]) >= 1e-3, "member sign or lower bound violated");

    StructuralGraph zero;
    for (Eigen::Vector3d x : {Eigen::Vector3d(0,0,0), Eigen::Vector3d(2,1,1), Eigen::Vector3d(1,.5,.5)}) {
        Node n; n.x0 = x; zero.nodes.push_back(n);
    }
    for (int i = 0; i < 2; ++i) for (bool& f : zero.nodes[i].fixed) f = true;
    zero.edges = {{0,2,MemberType::Tension,1}, {1,2,MemberType::Tension,1}};
    auto zeroLoad = solveProjectedMixedFDM(zero);
    require(zeroLoad.q.size() == 2 && std::abs(zeroLoad.q[0] + 1) < 1e-12 && zeroLoad.equilibriumValidated,
            "zero-load density scale was not anchored");

    zero.nodes[2].x0 += Eigen::Vector3d(0,.2,0);
    auto impossibleShape = solveProjectedMixedFDM(zero);
    require(impossibleShape.fixedGeometryResidual > 1e-3,
            "sign-infeasible zero-load input geometry was not diagnosed");

    zero.edges[1].type = MemberType::Compression;
    auto singular = solveProjectedMixedFDM(zero);
    require(!singular.equilibriumValidated && singular.reason.find("singular") != std::string::npos,
            "singular signed FDM was accepted");

    g.nodes[0].fixed[2] = false;
    g.nodes[1].fixed[2] = false;
    auto unsupported = solveProjectedMixedFDM(g);
    require(unsupported.reason.find("support") != std::string::npos, "missing support was accepted");
}
} // namespace

int runProjectedMixedFDMChecks() {
    try {
        checkAssembly();
        checkSolve();
        std::cout << "Projected mixed FDM checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Projected mixed FDM check failed: " << e.what() << '\n';
        return 1;
    }
}
} // namespace alice2::fdm
