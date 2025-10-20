#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>

#include <computeGeom/scalarField.h>
#include <computeGeom/scalarField3D.h>
#include <objects/GraphObject.h>
#include <objects/MeshObject.h>

// === Latent SDF CUDA decoder + navigator ===
#include <ML/DeepSDF/LatentSDF_CUDA.h>
#include <ML/DeepSDF/LatentNavigator_CUDA.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

using namespace alice2;
using DeepSDF::TinyAutoDecoderCUDA;
using DeepSDF::LatentNavigator_CUDA;
using DeepSDF::FieldRenderConfig;
using DeepSDF::GridField;
using DeepSDF::FieldDomain;

// ----------------------------------------------------------------------------------------

class SDFStack_FromLatentPanel : public ISketch {
public:
    std::string getName() const override        { return "SDF Stack (Latent Panel)"; }
    std::string getDescription() const override { return "Click latent panel to stack decoded SDF contours; G to generate interpolated stack"; }
    std::string getAuthor() const override      { return "alice2 User"; }

    void setup() override {
        // Scene
        scene().setBackgroundColor(Color(0.92f, 0.92f, 0.92f));
        scene().setShowAxes(false);
        scene().setShowGrid(false);

        // Mesh obj
        m_meshObject = std::make_shared<MeshObject>("StackIsoMesh");
        m_meshObject->setVisible(false);
        m_meshObject->setRenderMode(MeshRenderMode::NormalShaded);
        m_meshObject->setShowEdges(false);
        m_meshObject->setShowVertices(false);
        scene().addObject(m_meshObject);

        // ---- Load latent model JSON (CUDA decoder + domain + Z) ----
        bool loaded = m_decoder.loadModelJSON(m_snapshotPath, m_domain, /*maxBatch*/256, /*seed*/1234);
        if (!loaded) {
            // Fallback init so the sketch keeps running with default ranges.
            m_decoder.initialize(/*numShapes*/4, /*latentDim*/16, /*hidden*/{64,64,64},
                                 /*seed*/1234, /*maxBatch*/256);
            m_domain = FieldDomain{};
        } else {
            // Ensure the host mirror of latents is current for navigator usage.
            m_decoder.syncLatentsToHost();
        }

        // ---- CUDA latent navigator for panel rendering + sampling ----
        m_navigator.initialize(&m_decoder, &m_domain, &m_decoder.latents());
        m_navigator.setPanelResolution(m_panelN, m_panelTileRes, m_panelGap);
        m_navigator.markPanelDirty();
        m_hoverUV_.reset();
        updateDetailField(0.5f, 0.5f);

        // UI
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);

        // Stack controls
        m_ui->addSlider("Total Height",  Vec2{10, 130}, 240.0f, 00.0f, 500.0f, m_totalHeight);
        m_ui->addSlider("Iso Level",     Vec2{10, 160}, 240.0f, -0.5f, 0.5f,    m_isoLevel);
        m_ui->addSlider("Gen Layers",    Vec2{10, 190}, 240.0f, 2.0f,  200.0f,  m_genLayersF);
        m_ui->addToggle("Smooth*5",      UIRect{10, 220, 100, 22},  m_btnSmooth);
        m_ui->addToggle("Laplacian*5",   UIRect{120,220, 120, 22},  m_btnLaplacian);
        m_ui->addToggle("Build Mesh",    UIRect{10,  250, 100, 22}, m_btnBuildMesh);
        m_ui->addToggle("Display Mesh",  UIRect{120, 250, 120, 22}, m_meshVisible);

        // Export
        m_ui->addToggle("Export",         UIRect{10, 280, 100, 22}, m_btnExport);

