// #define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>
#include <zspace/io.h>
#include <zspace/toolsets.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <utils/OpenGL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace alice2;

namespace {

constexpr const char* kXianUsdaPath         = "data/vault/bestFit_xian.usda";
constexpr const char* kStriatus3DUsdaPath   = "data/vault/striatus_01.usda";
constexpr const char* kScmPodUsdaPath       = "data/vault/SCM_pod_01.usda";
constexpr const char* kDemoUsdaPath         = "data/vault_constraints_demo.usda";
constexpr const char* kExportUsdaPath       = "data/vault_output.usda";
constexpr const char* kExportObjPath        = "data/vault_output.obj";
constexpr int kPresetCount                  = 5;

std::string resolveDataPath(const std::string& relativePath)
{
    namespace fs = std::filesystem;
    if (fs::path(relativePath).is_absolute() && fs::exists(relativePath)) {
        return relativePath;
    }
    const std::vector<fs::path> prefixes = {
        fs::path("."),
        fs::path(".."),
        fs::path("../.."),
        fs::path("alice2")
    };
    for (const auto& p : prefixes) {
        fs::path candidate = p / relativePath;
        if (fs::exists(candidate)) {
            return candidate.string();
        }
    }
    return relativePath;
}

std::string trimQuotesAndWhitespace(const std::string& raw)
{
    std::string s = raw;
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n' || s.front() == '"')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n' || s.back() == '"')) {
        s.pop_back();
    }
    return s;
}

zSpace::zObjectMesh makeVaultGridMesh(int nx, int ny, float halfSpan, float domeHeight, zSpace::zIntArray& outSupports)
{
    zSpace::zPointArray pts;
    zSpace::zIntArray counts, connects;
    const int sx = nx + 1;
    const int sy = ny + 1;
    pts.reserve(size_t(sx * sy));

    for (int j = 0; j <= ny; ++j) {
        const float v = -1.0f + 2.0f * float(j) / float(ny);
        for (int i = 0; i <= nx; ++i) {
            const float u = -1.0f + 2.0f * float(i) / float(nx);
            const float x = u * halfSpan;
            const float y = v * halfSpan;
            const float r2 = u * u + v * v;
            const float z = std::max(0.0f, domeHeight * (1.0f - 0.5f * r2));
            pts.emplace_back(x, y, z);
        }
    }

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int v0 = j * sx + i;
            const int v1 = v0 + 1;
            const int v2 = v0 + sx + 1;
            const int v3 = v0 + sx;
            counts.push_back(4);
            connects.push_back(v0);
            connects.push_back(v1);
            connects.push_back(v2);
            connects.push_back(v3);
        }
    }

    outSupports = {0, nx, ny * sx, ny * sx + nx};

    zSpace::zObjectMesh mesh;
    zSpace::zFnMesh fn(mesh);
    fn.create(pts, counts, connects);
    fn.computeMeshNormals();
    return mesh;
}

zSpace::zObjectMesh makeAnnularOculusVault(float outerR, float innerR, float height, zSpace::zIntArray& outSupports)
{
    // 8-sector annular vault with unsupported central oculus opening
    const int nSeg = 8;
    const int nRings = 3; // 0 = outer, 1 = mid, 2 = oculus
    zSpace::zPointArray pts;
    zSpace::zIntArray counts, connects;
    constexpr float kPi = 3.14159265358979323846f;

    for (int r = 0; r < nRings; ++r) {
        const float t = float(r) / float(nRings - 1);
        const float rad = outerR + (innerR - outerR) * t;
        const float z = height * std::sin(t * 0.5f * kPi);
        for (int s = 0; s < nSeg; ++s) {
            const float ang = 2.0f * kPi * float(s) / float(nSeg);
            pts.emplace_back(rad * std::cos(ang), rad * std::sin(ang), z);
        }
    }

    for (int r = 0; r < nRings - 1; ++r) {
        for (int s = 0; s < nSeg; ++s) {
            const int sNext = (s + 1) % nSeg;
            const int v0 = r * nSeg + s;
            const int v1 = r * nSeg + sNext;
            const int v2 = (r + 1) * nSeg + sNext;
            const int v3 = (r + 1) * nSeg + s;
            counts.push_back(4);
            connects.push_back(v0);
            connects.push_back(v1);
            connects.push_back(v2);
            connects.push_back(v3);
        }
    }

    // 4 cardinal outer supports; inner oculus is completely unsupported (single dual vertex in TNA!)
    outSupports = {0, 2, 4, 6};

    zSpace::zObjectMesh mesh;
    zSpace::zFnMesh fn(mesh);
    fn.create(pts, counts, connects);
    fn.computeMeshNormals();
    return mesh;
}

zSpace::zObjectMesh makeMixedSaddleMesh(zSpace::zIntArray& outSupports, zSpace::zIntArray& outTensionEdges)
{
    zSpace::zPointArray pts;
    zSpace::zIntArray counts, connects;
    const int nx = 4, ny = 4, sx = nx + 1;
    const float halfSpan = 4.0f;

    for (int j = 0; j <= ny; ++j) {
        const float v = -1.0f + 2.0f * float(j) / float(ny);
        for (int i = 0; i <= nx; ++i) {
            const float u = -1.0f + 2.0f * float(i) / float(nx);
            const float z = 1.6f * (u * u - v * v); // Saddle: high along X, low along Y
            pts.emplace_back(u * halfSpan, v * halfSpan, z);
        }
    }

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const int v0 = j * sx + i;
            counts.push_back(4);
            connects.push_back(v0);
            connects.push_back(v0 + 1);
            connects.push_back(v0 + sx + 1);
            connects.push_back(v0 + sx);
        }
    }

    zSpace::zObjectMesh mesh;
    zSpace::zFnMesh fn(mesh);
    fn.create(pts, counts, connects);
    fn.computeMeshNormals();

    outSupports = {0, nx, ny * sx, ny * sx + nx};
    outTensionEdges.clear();
    zSpace::zIntArray edgeData;
    fn.getEdgeData(edgeData);
    const int ne = int(edgeData.size() / 2);
    for (int e = 0; e < ne; ++e) {
        const int u = edgeData[2 * e];
        const int v = edgeData[2 * e + 1];
        // Horizontal (X-spanning) edges in tension, vertical (Y-spanning) arches in compression
        if (std::abs(u - v) == 1) {
            outTensionEdges.push_back(e);
        }
    }
    return mesh;
}

