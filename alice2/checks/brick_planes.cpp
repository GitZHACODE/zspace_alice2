#include <bricks/BrickGenerator.h>
#include <Eigen/Geometry>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

using namespace alice2::bricks;
namespace {
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
AlignmentPlane xPlane(double x){return {Eigen::Vector3d(x,0,0),Eigen::Vector3d::UnitX()};}
}
void checkBrickPlanes() {
    constexpr int nx=31,nz=11;
    Eigen::MatrixXd v(nx*nz,3);
    Eigen::MatrixXi f((nx-1)*(nz-1)*2,3);
    int fi=0;
    for(int j=0;j<nz;++j)for(int i=0;i<nx;++i){
        const int k=j*nx+i;v.row(k)<<i*0.1,0,j*0.1;
        if(i+1<nx&&j+1<nz){f.row(fi++)<<k,k+1,k+nx+1;f.row(fi++)<<k,k+nx+1,k+nx;}
    }
    BrickGenerator g;const BrickOptions options{0.02,0.04,0.04,0.2,0.1};
    g.setMesh(v,f);g.computeField(FieldMethod::Height);g.extractContours(0.1);
    g.setAlignmentPlanes({xPlane(1),xPlane(2),{Eigen::Vector3d(1,0,0),-Eigen::Vector3d::UnitX()}});
    check(g.alignmentPlanes().size()==2,"Coincident opposite plane normals must deduplicate");
    g.generateBricks(options);
    check(g.contourSegments().size()==27,"Two cuts should make three segments on nine contours");
    int aligned=0;const Brick* previous=nullptr;
    for(const auto& b:g.bricks()){
        check(b.segmentIndex>=0 && b.segmentIndex<static_cast<int>(g.contourSegments().size()),"Brick segment metadata");
        if(b.alignmentPlaneIndex>=0){
            ++aligned;
            check(std::abs(b.referencePoint.x()-(b.alignmentPlaneIndex==0?1:2))<1e-10,"Brick center must lie on plane");
            check(b.isLengthA==(b.levelIndex%2==0),"A/B at planes alternates by level");
            check(!b.trimmed && std::abs(b.length-(b.isLengthA?(2.0/6-0.12-0.02):0.1))<1e-10,"Shared layout keeps B fixed and adapts A");
            const auto& plane=g.alignmentPlanes()[b.alignmentPlaneIndex];
            check(plane.normal.dot(b.vertices[0]-plane.origin)*plane.normal.dot(b.vertices[1]-plane.origin)<0,
                  "One unbroken brick must straddle the plane, with no center cut");
        }
        if(previous && previous->pathIndex==b.pathIndex)
            check(b.startDistance-previous->endDistance>=options.gap-1e-9,"Plane layout must not duplicate/overlap bricks");
        previous=&b;
    }
    check(aligned==18,"Each plane crossing must emit exactly one shared center brick");
    check(g.bricks().size()==171,"Shared BrickCount: six equal center intervals per one-unit segment");
    for(const auto& b:g.bricks()) {
        check(std::abs(b.referenceDistance*6-std::round(b.referenceDistance*6))<1e-9,"Shared ComputeBricks uses equally divided centers");
        if(!b.isLengthA && !b.trimmed)check(std::abs(b.length-options.lengthB)<1e-10,"B must keep its fixed material length");
    }
    for(const auto& s:g.contourSegments())check(std::abs(s.endDistance-s.startDistance-1)<1e-10,"Contour segment length");

    // Hand-calculated Shared/Utils fixture: segment lengths 1, 1.2, 0.8
    // give divisions 6, 8, 6. At x=1 the joined A uses half of each
    // neighboring adjusted pitch, with one gap subtraction after joining.
    g.setAlignmentPlanes({xPlane(1),xPlane(2.2)});g.generateBricks(options);
    int unequalJoins=0;
    for(const auto& b:g.bricks())if(b.levelIndex==0 && b.alignmentPlaneIndex==0) {
        ++unequalJoins;
        const double leftPitch=2.0/6-0.12,rightPitch=2.4/8-0.12;
        check(std::abs(b.length-(leftPitch/2+rightPitch/2-0.02))<1e-10,"CombineBrickes must unite unequal halves before applying gap");
        check(std::abs(b.referencePoint.x()-1)<1e-10,"Combined record keeps the shared boundary reference");
        check(std::abs(b.origin.x()-(1+(rightPitch-leftPitch)/4))<1e-10,"Asymmetric width must be instantiated in the reference frame");
        check(b.startDistance<1 && b.endDistance>1,"No gap through the plane join");
    }
    check(unequalJoins==1,"One combined brick per unequal shared boundary");
    const auto joinedPattern=g.bricks();
    g.generateBricks({0.02,0,0.04,0.2,0.1});
    size_t visibleJoin=0;
    for(const auto& original:joinedPattern)if(!original.isLengthA) {
        check(visibleJoin<g.bricks().size(),"Visible B must retain its plane-layout slot");
        const auto& kept=g.bricks()[visibleJoin++];
        check(kept.alignmentPlaneIndex==original.alignmentPlaneIndex &&
              kept.referenceDistance==original.referenceDistance && std::abs(kept.length-original.length)<1e-12,
              "Zero A depth must preserve combined B bricks and plane alignment");
    }
    check(visibleJoin==g.bricks().size(),"Zero A depth must omit all A plane bricks");

    // Equal input lengths must share fitting adjustments rather than adapting
    // only A and then swapping different widths between consecutive courses.
    const BrickOptions equalOptions{0.02,0.04,0.04,0.10,0.10};
    g.generateBricks(equalOptions);
    std::vector<const Brick*> firstCourse,secondCourse;
    for(const auto& b:g.bricks()) {
        if(b.levelIndex==0)firstCourse.push_back(&b);
        if(b.levelIndex==1)secondCourse.push_back(&b);
    }
    check(!firstCourse.empty() && firstCourse.size()==secondCourse.size(),"Equal bond course counts");
    for(size_t i=0;i<firstCourse.size();++i) {
        const auto& a=*firstCourse[i];const auto& b=*secondCourse[i];
        check(std::abs(a.startDistance-b.startDistance)<1e-10 && std::abs(a.endDistance-b.endDistance)<1e-10,
              "Equal lengths must align joints across courses, including plane joins");
        check(std::abs(a.origin.x()-b.origin.x())<1e-10 && std::abs(a.length-b.length)<1e-10,
              "Equal bond solids must align independently of A/B course parity");
    }
    // The chosen lengths deliberately do not divide the first plane span exactly.
    check(std::abs(firstCourse[1]->length-0.08)<1e-10 && std::abs(firstCourse[2]->length-0.08)<1e-10,
          "Equal bond must distribute the fit equally to both types");
    g.setAlignmentPlanes({});g.generateBricks(equalOptions);
    firstCourse.clear();secondCourse.clear();
    for(const auto& b:g.bricks()) {
        if(b.levelIndex==0)firstCourse.push_back(&b);
        if(b.levelIndex==1)secondCourse.push_back(&b);
    }
    check(firstCourse.size()==secondCourse.size(),"Equal bond without planes has matching course counts");
    for(size_t i=0;i<firstCourse.size();++i)
        check(std::abs(firstCourse[i]->startDistance-secondCourse[i]->startDistance)<1e-10 &&
              std::abs(firstCourse[i]->endDistance-secondCourse[i]->endDistance)<1e-10,
              "Equal bond without planes must keep joints aligned");

    // Bending changes arc lengths between the same planes from course to course.
    // Equal bond must keep a shared count and normalized center positions.
    Eigen::MatrixXd curved=v;
    for(int i=0;i<curved.rows();++i)curved(i,1)=0.4*curved(i,2)*curved(i,0)*curved(i,0);
    BrickGenerator bending;
    bending.setMesh(curved,f);bending.computeField(FieldMethod::Height);bending.extractContours(0.1);
    bending.setAlignmentPlanes({xPlane(1),xPlane(2.2)});bending.generateBricks(equalOptions);
    std::map<int,std::vector<double>> courseFractions;
    for(const auto& b:bending.bricks()) {
        const auto& s=bending.contourSegments()[b.segmentIndex];
        if(s.startPlane==0 && s.endPlane==1)
            courseFractions[b.levelIndex].push_back((b.referenceDistance-s.startDistance)/(s.endDistance-s.startDistance));
    }
    check(courseFractions.size()==9,"Bent wall must retain matching plane sections on all courses");
    const auto& expectedFractions=courseFractions.begin()->second;
    for(const auto& [level,fractions]:courseFractions) {
        check(fractions.size()==expectedFractions.size(),"Equal bond must share subdivision counts across changing contour lengths");
        for(size_t i=0;i<fractions.size();++i)
            check(std::abs(fractions[i]-expectedFractions[i])<1e-9,"Equal bond must align normalized center positions on bent sections");
    }

    // Boundary, middle, terminal and equality-sized fragments merge repeatedly.
    g.setAlignmentPlanes({xPlane(0.4),xPlane(1),xPlane(1.4),xPlane(2),xPlane(2.8)});
    check(g.bricks().empty() && g.contourSegments().empty(),"Plane edits invalidate population only");
    check(g.field().size() && g.contours().edges.rows(),"Plane edits preserve fields/contours");
    g.generateBricks(options);
    check(g.contourSegments().size()==27,"Threshold equality and tiny end fragments must merge");
    for(const auto& s:g.contourSegments())check(s.endDistance-s.startDistance>0.4,"No short fragments remain when merging possible");
    for(const auto& b:g.bricks())if(b.alignmentPlaneIndex>=0)
        check(std::abs(b.origin.x()-1)<1e-9 || std::abs(b.origin.x()-2)<1e-9,"Removed cuts must not anchor bricks");
    g.generateBricks({0.02,0.04,0.04,0.1,0.05});
    check(g.contourSegments().size()>27,"Length changes must recompute short-segment merging");
    g.setAlignmentPlanes({{Eigen::Vector3d(0,0,0.2),Eigen::Vector3d::UnitZ()}});
    g.generateBricks(options);
    check(g.contourSegments().size()==9,"Coplanar contours must not be infinitely split");
    for(const auto& b:g.bricks())check(b.alignmentPlaneIndex<0,"Coplanar plane is not a crossing");

    // Native Rhino degree-1 curve export and OBJ polylines both define X/Z planes.
    const auto path=std::filesystem::temp_directory_path()/"alice2_plane_checks.obj";
    {
        std::ofstream out(path);
        out<<"v 1 -1 0\nv 1 1 0\nv 2 -1 0\nv 2 1 0\n"
             "cstype bspline\ndeg 1\ncurv 0 2 1 2\nparm u 0 0 2 2\nend\nl 3 4\nl 2 1\n";
    }
    g.loadPlanesObj(path.string());
    check(g.alignmentPlanes().size()==2,"OBJ lines must load and deduplicate");
    check(std::abs(g.alignmentPlanes()[0].normal.dot(Eigen::Vector3d::UnitZ()))<1e-12,"Imported line planes contain world Z");
    g.generateBricks(options);check(g.contourSegments().size()==27,"Imported planes split contours");
    {
        std::ofstream out(path);out<<"v 0 0 0\nv 0 0 1\nl 1 2\n";
    }
    bool rejected=false;try{g.loadPlanesObj(path.string());}catch(const std::exception&){rejected=true;}
    std::filesystem::remove(path);
    check(rejected && g.alignmentPlanes().size()==2,"World-Z line is invalid; failed import preserves planes");

    // Closed cylinder: plane crosses twice, including at the original path seam.
    constexpr int sides=32,rows=6;
    v.resize(sides*rows,3);f.resize(sides*(rows-1)*2,3);fi=0;
    const double pi=std::acos(-1.0);
    for(int j=0;j<rows;++j)for(int i=0;i<sides;++i){
        v.row(j*sides+i)<<std::cos(2*pi*i/sides),std::sin(2*pi*i/sides),j*0.2;
        if(j+1<rows){int a=j*sides+i,b=j*sides+(i+1)%sides;f.row(fi++)<<a,b,b+sides;f.row(fi++)<<a,b+sides,a+sides;}
    }
    g.setMesh(v,f);g.computeField(FieldMethod::Height);g.extractContours(0.2);
    g.setAlignmentPlanes({{Eigen::Vector3d::Zero(),Eigen::Vector3d::UnitY()}});g.generateBricks(options);
    check(g.contourSegments().size()==8,"Closed contours must split at both crossings");
    aligned=0;
    for(const auto& b:g.bricks())if(b.alignmentPlaneIndex>=0){++aligned;check(std::abs(b.referencePoint.y())<1e-10,"Closed seam center plane alignment");}
    check(aligned==8,"Closed seam anchors must be emitted once");
    g.setAlignmentPlanes({xPlane(-1)});g.generateBricks(options);
    check(g.contourSegments().size()==4,"Tangential plane contact does not divide a closed contour");
    for(const auto& b:g.bricks())check(b.alignmentPlaneIndex<0,"Tangencies must not introduce anchors");
    g.extractContours(0.3);check(g.contourSegments().empty(),"Contour edits clear plane segments");
}
