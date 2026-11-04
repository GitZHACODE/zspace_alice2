#include <bricks/BrickGenerator.h>
#include <Eigen/Geometry>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace alice2::bricks;
namespace {
void check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid population input must be rejected");
}
void checkBox(const Brick& b) {
    check(b.origin.allFinite() && b.normal.allFinite(), "Finite brick frame");
    check(std::abs(b.normal.norm()-1) < 1e-10 && std::abs(b.normal.dot(b.tangent)) < 1e-10,
          "Brick axes must be orthonormal");
    check((b.tangent.cross(b.widthDirection)-b.normal).norm() < 1e-10, "Right handed frame");
    check(std::abs((b.vertices[1]-b.vertices[0]).norm()-b.length) < 1e-10, "Brick length");
    check(std::abs((b.vertices[3]-b.vertices[0]).norm()-b.width) < 1e-10, "Brick width");
    for (int k=0; k<4; ++k) check((b.vertices[k+4]-b.vertices[k]-b.depth*b.normal).norm() < 1e-10, "Normal extrusion");
    check(std::abs(b.barycentric.sum()-1) < 1e-10 && b.barycentric.minCoeff() >= -1e-9, "Barycentric weights");
}
}

void checkBrickPopulation() {
    constexpr int nx=11, nz=11;
    Eigen::MatrixXd v(nx*nz,3);
    Eigen::MatrixXi f((nx-1)*(nz-1)*2,3);
    int fi=0;
    for (int j=0; j<nz; ++j) for (int i=0; i<nx; ++i) {
        const int k=j*nx+i;
        v.row(k) << i/10.0,0,j/10.0;
        if (i+1<nx && j+1<nz) {
            f.row(fi++) << k,k+1,k+nx+1;
            f.row(fi++) << k,k+nx+1,k+nx;
        }
    }
    BrickGenerator g;
    const BrickOptions options{0.02,0.04,0.04,0.20,0.10};
    rejects([&] { g.generateBricks(options); });
    g.setMesh(v,f); g.computeField(FieldMethod::Height); g.extractContours(0.1); g.generateBricks(options);
    check(g.contourPaths().size() == 9, "Planar wall should have nine open courses");
    check(!g.bricks().empty(), "Wall must populate");
    const Brick* firstA=nullptr;
    const Brick* nextHeader=nullptr;
    const Brick* previous=nullptr;
    for (const auto& b : g.bricks()) {
        checkBox(b);
        check(std::abs(b.width-0.08)<1e-12 && b.depth==0.04, "Requested brick dimensions");
        check(b.length <= (b.isLengthA ? options.lengthA : options.lengthB)+1e-12, "Only terminal shortening allowed");
        check(b.startDistance >= 0.01-1e-12 && b.endDistance <= 0.99+1e-12, "Side margins");
        check((b.normal-Eigen::Vector3d(0,-1,0)).norm()<1e-12, "Planar wall normal");
        check(std::abs(b.origin.z()-g.contours().levels(b.levelIndex))<1e-12, "Course alignment");
        if (previous && previous->pathIndex==b.pathIndex) {
            check(std::abs(b.startDistance-previous->endDistance-options.gap)<1e-12, "Mortar gap along path");
            check(previous->isLengthA != b.isLengthA, "A/B alternation");
        }
        if (b.levelIndex==0 && b.isLengthA && !firstA) firstA=&b;
        if (b.levelIndex==1 && !b.isLengthA && !nextHeader) nextHeader=&b;
        previous=&b;
    }
    check(firstA && nextHeader && std::abs(firstA->origin.x()-nextHeader->origin.x())<1e-10,
          "Flemish header should center under adjacent stretcher");
    const auto originalCount=g.bricks().size();
    const auto fullPattern=g.bricks();
    size_t aCount=0,bCount=0;
    for(const auto& brick:fullPattern)(brick.isLengthA?aCount:bCount)++;
    g.generateBricks({0.02,0.02,0.07,0.20,0.10});
    check(g.bricks().size()==originalCount,"Changing depths must preserve the layout");
    for(size_t i=0;i<fullPattern.size();++i) {
        const auto& b=g.bricks()[i];
        checkBox(b);
        check(b.depth==(b.isLengthA?0.02:0.07),"Each type must use its own depth");
        check((b.origin-fullPattern[i].origin).norm()<1e-12,"Depth edits must not move brick bases");
    }
    g.generateBricks({0.02,0,0.07,0.20,0.10});
    check(g.bricks().size()==bCount,"Zero depth A must remove only A bricks");
    size_t visible=0;
    for(const auto& original:fullPattern)if(!original.isLengthA) {
        const auto& b=g.bricks()[visible++];
        check(!b.isLengthA && b.depth==0.07,"B must remain with its selected depth");
        check(b.startDistance==original.startDistance && b.endDistance==original.endDistance &&
              (b.origin-original.origin).norm()<1e-12,"Removing A must not repack B");
    }
    g.generateBricks({0.02,0.02,0,0.20,0.10});
    check(g.bricks().size()==aCount,"Zero depth B must remove only B bricks");
    for(const auto& b:g.bricks())check(b.isLengthA && b.depth==0.02,"A must remain with its selected depth");
    g.generateBricks({0.02,0,0,0.20,0.10});
    check(g.bricks().empty() && g.contourPaths().size()==9,"Both depths zero must omit all solids and retain paths");
    g.generateBricks({0.02,0.04,0.04,0.10,0.05});
    check(g.bricks().size()>originalCount, "Length sliders change population");
    rejects([&] { g.generateBricks({0.1,0.04,0.04,0.2,0.1}); });
    check(g.bricks().empty(), "Invalid options clear stale bricks");
    rejects([&] { g.generateBricks({0.01,-0.01,0.04,0.2,0.1}); });
    rejects([&] { g.generateBricks({0.01,0.04,-0.01,0.2,0.1}); });
    rejects([&] { g.generateBricks({0.01,0.04,std::numeric_limits<double>::infinity(),0.2,0.1}); });
    rejects([&] { g.generateBricks({0.01,0.04,0.04,std::numeric_limits<double>::quiet_NaN(),0.1}); });

    // Curved wall: independently compute area-weighted normals and verify the
    // barycentric interpolant, including points whose normal differs from a face.
    for (int i=0; i<v.rows(); ++i) v(i,1)=0.3*v(i,0)*v(i,0)+0.15*v(i,2)*v(i,2);
    g.setMesh(v,f); g.computeField(FieldMethod::Height); g.extractContours(0.1); g.generateBricks(options);
    Eigen::MatrixXd normals=Eigen::MatrixXd::Zero(v.rows(),3);
    for (int i=0; i<f.rows(); ++i) {
        const Eigen::Vector3d a=v.row(f(i,1))-v.row(f(i,0)), b=v.row(f(i,2))-v.row(f(i,0));
        for (int k=0; k<3; ++k) normals.row(f(i,k))+=a.cross(b).transpose();
    }
    for (int i=0; i<normals.rows(); ++i) normals.row(i).normalize();
    bool smooth=false;
    for (const auto& b : g.bricks()) {
        checkBox(b);
        Eigen::Vector3d expected=Eigen::Vector3d::Zero(), origin=Eigen::Vector3d::Zero();
        for (int k=0; k<3; ++k) {
            expected+=b.barycentric(k)*normals.row(f(b.triangleIndex,k)).transpose();
            origin+=b.barycentric(k)*v.row(f(b.triangleIndex,k)).transpose();
        }
        check((expected.normalized()-b.normal).norm()<1e-10, "Smooth barycentric normal");
        check((origin-b.origin).norm()<1e-10, "Origin must lie on closest triangle");
        const Eigen::Vector3d e1=v.row(f(b.triangleIndex,1))-v.row(f(b.triangleIndex,0));
        const Eigen::Vector3d e2=v.row(f(b.triangleIndex,2))-v.row(f(b.triangleIndex,0));
        if ((e1.cross(e2).normalized()-b.normal).norm()>1e-4) smooth=true;
    }
    check(smooth, "Normals must interpolate rather than remain flat per face");
    g.extractContours(0.2);
    check(g.bricks().empty() && g.contourPaths().empty(), "Re-extraction invalidates population");

    // Closed contours: a cylinder produces one closed path per level, no lost
    // edges or duplicate course seam bricks.
    constexpr int sides=32, rows=6;
    v.resize(sides*rows,3); f.resize(sides*(rows-1)*2,3); fi=0;
    const double pi=std::acos(-1.0);
    for (int j=0; j<rows; ++j) for (int i=0; i<sides; ++i) {
        v.row(j*sides+i)<<std::cos(2*pi*i/sides),std::sin(2*pi*i/sides),0.2*j;
        if (j+1<rows) {
            int a=j*sides+i,b=j*sides+(i+1)%sides;
            f.row(fi++)<<a,b,b+sides; f.row(fi++)<<a,b+sides,a+sides;
        }
    }
    g.setMesh(v,f); g.computeField(FieldMethod::Height); g.extractContours(0.2); g.generateBricks(options);
    check(g.contourPaths().size()==4, "Cylinder course count");
    size_t pathEdges=0;
    for (const auto& path:g.contourPaths()) {
        check(path.closed && (path.points.front()-path.points.back()).norm()<1e-12, "Closed contour ordering");
        check(path.length>6 && path.length<6.3, "Closed contour arc length");
        pathEdges+=path.points.size()-1;
    }
    check(pathEdges==static_cast<size_t>(g.contours().edges.rows()), "Every contour edge visited once");
    for (const auto& b:g.bricks()) checkBox(b);
    g.computeField(FieldMethod::Height);
    check(g.bricks().empty(), "Field changes invalidate population");
}
