#include <alice2/viewer.h>
#include <input/CameraController.h>
#include <bricks/BrickGenerator.h>
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <map>
#include <sstream>

using namespace alice2;
using namespace alice2::bricks;

namespace {
Vec3 point(const Eigen::MatrixXd& v, int i) {
    return Vec3(static_cast<float>(v(i,0)), static_cast<float>(v(i,1)), static_cast<float>(v(i,2)));
}
Vec3 point(const Eigen::Vector3d& v) {
    return Vec3(static_cast<float>(v.x()),static_cast<float>(v.y()),static_cast<float>(v.z()));
}
}

class MeshBricksSketch : public ISketch {
public:
    std::string getName() const override { return "Brick Maker"; }
    std::string getDescription() const override { return "Contour courses, Flemish bond and smooth normal extrusion"; }
    void setup() override {
        scene().setBackgroundColor(Color(0.96f,0.96f,0.96f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        reload();
    }
    void update(float) override {
        lengthA_=std::round(lengthA_*1000.0f)/1000.0f;
        lengthB_=std::round(lengthB_*1000.0f)/1000.0f;
        depthA_=std::round(depthA_*1000.0f)/1000.0f;
        depthB_=std::round(depthB_*1000.0f)/1000.0f;
        if (method_ != lastMethod_) rebuildField();
        else if (spacing_ != lastSpacing_) rebuildContours();
        else if (gap_ != lastOptions_.gap || depthA_ != lastOptions_.depthA || depthB_ != lastOptions_.depthB ||
                 lengthA_ != lastOptions_.lengthA || lengthB_ != lastOptions_.lengthB || usePlanes_!=lastUsePlanes_) rebuildBricks();
        if (reload_) { reload_ = false; reload(); }
        if (frameRequested_) { frameRequested_=false; frame(); }
    }
    void draw(Renderer& r, Camera&) override {
        const auto& v = generator_.vertices();
        const auto& f = generator_.triangles();
        if (!positions_.empty() && showField_)
            r.drawMesh(positions_.data(), nullptr, colors_.data(), static_cast<int>(positions_.size()),
                       indices_.data(), static_cast<int>(indices_.size()), false);
        if (showWire_) for (int i = 0; i < f.rows(); ++i) for (int j = 0; j < 3; ++j)
            r.drawLine(point(v,f(i,j)), point(v,f(i,(j+1)%3)), Color(0.65f,0.65f,0.65f), 0.5f);
        if (showBricks_ && !brickPositions_.empty())
            r.drawMesh(brickPositions_.data(),nullptr,brickColors_.data(),static_cast<int>(brickPositions_.size()),
                       brickIndices_.data(),static_cast<int>(brickIndices_.size()),false);
        if (showBrickEdges_ && !brickEdges_.empty())
            r.drawLines(brickEdges_.data(),static_cast<int>(brickEdges_.size()),Color(0.15f,0.08f,0.04f),0.8f);
        const auto& c = generator_.contours();
        if (showContours_) for (int i = 0; i < c.edges.rows(); ++i)
            r.drawLine(point(c.vertices,c.edges(i,0)), point(c.vertices,c.edges(i,1)), Color(0.05f,0.08f,0.1f), 2);
        // Mesh annotations stay visible over the extruded bricks.
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_LINE_BIT | GL_CURRENT_BIT);
        glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDepthMask(GL_FALSE);
        const float scale=r.getContentScale();
        if(showCuts_ && !meshCuts_.empty())
            r.drawLines(meshCuts_.data(),static_cast<int>(meshCuts_.size()),Color(1.0f,0.02f,0.48f),1.0f*scale);
        if(!meshBoundary_.empty())
            r.drawLines(meshBoundary_.data(),static_cast<int>(meshBoundary_.size()),Color(0,0,0),3.0f*scale);
        if(showBoundaryVertices_) {
            for(int id:generator_.startVertices())r.drawPoint(point(v,id),Color(1,0,0),7*scale);
            for(int id:generator_.endVertices())r.drawPoint(point(v,id),Color(0,0.15f,1),7*scale);
        }
        glPopAttrib();
        drawUI(r);
    }
    bool onMousePress(int button, int state, int x, int y) override {
        const bool consumed=ui_ && ui_->onMousePress(button,state,x,y);
        if(x>=8 && x<=416 && y>=8 && y<=756) {
            if(button==0)input().setMouseButton(MouseButton::Left,KeyState::Released);
            return true;
        }
        return consumed;
    }
    bool onMouseMove(int x, int y) override { return ui_ && ui_->onMouseMove(x,y); }
    bool onKeyPress(unsigned char key, int, int) override {
        if (key == 'r' || key == 'R') { reload_ = true; return true; }
        if (key == 'f' || key == 'F') { frame(); return true; }
        return false;
    }
private:
    void makeUI() {
        ui_=std::make_unique<SimpleUI>(input());
        ui_->setTheme(SimpleUI::UITheme::Dark);
        ui_->addButtonGroup({"Height Z","Geodesic","Blend"},Vec2(20,84),120,28,10,method_);
        ui_->addSlider("Contour spacing",Vec2(20,154),300,0.01f,1.00f,spacing_);
        ui_->addSlider("Gap",Vec2(20,194),300,0.01f,0.10f,gap_);
        ui_->addSlider("Length A",Vec2(20,234),300,0.05f,0.20f,lengthA_);
        ui_->addSlider("Length B",Vec2(20,274),300,0.05f,0.20f,lengthB_);
        ui_->addSlider("Depth A",Vec2(20,314),120,0.00f,0.10f,depthA_);
        ui_->addSlider("Depth B",Vec2(220,314),120,0.00f,0.10f,depthB_);
        ui_->addToggle("Use planes",{20,366,180,28},usePlanes_);
        ui_->addToggle("Mesh cuts",{210,366,190,28},showCuts_);
        ui_->addToggle("Bricks",{20,428,180,28},showBricks_);
        ui_->addToggle("Brick edges",{210,428,190,28},showBrickEdges_);
        ui_->addToggle("Contours",{20,464,180,28},showContours_);
        ui_->addToggle("Mesh field",{210,464,190,28},showField_);
        ui_->addToggle("Mesh wire",{20,500,180,28},showWire_);
        ui_->addToggle("Boundary vertices",{210,500,190,28},showBoundaryVertices_);
        ui_->addToggle("Reload inputs",{20,540,180,28},reload_);
        ui_->addToggle("Frame mesh",{210,540,190,28},frameRequested_);
    }
    void drawUI(Renderer& r) {
        int vx,vy,vw,vh;r.getViewport(vx,vy,vw,vh);
        const float scale=r.getContentScale();
        const float height=std::min(756.0f,vh/scale-8.0f);
        const Color old=r.getCurrentColor();
        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_SCISSOR_BIT);
        glDisable(GL_DEPTH_TEST);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);
        glDepthMask(GL_FALSE);
        glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();
        glOrtho(0,vw/scale,vh/scale,0,-1,1);
        glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
        glColor4f(0.97f,0.97f,0.97f,1);
        glBegin(GL_QUADS);
        glVertex2f(8,8);glVertex2f(416,8);glVertex2f(416,height);glVertex2f(8,height);
        glEnd();
        glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
        glEnable(GL_SCISSOR_TEST);
        glScissor(vx+static_cast<int>(8*scale),vy+vh-static_cast<int>(height*scale),
                  static_cast<int>(408*scale),static_cast<int>((height-8)*scale));
        r.setColor(Color(0.10f,0.08f,0.06f));
        r.drawString("Brick Maker",20,30);
        r.drawString("R reload   F frame   Units: mesh",20,52);
        r.drawString("FIELD METHOD",20,76);
        r.drawString("BRICK DIMENSIONS",20,132);
        r.drawString("PLANE ALIGNMENT",20,350);
        r.drawString("DISPLAY",20,414);
        if(ui_)ui_->draw(r);
        r.setColor(Color(0.1f,0.1f,0.1f));
        r.drawString("STATUS",20,596);
        // Wrap status text within the control area, including long words.
        std::string remaining=status_;
        float y=618;
        while(!remaining.empty() && y+18<height) {
            size_t length=std::min(size_t(38),remaining.size());
            if(length<remaining.size()) {
                const auto space=remaining.rfind(' ',length);
                if(space!=std::string::npos && space>0)length=space;
            }
            std::string line=remaining.substr(0,length);
            remaining.erase(0,length);
            while(!remaining.empty() && remaining.front()==' ')remaining.erase(0,1);
            if(y+36>=height && !remaining.empty()) {line=line.substr(0,35)+"...";remaining.clear();}
            r.drawString(line,20,y);y+=18;
        }
        glPopAttrib();r.setColor(old);
    }
    void rebuildMeshAnnotations() {
        meshBoundary_.clear();meshCuts_.clear();
        const auto& v=generator_.vertices();const auto& f=generator_.triangles();
        std::map<std::pair<int,int>,int> edges;
        for(int i=0;i<f.rows();++i)for(int k=0;k<3;++k)++edges[std::minmax(f(i,k),f(i,(k+1)%3))];
        for(const auto& [edge,count]:edges)if(count==1){meshBoundary_.push_back(point(v,edge.first));meshBoundary_.push_back(point(v,edge.second));}
        const double tolerance=std::max(1.0,(v.colwise().maxCoeff()-v.colwise().minCoeff()).norm())*1e-9;
        for(const auto& plane:planeInputs_)for(int i=0;i<f.rows();++i) {
            std::array<Eigen::Vector3d,3> p;
            std::array<double,3> d;
            for(int k=0;k<3;++k){p[k]=v.row(f(i,k));d[k]=plane.normal.dot(p[k]-plane.origin);}
            if(std::abs(d[0])<=tolerance && std::abs(d[1])<=tolerance && std::abs(d[2])<=tolerance)continue;
            std::vector<Eigen::Vector3d> hits;
            const auto add=[&](const Eigen::Vector3d& hit){
                for(const auto& old:hits)if((hit-old).norm()<=tolerance)return;
                hits.push_back(hit);
            };
            for(int k=0;k<3;++k) {
                const int next=(k+1)%3;
                if(std::abs(d[k])<=tolerance)add(p[k]);
                if((d[k]>tolerance && d[next]<-tolerance)||(d[k]<-tolerance && d[next]>tolerance))
                    add(p[k]+d[k]/(d[k]-d[next])*(p[next]-p[k]));
            }
            if(hits.size()==2 && (hits[0]-hits[1]).norm()>tolerance){meshCuts_.push_back(point(hits[0]));meshCuts_.push_back(point(hits[1]));}
        }
    }
    void frame() {
        const auto& v = generator_.vertices();
        if (!v.rows()) return;
        const Eigen::RowVector3d low = v.colwise().minCoeff(), high = v.colwise().maxCoeff();
        const Eigen::RowVector3d centre = (low+high)/2;
        auto& controls = Application::getInstance()->getCameraController();
        controls.setOrbitCenter(Vec3(static_cast<float>(centre.x()), static_cast<float>(centre.y()), static_cast<float>(centre.z())));
        controls.setOrbitDistance(static_cast<float>((high-low).norm()*1.8));
        camera().setFarPlane(static_cast<float>(std::max(1000.0,(high-low).norm()*20)));
    }
    void reload() {
        try {
            // Runtime overrides first, stable source data folder last.
            std::filesystem::path path;
            for (const auto& candidate : {std::filesystem::path("input.obj"), std::filesystem::path("data/input.obj"),
                                          std::filesystem::path(ALICE2_BRICKS_INPUT_DIR)/"input.obj"})
                if (std::filesystem::exists(candidate)) { path = candidate; break; }
            if (path.empty()) throw std::runtime_error("Add input.obj to userSrc/bricks/data, then press R.");
            generator_.loadObj(path.string());
            positions_.clear(); indices_.clear();
            for (int i = 0; i < generator_.vertices().rows(); ++i) positions_.push_back(point(generator_.vertices(),i));
            const auto& f = generator_.triangles();
            for (int i = 0; i < f.rows(); ++i) for (int j = 0; j < 3; ++j) indices_.push_back(f(i,j));
            colors_.assign(positions_.size(), Color(0.8f,0.8f,0.8f));
            planeInputs_.clear(); planeFileNote_.clear();
            generator_.setAlignmentPlanes({});
            const auto planePath=path.parent_path()/"planes.obj";
            if(std::filesystem::exists(planePath)) {
                try {
                    generator_.loadPlanesObj(planePath.string());
                    planeInputs_=generator_.alignmentPlanes();
                } catch(const std::exception& e) { planeFileNote_=" | planes.obj: "+std::string(e.what()); }
            }
            rebuildMeshAnnotations();
            makeUI();
            if (!hasLoadedMesh_) {
                frame();
                hasLoadedMesh_ = true;
            }
            rebuildField();
        } catch (const std::exception& e) {
            status_ = e.what(); std::cerr << "[MeshBricks] " << status_ << '\n';
            if (!ui_) makeUI();
        }
    }
    void rebuildField() {
        lastMethod_ = method_;
        clearBrickPreview();
        try {
            generator_.computeField(static_cast<FieldMethod>(method_));
            const auto& field = generator_.field();
            const double minimum = field.minCoeff();
            const double span = field.maxCoeff()-minimum;
            for (int i = 0; i < field.size(); ++i) {
                const float t = span > 0 ? static_cast<float>((field(i)-minimum)/span) : 0;
                colors_[i] = Color(1-t, 0.25f+0.5f*(1-std::abs(2*t-1)), t);
            }
            rebuildContours();
        } catch (const std::exception& e) {
            colors_.assign(positions_.size(), Color(0.8f,0.8f,0.8f));
            lastSpacing_ = spacing_; status_ = e.what();
        }
    }
    void rebuildContours() {
        lastSpacing_ = spacing_;
        clearBrickPreview();
        try {
            generator_.extractContours(spacing_);
            rebuildBricks();
        } catch (const std::exception& e) { status_ = e.what(); }
    }
    void clearBrickPreview() {
        brickPositions_.clear(); brickColors_.clear(); brickIndices_.clear(); brickEdges_.clear();
    }
    void rebuildBricks() {
        lastOptions_ = {gap_,depthA_,depthB_,lengthA_,lengthB_};
        lastUsePlanes_=usePlanes_;
        clearBrickPreview();
        try {
            generator_.setAlignmentPlanes(usePlanes_?planeInputs_:std::vector<AlignmentPlane>{});
            generator_.generateBricks(lastOptions_);
            static constexpr int faces[12][3] = {
                {0,2,1},{0,3,2},{4,5,6},{4,6,7}, {0,1,5},{0,5,4},
                {1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
            static constexpr int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            const auto& bricks = generator_.bricks();
            brickPositions_.reserve(bricks.size()*8); brickColors_.reserve(bricks.size()*8);
            brickIndices_.reserve(bricks.size()*36); brickEdges_.reserve(bricks.size()*24);
            for (const auto& brick : bricks) {
                const int offset = static_cast<int>(brickPositions_.size());
                const Color color = brick.isLengthA ? Color(0.28f,0.17f,0.11f) : Color(0.38f,0.25f,0.16f);
                for (const auto& v : brick.vertices) { brickPositions_.push_back(point(v)); brickColors_.push_back(color); }
                for (const auto& face : faces) for (int id : face) brickIndices_.push_back(offset+id);
                for (const auto& edge : edges) for (int id : edge) brickEdges_.push_back(point(brick.vertices[id]));
            }
            std::ostringstream text;
            static const char* names[] = {"Height Z", "Exact geodesic", "Exact blend"};
            text << names[method_] << " | " << generator_.vertices().rows() << " vertices | red " << generator_.startVertices().size()
                 << " blue " << generator_.endVertices().size() << " | " << generator_.contours().levels.size()
                 << " courses, " << generator_.bricks().size() << " bricks | "
                 << generator_.alignmentPlanes().size() << " planes, " << generator_.contourSegments().size() << " segments";
            if (generator_.field().maxCoeff() == generator_.field().minCoeff()) text << " | Constant field";
            status_ = text.str()+planeFileNote_;
        } catch (const std::exception& e) { status_ = e.what(); }
    }
    BrickGenerator generator_;
    std::unique_ptr<SimpleUI> ui_;
    std::vector<Vec3> positions_;
    std::vector<Color> colors_;
    std::vector<int> indices_;
    std::vector<Vec3> brickPositions_, brickEdges_;
    std::vector<Color> brickColors_;
    std::vector<int> brickIndices_;
    BrickOptions lastOptions_{0,0,0,0,0};
    std::vector<AlignmentPlane> planeInputs_;
    std::string planeFileNote_;
    bool usePlanes_=true,lastUsePlanes_=true,showCuts_=false;
    bool showBoundaryVertices_=true,frameRequested_=false;
    std::vector<Vec3> meshBoundary_,meshCuts_;
    int method_ = 1, lastMethod_ = 1;
    float spacing_ = 0.5f, lastSpacing_ = 0.5f;
    float gap_ = 0.01f, depthA_ = 0.05f, depthB_ = 0.05f, lengthA_ = 0.20f, lengthB_ = 0.10f;
    bool showField_ = false, showWire_ = false, reload_ = false;
    bool showBricks_ = true, showBrickEdges_ = true, showContours_ = true;
    bool hasLoadedMesh_ = false;
    std::string status_;
};
ALICE2_REGISTER_SKETCH_AUTO(MeshBricksSketch)