        // Init status
        m_statusMessage = loaded
            ? "Loaded latent model. Click the panel to add slices. Press G to generate stack."
            : "Using fallback CUDA decoder. Panel will show default latent tiles.";
    }

    void cleanup() override {
        m_navigator.shutdown();
        clearAllSelections();
    }

    void update(float) override {
        postUpdateUI();
    }

    void draw(Renderer& r, Camera&) override {
        // Header
        r.setColor(Color(0.1f, 0.1f, 0.1f));
        r.drawString("SDF Stack (Latent Panel)", 10, 20);
        r.drawString("Slices: " + std::to_string(m_fields.size()), 10, 40);
        r.drawString("Total Height: " + std::to_string(int(m_totalHeight)), 10, 60);
        r.drawString("Iso: " + std::to_string(m_isoLevel) + "   GenLayers(G): " + std::to_string(int(m_genLayersF)), 10, 80);
        r.drawString(m_statusMessage, 10, 100);

        // Panel (400x400 @ top-left)
        m_navigator.drawPanel(r, kPanelLeft, kPanelTop, kPanelSize, m_fieldRenderCfg);
        if (m_hoverUV_) {
            const float u = m_hoverUV_->first;
            const float v = m_hoverUV_->second;
            const float x = kPanelLeft + u * kPanelSize;
            const float y = kPanelTop  + v * kPanelSize;
            r.draw2dLine(Vec2(x - 6.0f, y), Vec2(x + 6.0f, y), Color(1.0f, 0.25f, 0.25f), 2.0f);
            r.draw2dLine(Vec2(x, y - 6.0f), Vec2(x, y + 6.0f), Color(1.0f, 0.25f, 0.25f), 2.0f);
        }

        // Live detail field (panel hover preview) positioned to the right of the main panel
        const float detailLeft = kPanelLeft;
        const float detailTop  = kPanelTop - m_detailFieldSize - 20.0f;
        r.setColor(Color(0.8f, 0.8f, 0.9f));
        r.drawString("Hover Detail", detailLeft, detailTop - 18.0f);
        if (!m_detailField.values.empty()) {
            m_navigator.drawField(r, m_detailField, detailLeft, detailTop, m_detailFieldSize, m_fieldRenderCfg);
        }
        r.setColor(Color(0,0,0));

        // Place contours (height spacing derived from totalHeight)
        updateContourPlacement();

        // UI overlay
        if (m_ui) m_ui->draw(r);
    }

    // Mouse: click panel to append a decoded slice
    bool onMousePress(int button, int state, int x, int y) override {
        if (m_ui && m_ui->onMousePress(button, state, x, y)) return true;
        if (state != 0 /*down*/) return false;

        auto uv = panelUVFromMouse(static_cast<float>(x), static_cast<float>(y));
        if (!uv) return false;

        m_hoverUV_ = *uv;
        updateDetailField(uv->first, uv->second);

        // Record click in latent UV path
        m_clickUVs.push_back(*uv);

        // Decode full field at this UV and push to stack
        GridField gf;
        if (decodeAtUV(*uv, gf)) {
            pushFieldFromGrid(gf);
            m_statusMessage = "Added slice at panel uv=(" + f2(uv->first) + "," + f2(uv->second) + ")";
        } else {
            m_statusMessage = "Decode failed at that point.";
        }
        return true;
    }

    bool onMouseMove(int x, int y) override {
        if (m_ui && m_ui->onMouseMove(x, y)) return true;
        auto uv = panelUVFromMouse(static_cast<float>(x), static_cast<float>(y));
        if (uv) {
            m_hoverUV_ = *uv;
            updateDetailField(uv->first, uv->second);
            return true;
        }
        return false;
    }

    bool onKeyPress(unsigned char key, int, int) override {
        if (key=='g' || key=='G') { generateInterpolatedStack(); return true; }
        if (key=='s' || key=='S') { m_decoder.saveModelJSON(m_snapshotPath, m_domain); m_statusMessage="Saved model JSON."; return true; }

        // Keep J/K smoothing shortcuts
        if (key=='j' || key=='J') { for(int i=0;i<5;++i) smooth(); return true; }
        if (key=='k' || key=='K') { for(int i=0;i<5;++i) applyStackLaplacian(); return true; }
        if (key=='c' || key=='C') { clearAllSelections(); return true; }
        return false;
    }

