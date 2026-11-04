#include <alice2/viewer.h>
#include <input/CameraController.h>
#include <bricks/BrickGenerator.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
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
    std::string getName() const override { return "Mesh Bricks - Stage 2"; }
    std::string getDescription() const override { return "Contour courses, Flemish bond and smooth normal extrusion"; }
    void setup() override {
        scene().setBackgroundColor(Color(0.96f,0.96f,0.96f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        reload();
    }
    void update(float) override {
        if (method_ != lastMethod_) rebuildField();
        else if (spacing_ != lastSpacing_) rebuildContours();
        else if (gap_ != lastOptions_.gap || depth_ != lastOptions_.depth ||
                 lengthA_ != lastOptions_.lengthA || lengthB_ != lastOptions_.lengthB) rebuildBricks();
        if (reload_) { reload_ = false; reload(); }
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
        for (int id : generator_.startVertices()) r.drawPoint(point(v,id), Color(1,0,0), 8);
        for (int id : generator_.endVertices()) r.drawPoint(point(v,id), Color(0,0.15f,1), 8);
        r.setColor(Color(0.05f,0.05f,0.05f));
        r.drawString("Mesh Bricks / Stage 2   |   R reload input.obj   F frame mesh", 12, 24);
        r.drawString(status_, 12, 48);
        r.drawString("Red: start   Blue: end   |   spacing in mesh units (blend is approximate)", 12, 72);
        if (ui_) ui_->draw(r);
    }
    bool onMousePress(int button, int state, int x, int y) override { return ui_ && ui_->onMousePress(button,state,x,y); }
    bool onMouseMove(int x, int y) override { return ui_ && ui_->onMouseMove(x,y); }
    bool onKeyPress(unsigned char key, int, int) override {
        if (key == 'r' || key == 'R') { reload_ = true; return true; }
        if (key == 'f' || key == 'F') { frame(); return true; }
        return false;
    }
private:
    void makeUI() {
        ui_ = std::make_unique<SimpleUI>(input());
        ui_->setTheme(SimpleUI::UITheme::Dark);
        ui_->addButtonGroup({"Height Z", "Exact geodesic", "Exact blend"}, Vec2(12,92), 124,28,6,method_);
        ui_->addSlider("Contour spacing", Vec2(12,150), 300, 0.01f, 1.00f, spacing_);
        ui_->addSlider("Gap", Vec2(12,190), 300, 0.01f, 0.10f, gap_);
        ui_->addSlider("Depth", Vec2(12,230), 300, 0.01f, 0.10f, depth_);
        ui_->addSlider("Length A", Vec2(12,270), 300, 0.05f, 0.20f, lengthA_);
        ui_->addSlider("Length B", Vec2(12,310), 300, 0.05f, 0.20f, lengthB_);
        ui_->addToggle("Bricks", {12,340,90,26}, showBricks_);
        ui_->addToggle("Brick edges", {110,340,120,26}, showBrickEdges_);
        ui_->addToggle("Contours", {238,340,100,26}, showContours_);
        ui_->addToggle("Field", {12,376,90,26}, showField_);
        ui_->addToggle("Wire", {110,376,90,26}, showWire_);
        ui_->addToggle("Reload OBJ", {208,376,120,26}, reload_);
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
        lastOptions_ = {gap_,depth_,lengthA_,lengthB_};
        clearBrickPreview();
        try {
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
                const Color color = brick.isLengthA ? Color(0.75f,0.32f,0.18f) : Color(0.90f,0.56f,0.30f);
                for (const auto& v : brick.vertices) { brickPositions_.push_back(point(v)); brickColors_.push_back(color); }
                for (const auto& face : faces) for (int id : face) brickIndices_.push_back(offset+id);
                for (const auto& edge : edges) for (int id : edge) brickEdges_.push_back(point(brick.vertices[id]));
            }
            std::ostringstream text;
            static const char* names[] = {"Height Z", "Exact geodesic", "Exact blend"};
            text << names[method_] << " | " << generator_.vertices().rows() << " vertices | red " << generator_.startVertices().size()
                 << " blue " << generator_.endVertices().size() << " | " << generator_.contours().levels.size()
                 << " courses, " << generator_.bricks().size() << " bricks";
            if (generator_.field().maxCoeff() == generator_.field().minCoeff()) text << " | Constant field";
            status_ = text.str();
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
    BrickOptions lastOptions_{0,0,0,0};
    int method_ = 1, lastMethod_ = 1;
    float spacing_ = 0.5f, lastSpacing_ = 0.5f;
    float gap_ = 0.01f, depth_ = 0.05f, lengthA_ = 0.20f, lengthB_ = 0.10f;
    bool showField_ = false, showWire_ = false, reload_ = false;
    bool showBricks_ = true, showBrickEdges_ = true, showContours_ = true;
    bool hasLoadedMesh_ = false;
    std::string status_;
};
ALICE2_REGISTER_SKETCH_AUTO(MeshBricksSketch)