void drawFilledRect2D(float x, float y, float w, float h, const Color& fill, const Color& border, float borderW = 2.5f)
{
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, vp[2], vp[3], 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(fill.r, fill.g, fill.b, fill.a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();

    glLineWidth(borderW);
    glColor4f(border.r, border.g, border.b, border.a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

// Crisp bold uppercase vector stroke font for high-visibility status banners
void drawBoldStrokeText2D(Renderer& renderer, const std::string& text, float x, float y, float charH, const Color& col, float strokeW = 3.0f)
{
    const float w = charH * 0.58f;
    const float adv = charH * 0.80f;
    const float hm = charH * 0.5f;

    auto seg = [&](float ox, float x0, float y0, float x1, float y1) {
        renderer.draw2dLine(Vec2{ox + x0 * w, y + y0 * charH}, Vec2{ox + x1 * w, y + y1 * charH}, col, strokeW);
    };

    float cx = x;
    for (char raw : text) {
        char c = (raw >= 'a' && raw <= 'z') ? char(raw - 'a' + 'A') : raw;
        switch (c) {
            case 'A': seg(cx,0,1,0.5f,0); seg(cx,0.5f,0,1,1); seg(cx,0.18f,0.65f,0.82f,0.65f); break;
            case 'B': seg(cx,0,0,0,1); seg(cx,0,0,0.75f,0); seg(cx,0.75f,0,0.95f,0.22f); seg(cx,0.95f,0.22f,0.75f,0.48f); seg(cx,0,0.48f,0.78f,0.48f); seg(cx,0.78f,0.48f,1,0.72f); seg(cx,1,0.72f,0.78f,1); seg(cx,0,1,0.78f,1); break;
            case 'C': seg(cx,1,0.12f,0.75f,0); seg(cx,0.75f,0,0.2f,0); seg(cx,0.2f,0,0,0.22f); seg(cx,0,0.22f,0,0.78f); seg(cx,0,0.78f,0.2f,1); seg(cx,0.2f,1,0.75f,1); seg(cx,0.75f,1,1,0.88f); break;
            case 'D': seg(cx,0,0,0,1); seg(cx,0,0,0.68f,0); seg(cx,0.68f,0,1,0.25f); seg(cx,1,0.25f,1,0.75f); seg(cx,1,0.75f,0.68f,1); seg(cx,0,1,0.68f,1); break;
            case 'E': seg(cx,0,0,0,1); seg(cx,0,0,1,0); seg(cx,0,0.5f,0.82f,0.5f); seg(cx,0,1,1,1); break;
            case 'F': seg(cx,0,0,0,1); seg(cx,0,0,1,0); seg(cx,0,0.5f,0.82f,0.5f); break;
            case 'G': seg(cx,1,0.15f,0.75f,0); seg(cx,0.75f,0,0.2f,0); seg(cx,0.2f,0,0,0.22f); seg(cx,0,0.22f,0,0.78f); seg(cx,0,0.78f,0.2f,1); seg(cx,0.2f,1,0.85f,1); seg(cx,0.85f,1,1,0.8f); seg(cx,1,0.8f,1,0.52f); seg(cx,0.52f,0.52f,1,0.52f); break;
            case 'H': seg(cx,0,0,0,1); seg(cx,1,0,1,1); seg(cx,0,0.5f,1,0.5f); break;
            case 'I': seg(cx,0.15f,0,0.85f,0); seg(cx,0.5f,0,0.5f,1); seg(cx,0.15f,1,0.85f,1); break;
            case 'L': seg(cx,0,0,0,1); seg(cx,0,1,1,1); break;
            case 'M': seg(cx,0,1,0,0); seg(cx,0,0,0.5f,0.58f); seg(cx,0.5f,0.58f,1,0); seg(cx,1,0,1,1); break;
            case 'N': seg(cx,0,1,0,0); seg(cx,0,0,1,1); seg(cx,1,1,1,0); break;
            case 'O': seg(cx,0.2f,0,0.8f,0); seg(cx,0.8f,0,1,0.22f); seg(cx,1,0.22f,1,0.78f); seg(cx,1,0.78f,0.8f,1); seg(cx,0.8f,1,0.2f,1); seg(cx,0.2f,1,0,0.78f); seg(cx,0,0.78f,0,0.22f); seg(cx,0,0.22f,0.2f,0); break;
            case 'P': seg(cx,0,0,0,1); seg(cx,0,0,0.75f,0); seg(cx,0.75f,0,1,0.2f); seg(cx,1,0.2f,1,0.36f); seg(cx,1,0.36f,0.75f,0.54f); seg(cx,0,0.54f,0.75f,0.54f); break;
            case 'Q': seg(cx,0.2f,0,0.8f,0); seg(cx,0.8f,0,1,0.22f); seg(cx,1,0.22f,1,0.78f); seg(cx,1,0.78f,0.8f,1); seg(cx,0.8f,1,0.2f,1); seg(cx,0.2f,1,0,0.78f); seg(cx,0,0.78f,0,0.22f); seg(cx,0,0.22f,0.2f,0); seg(cx,0.58f,0.7f,1.05f,1.08f); break;
            case 'R': seg(cx,0,0,0,1); seg(cx,0,0,0.75f,0); seg(cx,0.75f,0,1,0.2f); seg(cx,1,0.2f,1,0.34f); seg(cx,1,0.34f,0.75f,0.52f); seg(cx,0,0.52f,0.75f,0.52f); seg(cx,0.55f,0.52f,1,1); break;
            case 'S': seg(cx,1,0.12f,0.78f,0); seg(cx,0.78f,0,0.2f,0); seg(cx,0.2f,0,0,0.18f); seg(cx,0,0.18f,0,0.34f); seg(cx,0,0.34f,0.2f,0.48f); seg(cx,0.2f,0.48f,0.8f,0.52f); seg(cx,0.8f,0.52f,1,0.68f); seg(cx,1,0.68f,1,0.84f); seg(cx,1,0.84f,0.78f,1); seg(cx,0.78f,1,0.18f,1); seg(cx,0.18f,1,0,0.88f); break;
            case 'T': seg(cx,0,0,1,0); seg(cx,0.5f,0,0.5f,1); break;
            case 'U': seg(cx,0,0,0,0.78f); seg(cx,0,0.78f,0.22f,1); seg(cx,0.22f,1,0.78f,1); seg(cx,0.78f,1,1,0.78f); seg(cx,1,0.78f,1,0); break;
            case 'V': seg(cx,0,0,0.5f,1); seg(cx,0.5f,1,1,0); break;
            case 'Y': seg(cx,0,0,0.5f,0.5f); seg(cx,1,0,0.5f,0.5f); seg(cx,0.5f,0.5f,0.5f,1); break;
            case '-': seg(cx,0.15f,0.5f,0.85f,0.5f); break;
            case '.': seg(cx,0.45f,0.92f,0.55f,1.0f); break;
            case ' ': break;
            default: (void)hm; break;
        }
        cx += (c == ' ') ? (adv * 0.55f) : adv;
    }
}

} // namespace

class zSpaceVaultSketch : public ISketch {
public:
    std::string getName() const override { return "zSpace Vault — FDM / Best-Fit / TNA"; }
    std::string getDescription() const override { return "Unified Regular FDM, Constrained Best-Fit FDM, and Stage 1+2 TNA Form/Force Diagrams with USD Color Constraints"; }
    std::string getAuthor() const override { return "zSpace"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(0.96f, 0.97f, 0.98f, 1.0f));
        scene().setShowGrid(false);
        scene().setShowAxes(true);
        scene().setAxesLength(3.0f);

        buildUI();

        if (const char* envMesh = std::getenv("VAULT_MESH")) {
            if (envMesh[0] != '\0' && loadExternalMeshFile(envMesh, "CLI / Env")) {
                return;
            }
        }
        loadPreset(0);
    }

    void update(float /*deltaTime*/) override
    {
        if (!m_vault.hasMesh()) return;

        // Detect clicks in either of the two mode button groups (Standard: FDM/TNA vs Best Fit: BF FDM/BF TNA)
        if (m_stdModeIdx != m_lastStdModeIdx && m_stdModeIdx >= 0) {
            setActiveMode(m_stdModeIdx); // 0 = FDM, 1 = TNA
            if (m_activeMode == 1) runTransitToTNA();
        } else if (m_bfModeIdx != m_lastBfModeIdx && m_bfModeIdx >= 0) {
            setActiveMode(2 + m_bfModeIdx); // 2 = BF FDM, 3 = BF TNA
            if (m_activeMode == 3) ensureLiveBfTnaDiagrams();
        }

        // Detect constraint/parameter changes from UI toggles & sliders
        bool liveSettingsChanged = false;
        bool massSettingsChanged = false;
        if (m_planConstraintIdx != m_lastPlanConstraintIdx) {
            m_lastPlanConstraintIdx = m_planConstraintIdx;
            liveSettingsChanged = true;
        }
        if (m_compressionOnly != m_lastCompressionOnly) {
            m_lastCompressionOnly = m_compressionOnly;
            liveSettingsChanged = true;
        }
        if (m_zeroWeight != m_lastZeroWeight) {
            m_lastZeroWeight = m_zeroWeight;
            applyMassMode();
            massSettingsChanged = true;
        }
        if (m_useFastDR != m_lastUseFastDR) {
            m_lastUseFastDR = m_useFastDR;
            liveSettingsChanged = true;
        }
        if (std::abs(m_qMinUI - m_lastQMinUI) > 1e-5f ||
            std::abs(m_qMaxUI - m_lastQMaxUI) > 1e-4f ||
            std::abs(m_geomWeightUI - m_lastGeomWeightUI) > 1e-4f ||
            std::abs(m_zPenaltyUI - m_lastZPenaltyUI) > 1e-4f ||
            std::abs(m_edgeRegUI - m_lastEdgeRegUI) > 1e-4f) {
            m_lastQMinUI = m_qMinUI;
            m_lastQMaxUI = m_qMaxUI;
            m_lastGeomWeightUI = m_geomWeightUI;
            m_lastZPenaltyUI = m_zPenaltyUI;
            m_lastEdgeRegUI = m_edgeRegUI;
            liveSettingsChanged = true;
        }
        // Non-invalidating tolerance & max-iteration updates (apply live without resetting current iteration!)
        if (std::abs(m_resTolUI - m_lastResTolUI) > 1e-5f ||
            std::abs(m_angleTolUI - m_lastAngleTolUI) > 1e-4f ||
            std::abs(m_maxItersUI - m_lastMaxItersUI) > 0.5f) {
            const bool maxItersIncreased = (m_maxItersUI > m_lastMaxItersUI + 0.5f);
            m_lastResTolUI = m_resTolUI;
            m_lastAngleTolUI = m_angleTolUI;
            m_lastMaxItersUI = m_maxItersUI;
            const int newMaxIters = std::max(10, static_cast<int>(std::lround(m_maxItersUI)));
            m_vault.setTolerances(effectiveInternalResTol(), 0.0, newMaxIters);
            if (maxItersIncreased && m_pausedAtIterLimit && m_hasSolved && m_vault.report().iteration < newMaxIters && !isEquilibriumReached()) {
                m_liveRunning = true;
                m_pausedAtIterLimit = false;
            }
            if (m_vault.hasDiagrams()) {
                rebuildDiagramBuffers();
            }
            if (m_hasSolved) {
                updateMetricsLine();
            }
        }
        if (m_useAreaMass != m_lastUseAreaMass) {
            m_lastUseAreaMass = m_useAreaMass;
            applyMassMode();
            massSettingsChanged = true;
        }
        if (m_rotate90Dual != m_lastRotate90Dual) {
            m_lastRotate90Dual = m_rotate90Dual;
            if (m_vault.hasDiagrams()) {
                if (m_activeMode == 3) ensureLiveBfTnaDiagrams();
                else runTransitToTNA();
            }
        }
        if (massSettingsChanged) {
            syncBestFitSettings(/*liveUpdate=*/false);
            if (m_activeMode == 0 && m_hasSolved) {
                runSolveAction();
            }
        } else if (liveSettingsChanged) {
            syncBestFitSettings(/*liveUpdate=*/true);
            if (m_hasSolved) {
                if (m_activeMode == 0) {
                    runSolveAction();
                } else {
                    if (m_activeMode == 3 && m_vault.hasDiagrams()) {
                        m_vault.updateLiveDiagrams(diagramSettings());
                    }
                    rebuildAllRenderBuffers();
                    updateMetricsLine();
                }
            }
        }

        // Live TNA force-diagram scale update (interactive rise/thrust control!)
        if (std::abs(m_tnaForceScaleUI - m_lastTnaForceScaleUI) > 1e-4f) {
            m_lastTnaForceScaleUI = m_tnaForceScaleUI;
            if (m_activeMode == 1 && m_vault.hasDiagrams()) {
                try {
                    auto vRep = m_vault.solveVerticalEquilibrium(static_cast<double>(m_tnaForceScaleUI), false);
                    m_hasSolved = true;
                    rebuildAllRenderBuffers();
                    updateMetricsLine();
                    char buf[220];
                    std::snprintf(buf, sizeof(buf),
                        "TNA Vertical Eq (scale=%.2f) | vRes=%.2e | hRes=%.2e | maxAngle=%.2f deg",
                        m_tnaForceScaleUI, vRep.verticalResidual, vRep.horizontalResidual, vRep.maxAngleDeg);
                    m_statusLine = buf;
                } catch (const std::exception& ex) {
                    m_statusLine = std::string("TNA Vertical error: ") + ex.what();
                }
            } else if (m_vault.hasDiagrams()) {
                rebuildDiagramBuffers();
            }
        }

        // Interactive live stepping (8 FastDR steps/frame for smooth readable animation, or 4 exact RK2 steps/frame)
        if (m_liveRunning) {
            m_pausedAtIterLimit = false;
            if (m_activeMode == 2 || m_activeMode == 3) {
                // Keep internal solver tolerance tight enough in BF TNA while angle is still above Ang Tol
                m_vault.setTolerances(
                    effectiveInternalResTol(),
                    0.0,
                    std::max(10, static_cast<int>(std::lround(m_maxItersUI))));
                runStepBatch(m_useFastDR ? 8 : 4);
                if (isEquilibriumReached() || m_vault.report().status == "stalled") {
                    m_liveRunning = false;
                    m_pausedAtIterLimit = false;
                } else if (m_vault.report().status == "iteration-limit") {
                    m_liveRunning = false;
                    m_pausedAtIterLimit = true;
                }
            } else if (m_activeMode == 1) {
                runTnaHorizontalStep(8);
                if (m_vault.tnaReport().maxAngleDeg <= static_cast<double>(m_angleTolUI)) {
                    m_liveRunning = false;
                    runTnaVertical();
                }
            } else {
                m_liveRunning = false;
            }
        }
    }

    void draw(Renderer& renderer, Camera& /*cam*/) override
    {
        if (m_vault.hasMesh()) {
            drawSceneGeometry(renderer);
        }

        const char* modeName =
            (m_activeMode == 0) ? "FDM" :
            (m_activeMode == 1) ? "TNA" :
            (m_activeMode == 2) ? "BF FDM" : "BF TNA";
        const char* planName = (m_planConstraintIdx == 0) ? "Free" : (m_planConstraintIdx == 1 ? "Penalize XY" : "Fix XY");

        renderer.setColor(Color(0.08f, 0.10f, 0.15f, 1.0f));
        renderer.drawString(
            std::string("zSpace Vault [") + modeName + "]  |  " + m_datasetLabel +
            "  (" + std::to_string(m_vault.numVertices()) + " verts, " +
            std::to_string(m_vault.numFaces()) + " faces, " +
            std::to_string(m_vault.supports().size()) + " supports, Plan: " + planName + ")",
            14.0f, 24.0f);

        renderer.setColor(Color(0.08f, 0.36f, 0.68f, 1.0f));
        renderer.drawString(m_metricsLine, 14.0f, 44.0f);

        renderer.setColor(Color(0.16f, 0.22f, 0.30f, 1.0f));
        renderer.drawString(m_statusLine, 14.0f, 64.0f);

        renderer.setColor(Color(0.38f, 0.42f, 0.48f, 1.0f));
        renderer.drawString(
            "Keys: [1] FDM  [2] TNA  [3] BF FDM  [4] BF TNA  [P] Fit/Solve  [S] Step  [L] Live  [H] Horiz  [V] Vert  [X] Plan  [Z] Undo  [R] Reset  [M] Preset",
            14.0f, 84.0f);

        renderer.setColor(Color(0.24f, 0.30f, 0.38f, 1.0f));
        renderer.drawString("Standard / Best Fit", 14.0f, 106.0f);
        renderer.drawString("Constraints & View", 208.0f, 106.0f);

        if (m_ui) {
            m_ui->draw(renderer);
        }

        drawEquilibriumBanner(renderer);
    }

    bool onKeyPress(unsigned char key, int /*x*/, int /*y*/) override
    {
        if (input().isCtrlPressed()) {
            if (key == 'c' || key == 'C') { copyVaultUsd(); return true; }
            if (key == 'v' || key == 'V') { pasteVaultUsd(); return true; }
        }

        switch (key) {
            case '1': setActiveMode(0); m_statusLine = "Mode: FDM (Schek 1974)."; return true;
            case '2': setActiveMode(1); runTransitToTNA(); return true;
            case '3': setActiveMode(2); m_statusLine = "Mode: BF FDM (Best-Fit Force Density)."; return true;
            case '4': setActiveMode(3); ensureLiveBfTnaDiagrams(); return true;
            case 'p': case 'P': runSolveAction(); return true;
            case 's': case 'S':
                if (m_activeMode == 1) runTnaHorizontalStep(10);
                else runStepBatch(10);
                return true;
            case 'k': case 'K': runSnapFDM(); return true;
            case 'l': case 'L': m_liveRunning = !m_liveRunning; return true;
            case 't': case 'T': setActiveMode(1); runTransitToTNA(); return true;
            case 'h': case 'H': runTnaHorizontalStep(50); return true;
            case 'v': case 'V': runTnaVertical(); return true;
            case 'x': case 'X': m_planConstraintIdx = (m_planConstraintIdx + 1) % 3; return true;
            case 'z': case 'Z': runUndoStep(); return true;
            case 'r': case 'R': resetActiveSolver(); return true;
            case 'u': case 'U': exportVaultUsd(); return true;
            case 'e': case 'E': exportVaultObj(); return true;
            case 'm': case 'M': loadPreset((m_presetIdx + 1) % kPresetCount); return true;
            case 'f': case 'F': frameCameraOnScene(); return true;
            default: return false;
        }
    }

    bool onFileDrop(const std::vector<std::string>& paths) override
    {
        if (paths.empty()) return false;
        return loadExternalMeshFile(paths.front(), "Dropped file");
    }

    bool onMousePress(int button, int state, int x, int y) override
    {
        if (m_ui && m_ui->onMousePress(button, state, x, y)) return true;
        return false;
    }

    bool onMouseMove(int x, int y) override
    {
        if (m_ui && m_ui->onMouseMove(x, y)) return true;
        return false;
    }

private:
    void setActiveMode(int mode)
    {
        m_activeMode = mode;
        m_liveRunning = false;
        if (mode == 0)      { m_stdModeIdx = 0;  m_bfModeIdx = -1; }
        else if (mode == 1) { m_stdModeIdx = 1;  m_bfModeIdx = -1; }
        else if (mode == 2) { m_stdModeIdx = -1; m_bfModeIdx = 0;  }
        else                { m_stdModeIdx = -1; m_bfModeIdx = 1;  }
        m_lastStdModeIdx = m_stdModeIdx;
        m_lastBfModeIdx = m_bfModeIdx;
    }

    void ensureLiveBfTnaDiagrams()
    {
        if (!m_vault.hasMesh()) return;
        try {
            if (!m_hasSolved) {
                syncBestFitSettings();
                m_vault.computeBestFitForceDensities();
                m_hasSolved = true;
                m_showDensities = true;
                m_showResiduals = true;
                m_showThrust = true;
            }
            const auto ds = diagramSettings();
            auto dRep = m_vault.updateLiveDiagrams(ds);
            m_showFormDiagram = true;
            m_showForceDiagram = true;
            rebuildAllRenderBuffers();
            updateMetricsLine();
            char buf[260];
            std::snprintf(buf, sizeof(buf),
                "BF TNA live diagrams ready (%s) | closure=%.4f | maxAngle=%.2f deg — press [L] for Live Run.",
                dRep.status.c_str(), dRep.closureError, m_vault.tnaReport().maxAngleDeg);
            m_statusLine = buf;
        } catch (const std::exception& ex) {
            m_statusLine = std::string("BF TNA diagram error: ") + ex.what();
        }
    }

    void buildUI()
    {
        m_ui = std::make_unique<SimpleUI>(input());
        m_ui->setTheme(SimpleUI::UITheme::Dark);

        const float btnW = 180.0f;
        const float btnH = 24.0f;
        const float rowStep = 28.0f;
        const float colGap = 14.0f;
        const float xCol1 = 14.0f;
        const float xCol2 = xCol1 + btnW + colGap; // 208.0f
        const float yStart = 114.0f;

        // Column 1: Two separate mode groups (Standard: FDM | TNA, Best Fit: BF FDM | BF TNA) + Actions + Sliders
        float y1 = yStart;
        m_ui->addButtonGroup({"FDM", "TNA"},       Vec2{xCol1, y1}, 88.0f, btnH, 4.0f, m_stdModeIdx);
        y1 += rowStep;
        m_ui->addButtonGroup({"BF FDM", "BF TNA"}, Vec2{xCol1, y1}, 88.0f, btnH, 4.0f, m_bfModeIdx);
        y1 += rowStep;

        m_ui->addButton("Fit / Solve [P]", UIRect{xCol1, y1, btnW, btnH}, [this]() { runSolveAction(); });
        y1 += rowStep;

        m_ui->addButton("Step +10 [S]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            if (m_activeMode == 1) runTnaHorizontalStep(10);
            else runStepBatch(10);
        });
        y1 += rowStep;

        m_ui->addButton("Snap FDM [K]", UIRect{xCol1, y1, btnW, btnH}, [this]() { runSnapFDM(); });
        y1 += rowStep;

        m_ui->addButton("Transit TNA [T]", UIRect{xCol1, y1, btnW, btnH}, [this]() {
            setActiveMode(1);
            runTransitToTNA();
        });
        y1 += rowStep;

        m_ui->addButton("TNA Horiz Eq [H]", UIRect{xCol1, y1, btnW, btnH}, [this]() { runTnaHorizontalStep(50); });
        y1 += rowStep;

        m_ui->addButton("TNA Vert Eq [V]", UIRect{xCol1, y1, btnW, btnH}, [this]() { runTnaVertical(); });
        y1 += rowStep;

        m_ui->addButton("Undo Step [Z]", UIRect{xCol1, y1, btnW, btnH}, [this]() { runUndoStep(); });
        y1 += rowStep;

        m_ui->addButton("Reset [R]", UIRect{xCol1, y1, btnW, btnH}, [this]() { resetActiveSolver(); });
        y1 += rowStep;

        m_ui->addButton("Copy USD [Ctrl+C]", UIRect{xCol1, y1, btnW, btnH}, [this]() { copyVaultUsd(); });
        y1 += rowStep;

        m_ui->addButton("Paste USD [Ctrl+V]", UIRect{xCol1, y1, btnW, btnH}, [this]() { pasteVaultUsd(); });
        y1 += rowStep;

        m_ui->addButton("Export USD [U]", UIRect{xCol1, y1, btnW, btnH}, [this]() { exportVaultUsd(); });
        y1 += rowStep;

        m_ui->addButton("Switch Preset [M]", UIRect{xCol1, y1, btnW, btnH}, [this]() { loadPreset((m_presetIdx + 1) % kPresetCount); });

        // Sliders below Column 1
        const float sliderW = 114.0f;
        float yS = y1 + btnH + 22.0f;
        m_ui->addSlider("qMin",      Vec2{xCol1, yS}, sliderW, 0.01f,  5.0f,     m_qMinUI);          yS += 30.0f;
        m_ui->addSlider("qMax",      Vec2{xCol1, yS}, sliderW, 1.0f,   1000.0f,  m_qMaxUI);          yS += 30.0f;
        m_ui->addSlider("Geom Wt",   Vec2{xCol1, yS}, sliderW, 0.0f,   1.0f,     m_geomWeightUI);    yS += 30.0f;
        m_ui->addSlider("Z Penalty", Vec2{xCol1, yS}, sliderW, 0.0f,   20.0f,    m_zPenaltyUI);      yS += 30.0f;
        m_ui->addSlider("Max Iters", Vec2{xCol1, yS}, sliderW, 100.0f, 10000.0f, m_maxItersUI);      yS += 30.0f;
        m_ui->addSlider("Edge Reg",  Vec2{xCol1, yS}, sliderW, 0.0f,   2.0f,     m_edgeRegUI);       yS += 30.0f;
        m_ui->addSlider("TNA Scale", Vec2{xCol1, yS}, sliderW, 0.2f,   4.0f,     m_tnaForceScaleUI); yS += 30.0f;
        m_ui->addSlider("Form Wt",   Vec2{xCol1, yS}, sliderW, 0.0f,   1.0f,     m_tnaFormWeightUI); yS += 30.0f;
        m_ui->addSlider("Vec Scale", Vec2{xCol1, yS}, sliderW, 0.05f,  3.0f,     m_vectorScaleUI);

        // Column 2: Constraint & Display Toggles
        float y2 = yStart;
        m_ui->addButtonGroup({"Free", "PenXY", "FixXY"}, Vec2{xCol2, y2}, 58.0f, btnH, 3.0f, m_planConstraintIdx);
        y2 += rowStep;

        m_ui->addToggle("Live Run [L]",     UIRect{xCol2, y2, btnW, btnH}, m_liveRunning);      y2 += rowStep;
        m_ui->addToggle("Fast DR Solver",   UIRect{xCol2, y2, btnW, btnH}, m_useFastDR);        y2 += rowStep;
        m_ui->addToggle("Compression Only", UIRect{xCol2, y2, btnW, btnH}, m_compressionOnly);  y2 += rowStep;
        m_ui->addToggle("Zero Weight (P=0)",UIRect{xCol2, y2, btnW, btnH}, m_zeroWeight);       y2 += rowStep;
        m_ui->addToggle("Area Mass",        UIRect{xCol2, y2, btnW, btnH}, m_useAreaMass);      y2 += rowStep;
        m_ui->addToggle("Rotate 90 Dual",   UIRect{xCol2, y2, btnW, btnH}, m_rotate90Dual);     y2 += rowStep;
        m_ui->addToggle("Shaded Mesh",      UIRect{xCol2, y2, btnW, btnH}, m_showShadedMesh);   y2 += rowStep;
        m_ui->addToggle("Force Densities",  UIRect{xCol2, y2, btnW, btnH}, m_showDensities);    y2 += rowStep;
        m_ui->addToggle("Residual Forces",  UIRect{xCol2, y2, btnW, btnH}, m_showResiduals);    y2 += rowStep;
        m_ui->addToggle("Support Thrust",   UIRect{xCol2, y2, btnW, btnH}, m_showThrust);       y2 += rowStep;
        m_ui->addToggle("Form Diagram",     UIRect{xCol2, y2, btnW, btnH}, m_showFormDiagram);  y2 += rowStep;
        m_ui->addToggle("Force Diagram",    UIRect{xCol2, y2, btnW, btnH}, m_showForceDiagram); y2 += rowStep;
        m_ui->addToggle("TNA Angle Check",  UIRect{xCol2, y2, btnW, btnH}, m_showTnaAngleCheck);y2 += rowStep;
        m_ui->addToggle("Orig Wireframe",   UIRect{xCol2, y2, btnW, btnH}, m_showOrigWire);     y2 += rowStep;
        m_ui->addToggle("USD Color View",   UIRect{xCol2, y2, btnW, btnH}, m_showUsdConstraintColors); y2 += rowStep;
        m_ui->addToggle("Solver Color",     UIRect{xCol2, y2, btnW, btnH}, m_showSolverAngleGradient);

        // Exposed equilibrium tolerances below Column 2
        float yS2 = y2 + btnH + 22.0f;
        m_ui->addSlider("Res Tol", Vec2{xCol2, yS2}, sliderW, 0.001f, 1.0f,  m_resTolUI);   yS2 += 30.0f;
        m_ui->addSlider("Ang Tol", Vec2{xCol2, yS2}, sliderW, 0.1f,   10.0f, m_angleTolUI);
    }

    zSpace::zVaultDiagramSettings diagramSettings() const
    {
        zSpace::zVaultDiagramSettings ds;
        ds.tolerance = 1e-5;
        ds.angleToleranceDeg = static_cast<double>(m_angleTolUI);
        ds.rotate90 = m_rotate90Dual;
        return ds;
    }

    zSpace::zVaultColorConstraintSettings colorConstraintSettings() const
    {
        zSpace::zVaultColorConstraintSettings cs;
        cs.qMin = static_cast<double>(std::min(m_qMinUI, m_qMaxUI));
        cs.qMax = static_cast<double>(std::max(m_qMinUI + 0.01f, m_qMaxUI));
        cs.defaultCompressionQ = -1.0;
        cs.clearExisting = true;
        cs.applyForceDensities = true;
        cs.applyBoundsFromColors = false;
        return cs;
    }

    double effectiveInternalResTol() const
    {
        const double uiTol = static_cast<double>(std::max(1e-5f, m_resTolUI));
        // In BF TNA, keep the internal residual threshold tight while TNA parallelism
        // angle is still above Ang Tol so stepping does not lock out before angle converges.
        if (m_activeMode == 3 && m_vault.hasDiagrams() &&
            m_vault.tnaReport().maxAngleDeg > static_cast<double>(m_angleTolUI)) {
            return std::min(uiTol, 1e-6);
        }
        return uiTol;
    }

    void syncBestFitSettings(bool liveUpdate = false)
    {
        if (!m_vault.hasMesh()) return;
        zSpace::zVaultBestFitSettings s;
        s.qMin = static_cast<double>(std::max(0.001f, std::min(m_qMinUI, m_qMaxUI)));
        s.qMax = static_cast<double>(std::max(m_qMinUI + 0.01f, m_qMaxUI));
        s.compressionOnly = m_compressionOnly;
        s.allowZeroLoad = m_zeroWeight;
        s.useFastDR = m_useFastDR;
        s.planConstraint = static_cast<zSpace::zVaultPlanConstraint>(m_planConstraintIdx);
        s.fixXY = (m_planConstraintIdx == 2);
        s.xyPenaltyWeight = 2.0;
        s.zPenaltyWeight = static_cast<double>(m_zPenaltyUI);
        s.geometryWeight = static_cast<double>(m_geomWeightUI);
        s.minMaxEdgeRatio = 0.35;
        s.edgeRegularizationStrength = static_cast<double>(m_edgeRegUI);
        s.absoluteTolerance = effectiveInternalResTol();
        s.relativeTolerance = 0.0;
        s.initialStep = 0.1;
        s.integrationTolerance = 1e-4;
        s.maxIterations = std::max(10, static_cast<int>(std::lround(m_maxItersUI)));
        if (liveUpdate && m_hasSolved) {
            m_vault.updateBestFitSettingsLive(s);
        } else {
            m_vault.setBestFitSettings(s);
        }
    }

    void applyMassMode()
    {
        if (!m_vault.hasMesh()) return;
        if (m_zeroWeight) {
            m_vault.setVertexMass(0.0);
        } else if (m_useAreaMass) {
            m_vault.setVertexMassFromArea(0.25, 1.0);
        } else {
            m_vault.setVertexMass(1.0);
        }
    }

    void loadPreset(int idx)
    {
        m_presetIdx = idx;
        m_liveRunning = false;
        m_hasSolved = false;
        m_showDensities = false;
        m_showResiduals = false;
        m_showThrust = false;

        try {
            if (idx == 0 && std::filesystem::exists(resolveDataPath(kXianUsdaPath))) {
                setActiveMode(3); // BF TNA (shows live Form below + Force beside!)
                m_planConstraintIdx = 2; // Fix XY
                m_zeroWeight = true;
                m_useAreaMass = false;
                m_compressionOnly = false;
                m_qMinUI = 0.1f; m_qMaxUI = 1000.0f;
                m_geomWeightUI = 0.25f; m_zPenaltyUI = 0.0f;
                auto crep = m_vault.loadInputFromFile(resolveDataPath(kXianUsdaPath), colorConstraintSettings(), true);
                m_datasetLabel = "Xi'an Tensile Net (USD: " + std::to_string(crep.supportCount) + " outer sups, Fix XY)";
            } else if (idx == 1 && std::filesystem::exists(resolveDataPath(kStriatus3DUsdaPath))) {
                setActiveMode(3); // BF TNA on 3D target shape
                m_planConstraintIdx = 2; // Fix XY
                m_zeroWeight = false;
                m_useAreaMass = true;
                m_compressionOnly = true;
                m_qMinUI = 0.01f; m_qMaxUI = 1000.0f;
                m_geomWeightUI = 0.05f; m_zPenaltyUI = 2.0f;
                auto crep = m_vault.loadInputFromFile(resolveDataPath(kStriatus3DUsdaPath), colorConstraintSettings(), true);
                m_datasetLabel = "Striatus 3D (USD: " + std::to_string(crep.supportCount) + " footing sups, Fix XY Comp)";
            } else if (idx == 2 && std::filesystem::exists(resolveDataPath(kScmPodUsdaPath))) {
                setActiveMode(3); // BF TNA minimal surface
                m_planConstraintIdx = 0; // Free 3D minimal surface
                m_zeroWeight = true;
                m_useAreaMass = false;
                m_compressionOnly = false;
                m_qMinUI = 0.1f; m_qMaxUI = 1000.0f;
                m_geomWeightUI = 0.35f; m_zPenaltyUI = 0.0f;
                auto crep = m_vault.loadInputFromFile(resolveDataPath(kScmPodUsdaPath), colorConstraintSettings(), true);
                m_datasetLabel = "SCM POD Minimal Surface (USD: " + std::to_string(crep.supportCount) + " boundary sups, P=0)";
            } else if (idx == 3) {
                zSpace::zIntArray sups;
                auto mesh = makeVaultGridMesh(4, 4, 4.0f, 2.4f, sups);
                m_vault.setInputMesh(mesh);
                m_vault.setSupports(sups);
                m_vault.setForceDensity(-1.0);
                setActiveMode(1); // TNA showcase preset
                m_planConstraintIdx = 0;
                m_zeroWeight = false;
                m_useAreaMass = false;
                m_compressionOnly = true;
                m_qMinUI = 0.05f; m_qMaxUI = 1000.0f;
                m_geomWeightUI = 0.5f; m_zPenaltyUI = 0.0f;
                m_datasetLabel = "4x4 Masonry Vault Grid (FDM / TNA / BF)";
            } else if (idx == 4) {
                zSpace::zIntArray sups;
                auto mesh = makeAnnularOculusVault(5.0f, 1.8f, 2.4f, sups);
                m_vault.setInputMesh(mesh);
                m_vault.setSupports(sups);
                m_vault.setForceDensity(-1.0);
                setActiveMode(1); // TNA
                m_planConstraintIdx = 0;
                m_zeroWeight = false;
                m_useAreaMass = false;
                m_compressionOnly = true;
                m_qMinUI = 0.05f; m_qMaxUI = 1000.0f;
                m_geomWeightUI = 0.5f; m_zPenaltyUI = 0.0f;
                m_datasetLabel = "Annular Vault with Oculus (TNA Hole Dual)";
            } else {
                zSpace::zIntArray sups;
                auto mesh = makeVaultGridMesh(4, 4, 4.0f, 2.4f, sups);
                m_vault.setInputMesh(mesh);
                m_vault.setSupports(sups);
                m_vault.setForceDensity(-1.0);
                setActiveMode(2); // BF FDM
                m_planConstraintIdx = 0;
                m_zeroWeight = false;
                m_useAreaMass = false;
                m_compressionOnly = true;
                m_qMinUI = 0.05f; m_qMaxUI = 1000.0f;
                m_geomWeightUI = 0.5f; m_zPenaltyUI = 0.0f;
                m_datasetLabel = "4x4 Masonry Vault Grid";
            }

            m_lastPlanConstraintIdx = m_planConstraintIdx;
            m_lastCompressionOnly = m_compressionOnly;
            m_lastZeroWeight = m_zeroWeight;
            m_lastUseAreaMass = m_useAreaMass;
            m_lastQMinUI = m_qMinUI;
            m_lastQMaxUI = m_qMaxUI;
            m_lastGeomWeightUI = m_geomWeightUI;
            m_lastZPenaltyUI = m_zPenaltyUI;
            m_vault.setGravity(zSpace::zVector(0.0f, 0.0f, -1.0f));
            applyMassMode();
            syncBestFitSettings();
            computeDiagramOffsets();
            rebuildAllRenderBuffers();
            frameCameraOnScene();

            m_metricsLine = "Neutral clay mesh loaded — click [Fit / Solve] (P), [Snap FDM] (K), or [Transit TNA] (T).";
            m_statusLine = "Loaded " + m_datasetLabel + ".";
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Preset load error: ") + ex.what();
            std::cerr << "[Vault] " << m_statusLine << std::endl;
        }
    }

    bool loadExternalMeshFile(const std::string& filePath, const char* sourceLabel)
    {
        m_liveRunning = false;
        m_hasSolved = false;
        m_showDensities = false;
        try {
            auto crep = m_vault.loadInputFromFile(filePath, colorConstraintSettings(), true);
            // If loaded mesh had no blue support colors, default to lowest-Z / corner vertices
            if (m_vault.supports().empty()) {
                assignCornerSupportsFallback();
            }
            m_vault.setGravity(zSpace::zVector(0.0f, 0.0f, -1.0f));
            applyMassMode();
            syncBestFitSettings();
            computeDiagramOffsets();
            rebuildAllRenderBuffers();
            frameCameraOnScene();

            m_datasetLabel = std::filesystem::path(filePath).filename().string();
            m_metricsLine = "Loaded neutral mesh — USD constraints: " +
                std::to_string(crep.supportCount) + " blue supports, " +
                std::to_string(crep.compressionEdgeCount) + " red/black comp, " +
                std::to_string(crep.tensionEdgeCount) + " green tens.";
            m_statusLine = std::string(sourceLabel) + ": " + m_datasetLabel + ". Press [P] to Fit/Solve or [T] for TNA.";
            std::cout << "[Vault] " << m_statusLine << std::endl;
            return true;
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Load error: ") + ex.what();
            std::cerr << "[Vault] " << m_statusLine << std::endl;
            return false;
        }
    }

    void assignCornerSupportsFallback()
    {
        const auto& pts = m_vault.originalPositions();
        if (pts.empty()) return;
        float minZ = pts[0].z;
        for (const auto& p : pts) minZ = std::min(minZ, p.z);
        zSpace::zIntArray sups;
        for (int i = 0; i < int(pts.size()); ++i) {
            if (pts[i].z <= minZ + 1e-3f) sups.push_back(i);
        }
        if (sups.empty()) sups.push_back(0);
        if (int(sups.size()) >= int(pts.size())) {
            sups.resize(std::max<size_t>(1, pts.size() / 4));
        }
        m_vault.setSupports(sups);
    }

    void runSolveAction()
    {
        if (!m_vault.hasMesh()) return;
        m_liveRunning = false;
        m_pausedAtIterLimit = false;
        syncBestFitSettings(/*liveUpdate=*/true);

        try {
            if (m_activeMode == 0) {
                // Mode 0: Regular FDM (Schek 1974 direct solve)
                zSpace::zVaultFDMSettings fs;
                fs.fixXY = (m_planConstraintIdx == 2);
                fs.geometryWeight = 0.0;
                fs.allowZeroLoad = true;
                auto rep = m_vault.solveFDM(fs);
                m_hasSolved = true;
                m_showDensities = true;
                m_showResiduals = false;
                m_showThrust = true;
                rebuildAllRenderBuffers();
                updateMetricsLine();
                char buf[220];
                std::snprintf(buf, sizeof(buf),
                    "Regular FDM solved: status=%s | res=%.2e | maxDisp=%.4f",
                    rep.status.c_str(), rep.residualNorm, rep.maxDisplacement);
                m_statusLine = buf;
            } else if (m_activeMode == 1) {
                // Mode 1: Classic TNA (Transit + Horizontal + Vertical equilibrium)
                if (!m_vault.hasDiagrams()) {
                    runTransitToTNA();
                }
                runTnaHorizontalStep(150);
                runTnaVertical();
            } else {
                // Mode 2 (BF FDM) & Mode 3 (BF TNA): Best-Fit solve (continues from current state if already initialized!)
                zSpace::zVaultSolveReport fitRep = m_vault.report();
                if (!m_hasSolved || fitRep.status == "uninitialized") {
                    if (m_activeMode == 2 && m_vault.hasDiagrams()) {
                        m_vault.commitResultAsInput(false);
                    }
                    fitRep = m_vault.computeBestFitForceDensities();
                }
                if (!isEquilibriumReached()) {
                    fitRep = m_vault.stepBestFit(m_useFastDR ? 600 : 60);
                }
                m_hasSolved = true;
                m_showDensities = true;
                m_showResiduals = true;
                m_showThrust = true;
                if (m_activeMode == 3) {
                    const auto ds = diagramSettings();
                    m_vault.updateLiveDiagrams(ds);
                    m_showFormDiagram = true;
                    m_showForceDiagram = true;
                }
                rebuildAllRenderBuffers();
                updateMetricsLine();
                char buf[260];
                std::snprintf(buf, sizeof(buf),
                    "%s: status=%s | iter=%d | res=%.5f | maxDisp=%.4f | XYrms=%.4f | Zrms=%.4f",
                    (m_activeMode == 3 ? "BF TNA" : "BF FDM"),
                    fitRep.status.c_str(), fitRep.iteration, fitRep.residualNorm,
                    fitRep.maxDisplacement, fitRep.rmsXYDisplacement, fitRep.rmsZDisplacement);
                m_statusLine = buf;
            }
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Solve error: ") + ex.what();
            std::cerr << "[Vault] " << m_statusLine << std::endl;
        }
    }

    void runStepBatch(int steps)
    {
        if (!m_vault.hasMesh()) return;
        try {
            if (!m_hasSolved || m_vault.report().status == "uninitialized") {
                syncBestFitSettings();
                m_vault.computeBestFitForceDensities();
                m_hasSolved = true;
                m_showDensities = true;
                m_showResiduals = true;
                m_showThrust = true;
            }
            auto rep = m_vault.stepBestFit(steps);
            if (m_activeMode == 3) {
                const auto ds = diagramSettings();
                m_vault.updateLiveDiagrams(ds);
                m_showFormDiagram = true;
                m_showForceDiagram = true;
            }
            rebuildAllRenderBuffers();
            updateMetricsLine();
            char buf[240];
            if (m_activeMode == 3) {
                std::snprintf(buf, sizeof(buf),
                    "BF TNA +%d -> iter=%d (%s) | res=%.4f | closure=%.4f | maxAngle=%.2f deg",
                    steps, rep.iteration, rep.status.c_str(), rep.residualNorm,
                    m_vault.diagramReport().closureError, m_vault.tnaReport().maxAngleDeg);
            } else {
                std::snprintf(buf, sizeof(buf),
                    "BF FDM +%d -> iter=%d (%s) | res=%.5f | step=%.4f",
                    steps, rep.iteration, rep.status.c_str(), rep.residualNorm, rep.stepSize);
            }
            m_statusLine = buf;
        } catch (const std::exception& ex) {
            m_liveRunning = false;
            m_statusLine = std::string("Step error: ") + ex.what();
        }
    }

    void runSnapFDM()
    {
        if (!m_vault.hasMesh()) return;
        m_liveRunning = false;
        try {
            zSpace::zVaultSolveReport rep;
            if (m_hasSolved) {
                rep = m_vault.snapFixedDensities(0.0);
            } else {
                zSpace::zVaultFDMSettings fs;
                fs.fixXY = (m_planConstraintIdx == 2);
                rep = m_vault.solveFDM(fs);
                m_hasSolved = true;
            }
            m_showDensities = true;
            m_showThrust = true;
            rebuildAllRenderBuffers();
            updateMetricsLine();
            char buf[220];
            std::snprintf(buf, sizeof(buf), "Snapped fixed-q FDM equilibrium: status=%s | res=%.2e",
                          rep.status.c_str(), rep.residualNorm);
            m_statusLine = buf;
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Snap FDM error: ") + ex.what();
        }
    }

    void runTransitToTNA()
    {
        if (!m_vault.hasMesh()) return;
        m_liveRunning = false;
        try {
            const auto ds = diagramSettings();
            auto dRep = m_vault.transitToTNA(ds);
            m_showFormDiagram = true;
            m_showForceDiagram = true;
            m_showDensities = true;
            rebuildAllRenderBuffers();
            updateMetricsLine();
            char buf[260];
            std::snprintf(buf, sizeof(buf),
                "TNA Diagrams (%s, rot90=%s): closure=%.2e | mismatch=%.2e | maxAngle=%.2f deg",
                dRep.status.c_str(), m_rotate90Dual ? "ON" : "OFF",
                dRep.closureError, dRep.maxEdgeMismatch, m_vault.tnaReport().maxAngleDeg);
            m_statusLine = buf;
        } catch (const std::exception& ex) {
            m_statusLine = std::string("TNA Transit error: ") + ex.what();
            std::cerr << "[Vault] " << m_statusLine << std::endl;
        }
    }

    void runTnaHorizontalStep(int iters)
    {
        if (!m_vault.hasMesh()) return;
        try {
            if (!m_vault.hasDiagrams()) runTransitToTNA();
            zSpace::zVaultTNASettings ts;
            ts.formWeight = static_cast<double>(m_tnaFormWeightUI);
            ts.dT = 0.5;
            ts.angleToleranceDeg = static_cast<double>(m_angleTolUI);
            ts.minMaxFormEdge = 0.05;
            ts.minMaxForceEdge = 0.05;
            ts.rotate90 = m_rotate90Dual;
            auto hRep = m_vault.stepHorizontalEquilibrium(ts, iters);
            rebuildDiagramBuffers();
            updateMetricsLine();
            char buf[240];
            std::snprintf(buf, sizeof(buf),
                "TNA Horizontal (%s, iter=%d): min=%.2f deg | mean=%.2f deg | max=%.2f deg",
                hRep.status.c_str(), hRep.iteration, hRep.minAngleDeg, hRep.meanAngleDeg, hRep.maxAngleDeg);
            m_statusLine = buf;
        } catch (const std::exception& ex) {
            m_liveRunning = false;
            m_statusLine = std::string("TNA Horiz error: ") + ex.what();
        }
    }

    void runTnaVertical()
    {
        if (!m_vault.hasMesh()) return;
        try {
            if (!m_vault.hasDiagrams()) runTransitToTNA();
            auto vRep = m_vault.solveVerticalEquilibrium(static_cast<double>(m_tnaForceScaleUI), false);
            m_hasSolved = true;
            m_showDensities = true;
            m_showThrust = true;
            rebuildAllRenderBuffers();
            updateMetricsLine();
            char buf[240];
            std::snprintf(buf, sizeof(buf),
                "TNA Vertical (scale=%.2f): status=%s | vRes=%.2e | hRes=%.2e | maxAngle=%.2f deg",
                m_tnaForceScaleUI, vRep.status.c_str(), vRep.verticalResidual, vRep.horizontalResidual, vRep.maxAngleDeg);
            m_statusLine = buf;
        } catch (const std::exception& ex) {
            m_statusLine = std::string("TNA Vert error: ") + ex.what();
        }
    }

    void runUndoStep()
    {
        if (!m_hasSolved) return;
        m_liveRunning = false;
        try {
            if (m_vault.previousBestFit()) {
                if (m_activeMode == 3) {
                    const auto ds = diagramSettings();
                    m_vault.updateLiveDiagrams(ds);
                }
                rebuildAllRenderBuffers();
                updateMetricsLine();
                m_statusLine = "Rewound to iteration " + std::to_string(m_vault.report().iteration) + ".";
            } else {
                m_statusLine = "Already at initial fitted state (no earlier step in history).";
            }
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Undo error: ") + ex.what();
        }
    }

    void resetActiveSolver()
    {
        const bool wasLive = m_liveRunning;
        try {
            if (m_hasSolved) {
                if (m_vault.report().status == "uninitialized") {
                    syncBestFitSettings();
                    m_vault.computeBestFitForceDensities();
                } else {
                    m_vault.resetBestFit();
                }
                if (m_activeMode == 3) {
                    const auto ds = diagramSettings();
                    m_vault.updateLiveDiagrams(ds);
                } else if (m_activeMode == 1) {
                    runTransitToTNA();
                }
                rebuildAllRenderBuffers();
                updateMetricsLine();
                m_statusLine = "Reset solver to initial fitted state (iter 0).";
            } else {
                loadPreset(m_presetIdx);
            }
            m_liveRunning = wasLive;
        } catch (const std::exception& ex) {
            m_liveRunning = false;
            m_statusLine = std::string("Reset error: ") + ex.what();
        }
    }

    void copyVaultUsd()
    {
        if (!m_vault.hasMesh()) return;
        auto* app = Application::getInstance();
        if (!app) return;
        try {
            const std::string usda = m_vault.exportToUSDA(
                /*includeConstraints=*/true,
                /*includeDiagrams=*/m_vault.hasDiagrams(),
                m_formOffsetX, m_forceOffsetX, m_tnaForceScaleUI);
            if (app->setClipboardText(usda)) {
                m_statusLine = "Copied vault + USD color constraints to clipboard (" + std::to_string(usda.size()) + " bytes).";
            } else {
                m_statusLine = "Failed to write system clipboard.";
            }
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Copy USD error: ") + ex.what();
        }
    }

    void pasteVaultUsd()
    {
        auto* app = Application::getInstance();
        if (!app) return;
        m_liveRunning = false;
        const std::string clipText = app->getClipboardText();

        if (!clipText.empty() && zSpace::zTsVault::isUSDAClipboardText(clipText)) {
            try {
                auto crep = m_vault.loadInputFromUSDA(clipText, colorConstraintSettings(), true);
                if (m_vault.supports().empty()) assignCornerSupportsFallback();
                m_vault.setGravity(zSpace::zVector(0.0f, 0.0f, -1.0f));
                applyMassMode();
                syncBestFitSettings();
                m_hasSolved = false;
                m_showDensities = false;
                computeDiagramOffsets();
                rebuildAllRenderBuffers();
                frameCameraOnScene();
                m_datasetLabel = "Pasted USD Clipboard";
                m_metricsLine = "Pasted USD constraints: " +
                    std::to_string(crep.supportCount) + " blue supports, " +
                    std::to_string(crep.compressionEdgeCount) + " comp edges, " +
                    std::to_string(crep.tensionEdgeCount) + " tens edges.";
                m_statusLine = "Pasted USD mesh. Press [P] to Fit/Solve or [T] for TNA.";
                return;
            } catch (const std::exception& ex) {
                m_statusLine = std::string("Paste USD error: ") + ex.what();
                return;
            }
        }

        if (!clipText.empty()) {
            const std::string trimmed = trimQuotesAndWhitespace(clipText);
            if (!trimmed.empty() && std::filesystem::exists(trimmed)) {
                loadExternalMeshFile(trimmed, "Pasted path");
                return;
            }
        }

        const std::string copiedFile = app->getClipboardFilePath();
        if (!copiedFile.empty() && std::filesystem::exists(copiedFile)) {
            loadExternalMeshFile(copiedFile, "Pasted file");
            return;
        }

        m_statusLine = "Clipboard does not contain #usda 1.0 text or a valid mesh file path.";
    }

    void exportVaultUsd()
    {
        if (!m_vault.hasMesh()) return;
        try {
            const std::string usda = m_vault.exportToUSDA(
                /*includeConstraints=*/true,
                /*includeDiagrams=*/m_vault.hasDiagrams(),
                m_formOffsetX, m_forceOffsetX, m_tnaForceScaleUI);
            std::filesystem::create_directories(std::filesystem::path(kExportUsdaPath).parent_path());
            std::ofstream ofs(kExportUsdaPath);
            ofs << usda;
            m_statusLine = std::string("Exported vault + constraints + diagrams to ") + kExportUsdaPath;
        } catch (const std::exception& ex) {
            m_statusLine = std::string("Export USD error: ") + ex.what();
        }
    }

    void exportVaultObj()
    {
        if (!m_vault.hasMesh()) return;
        std::filesystem::create_directories(std::filesystem::path(kExportObjPath).parent_path());
        const auto res = zSpace::zIO::writeMesh(kExportObjPath, m_vault.resultMesh());
        m_statusLine = res
            ? (std::string("Exported result OBJ to ") + kExportObjPath)
            : (std::string("Export OBJ failed: ") + res.message());
    }

    void computeDiagramOffsets()
    {
        const auto& pts = m_vault.originalPositions();
        if (pts.empty()) {
            m_formOffsetX = 0.0f;
            m_formOffsetZ = -3.0f;
            m_forceOffsetX = 14.0f;
            m_forceCenterY = 0.0f;
            return;
        }
        float minX = pts[0].x, maxX = pts[0].x;
        float minY = pts[0].y, maxY = pts[0].y;
        float minZ = pts[0].z, maxZ = pts[0].z;
        for (const auto& p : pts) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
        }
        const float spanX = std::max(4.0f, maxX - minX);
        const float spanY = std::max(4.0f, maxY - minY);
        const float span  = std::max(spanX, spanY);
        const float height = std::max(1.0f, maxZ - minZ);

        // Place 2D Form Diagram directly BELOW the 3D Result Mesh (X, Y aligned 1:1!)
        m_formOffsetX = 0.0f;
        m_formOffsetZ = minZ - std::max(height * 0.45f, span * 0.16f);

        // Place auto-normalized 2D Force Diagram on the same ground plane to the right (+X)
        m_forceOffsetX = maxX + span * 0.68f;
        m_forceCenterY = 0.5f * (minY + maxY);
    }

    void updateMetricsLine()
    {
        const auto& rep = m_vault.report();
        const auto& qs = m_vault.forceDensities();
        double qMinVal = 1e30, qMaxVal = -1e30;
        for (int e : m_vault.activeEdgeIds()) {
            if (size_t(e) < qs.size()) {
                qMinVal = std::min(qMinVal, qs[e]);
                qMaxVal = std::max(qMaxVal, qs[e]);
            }
        }
        if (qMinVal > qMaxVal) { qMinVal = 0.0; qMaxVal = 0.0; }

        char buf[340];
        if (m_vault.hasDiagrams()) {
            const auto& tRep = m_vault.tnaReport();
            std::snprintf(buf, sizeof(buf),
                "Iter %d | Res %.4f (max %.4f) | q [%.2f, %.2f] | XYrms %.3f | TNA Angle max %.2f deg (mean %.2f)",
                rep.iteration, rep.residualNorm, rep.maxResidual, qMinVal, qMaxVal,
                rep.rmsXYDisplacement, tRep.maxAngleDeg, tRep.meanAngleDeg);
        } else {
            std::snprintf(buf, sizeof(buf),
                "Iter %d (%s) | Res %.4f (max %.4f) | q [%.2f, %.2f] | XYrms %.4f | Zrms %.4f | EdgeRatio %.3f",
                rep.iteration, rep.status.c_str(), rep.residualNorm, rep.maxResidual,
                qMinVal, qMaxVal, rep.rmsXYDisplacement, rep.rmsZDisplacement, rep.minMaxEdgeRatio);
        }
        m_metricsLine = buf;
    }

    void rebuildAllRenderBuffers()
    {
        zSpace::zPointArray currPts;
        zSpace::zIntArray counts, connects;
        zSpace::zFnMesh fn(m_vault.resultMesh());
        fn.getVertexPositions(currPts);
        fn.getPolygonData(connects, counts);

        const auto& origPts = m_vault.originalPositions();
        const size_t nv = currPts.size();
        m_currVerts.resize(nv);
        m_origVerts.resize(origPts.size());
        for (size_t i = 0; i < nv; ++i) m_currVerts[i] = Vec3(currPts[i].x, currPts[i].y, currPts[i].z);
        for (size_t i = 0; i < origPts.size(); ++i) m_origVerts[i] = Vec3(origPts[i].x, origPts[i].y, origPts[i].z);

        // Directional-lit neutral clay triangles
        m_triPositions.clear();
        m_triColors.clear();
        const float lx = 0.42f, ly = -0.56f, lz = 0.71f;
        size_t offset = 0;
        for (int c : counts) {
            if (c >= 3) {
                const Vec3& p0 = m_currVerts[connects[offset]];
                for (int k = 1; k + 1 < c; ++k) {
                    const Vec3& p1 = m_currVerts[connects[offset + k]];
                    const Vec3& p2 = m_currVerts[connects[offset + k + 1]];
                    const Vec3 e1 = p1 - p0, e2 = p2 - p0;
                    Vec3 n(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);
                    const float nLen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                    if (nLen > 1e-7f) { n.x /= nLen; n.y /= nLen; n.z /= nLen; }
                    if (n.z < 0.0f) { n.x = -n.x; n.y = -n.y; n.z = -n.z; }
                    const float ndotl = std::max(0.0f, n.x * lx + n.y * ly + n.z * lz);
                    const float shade = 0.62f + 0.30f * ndotl + 0.08f * n.z;
                    const Color clay(std::min(1.0f, 0.80f * shade), std::min(1.0f, 0.82f * shade), std::min(1.0f, 0.85f * shade), 1.0f);
                    m_triPositions.push_back(p0); m_triPositions.push_back(p1); m_triPositions.push_back(p2);
                    m_triColors.push_back(clay);  m_triColors.push_back(clay);  m_triColors.push_back(clay);
                }
            }
            offset += size_t(c);
        }

        // Edges (neutral grey vs force-density colored vs USD constraint black->red / green gradient)
        m_edgeIndices = m_vault.edgeConnects();
        const size_t ne = m_edgeIndices.size() / 2;
        m_neutralEdgeColors.assign(ne, Color(0.18f, 0.21f, 0.26f, 1.0f));
        m_origEdgeColors.assign(ne, Color(0.72f, 0.75f, 0.80f, 1.0f));
        m_densityEdgeColors.assign(ne, Color(0.0f, 0.0f, 0.0f, 1.0f));
        m_usdConstraintEdgeColors.assign(ne, Color(0.35f, 0.38f, 0.44f, 1.0f));

        std::vector<uint8_t> isSup(nv, 0);
        for (int v : m_vault.supports()) if (v >= 0 && size_t(v) < nv) isSup[v] = 1;
        std::vector<uint8_t> isTens(ne, 0);
        for (int e : m_vault.tensionEdges()) if (e >= 0 && size_t(e) < ne) isTens[e] = 1;

        const auto& qs = m_vault.forceDensities();
        double minMag = 1e30, maxMag = 0.0;
        for (size_t e = 0; e < ne; ++e) {
            if (e < qs.size() && std::abs(qs[e]) > 1e-12) {
                minMag = std::min(minMag, std::abs(qs[e]));
                maxMag = std::max(maxMag, std::abs(qs[e]));
            }
        }
        const double magSpan = (maxMag > minMag + 1e-12) ? (maxMag - minMag) : 1.0;

        // Webviewer / zSpace brand statics colors: Tension #F0008C, Compression #0028F0
        const Color colTens(240.0f / 255.0f, 0.0f, 140.0f / 255.0f, 1.0f);
        const Color colComp(0.0f, 40.0f / 255.0f, 240.0f / 255.0f, 1.0f);
        const Color colInactive(0.05f, 0.05f, 0.08f, 1.0f);

        for (size_t e = 0; e < ne; ++e) {
            const int u = m_edgeIndices[2 * e], v = m_edgeIndices[2 * e + 1];
            const bool bothFixed = isSup[u] && isSup[v];
            if (bothFixed) {
                m_densityEdgeColors[e] = colInactive;
                m_usdConstraintEdgeColors[e] = colInactive;
                continue;
            }
            const bool hasQ = (m_hasSolved && e < qs.size());
            const double qVal = hasQ ? qs[e] : (isTens[e] ? 1.0 : -1.0);
            m_densityEdgeColors[e] = (qVal > 0.0) ? colTens : colComp;

            const float t = (hasQ && maxMag > minMag + 1e-12)
                ? float(std::clamp((std::abs(qVal) - minMag) / magSpan, 0.0, 1.0))
                : 0.5f;
            m_usdConstraintEdgeColors[e] = (qVal > 0.0)
                ? Color(0.0f, std::max(0.15f, t), 0.0f, 1.0f) // Green gradient for tension
                : Color(t, 0.0f, 0.0f, 1.0f);                  // Black->Red gradient for compression
        }

        // Support points
        m_supportPoints.clear();
        for (int v : m_vault.supports()) {
            if (v >= 0 && size_t(v) < nv) m_supportPoints.push_back(m_currVerts[v]);
        }

        // Residual & support thrust vectors
        m_residualLines.clear();
        m_thrustLines.clear();
        if (m_hasSolved) {
            const auto& rf = m_vault.residualForces();
            const auto& sr = m_vault.supportReactions();
            const float sc = m_vectorScaleUI;
            for (size_t i = 0; i < nv; ++i) {
                if (!isSup[i] && i < rf.size()) {
                    const Vec3 vec(rf[i].x * sc, rf[i].y * sc, rf[i].z * sc);
                    if (vec.length() > 1e-5f) {
                        m_residualLines.push_back(m_currVerts[i]);
                        m_residualLines.push_back(m_currVerts[i] + vec);
                    }
                } else if (isSup[i] && i < sr.size()) {
                    // Thrust exerted onto support = -reaction (consistent with vaultSolver.ts)
                    const Vec3 vec(-sr[i].x * sc, -sr[i].y * sc, -sr[i].z * sc);
                    if (vec.length() > 1e-5f) {
                        m_thrustLines.push_back(m_currVerts[i]);
                        m_thrustLines.push_back(m_currVerts[i] + vec);
                    }
                }
            }
        }

        rebuildDiagramBuffers();
    }

    void rebuildDiagramBuffers()
    {
        m_formVerts.clear();
        m_formEdges.clear();
        m_formEdgeCols.clear();
        m_formAngleCols.clear();
        m_formSolverCols.clear();
        m_supportDropLines.clear();
        m_forceVerts.clear();
        m_forceEdges.clear();
        m_forceEdgeCols.clear();
        m_forceAngleCols.clear();
        m_forceSolverCols.clear();

        if (!m_vault.hasDiagrams()) return;

        const double angTol = static_cast<double>(std::max(0.05f, m_angleTolUI));
        const double angRedCap = std::max(5.0, angTol * 5.0);
        auto angleToGreenRed = [&](double ang) -> Color {
            if (ang < 0.0) return Color(0.15f, 0.15f, 0.18f, 1.0f);
            if (ang <= angTol) return Color(0.0f, 0.80f, 0.32f, 1.0f); // Solved: crisp green
            const float t = static_cast<float>(std::clamp((ang - angTol) / std::max(0.5, angRedCap - angTol), 0.0, 1.0));
            if (t < 0.5f) {
                const float u = t * 2.0f;
                return Color(0.15f + 0.81f * u, 0.80f - 0.06f * u, 0.25f * (1.0f - u), 1.0f); // Green -> Yellow/Amber
            }
            const float u = (t - 0.5f) * 2.0f;
            return Color(0.96f - 0.02f * u, 0.74f * (1.0f - u) + 0.08f * u, 0.04f + 0.04f * u, 1.0f); // Amber -> Red
        };

        // 1. Form diagram placed directly BELOW the 3D Result Mesh at z = m_formOffsetZ
        float formSpan = 1.0f;
        {
            zSpace::zObjectMesh formCopy = m_vault.formMesh();
            zSpace::zFnMesh fnForm(formCopy);
            zSpace::zPointArray fPts;
            zSpace::zColorArray eCols;
            fnForm.getVertexPositions(fPts);
            fnForm.getEdgeData(m_formEdges);
            fnForm.getEdgeColors(eCols);
            m_formVerts.resize(fPts.size());
            float fMinX = 1e30f, fMaxX = -1e30f, fMinY = 1e30f, fMaxY = -1e30f;
            for (size_t i = 0; i < fPts.size(); ++i) {
                const float x = fPts[i].x + m_formOffsetX;
                const float y = fPts[i].y;
                m_formVerts[i] = Vec3(x, y, m_formOffsetZ);
                fMinX = std::min(fMinX, x); fMaxX = std::max(fMaxX, x);
                fMinY = std::min(fMinY, y); fMaxY = std::max(fMaxY, y);
            }
            if (!fPts.empty()) {
                formSpan = std::max({1.0f, fMaxX - fMinX, fMaxY - fMinY});
            }

            // Subtle vertical drop-lines linking 3D supports straight down to the 2D Form Diagram
            for (int v : m_vault.supports()) {
                if (v >= 0 && size_t(v) < m_currVerts.size() && size_t(v) < m_formVerts.size()) {
                    m_supportDropLines.push_back(m_currVerts[v]);
                    m_supportDropLines.push_back(m_formVerts[v]);
                }
            }

            const size_t nfe = m_formEdges.size() / 2;
            m_formEdgeCols.resize(nfe);
            m_formAngleCols.resize(nfe);
            m_formSolverCols.resize(nfe);
            const auto& devs = m_vault.tnaEdgeAngleDeviations();
            const Color okCol(0.0f, 240.0f / 255.0f, 140.0f / 255.0f, 1.0f);  // #00F08C
            const Color badCol(240.0f / 255.0f, 0.0f, 140.0f / 255.0f, 1.0f); // #F0008C
            for (size_t e = 0; e < nfe; ++e) {
                if (m_hasSolved && e < m_densityEdgeColors.size()) m_formEdgeCols[e] = m_densityEdgeColors[e];
                else if (e < eCols.size()) m_formEdgeCols[e] = Color(eCols[e].r, eCols[e].g, eCols[e].b, 1.0f);
                else m_formEdgeCols[e] = Color(0.0f, 40.0f / 255.0f, 240.0f / 255.0f, 1.0f);

                const double ang = (e < devs.size()) ? devs[e] : -1.0;
                if (ang < 0.0) m_formAngleCols[e] = Color(0.15f, 0.15f, 0.18f, 1.0f);
                else m_formAngleCols[e] = (ang <= angTol) ? okCol : badCol;
                m_formSolverCols[e] = angleToGreenRed(ang);
            }
        }

        // 2. Auto-normalized Force Diagram placed on the same ground plane z = m_formOffsetZ beside the Form Diagram
        {
            zSpace::zObjectMesh forceCopy = m_vault.forceMesh();
            zSpace::zFnMesh fnForce(forceCopy);
            zSpace::zPointArray pPts;
            zSpace::zColorArray eCols;
            fnForce.getVertexPositions(pPts);
            fnForce.getEdgeData(m_forceEdges);
            fnForce.getEdgeColors(eCols);
            float pMinX = 1e30f, pMaxX = -1e30f, pMinY = 1e30f, pMaxY = -1e30f;
            for (const auto& p : pPts) {
                pMinX = std::min(pMinX, p.x); pMaxX = std::max(pMaxX, p.x);
                pMinY = std::min(pMinY, p.y); pMaxY = std::max(pMaxY, p.y);
            }
            const float cx = pPts.empty() ? 0.0f : 0.5f * (pMinX + pMaxX);
            const float cy = pPts.empty() ? 0.0f : 0.5f * (pMinY + pMaxY);
            const float rawSpan = pPts.empty() ? 1.0f : std::max(pMaxX - pMinX, pMaxY - pMinY);
            const float autoNormScale = (rawSpan > 1e-6f ? (formSpan * 0.85f / rawSpan) : 1.0f) * m_tnaForceScaleUI;

            m_forceVerts.resize(pPts.size());
            for (size_t i = 0; i < pPts.size(); ++i) {
                m_forceVerts[i] = Vec3(
                    (pPts[i].x - cx) * autoNormScale + m_forceOffsetX,
                    (pPts[i].y - cy) * autoNormScale + m_forceCenterY,
                    m_formOffsetZ);
            }
            const size_t npe = m_forceEdges.size() / 2;
            m_forceEdgeCols.resize(npe);
            m_forceAngleCols.resize(npe);
            m_forceSolverCols.resize(npe);
            const auto& fToForm = m_vault.forceHalfEdgeToFormHalfEdge();
            const auto& devs = m_vault.tnaEdgeAngleDeviations();
            const Color okCol(0.0f, 240.0f / 255.0f, 140.0f / 255.0f, 1.0f);
            const Color badCol(240.0f / 255.0f, 0.0f, 140.0f / 255.0f, 1.0f);
            for (size_t e = 0; e < npe; ++e) {
                const size_t he = 2 * e;
                const int formEdgeIdx = (he < fToForm.size() && fToForm[he] >= 0) ? (fToForm[he] / 2) : -1;
                if (m_hasSolved && formEdgeIdx >= 0 && size_t(formEdgeIdx) < m_densityEdgeColors.size()) {
                    m_forceEdgeCols[e] = m_densityEdgeColors[size_t(formEdgeIdx)];
                } else if (e < eCols.size()) {
                    m_forceEdgeCols[e] = Color(eCols[e].r, eCols[e].g, eCols[e].b, 1.0f);
                } else {
                    m_forceEdgeCols[e] = Color(0.0f, 40.0f / 255.0f, 240.0f / 255.0f, 1.0f);
                }

                // Map force edge halfedge 2*e -> form halfedge -> source edge angle
                const double ang = (formEdgeIdx >= 0 && size_t(formEdgeIdx) < devs.size()) ? devs[size_t(formEdgeIdx)] : -1.0;
                m_forceAngleCols[e] = (ang > angTol) ? badCol : okCol;
                m_forceSolverCols[e] = angleToGreenRed(ang);
            }
        }
    }

    void drawSceneGeometry(Renderer& renderer)
    {
        if (m_showOrigWire && !m_edgeIndices.empty()) {
            renderer.setLineWidth(1.0f);
            renderer.drawMeshEdges(m_origVerts.data(), m_edgeIndices.data(),
                                   m_origEdgeColors.data(), int(m_origEdgeColors.size()));
        }

        if (m_showShadedMesh && !m_triPositions.empty()) {
            renderer.drawMesh(m_triPositions.data(), nullptr, m_triColors.data(),
                              int(m_triPositions.size()), nullptr, 0, false);
        }

        if (!m_edgeIndices.empty()) {
            const Color* edgeCols = m_showUsdConstraintColors
                ? m_usdConstraintEdgeColors.data()
                : (m_showDensities ? m_densityEdgeColors.data() : m_neutralEdgeColors.data());
            renderer.setLineWidth((m_showDensities || m_showUsdConstraintColors) ? 2.2f : 1.2f);
            renderer.drawMeshEdges(m_currVerts.data(), m_edgeIndices.data(),
                                   edgeCols, int(m_neutralEdgeColors.size()));
        }

        if (!m_supportPoints.empty()) {
            // Blue markers in USD constraint mode, crisp dark anchor dots otherwise
            renderer.setColor(m_showUsdConstraintColors ? Color(0.0f, 0.0f, 1.0f, 1.0f) : Color(0.05f, 0.06f, 0.10f, 1.0f));
            renderer.setPointSize(9.0f);
            renderer.drawPoints(m_supportPoints.data(), int(m_supportPoints.size()));
        }

        if (m_showResiduals && !m_residualLines.empty()) {
            renderer.drawLines(m_residualLines.data(), int(m_residualLines.size()),
                               Color(0.96f, 0.68f, 0.0f, 1.0f), 2.0f);
        }

        if (m_showThrust && !m_thrustLines.empty()) {
            renderer.drawLines(m_thrustLines.data(), int(m_thrustLines.size()),
                               Color(0.0f, 240.0f / 255.0f, 140.0f / 255.0f, 1.0f), 2.4f);
        }

        if (m_vault.hasDiagrams()) {
            if (m_showFormDiagram && !m_formEdges.empty()) {
                if (!m_supportDropLines.empty()) {
                    renderer.drawLines(m_supportDropLines.data(), int(m_supportDropLines.size()),
                                       Color(0.68f, 0.72f, 0.78f, 1.0f), 1.0f);
                }
                renderer.setLineWidth(m_showSolverAngleGradient ? 2.4f : 2.0f);
                const Color* cols = m_showSolverAngleGradient
                    ? m_formSolverCols.data()
                    : (m_showTnaAngleCheck ? m_formAngleCols.data() : m_formEdgeCols.data());
                renderer.drawMeshEdges(m_formVerts.data(), m_formEdges.data(), cols, int(m_formEdgeCols.size()));
            }
            if (m_showForceDiagram && !m_forceEdges.empty()) {
                renderer.setLineWidth(m_showSolverAngleGradient ? 2.4f : 2.0f);
                const Color* cols = m_showSolverAngleGradient
                    ? m_forceSolverCols.data()
                    : (m_showTnaAngleCheck ? m_forceAngleCols.data() : m_forceEdgeCols.data());
                renderer.drawMeshEdges(m_forceVerts.data(), m_forceEdges.data(), cols, int(m_forceEdgeCols.size()));
            }
        }
    }

    bool isEquilibriumReached() const
    {
        if (!m_hasSolved) return false;
        const auto& rep = m_vault.report();
        const double resTol = static_cast<double>(std::max(1e-5f, m_resTolUI));
        const double angTol = static_cast<double>(std::max(0.05f, m_angleTolUI));

        if (m_activeMode == 0) {
            return rep.status == "fdm-solved" || rep.status == "direct-equilibrium" || rep.status == "direct-height" || rep.residualNorm <= resTol;
        }
        if (m_activeMode == 1) {
            const auto& tRep = m_vault.tnaReport();
            return m_vault.hasDiagrams() && tRep.maxAngleDeg <= angTol && rep.residualNorm <= resTol;
        }
        const bool resOk = (rep.residualNorm <= resTol) || rep.status == "direct-equilibrium" || rep.status == "direct-height";
        if (m_activeMode == 3 && m_vault.hasDiagrams()) {
            return resOk && (m_vault.tnaReport().maxAngleDeg <= angTol);
        }
        return resOk;
    }

    void drawEquilibriumBanner(Renderer& renderer)
    {
        GLint vp[4] = {0, 0, 1280, 720};
        glGetIntegerv(GL_VIEWPORT, vp);
        const float vpW = static_cast<float>(vp[2]);

        const float bannerW = 416.0f;
        const float bannerH = 62.0f;
        const float bx = std::max(410.0f, vpW - bannerW - 18.0f);
        const float by = 14.0f;

        std::string headline;
        std::string subline;
        Color fillCol, borderCol;
        const Color textCol(1.0f, 1.0f, 1.0f, 1.0f);

        const auto& rep = m_vault.report();
        char subBuf[180];

        if (!m_hasSolved) {
            headline = "READY - NOT SOLVED";
            std::snprintf(subBuf, sizeof(subBuf), "Tol: Res<=%.3f | Ang<=%.1f deg  ([P] Solve / [L] Live)",
                          m_resTolUI, m_angleTolUI);
            fillCol   = Color(0.24f, 0.27f, 0.34f, 0.90f);
            borderCol = Color(0.45f, 0.50f, 0.58f, 1.0f);
        } else if (isEquilibriumReached()) {
            headline = "EQUILIBRIUM REACHED";
            if (m_vault.hasDiagrams()) {
                std::snprintf(subBuf, sizeof(subBuf), "Res %.4f/%.3f | Ang %.2f/%.1f deg | It %d",
                              rep.residualNorm, m_resTolUI, m_vault.tnaReport().maxAngleDeg, m_angleTolUI, rep.iteration);
            } else {
                std::snprintf(subBuf, sizeof(subBuf), "Res %.4f / tol %.3f | It %d",
                              rep.residualNorm, m_resTolUI, rep.iteration);
            }
            fillCol   = Color(0.0f, 0.66f, 0.38f, 0.95f);
            borderCol = Color(0.0f, 240.0f / 255.0f, 140.0f / 255.0f, 1.0f);
        } else if (m_liveRunning) {
            headline = "SOLVING...";
            if (m_vault.hasDiagrams()) {
                std::snprintf(subBuf, sizeof(subBuf), "Res %.4f/%.3f | Ang %.2f/%.1f deg | It %d",
                              rep.residualNorm, m_resTolUI, m_vault.tnaReport().maxAngleDeg, m_angleTolUI, rep.iteration);
            } else {
                std::snprintf(subBuf, sizeof(subBuf), "Res %.4f / tol %.3f | It %d / %d",
                              rep.residualNorm, m_resTolUI, rep.iteration, int(std::lround(m_maxItersUI)));
            }
            fillCol   = Color(0.84f, 0.42f, 0.02f, 0.94f);
            borderCol = Color(1.0f, 0.68f, 0.15f, 1.0f);
        } else {
            headline = "NOT IN EQUILIBRIUM";
            if (m_vault.hasDiagrams()) {
                std::snprintf(subBuf, sizeof(subBuf), "Res %.4f/%.3f | Ang %.2f/%.1f deg | It %d",
                              rep.residualNorm, m_resTolUI, m_vault.tnaReport().maxAngleDeg, m_angleTolUI, rep.iteration);
            } else {
                std::snprintf(subBuf, sizeof(subBuf), "Res %.4f / tol %.3f | It %d",
                              rep.residualNorm, m_resTolUI, rep.iteration);
            }
            fillCol   = Color(220.0f / 255.0f, 0.0f, 128.0f / 255.0f, 0.94f);
            borderCol = Color(240.0f / 255.0f, 0.0f, 140.0f / 255.0f, 1.0f);
        }
        subline = subBuf;

        drawFilledRect2D(bx, by, bannerW, bannerH, fillCol, borderCol, 3.0f);
        drawBoldStrokeText2D(renderer, headline, bx + 16.0f, by + 13.0f, 20.0f, textCol, 3.2f);
        renderer.setColor(Color(0.96f, 0.98f, 1.0f, 1.0f));
        renderer.drawString(subline, bx + 16.0f, by + 51.0f);
    }

    void frameCameraOnScene()
    {
        if (m_currVerts.empty()) return;
        Vec3 mn = m_currVerts[0], mx = m_currVerts[0];
        auto expand = [&](const std::vector<Vec3>& pts) {
            for (const auto& p : pts) {
                mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
                mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
            }
        };
        expand(m_currVerts);
        if (m_vault.hasDiagrams() && m_showFormDiagram) expand(m_formVerts);
        if (m_vault.hasDiagrams() && m_showForceDiagram) expand(m_forceVerts);

        const Vec3 size = mx - mn;
        const float maxSize = std::max({size.x, size.y, size.z, 2.0f});
        const float diag = std::max(2.0f, size.length());
        camera().setNearPlane(std::max(0.05f, diag * 0.0005f));
        camera().setFarPlane(std::max(1000.0f, diag * 14.0f));
        if (auto* app = Application::getInstance()) {
            auto& ctrl = app->getCameraController();
            ctrl.focusOnBounds(mn, mx);
            ctrl.setOrbitDistance(maxSize * 1.45f);
            const Vec3 shifted = ctrl.getOrbitCenter() - camera().getRight() * (maxSize * 0.14f);
            ctrl.setOrbitCenter(shifted);
        }
    }

    zSpace::zTsVault m_vault;
    std::unique_ptr<SimpleUI> m_ui;

    // UI state
    int m_presetIdx = 0;
    int m_activeMode = 3;      // 0=FDM, 1=TNA, 2=BF FDM, 3=BF TNA
    int m_stdModeIdx = -1;     // Group 1: 0=FDM, 1=TNA (-1 when Best Fit active)
    int m_lastStdModeIdx = -1;
    int m_bfModeIdx = 1;       // Group 2 (Best Fit): 0=BF FDM, 1=BF TNA (-1 when Standard active)
    int m_lastBfModeIdx = 1;
    int m_planConstraintIdx = 0; // 0=Free, 1=PenalizeXY, 2=FixXY
    int m_lastPlanConstraintIdx = 0;

    bool m_liveRunning = false;
    bool m_pausedAtIterLimit = false;
    bool m_useFastDR = true;
    bool m_lastUseFastDR = true;
    bool m_hasSolved = false;
    bool m_compressionOnly = true;
    bool m_lastCompressionOnly = true;
    bool m_zeroWeight = false;
    bool m_lastZeroWeight = false;
    bool m_useAreaMass = false;
    bool m_lastUseAreaMass = false;
    bool m_rotate90Dual = true;
    bool m_lastRotate90Dual = true;

    bool m_showShadedMesh = true;
    bool m_showDensities = false;
    bool m_showResiduals = false;
    bool m_showThrust = false;
    bool m_showFormDiagram = true;
    bool m_showForceDiagram = true;
    bool m_showTnaAngleCheck = false;
    bool m_showOrigWire = false;
    bool m_showUsdConstraintColors = false;
    bool m_showSolverAngleGradient = false;

    float m_qMinUI = 0.05f;
    float m_lastQMinUI = 0.05f;
    float m_qMaxUI = 1000.0f;
    float m_lastQMaxUI = 1000.0f;
    float m_geomWeightUI = 0.5f;
    float m_lastGeomWeightUI = 0.5f;
    float m_zPenaltyUI = 0.0f;
    float m_lastZPenaltyUI = 0.0f;
    float m_maxItersUI = 2000.0f;
    float m_lastMaxItersUI = 2000.0f;
    float m_edgeRegUI = 0.0f;
    float m_lastEdgeRegUI = 0.0f;
    float m_resTolUI = 0.05f;
    float m_lastResTolUI = 0.05f;
    float m_angleTolUI = 0.5f;
    float m_lastAngleTolUI = 0.5f;
    float m_tnaForceScaleUI = 1.0f;
    float m_lastTnaForceScaleUI = 1.0f;
    float m_tnaFormWeightUI = 0.5f; // Default 0.5: coupled form + force parallel relaxation (1.0 = fixed form, 0.0 = fixed force)
    float m_vectorScaleUI = 0.5f;

    float m_formOffsetX = 0.0f;
    float m_formOffsetZ = -3.0f;
    float m_forceOffsetX = 16.0f;
    float m_forceCenterY = 0.0f;

    std::string m_datasetLabel = "Vault";
    std::string m_metricsLine = "Ready";
    std::string m_statusLine = "Ready";

    // Cached render buffers
    std::vector<Vec3> m_currVerts;
    std::vector<Vec3> m_origVerts;
    std::vector<Vec3> m_triPositions;
    std::vector<Color> m_triColors;
    std::vector<int> m_edgeIndices;
    std::vector<Color> m_neutralEdgeColors;
    std::vector<Color> m_origEdgeColors;
    std::vector<Color> m_densityEdgeColors;
    std::vector<Color> m_usdConstraintEdgeColors;
    std::vector<Vec3> m_supportPoints;
    std::vector<Vec3> m_residualLines;
    std::vector<Vec3> m_thrustLines;

    std::vector<Vec3> m_formVerts;
    std::vector<int> m_formEdges;
    std::vector<Color> m_formEdgeCols;
    std::vector<Color> m_formAngleCols;
    std::vector<Color> m_formSolverCols;
    std::vector<Vec3> m_supportDropLines;

    std::vector<Vec3> m_forceVerts;
    std::vector<int> m_forceEdges;
    std::vector<Color> m_forceEdgeCols;
    std::vector<Color> m_forceAngleCols;
    std::vector<Color> m_forceSolverCols;
};

ALICE2_REGISTER_SKETCH_AUTO(zSpaceVaultSketch)

#endif // __MAIN__