private:
    // ===== Latent → GridField → ScalarField2D helpers =====
    std::optional<std::pair<float,float>> panelUVFromMouse(float mouseX, float mouseY) const {
        if (mouseX < kPanelLeft || mouseY < kPanelTop ||
            mouseX > kPanelLeft + kPanelSize || mouseY > kPanelTop + kPanelSize) {
            return std::nullopt;
        }
        const float u = (mouseX - kPanelLeft) / kPanelSize;
        const float v = (mouseY - kPanelTop) / kPanelSize;
        return std::make_pair(std::clamp(u, 0.0f, 1.0f),
                              std::clamp(v, 0.0f, 1.0f));
    }

    void updateDetailField(float u, float v) {
        if (!m_navigator.getBlendedField(u, v, m_detailField)) {
            m_detailField.values.clear();
            m_detailField.minValue = m_detailField.maxValue = 0.0f;
        }
    }

    bool decodeAtUV(const std::pair<float,float>& uv, GridField& out) {
        return m_navigator.getBlendedField(uv.first, uv.second, out);
    }

    void pushFieldFromGrid(const GridField& g) {
        const int resX = m_domain.resX;
        const int resY = m_domain.resY;

        // bounds from domain
        m_boundsMin = Vec3(m_domain.xMin, m_domain.yMin, 0.0f);
        m_boundsMax = Vec3(m_domain.xMax, m_domain.yMax, 0.0f);

        ScalarField2D slice(m_boundsMin, m_boundsMax, resX, resY);
        slice.set_values(g.values);
        m_fields.emplace_back(std::move(slice));

        regenerateContours();
        invalidateVolumeMesh();
    }

    // ===== Interpolated stack generation (G): replace stack with N layers along path =====
    void generateInterpolatedStack() {
        const int L = std::max(2, int(std::round(m_genLayersF)));
        if (m_clickUVs.size() < 2) {
            m_statusMessage = "Need at least 2 clicks on the panel to generate a path stack.";
            return;
        }

        // Build polyline samples across all segments with L layers total (evenly distributed)
        std::vector<std::pair<float,float>> samples;
        const int S = int(m_clickUVs.size()) - 1;
        for (int i=0;i<L;++i) {
            float t = (L==1)?0.f: float(i)/float(L-1);   // global 0..1
            float segF = t * S;
            int seg = std::min(S-1, int(std::floor(segF)));
            float lt = std::clamp(segF - float(seg), 0.f, 1.f);
            const auto& A = m_clickUVs[seg];
            const auto& B = m_clickUVs[seg+1];
            float u = A.first  * (1-lt) + B.first  * lt;
            float v = A.second * (1-lt) + B.second * lt;
            samples.emplace_back(u,v);
        }

        // Decode each sample; replace m_fields
        std::vector<ScalarField2D> newFields;
        newFields.reserve(samples.size());
        GridField tmp;
        for (auto& uv : samples) {
            if (!decodeAtUV(uv, tmp)) continue;
            ScalarField2D slice(Vec3(m_domain.xMin, m_domain.yMin, 0),
                                Vec3(m_domain.xMax, m_domain.yMax, 0),
                                m_domain.resX, m_domain.resY);
            slice.set_values(tmp.values);
            newFields.emplace_back(std::move(slice));
        }
        m_fields.swap(newFields);

        regenerateContours();
        invalidateVolumeMesh();

        m_statusMessage = "Generated interpolated stack: " + std::to_string(m_fields.size()) + " layers.";
    }

    void clearAllSelections() {
        // clear latent picks
        m_clickUVs.clear();

        // clear 2D slices & contours
        m_fields.clear();
        clearContourObjects(); // removes contour GraphObjects from the scene

        // clear volume + mesh
        invalidateVolumeMesh(); // hides mesh and resets m_volumeField

        m_statusMessage = "Cleared selections and stack.";
    }

    // ===== Stack processing (unchanged logic) =====
    void applyStackLaplacian() {
        if (m_fields.size() < 3) { m_statusMessage = "Need 3+ slices for Laplacian"; return; }
        const size_t n = m_fields.size();
        std::vector<std::vector<float>> smoothed(n);
        for (size_t i=0;i<n;++i) smoothed[i] = m_fields[i].get_values();
        for (size_t i=1;i+1<n;++i) {
            const auto& prev = m_fields[i-1].get_values();
            const auto& next = m_fields[i+1].get_values();
            auto& dest = smoothed[i];
            for (size_t k=0;k<dest.size();++k) dest[k] = 0.5f*(prev[k]+next[k]);
        }
        for (size_t i=0;i<n;++i) m_fields[i].set_values(smoothed[i]);
        regenerateContours(); invalidateVolumeMesh();
        m_statusMessage = "Stack Laplacian x5 applied";
    }

    void smooth() {
        if (m_fields.empty()) { m_statusMessage = "No slices"; return; }
        for (auto& f : m_fields) smoothField(f);
        regenerateContours(); invalidateVolumeMesh();
        m_statusMessage = "In-plane smoothing x5 applied";
    }

    void smoothField(ScalarField2D& field) {
        const auto& vals = field.get_values();
        if (vals.empty()) return;
        auto res = field.get_resolution();
        const int rx = res.first, ry = res.second;
        std::vector<float> out(vals.size(), 0.0f);
        auto idx = [rx](int x,int y){return y*rx+x;};
        for (int y=0;y<ry;++y) for (int x=0;x<rx;++x) {
            float sum=0; int cnt=0;
            for (int ny=std::max(0,y-1); ny<=std::min(ry-1,y+1); ++ny)
            for (int nx=std::max(0,x-1); nx<=std::min(rx-1,x+1); ++nx) {
                sum += vals[idx(nx,ny)]; ++cnt;
            }
            out[idx(x,y)] = cnt>0? sum/float(cnt) : vals[idx(x,y)];
        }
        field.set_values(out);
    }

    bool buildVolumeMeshFromStack() {
        if (m_fields.empty()) { m_statusMessage = "Add or generate slices first"; return false; }

        const auto res = m_fields.front().get_resolution();
        const int rx = res.first, ry = res.second, rz = int(m_fields.size());
        if (rx<=0 || ry<=0 || rz<=0) { m_statusMessage = "Invalid resolution"; return false; }

        // Height spacing derived from total height and slice count (per your spec)
        m_sliceSpacing = (rz <= 1) ? m_totalHeight : (m_totalHeight / float(rz - 1));

        Vec3 minB = Vec3(m_domain.xMin, m_domain.yMin, 0.0f);
        Vec3 maxB = Vec3(m_domain.xMax, m_domain.yMax, std::max(0.001f, m_totalHeight));

        m_volumeField = std::make_unique<ScalarField3D>(minB, maxB, rx, ry, rz);

        const size_t layerSize = size_t(rx)*size_t(ry);
        std::vector<float> vol(layerSize*size_t(rz), 0.0f);
        for (int z=0; z<rz; ++z) {
            const auto& slice = m_fields[size_t(z)].get_values();
            std::copy(slice.begin(), slice.end(), vol.begin()+layerSize*size_t(z));
        }
        m_volumeField->set_values(vol);

        auto meshData = m_volumeField->generate_mesh(m_isoLevel);
        if (!meshData || meshData->vertices.empty()) {
            m_statusMessage = "Mesh: empty result";
            m_meshGenerated=false; m_meshVisible=false; if(m_meshObject) m_meshObject->setVisible(false); return false;
        }
        if (!m_meshObject) {
            m_meshObject = std::make_shared<MeshObject>("StackIsoMesh");
            m_meshObject->setVisible(false);
            m_meshObject->setShowEdges(false);
            m_meshObject->setShowVertices(false);
            scene().addObject(m_meshObject);
        }
        m_meshObject->setMeshData(meshData);
        m_meshObject->setRenderMode(MeshRenderMode::NormalShaded);
        m_meshObject->setNormalShadingColors(Color(0.1f,0.1f,0.1f), Color(1,1,1));
        m_meshGenerated = true; m_meshVisible = true; m_meshObject->setVisible(true);
        m_statusMessage = "3D mesh generated";
        return true;
    }

    void exportAll() {
        if (!m_meshGenerated || !m_meshObject) {
            if (!buildVolumeMeshFromStack()) { m_statusMessage = "Export aborted (no mesh)"; return; }
        }

        m_meshObject->writeToObj(m_outputMeshName);
        m_statusMessage = "Exported mesh -> " + m_outputMeshName;
    }

    void toggleMeshVisibleFromUI() {
        if (m_meshObject) m_meshObject->setVisible(m_meshVisible);
        m_statusMessage = m_meshVisible ? "Mesh visible" : "Mesh hidden";
    }

    // ===== Contours management =====
    void regenerateContours() {
        clearContourObjects();
        m_contours.reserve(m_fields.size());
        for (size_t i=0;i<m_fields.size();++i) {
            GraphObject g = m_fields[i].get_contours(m_isoLevel);
            g.setShowVertices(false);
            g.setShowEdges(true);
            g.setEdgeWidth(1.0f);

            auto contour = std::make_shared<GraphObject>(std::move(g));
            contour->setName("FieldSliceContour_" + std::to_string(i));
            scene().addObject(contour);
            m_contours.emplace_back(std::move(contour));
        }
        updateContourPlacement();
    }

    void clearContourObjects() {
        for (auto& c : m_contours) if (c) scene().removeObject(c);
        m_contours.clear();
    }

    void updateContourPlacement() {
        const size_t n = m_contours.size();
        if (n==0) return;

        // spacing based on total height / number of slices (as requested)
        m_sliceSpacing = (n <= 1) ? m_totalHeight : (m_totalHeight / float(n - 1));

        for (size_t i=0;i<n;++i) {
            auto& c = m_contours[i]; if (!c) continue;
            const float hue = 360.0f * (float(i) / std::max<size_t>(1,n));
            float r,g,b; ScalarFieldUtils::get_hsv_color(hue/360.0f*2.0f - 1.0f, r,g,b);
            c->setEdgeColor(Color(r,g,b));
            c->setColor(Color(r,g,b));
            c->getTransform().setTranslation(Vec3(0,0, float(i)*m_sliceSpacing));
        }
    }

    // ===== UI sync =====
    void postUpdateUI() {
        // Momentary toggles
        if (m_btnBuildMesh) { buildVolumeMeshFromStack(); m_btnBuildMesh=false; }
        if (m_btnExport)    { exportAll();               m_btnExport=false; }
        if (m_btnSmooth)    { for(int i=0;i<5;++i) smooth(); m_btnSmooth=false; }
        if (m_btnLaplacian) { for(int i=0;i<5;++i) applyStackLaplacian(); m_btnLaplacian=false; }

        // Mesh visibility
        if (m_meshVisible != m_lastMeshVisible) { toggleMeshVisibleFromUI(); m_lastMeshVisible = m_meshVisible; }

        // Re-place contours if height changed
        static float lastTotalH = m_totalHeight;
        static float lastIso = m_isoLevel;
        static float lastGen = m_genLayersF;
        if (std::fabs(m_totalHeight - lastTotalH) > 1e-4f) { updateContourPlacement(); lastTotalH = m_totalHeight; }
        if (std::fabs(m_isoLevel - lastIso) > 1e-6f) { regenerateContours(); lastIso = m_isoLevel; }
        if (std::fabs(m_genLayersF - lastGen) > 1e-6f) { lastGen = m_genLayersF; }
    }

    // Small formatting
    static std::string f2(float v) { std::ostringstream o; o<<std::fixed<<std::setprecision(2)<<v; return o.str(); }

