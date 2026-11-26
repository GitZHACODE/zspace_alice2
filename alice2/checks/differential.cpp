#include <differential/DiscreteExteriorCalculus.h>
#include <differential/LaboratoryMeshes.h>
#include <Eigen/Cholesky>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

using namespace alice2;
using namespace alice2::differential;
using namespace alice2::differential::laboratory;
using Vector = DiscreteExteriorCalculus::Vector;
using Sparse = DiscreteExteriorCalculus::Sparse;

namespace {
void require(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}

Vector scalar(const MeshData& mesh, auto function) {
    Vector u(mesh.vertices.size());
    for (int i = 0; i < u.size(); ++i) u[i] = function(mesh.vertices[i].position);
    return u;
}

Eigen::MatrixXd faceField(const MeshData& mesh, auto function) {
    Eigen::MatrixXd result(mesh.faces.size(), 3);
    for (int f = 0; f < result.rows(); ++f) {
        const auto& v = mesh.faces[f].vertices;
        const Vec3 p = (mesh.vertices[v[0]].position + mesh.vertices[v[1]].position + mesh.vertices[v[2]].position) / 3;
        result.row(f) = function(p);
    }
    return result;
}

Vector angle(const MeshData& mesh, const DiscreteExteriorCalculus& dec, bool minor = false) {
    const Vector theta = scalar(mesh, [=](Vec3 p) {
        return minor ? std::atan2(p.z, std::hypot(p.x,p.y)-1) : std::atan2(p.y,p.x);
    });
    return dec.derivative0(theta).unaryExpr([](double v) { return std::remainder(v,tau); });
}

double period(const DiscreteExteriorCalculus& dec, const Vector& form, int count, int start = 0) {
    double sum = 0;
    for (int e = 0; e < form.size(); ++e) {
        const auto [a,b] = dec.edges()[e];
        if (a < start || b >= start+count) continue;
        if (b-a == 1) sum += form[e];
        if (a == start && b == start+count-1) sum -= form[e];
    }
    return sum;
}

void verifyBasis(DiscreteExteriorCalculus& dec, int count) {
    require(dec.harmonicModeCount() == count, "Wrong topological harmonic dimension");
    const auto basis = dec.harmonicBasis();
    require(basis.cols() == count, "Wrong harmonic basis rank");
    require((basis.transpose()*dec.H1()*basis-Eigen::MatrixXd::Identity(count,count)).norm()<1e-9,
            "Harmonic basis is not H1-orthonormal");
    for (int i = 0; i < count; ++i) {
        require(dec.derivative1(basis.col(i)).norm()<1e-9, "Harmonic basis is not closed");
        require(dec.codifferential1(basis.col(i)).norm()<1e-8, "Harmonic basis is not coclosed");
        require(dec.laplaceForm(basis.col(i)).norm()<1e-7, "Harmonic basis is not in the 1-form Laplacian kernel");
    }
}

void verifyDecomposition(DiscreteExteriorCalculus& dec, const Vector& w, const HodgeResult& h) {
    const double norm = dec.edgeNorm(w);
    const auto orthogonal = [&](const Vector& a, const Vector& b) {
        return std::abs(a.dot(dec.H1()*b)) / (norm*norm);
    };
    require(dec.edgeNorm(w-h.exact-h.coexact-h.harmonic)/norm < 1e-9, "Hodge reconstruction");
    require(orthogonal(h.exact,h.coexact)<1e-8 && orthogonal(h.exact,h.harmonic)<1e-8 &&
            orthogonal(h.coexact,h.harmonic)<1e-8, "Hodge orthogonality");
    require(dec.derivative1(h.harmonic).norm() < 1e-9, "Harmonic component is not closed");
    require((dec.D0().transpose()*(dec.H1()*h.harmonic)).norm()/norm < 1e-9, "Harmonic component is not coclosed");
}

void gridChecks() {
    for (bool irregular : {false,true}) {
        auto mesh = plane(17,0,irregular);
        DiscreteExteriorCalculus dec;
        dec.build(mesh);
        const Sparse boundary = dec.D1()*dec.D0();
        require(boundary.norm() == 0, "D1 D0 must be exactly zero");
        require((dec.laplacian0()*Vector::Ones(mesh.vertices.size())).norm()<1e-12, "Constant Laplacian nullspace");
        const Sparse transpose = dec.laplacian0().transpose();
        require((dec.laplacian0()-transpose).norm()<1e-12, "Asymmetric stiffness");
        Eigen::SimplicialLLT<Sparse> llt(dec.H1());
        require(llt.info()==Eigen::Success, "Whitney mass is not SPD");
        const Vector manufactured = scalar(mesh, [](Vec3 p) { return std::sin(p.x+3*p.y); });
        const Vector exact = dec.derivative0(manufactured);
        const double gradError = dec.edgeNorm(dec.derivative0(dec.integrate(exact))-exact)/dec.edgeNorm(exact);
        require(gradError<1e-9, "Manufactured gradient recovery");
        const Vector x = scalar(mesh, [](Vec3 p) { return p.x; });
        std::vector<int> fixed, free;
        for (int i = 0; i < x.size(); ++i) (x[i]==0 || x[i]==1 ? fixed : free).push_back(i);
        Vector values(fixed.size());
        for (int i = 0; i < values.size(); ++i) values[i]=x[fixed[i]];
        const Vector u = dec.harmonicField(fixed,values);
        const Vector residual = dec.laplacian0()*u;
        Vector prescribed = Vector::Zero(x.size());
        for (int i = 0; i < values.size(); ++i) prescribed[fixed[i]]=values[i];
        double freeResidual = 0;
        for (int i : free) freeResidual += residual[i]*residual[i];
        freeResidual = std::sqrt(freeResidual)/(dec.laplacian0()*prescribed).norm();
        require(freeResidual<1e-9 && (x-u).lpNorm<Eigen::Infinity>()<1e-11, "Dirichlet harmonic square");
        std::cout << (irregular?"Irregular":"Regular") << " grid gradient error " << gradError
                  << ", harmonic residual " << freeResidual << '\n';
    }

    double prior = 0;
    for (int n : {8,16,32}) {
        const auto mesh = plane(n);
        DiscreteExteriorCalculus dec;
        dec.build(mesh);
        const auto sample = faceField(mesh, [](Vec3 p) { return Eigen::RowVector3d(1-p.y,p.x,0); });
        const auto rotated = dec.projectFaceField(sample);
        const double area = 1.0/(2*n*n);
        const double error = (dec.derivative1(rotated).array()-2*area).matrix().norm()/(2*area*std::sqrt(sample.rows()));
        require(n==8 || error<0.85*prior, "Projected curl did not converge under refinement");
        prior = error;
        double previous = -1;
        for (double alpha : {0.,0.5,1.,2.}) {
            const Vector w = dec.projectFaceField(faceField(mesh, [=](Vec3 p) { return Eigen::RowVector3d(1-alpha*p.y,alpha*p.x,0); }));
            const double residual = dec.edgeNorm(w-dec.derivative0(dec.integrate(w)));
            require(alpha==0 ? residual<1e-11 : residual>previous, "Rotational integration residual");
            previous = residual;
        }
        std::cout << "Projected curl, " << n << " subdivisions, relative error " << error << '\n';
    }
}

void diskCheck() {
    auto mesh = makeMesh(Geometry::Disk,10);
    DiscreteExteriorCalculus dec;
    dec.build(mesh);
    const Vector exact = dec.derivative0(scalar(mesh, [](Vec3 p) { return p.x*p.x+p.y*p.y; }));
    Eigen::SimplicialLDLT<Sparse> mass(dec.H1());
    const Vector z = Vector::LinSpaced(mesh.faces.size(),-0.2,0.2);
    const Vector coexact = mass.solve(dec.D1().transpose()*z);
    const Vector w = exact+coexact;
    const auto h = dec.decompose(w);
    verifyDecomposition(dec,w,h);
    require(dec.edgeNorm(h.exact-exact)/dec.edgeNorm(exact)<1e-9 &&
            dec.edgeNorm(h.coexact-coexact)/dec.edgeNorm(coexact)<1e-9 &&
            dec.edgeNorm(h.harmonic)<1e-9, "Manufactured disk components");
    verifyBasis(dec,0);
    std::cout << "Disk component relative errors " << dec.edgeNorm(h.exact-exact)/dec.edgeNorm(exact)
              << ", " << dec.edgeNorm(h.coexact-coexact)/dec.edgeNorm(coexact) << '\n';
}

void topologyChecks() {
    constexpr int n = 18;
    DiscreteExteriorCalculus dec;
    auto mesh = makeMesh(Geometry::Annulus,n);
    dec.build(mesh);
    const Vector angular = angle(mesh,dec);
    require(dec.boundaryLoopCount()==2 && dec.derivative1(angular).norm()<1e-12, "Annulus circulation");
    const auto h = dec.decompose(angular);
    verifyDecomposition(dec,angular,h);
    require(std::abs(period(dec,h.harmonic,2*n)-tau)<1e-12, "Wrong annulus harmonic period");
    require(dec.edgeNorm(h.harmonic)>1, "Uncut annulus integrated globally");
    verifyBasis(dec,1);
    std::cout << "Annulus harmonic period " << period(dec,h.harmonic,2*n) << '\n';

    mesh = makeMesh(Geometry::CutAnnulus,n);
    dec.build(mesh);
    const Vector cutAngular = angle(mesh,dec);
    const Vector u = dec.integrate(cutAngular);
    require(dec.edgeNorm(dec.derivative0(u)-cutAngular)<1e-10, "Cut annulus potential recovery");
    for (int j = 0; j <= n; ++j)
        require(std::abs(u[j*(2*n+1)+2*n]-u[j*(2*n+1)]-tau)<1e-10, "Cut annulus seam jump");
    verifyBasis(dec,0);

    dec.build(makeMesh(Geometry::Cylinder,n));
    verifyBasis(dec,1);
    dec.build(makeMesh(Geometry::Sphere,n));
    verifyBasis(dec,0);
    mesh = makeMesh(Geometry::Torus,n);
    dec.build(mesh);
    verifyBasis(dec,2);
    const auto basis = dec.harmonicBasis();
    const Vector a = angle(mesh,dec), b = angle(mesh,dec,true);
    const auto ha = dec.decompose(a), hb = dec.decompose(b);
    const Eigen::Vector2d ca = basis.transpose()*dec.H1()*ha.harmonic;
    const Eigen::Vector2d cb = basis.transpose()*dec.H1()*hb.harmonic;
    require((basis*ca-ha.harmonic).norm()<1e-9 && (basis*cb-hb.harmonic).norm()<1e-9 &&
            std::abs(ca.x()*cb.y()-ca.y()*cb.x())>1, "Torus angular representatives must be independent");
    const Vector nonclosed = a + Vector::LinSpaced(a.size(),0,0.2);
    verifyDecomposition(dec,nonclosed,dec.decompose(nonclosed));
    dec.build(makeMesh(Geometry::GenusTwo,n));
    verifyBasis(dec,4);
    require(dec.totalGenus()==2, "Wrong genus on the closed two-handle surface");
    std::cout << "Harmonic dimensions, sphere 0, cylinder 1, torus 2, genus two 4\n";
}

void metricAndOrientationChecks() {
    auto mesh = plane(13,0,true);
    DiscreteExteriorCalculus dec;
    dec.build(mesh);
    const Sparse d0 = dec.D0(), d1 = dec.D1(), h0 = dec.H0(), h1 = dec.H1(), h2 = dec.H2();
    for (double bend : {.2,.8,1.5}) {
        mesh = plane(13,bend,true);
        dec.updateMetric(mesh);
        require((dec.D0()-d0).norm()==0 && (dec.D1()-d1).norm()==0 && (dec.D1()*dec.D0()).norm()==0,
                "Deforming coordinates changed topology");
        require((dec.H0()-h0).norm()>0 && (dec.H1()-h1).norm()>0 && (dec.H2()-h2).norm()>0,
                "Deformation must update all metric matrices");
        const auto w = dec.projectFaceField(faceField(mesh, [](Vec3 p) { return Eigen::RowVector3d(1-p.y,p.x,0); }));
        verifyDecomposition(dec,w,dec.decompose(w));
    }
    const auto vectors = faceField(mesh, [](Vec3 p) { return Eigen::RowVector3d(1-p.y,p.x,0.2); });
    const Vector w = dec.projectFaceField(vectors);
    const auto h = dec.decompose(w);
    const auto before = dec.reconstructFaceField(h.exact);
    const auto beforeCoexact = dec.reconstructFaceField(h.coexact);
    std::vector<int> ids(mesh.vertices.size());
    std::iota(ids.begin(),ids.end(),0);
    std::mt19937 random(148);
    std::shuffle(ids.begin(),ids.end(),random);
    auto renumbered = mesh;
    for (size_t i = 0; i < ids.size(); ++i) renumbered.vertices[ids[i]] = mesh.vertices[i];
    for (size_t f = 0; f < renumbered.faces.size(); ++f) {
        auto& face = renumbered.faces[f].vertices;
        for (auto& i : face) i = ids[i];
        if (f%3==0) std::swap(face[1],face[2]);
    }
    dec.build(renumbered);
    const auto after = dec.decompose(dec.projectFaceField(vectors));
    require((dec.reconstructFaceField(after.exact)-before).norm()/before.norm()<1e-10 &&
            (dec.reconstructFaceField(after.coexact)-beforeCoexact).norm()/beforeCoexact.norm()<1e-10,
            "Physical field changed with edge and face orientation");

    auto invalid = renumbered;
    invalid.vertices[invalid.faces[0].vertices[0]].position = invalid.vertices[invalid.faces[0].vertices[1]].position;
    try { dec.updateMetric(invalid); throw std::runtime_error("Accepted a degenerate triangle"); }
    catch (const std::invalid_argument&) {}
    invalid.faces[0].vertices.push_back(0);
    try { dec.build(invalid); throw std::runtime_error("Accepted a non-triangle"); }
    catch (const std::invalid_argument&) {}
    require((dec.reconstructFaceField(after.exact)-before).norm()/before.norm()<1e-10,
            "Failed mesh update changed the previous valid state");

    auto disconnected = plane(3);
    auto other = makeMesh(Geometry::Annulus,6);
    const int shift = static_cast<int>(disconnected.vertices.size());
    disconnected.vertices.insert(disconnected.vertices.end(),other.vertices.begin(),other.vertices.end());
    for (auto& face : other.faces) {
        for (auto& i : face.vertices) i+=shift;
        disconnected.faces.push_back(face);
    }
    dec.build(disconnected);
    require(dec.componentCount()==2 && dec.boundaryLoopCount()==3 && dec.totalGenus()==0,
            "Disconnected components, boundaries or genus");
    verifyBasis(dec,1);
    const Vector exact = dec.derivative0(scalar(disconnected, [](Vec3 p) { return p.x; }));
    require(dec.edgeNorm(dec.derivative0(dec.integrate(exact))-exact)<1e-10, "Component gauges");
}

void benchmark(int n, bool irregular) {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](auto a, auto b) { return std::chrono::duration<double,std::milli>(b-a).count(); };
    const auto mesh = plane(n,0,irregular);
    DiscreteExteriorCalculus dec;
    const auto start = Clock::now();
    dec.build(mesh);
    const auto assembled = Clock::now();
    const Vector w = Vector::LinSpaced(dec.edges().size(),-.1,.3);
    const auto before = Clock::now();
    const auto first = dec.decompose(w);
    const auto factored = Clock::now();
    const auto second = dec.decompose(w);
    const auto solved = Clock::now();
    verifyDecomposition(dec,w,second);
    require(dec.edgeNorm(first.harmonic-second.harmonic)<1e-12,"Cached solve");
    const double cached = ms(factored,solved);
    std::cout << (irregular?"irregular":"regular") << "," << mesh.vertices.size() << ","
              << ms(start,assembled) << "," << ms(before,factored)-cached << "," << cached << ",";
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) std::cout << info.PeakWorkingSetSize/1048576.;
#else
    rusage info{};
    if (getrusage(RUSAGE_SELF,&info)==0) {
#ifdef __APPLE__
        std::cout << info.ru_maxrss/1048576.;
#else
        std::cout << info.ru_maxrss/1024.;
#endif
    }
#endif
    std::cout << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(12);
        if (argc==4 && std::string(argv[1])=="--benchmark") {
            const int n = std::stoi(argv[2]);
            if (n<3 || n>512) throw std::invalid_argument("Benchmark subdivisions must be 3 through 512");
            benchmark(n,std::string(argv[3])=="irregular");
            return 0;
        }
        gridChecks();
        diskCheck();
        topologyChecks();
        metricAndOrientationChecks();
        std::cout << "Differential checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
