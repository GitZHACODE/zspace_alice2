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
}

class MeshBricksSketch : public ISketch {
public:
    std::string getName() const override { return "Mesh Bricks - Stage 1"; }
    std::string getDescription() const override { return "Height / exact geodesic / exact blend fields and contours"; }
    void setup() override {
        scene().setBackgroundColor(Color(0.96f,0.96f,0.96f));
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        reload();
    }
    void update(float) override {
        if (method_ != lastMethod_) rebuildField();
        else if (spacing_ != lastSpacing_) rebuildContours();
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
        const auto& c = generator_.contours();
        for (int i = 0; i < c.edges.rows(); ++i)
            r.drawLine(point(c.vertices,c.edges(i,0)), point(c.vertices,c.edges(i,1)), Color(0.05f,0.08f,0.1f), 2);
        for (int id : generator_.startVertices()) r.drawPoint(point(v,id), Color(1,0,0), 8);
        for (int id : generator_.endVertices()) r.drawPoint(point(v,id), Color(0,0.15f,1), 8);
        r.setColor(Color(0.05f,0.05f,0.05f));
        r.drawString("Mesh Bricks / Stage 1   |   R reload input.obj   F frame mesh", 12, 24);
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
        ui_->addToggle("Field", {12,180,90,26}, showField_);
        ui_->addToggle("Wire", {110,180,90,26}, showWire_);
        ui_->addToggle("Reload OBJ", {208,180,120,26}, reload_);
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
        try {
            generator_.extractContours(spacing_);
            std::ostringstream text;
            static const char* names[] = {"Height Z", "Exact geodesic", "Exact blend"};
            text << names[method_] << " | " << generator_.vertices().rows() << " vertices | red " << generator_.startVertices().size()
                 << " blue " << generator_.endVertices().size() << " | " << generator_.contours().levels.size()
                 << " contour levels, " << generator_.contours().edges.rows() << " edges";
            if (generator_.field().maxCoeff() == generator_.field().minCoeff()) text << " | Constant field";
            status_ = text.str();
        } catch (const std::exception& e) { status_ = e.what(); }
    }
    BrickGenerator generator_;
    std::unique_ptr<SimpleUI> ui_;
    std::vector<Vec3> positions_;
    std::vector<Color> colors_;
    std::vector<int> indices_;
    int method_ = 1, lastMethod_ = 1;
    float spacing_ = 0.5f, lastSpacing_ = 0.5f;
    bool showField_ = true, showWire_ = false, reload_ = false;
    bool hasLoadedMesh_ = false;
    std::string status_;
};
ALICE2_REGISTER_SKETCH_AUTO(MeshBricksSketch)