private:
    // ---- Latent model + panel ----
    TinyAutoDecoderCUDA m_decoder;
    FieldDomain         m_domain{};
    LatentNavigator_CUDA m_navigator;
    FieldRenderConfig   m_fieldRenderCfg{};
    GridField           m_detailField;
    std::optional<std::pair<float,float>> m_hoverUV_;
    float               m_detailFieldSize = 280.0f;
    int                 m_panelN = 10;
    int                 m_panelTileRes = 56;
    int                 m_panelGap = 4;
    std::vector<std::pair<float,float>> m_clickUVs; // user-picked latent UVs

    static constexpr float kPanelLeft  = 10.0f;
    static constexpr float kPanelTop   = 660.0f;
    static constexpr float kPanelSize  = 280.0f;

    std::string   m_snapshotPath = "latent_model.json";

    // ---- Stack data ----
    std::vector<ScalarField2D> m_fields;
    std::vector<std::shared_ptr<GraphObject>> m_contours;

    float m_isoLevel = 0.01f;
    float m_totalHeight = 200.0f;   // requested “total height”
    float m_sliceSpacing = 3.5f;    // computed from totalHeight / #slices

    Vec3 m_boundsMin = Vec3(-1, -1, 0);
    Vec3 m_boundsMax = Vec3( 1,  1, 0);

    // Volume + mesh
    std::unique_ptr<ScalarField3D> m_volumeField;
    std::shared_ptr<MeshObject>    m_meshObject;
    bool m_meshVisible = false;
    bool m_meshGenerated = false;

    // UI & actions
    std::unique_ptr<SimpleUI> m_ui;
    bool m_btnBuildMesh{false};
    bool m_btnExport{false};
    bool m_btnInitColumns{false};
    bool m_btnSmooth{false};
    bool m_btnLaplacian{false};
    bool m_lastMeshVisible{false};

    float m_genLayersF = 60.0f; // user-controlled number of generated layers

    std::string m_outputMeshName  = "outMesh.obj";
    std::string m_statusMessage = "Ready";

    // // Dummy renderer to allow panel warm build in setup (safe no-op)
    // struct DummyR : Renderer {
    //     void draw2dPoint(const Vec2&, const Color&, float) override {}
    //     void draw2dRect(const Vec2&, const Vec2&, float) override {}
    //     void drawString(const std::string&, float, float) override {}
    // } m_dummyRenderer;

    // Housekeeping
    void invalidateVolumeMesh() {
        m_volumeField.reset();
        m_meshGenerated = false;
        m_meshVisible = false;
        if (m_meshObject) m_meshObject->setVisible(false);
    }
};

ALICE2_REGISTER_SKETCH_AUTO(SDFStack_FromLatentPanel)

#endif // __MAIN__
