#define __MAIN__
#ifdef __MAIN__

#include <zspace/interface.h>

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <slicer/zUnroller.h>
#include <slicer/zSlicingParameters.h>

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <tetgen.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace alice2;

namespace {

constexpr const char* kInputMeshPath = "data/rabatFooting/footing_2.obj";
constexpr int kDefaultLayerCount = 30;
constexpr int kMinLayerCount = 2;
constexpr int kMaxLayerCount = 200;
constexpr int kExpectedLoopCount = 3;
constexpr double kWeldTolerance = 1e-4;

// Method 3: band-constrained harmonic slicing.
constexpr float kTargetLayerHeight = alice2::SlicingParameters::printHeight;
constexpr float kLayerHeightBandMin = 0.7f * kTargetLayerHeight;
constexpr float kLayerHeightBandMax = 1.3f * kTargetLayerHeight;
constexpr int kEqualisationIterations = 2;
constexpr float kCoverageBlend = 0.5f;
// false: mean |grad psi| over the triangles crossed by a contour, true: min.
constexpr bool kGradientStatUseMin = false;
constexpr int kDebugModeCount = 22;

// Default scripted key tour (ALICE2_FOOTING_COMPARE_TOUR=1). Autorun ends in
// mode 8 (metrics): walk to the Method 3 modes and exercise w/s, a, b, l/m, n,
// visit the Method 4 modes 17-21 with their bottom/top toggle and slices,
// cycle through modes 0-8, then lower the shared layer count (30 -> 22, about
// h = 0.3 on this footing) and re-print the metrics for all four methods.
constexpr const char* kDefaultTourKeys = "dddddd" "wws" "dwwabbbbbwws" "dwa" "lmlm" "nnn" "ddwa" "wsws"
    "dddd" "nn" "dn" "dna" "d" "dwwsa" "ddddddddd" "--------k";

enum class BoundaryTag : int {
    Interior = 0,
    Bottom = 10,
    Top = 20,
    Side = 30
};

struct BoundaryLoop {
    zSpace::zIntArray vertexIds;
    zSpace::zPoint centroid;
    BoundaryTag tag = BoundaryTag::Side;
};

struct TriangleSoup {
    zSpace::zPointArray positions;
    std::vector<std::array<int, 3>> triangles;
};

struct TetVolume {
    zSpace::zPointArray vertices;
    std::vector<std::array<int, 4>> tets;
    zSpace::zIntArray markers;
    zSpace::zIntArray bottomVertexIds;
    zSpace::zIntArray topVertexIds;
};

struct SurfaceOperators {
    Eigen::SparseMatrix<double> laplacian;
    Eigen::VectorXd mass;
    double meanEdgeLength = 0.0;
};

// Linear tet element: basis-function gradients and the local stiffness block.
struct TetElement {
    bool valid = false;
    double volume = 0.0;
    Eigen::Matrix<double, 3, 4> gradients = Eigen::Matrix<double, 3, 4>::Zero();
    Eigen::Matrix4d stiffness = Eigen::Matrix4d::Zero();
};

struct GradientStats {
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;   // volume- or area-weighted
    float spread = 0.0f; // (max - min) / mean
};

// One pass of the adaptive iso-value schedule (Stage B).
struct ScheduleRun {
    std::vector<float> isoValues;                 // psi_0 = 0, psi_1, ...
    std::vector<float> gradients;                 // g_k on the contour at psi_k
    std::vector<zSpace::zIntArray> crossedTriangles; // side triangles crossed at psi_k
};

// Print height between two consecutive layers, sampled on the upper layer.
struct LayerGapStats {
    float minHeight = 0.0f;
    float meanHeight = 0.0f;
    float maxHeight = 0.0f;
    float worstHeight = 0.0f;
    zSpace::zPoint worstPoint;
    int sampleCount = 0;
    bool inBand = false;
};

struct LayerHeightReport {
    // Gap g: layer g-1 -> layer g, with layer 0 = bottom loop and the last
    // layer = top loops, so a method with n contours has n + 1 gaps.
    std::vector<LayerGapStats> gaps;
    std::vector<zSpace::zPointArray> samplePositions;
    std::vector<zSpace::zIntArray> sampleEdges;
    std::vector<std::vector<float>> sampleHeights;
    int violations = 0;
    float globalMin = 0.0f;
    float globalMean = 0.0f;
    float globalMax = 0.0f;
    bool valid = false;
};

struct ColoredTriangles {
    std::vector<Vec3> vertices;
    std::vector<Color> colors;
};

using EdgeKey = std::pair<int, int>;

EdgeKey sortedEdge(int a, int b)
{
    return (a < b) ? EdgeKey(a, b) : EdgeKey(b, a);
}

Eigen::Vector3d toEigen(const zSpace::zPoint& p)
{
    return Eigen::Vector3d(p.x, p.y, p.z);
}

} // namespace

class FootingSlicingComparisonSketch : public ISketch {
public:
    std::string getName() const override { return "Footing Slicing Comparison"; }
    std::string getDescription() const override { return "Caps footing.obj and compares heat-geodesic contours against tet-volume harmonic slices."; }
    std::string getAuthor() const override { return "alice2 + zspace_core"; }

    void setup() override
    {
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
        scene().setShowGrid(true);
        scene().setGridSize(8.0f);
        scene().setGridDivisions(8);
        scene().setShowAxes(true);
        scene().setAxesLength(1.5f);
        readInputMesh();

        const char* autoRun = std::getenv("ALICE2_FOOTING_COMPARE_AUTORUN");
        const char* tour = std::getenv("ALICE2_FOOTING_COMPARE_TOUR");
        m_tour = tour && *tour;
        // "1" runs the default key tour; any other value is used as the key sequence.
        m_tourKeys = (m_tour && std::string(tour) != "1") ? std::string(tour) : std::string(kDefaultTourKeys);
        if ((autoRun && std::string(autoRun) == "1") || m_tour) {
            std::cout << "[FootingCompare][autorun] running c, g, h, k, j, v, k" << std::endl;
            capHoles();
            computeHeatSlicing();
            computeHarmonicSlicing();
            printSliceMetrics();
            computeBandSlicing();
            computeVolumeHeatSlicing();
            printSliceMetrics();
        }
    }

    // Scripted key tour (ALICE2_FOOTING_COMPARE_TOUR=1 or a key string):
    // presses one key every third frame so every mode is drawn, then quits.
    void update(float) override
    {
        if (!m_tour) return;
        if (++m_tourFrame % 3 != 0) return;
        if (m_tourCursor >= m_tourKeys.size()) {
            std::cout << "[FootingCompare][tour] done after " << m_tourFrame << " frames" << std::endl;
            m_tour = false;
            Application::getInstance()->quit();
            return;
        }
        const unsigned char key = static_cast<unsigned char>(m_tourKeys[m_tourCursor++]);
        onKeyPress(key, 0, 0);
        std::cout << "[FootingCompare][tour] key '" << static_cast<char>(key) << "' -> mode " << m_debugMode
            << " (" << modeName() << ") slice=" << sliceLabel() << (m_showAll ? " all" : "") << std::endl;
    }

    void draw(Renderer& renderer, Camera&) override
    {
        switch (m_debugMode) {
        case 0: drawInput(); break;
        case 1: drawCaps(); break;
        case 2: drawHeatField(m_heatBottomMesh); break;
        case 3: drawHeatField(m_heatTopMesh); break;
        case 4: drawHeatContours(); break;
        case 5: drawTetWire(); break;
        case 6: drawPhiPoints(renderer); break;
        case 7: drawHarmonicIsos(); break;
        case 8: drawOverlay(); break;
        case 9: drawDirectionField(renderer); break;
        case 10: drawSideGradient(renderer, m_gradPhiTriangles); break;
        case 11: drawEqualisedField(renderer); break;
        case 12: drawSideGradient(renderer, m_gradPsiTriangles); break;
        case 13: drawScheduleTriangles(renderer); break;
        case 14: drawAdaptiveIsoValues(renderer); break;
        case 15: drawLayerHeightCheck(renderer); break;
        case 16: drawThreeWayOverlay(); break;
        case 17: drawVolumeHeatU(renderer); break;
        case 18: drawVolumeHeatDirection(renderer); break;
        case 19: drawVolumeDistance(renderer); break;
        case 20: drawVolumeDiff(); break;
        case 21: drawVolumeHeatIsos(); break;
        default: break;
        }

        renderer.setColor(Color(0.08f, 0.09f, 0.12f, 1.0f));
        renderer.drawString(getName(), 10, 28);
        renderer.drawString("Mode " + std::to_string(m_debugMode) + ": " + modeName()
            + " | layers=" + std::to_string(m_layerCount)
            + " slice=" + sliceLabel()
            + (m_showAll ? " (all)" : ""), 10, 50);
        renderer.drawString(m_status, 10, 72);
        renderer.drawString("r reset | c cap | g heat | h harmonic | k metrics | j band | d mode | a all | w/s slice | n iter | b height view | t overlay 1+2 | + - layers", 10, 94);
        renderer.drawString(std::string("u heat ") + (m_showHeatCurves ? "on" : "off")
            + " | i harmonic " + (m_showHarmonicCurves ? "on" : "off")
            + " | o band " + (m_showBandCurves ? "on" : "off")
            + " | l volume heat " + (m_showVolumeHeatCurves ? "on" : "off")
            + "  (boundary curves)", 10, 116);
        renderer.drawString(std::string("y harmonic surface ") + (m_showHarmonicSurfaces ? "on" : "off")
            + " | p band surface " + (m_showBandSurfaces ? "on" : "off")
            + " | m volume heat surface " + (m_showVolumeHeatSurfaces ? "on" : "off")
            + "  (no inner edges; surface heat has no volume surface)", 10, 138);
        renderer.drawString("v volume heat (Method 4: surface heat pinned on the wall, Laplace inside) | n bottom/top in modes 18-19", 10, 160);
    }

    bool onKeyPress(unsigned char key, int, int) override
    {
        switch (key) {
        case 'r': case 'R': readInputMesh(); return true;
        case 'c': case 'C': capHoles(); return true;
        case 'g': case 'G': computeHeatSlicing(); return true;
        case 'h': case 'H': computeHarmonicSlicing(); return true;
        case 'k': case 'K': printSliceMetrics(); return true;
        case 'j': case 'J': computeBandSlicing(); return true;
        case 'v': case 'V': computeVolumeHeatSlicing(); return true;
        case 'n': case 'N':
            if (isVolumeHeatFieldMode()) toggleVolumeHeatView();
            else stepEqualisationView();
            return true;
        case 'b': case 'B': cycleHeightView(); return true;
        case 'd': case 'D': cycleDebugMode(); return true;
        case 'a': case 'A': m_showAll = !m_showAll; return true;
        case 's': case 'S': case '[': case '{': changeSlice(-1); return true;
        case 'w': case 'W': case ']': case '}': changeSlice(1); return true;
        case '+': case '=': changeLayerCount(1); return true;
        case '-': case '_': changeLayerCount(-1); return true;
        case 'u': case 'U': toggleMethodCurves(0); return true;
        case 'i': case 'I': toggleMethodCurves(1); return true;
        case 'o': case 'O': toggleMethodCurves(2); return true;
        case 'l': case 'L': toggleMethodCurves(3); return true;
        case 'm': case 'M': toggleMethodSurfaces(3); return true;
        case 't': case 'T':
            m_debugMode = 8;
            m_status = "Overlay heat vs harmonic (faces only, no inner edges). y toggles harmonic surface.";
            std::cout << "[FootingCompare][t] " << m_status << std::endl;
            return true;
        case 'y': case 'Y': toggleMethodSurfaces(1); return true;
        case 'p': case 'P': toggleMethodSurfaces(2); return true;
        default: return false;
        }
    }

private:
    // Input and derived surfaces.
    zSpace::zObjMesh m_inputMesh;
    zSpace::zObjMesh m_sideMesh;
    zSpace::zObjMesh m_cappedMesh;
    zSpace::zObjMesh m_bottomCapMesh;
    zSpace::zObjMesh m_topCapMesh;
    TriangleSoup m_sideSoup;
    TriangleSoup m_cappedSoup;
    std::vector<BoundaryLoop> m_loops;
    zSpace::zIntArray m_capVertexTags;
    zSpace::zObjGraph m_bottomLoopGraph;
    zSpace::zObjGraph m_topLoopGraph;

    // Method 1: heat geodesics.
    zSpace::zFloatArray m_distBottom;
    zSpace::zFloatArray m_distTop;
    zSpace::zObjMesh m_heatBottomMesh;
    zSpace::zObjMesh m_heatTopMesh;
    zSpace::zObjGraphArray m_heatContours;

    // Method 2: harmonic tet volume.
    TetVolume m_tet;
    std::vector<TetElement> m_tetElements;
    zSpace::zFloatArray m_phi;
    zSpace::zObjGraph m_tetWireGraph;
    zSpace::zObjMeshArray m_harmonicIsoMeshes;
    zSpace::zObjGraphArray m_harmonicBoundaryGraphs;

    // Method 3: band-constrained harmonic slicing.
    std::vector<Eigen::Vector3d> m_directionField;   // unit grad phi per tet
    std::vector<zSpace::zFloatArray> m_psiIterations; // iteration 0 = phi
    zSpace::zFloatArray m_psi;
    zSpace::zFloatArray m_psiSide;
    zSpace::zIntArray m_sideToTet;
    std::vector<float> m_gradPhiSide;                 // per side triangle
    std::vector<float> m_gradPsiSide;
    ColoredTriangles m_gradPhiTriangles;
    ColoredTriangles m_gradPsiTriangles;
    zSpace::zObjMesh m_psiSideMesh;
    std::vector<float> m_adaptiveIsoValues;           // interior iso-values psi_1 .. psi_{N-1}
    std::vector<float> m_adaptiveSteps;               // d_psi_k for k = 0 .. N-1
    std::vector<float> m_scheduleGradients;           // g_k for k = 0 .. N-1
    std::vector<zSpace::zIntArray> m_scheduleTriangles; // crossed side triangles at psi_k
    zSpace::zObjGraphArray m_bandContours;
    zSpace::zObjMeshArray m_bandIsoMeshes;
    LayerHeightReport m_heightReports[4];             // heat, harmonic, band, volume heat
    zSpace::zFloatArray m_streamlineLengthSide;
    zSpace::zObjMesh m_streamlineMesh;
    float m_streamlineMin = 0.0f;
    float m_streamlineMax = 0.0f;
    int m_feasibleLayersLow = 0;
    int m_feasibleLayersHigh = 0;
    float m_gradientTarget = 0.0f;
    float m_gradScaleLow = 0.0f;
    float m_gradScaleHigh = 1.0f;
    float m_meanTetEdgeLength = 0.0f;
    float m_fittedStepHeight = 0.0f;
    int m_bandLayerCount = 0;
    int m_equalisationView = 0;
    int m_heightView = 2;                             // 0 heat, 1 harmonic, 2 band, 3 streamline length, 4 volume heat
    int m_currentBandSlice = 0;
    int m_currentGap = 0;
    bool m_havePsi = false;
    bool m_haveBand = false;

    // Method 4: surface heat distances pinned on the wall, Laplace on inner
    // tet vertices. Index 0 = bottom source, 1 = top.
    std::vector<Eigen::Vector3d> m_volumeHeatDirection[2]; // grad D / |grad D| per tet
    zSpace::zFloatArray m_volumeDist[2];              // D_b, D_t per tet vertex
    zSpace::zFloatArray m_volumeDistSide[2];          // D_b, D_t copied back to the side vertices
    float m_volumeDistMax[2] = { 0.0f, 0.0f };
    int m_volumePinned[2] = { 0, 0 };                 // wall vertices holding the surface heat distance
    int m_volumeFree[2] = { 0, 0 };                   // cap centroids and interior vertices solved by Laplace
    zSpace::zFloatArray m_volumeRatio;                // F = D_b / (D_b + D_t)
    zSpace::zObjMeshArray m_volumeHeatIsoMeshes;
    zSpace::zObjGraphArray m_volumeHeatBoundaryGraphs;
    zSpace::zObjMesh m_volumeDiffMesh;                // side coloured by |D_b (volume) - d_b (surface)|
    float m_volumeDiffMeanAbs = 0.0f;
    float m_volumeDiffMax = 0.0f;
    float m_volumeDiffShorterFraction = 0.0f;
    float m_volumeHeatEdgeLength = 0.0f;
    int m_volumeHeatView = 0;                         // 0 bottom, 1 top (modes 17-19)
    bool m_haveVolumeHeat = false;
    bool m_haveVolumeDiff = false;

    // Scripted tour state.
    bool m_tour = false;
    std::string m_tourKeys;
    int m_tourFrame = 0;
    size_t m_tourCursor = 0;

    std::vector<float> m_isoValues;
    std::string m_status = "Waiting for input.";
    int m_layerCount = kDefaultLayerCount;
    int m_debugMode = 0;
    int m_currentSlice = 0;
    bool m_showAll = false;
    bool m_showHeatCurves = true;
    bool m_showHarmonicCurves = true;
    bool m_showBandCurves = true;
    bool m_showHarmonicSurfaces = true;
    bool m_showBandSurfaces = true;
    bool m_showVolumeHeatCurves = true;
    bool m_showVolumeHeatSurfaces = true;
    bool m_loaded = false;
    bool m_haveLoops = false;
    bool m_haveCaps = false;
    bool m_haveHeat = false;
    bool m_haveVolume = false;
    bool m_havePhi = false;

    // ------------------------------------------------------------------ utilities

    static Vec3 toVec3(const zSpace::zVector& p)
    {
        return Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
    }

    static double triangleArea2(zSpace::zPoint a, zSpace::zPoint b, zSpace::zPoint c)
    {
        return ((b - a) ^ (c - a)).length();
    }

    static void clearGraph(zSpace::zObjGraph& graph)
    {
        zSpace::zFnGraph fn(graph);
        fn.clear();
    }

    static void clearMesh(zSpace::zObjMesh& mesh)
    {
        zSpace::zFnMesh fn(mesh);
        fn.clear();
    }

    static void createMeshFromSoup(const TriangleSoup& soup, zSpace::zObjMesh& mesh)
    {
        clearMesh(mesh);
        if (soup.triangles.empty()) return;
        zSpace::zPointArray positions = soup.positions;
        zSpace::zIntArray counts;
        zSpace::zIntArray connects;
        counts.reserve(soup.triangles.size());
        connects.reserve(soup.triangles.size() * 3);
        for (const auto& tri : soup.triangles) {
            counts.push_back(3);
            connects.push_back(tri[0]);
            connects.push_back(tri[1]);
            connects.push_back(tri[2]);
        }
        zSpace::zFnMesh fn(mesh);
        fn.create(positions, counts, connects);
    }

    static float graphLength(zSpace::zObjGraph& graph, int* segmentCount = nullptr)
    {
        zSpace::zFnGraph fn(graph);
        if (segmentCount) *segmentCount = fn.numEdges();
        if (fn.numVertices() == 0) return 0.0f;
        zSpace::zPointArray positions;
        zSpace::zIntArray edges;
        fn.getVertexPositions(positions);
        fn.getEdgeData(edges);
        float length = 0.0f;
        for (size_t i = 0; i + 1 < edges.size(); i += 2) {
            length += static_cast<float>((positions[edges[i + 1]] - positions[edges[i]]).length());
        }
        return length;
    }

    static double pointSegmentDistance(const zSpace::zPoint& p, const zSpace::zPoint& a, const zSpace::zPoint& b)
    {
        const Eigen::Vector3d pa = toEigen(p) - toEigen(a);
        const Eigen::Vector3d ab = toEigen(b) - toEigen(a);
        const double ab2 = ab.squaredNorm();
        const double t = (ab2 < 1e-16) ? 0.0 : std::clamp(pa.dot(ab) / ab2, 0.0, 1.0);
        return (pa - t * ab).norm();
    }

    static void buildBoundaryGraph(zSpace::zObjMesh& mesh, zSpace::zObjGraph& graph)
    {
        clearGraph(graph);
        zSpace::zFnMesh fn(mesh);
        if (fn.numVertices() == 0) return;
        zSpace::zPointArray positions;
        zSpace::zIntArray edgeConnects;
        for (zSpace::zItMeshHalfEdge he(mesh); !he.end(); he++) {
            if (!he.onBoundary()) continue;
            const int base = static_cast<int>(positions.size());
            positions.push_back(he.getStartVertex().getPosition());
            positions.push_back(he.getVertex().getPosition());
            edgeConnects.push_back(base);
            edgeConnects.push_back(base + 1);
        }
        if (!positions.empty()) {
            zSpace::zFnGraph fnGraph(graph);
            fnGraph.create(positions, edgeConnects);
        }
    }

    int interiorSliceCount() const { return std::max(0, m_layerCount - 1); }

    float isoValue(int sliceIndex) const
    {
        return static_cast<float>(sliceIndex + 1) / static_cast<float>(m_layerCount);
    }

    // ------------------------------------------------------------------ state resets

    void clearHeat()
    {
        m_distBottom.clear();
        m_distTop.clear();
        clearMesh(m_heatBottomMesh);
        clearMesh(m_heatTopMesh);
        m_heatContours.clear();
        m_haveHeat = false;
        // Method 3 diagnostics derived from the heat distances.
        m_heightReports[0] = LayerHeightReport();
        m_streamlineLengthSide.clear();
        clearMesh(m_streamlineMesh);
        m_streamlineMin = m_streamlineMax = 0.0f;
        m_feasibleLayersLow = m_feasibleLayersHigh = 0;
        // Method 4 wall comparison against the surface distances.
        clearVolumeDiff();
    }

    void clearVolumeDiff()
    {
        clearMesh(m_volumeDiffMesh);
        m_volumeDiffMeanAbs = m_volumeDiffMax = m_volumeDiffShorterFraction = 0.0f;
        m_haveVolumeDiff = false;
    }

    void clearVolumeHeat()
    {
        for (int s = 0; s < 2; ++s) {
            m_volumeHeatDirection[s].clear();
            m_volumeDist[s].clear();
            m_volumeDistSide[s].clear();
            m_volumeDistMax[s] = 0.0f;
            m_volumePinned[s] = 0;
            m_volumeFree[s] = 0;
        }
        m_volumeRatio.clear();
        m_volumeHeatIsoMeshes.clear();
        m_volumeHeatBoundaryGraphs.clear();
        clearVolumeDiff();
        m_heightReports[3] = LayerHeightReport();
        m_volumeHeatEdgeLength = 0.0f;
        m_volumeHeatView = 0;
        m_haveVolumeHeat = false;
    }

    void clearBand()
    {
        m_directionField.clear();
        m_psiIterations.clear();
        m_psi.clear();
        m_psiSide.clear();
        m_sideToTet.clear();
        m_gradPhiSide.clear();
        m_gradPsiSide.clear();
        m_gradPhiTriangles = ColoredTriangles();
        m_gradPsiTriangles = ColoredTriangles();
        clearMesh(m_psiSideMesh);
        m_adaptiveIsoValues.clear();
        m_adaptiveSteps.clear();
        m_scheduleGradients.clear();
        m_scheduleTriangles.clear();
        m_bandContours.clear();
        m_bandIsoMeshes.clear();
        for (LayerHeightReport& report : m_heightReports) report = LayerHeightReport();
        m_gradientTarget = 0.0f;
        m_gradScaleLow = 0.0f;
        m_gradScaleHigh = 1.0f;
        m_fittedStepHeight = 0.0f;
        m_bandLayerCount = 0;
        m_equalisationView = 0;
        m_currentBandSlice = 0;
        m_currentGap = 0;
        m_havePsi = false;
        m_haveBand = false;
    }

    void clearHarmonic()
    {
        clearBand();
        clearVolumeHeat(); // Method 4 lives on the same tet volume.
        m_tet = TetVolume();
        m_tetElements.clear();
        m_phi.clear();
        clearGraph(m_tetWireGraph);
        m_harmonicIsoMeshes.clear();
        m_harmonicBoundaryGraphs.clear();
        m_haveVolume = false;
        m_havePhi = false;
    }

    void clearDerived()
    {
        clearHeat();
        clearHarmonic();
        m_cappedSoup = TriangleSoup();
        clearMesh(m_cappedMesh);
        clearMesh(m_bottomCapMesh);
        clearMesh(m_topCapMesh);
        clearGraph(m_bottomLoopGraph);
        clearGraph(m_topLoopGraph);
        m_capVertexTags.clear();
        m_isoValues.clear();
        m_currentSlice = 0;
        m_haveCaps = false;
    }

    // ------------------------------------------------------------------ r: load

    void readInputMesh()
    {
        clearDerived();
        m_loops.clear();
        m_sideSoup = TriangleSoup();
        clearMesh(m_sideMesh);
        clearMesh(m_inputMesh);
        m_haveLoops = false;

        std::string message;
        m_loaded = alice2::loadMesh(kInputMeshPath, m_inputMesh, &message);
        if (!m_loaded) {
            m_status = "Input load failed: " + message;
            std::cout << "[FootingCompare][r] " << m_status << std::endl;
            return;
        }

        zSpace::zFnMesh fn(m_inputMesh);
        buildSideSoup();
        createMeshFromSoup(m_sideSoup, m_sideMesh);
        deriveBoundaryLoops();

        std::ostringstream out;
        out << "Input vertices=" << fn.numVertices() << " faces=" << fn.numPolygons()
            << " triangles=" << m_sideSoup.triangles.size()
            << " boundary loops=" << m_loops.size();
        m_status = out.str();
        m_debugMode = 0;
        std::cout << "[FootingCompare][r] " << m_status << " path=" << kInputMeshPath << std::endl;

        zSpace::zPoint minBB;
        zSpace::zPoint maxBB;
        fn.getBounds(minBB, maxBB);
        Application::getInstance()->getCameraController().focusOnBounds(toVec3(minBB), toVec3(maxBB));
    }

    // The OBJ export carries unwelded seams (duplicate positions), which read
    // as extra boundary loops. Weld coincident vertices before any topology walk.
    void buildSideSoup()
    {
        zSpace::zFnMesh fn(m_inputMesh);
        zSpace::zPointArray rawPositions;
        fn.getVertexPositions(rawPositions);

        std::map<std::array<long long, 3>, int> weldedIndex;
        std::vector<int> remap(rawPositions.size(), -1);
        const double inverseTolerance = 1.0 / kWeldTolerance;
        for (int i = 0; i < static_cast<int>(rawPositions.size()); ++i) {
            const zSpace::zPoint& p = rawPositions[i];
            const std::array<long long, 3> key = {
                static_cast<long long>(std::llround(p.x * inverseTolerance)),
                static_cast<long long>(std::llround(p.y * inverseTolerance)),
                static_cast<long long>(std::llround(p.z * inverseTolerance)) };
            const auto found = weldedIndex.find(key);
            if (found != weldedIndex.end()) {
                remap[i] = found->second;
                continue;
            }
            remap[i] = static_cast<int>(m_sideSoup.positions.size());
            weldedIndex[key] = remap[i];
            m_sideSoup.positions.push_back(p);
        }

        zSpace::zIntArray connects;
        zSpace::zIntArray counts;
        fn.getPolygonData(connects, counts);
        int cursor = 0;
        int collapsedTriangles = 0;
        for (int count : counts) {
            for (int i = 1; i + 1 < count; ++i) {
                const std::array<int, 3> tri = { remap[connects[cursor]], remap[connects[cursor + i]], remap[connects[cursor + i + 1]] };
                if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]) {
                    ++collapsedTriangles;
                    continue;
                }
                m_sideSoup.triangles.push_back(tri);
            }
            cursor += count;
        }

        std::cout << "[FootingCompare][r] welded vertices " << rawPositions.size() << " -> " << m_sideSoup.positions.size()
            << " (tolerance " << kWeldTolerance << "), collapsed triangles=" << collapsedTriangles << std::endl;
    }

    void deriveBoundaryLoops()
    {
        m_loops.clear();
        m_haveLoops = false;
        zSpace::zFnMesh fn(m_sideMesh);
        if (fn.numVertices() == 0) return;

        std::set<int> visited;
        for (zSpace::zItMeshHalfEdge he(m_sideMesh); !he.end(); he++) {
            if (!he.onBoundary() || visited.count(he.getId())) continue;

            BoundaryLoop loop;
            zSpace::zItMeshHalfEdge walker = he;
            zSpace::zPoint centroid;
            int guard = 0;
            const int maxSteps = fn.numEdges() * 2 + 1;
            do {
                visited.insert(walker.getId());
                zSpace::zItMeshVertex start = walker.getStartVertex();
                loop.vertexIds.push_back(start.getId());
                centroid += start.getPosition();
                walker = walker.getNext();
                ++guard;
            } while (walker != he && guard < maxSteps);

            if (guard >= maxSteps || loop.vertexIds.empty()) {
                std::cout << "[FootingCompare][r] WARNING boundary walk did not close from halfedge " << he.getId() << std::endl;
                continue;
            }
            loop.centroid = centroid / static_cast<double>(loop.vertexIds.size());
            m_loops.push_back(loop);
        }

        const bool loopCountOk = static_cast<int>(m_loops.size()) == kExpectedLoopCount;
        int bottomId = 0;
        for (int i = 1; i < static_cast<int>(m_loops.size()); ++i) {
            if (m_loops[i].centroid.z < m_loops[bottomId].centroid.z) bottomId = i;
        }
        for (int i = 0; i < static_cast<int>(m_loops.size()); ++i) {
            m_loops[i].tag = (i == bottomId) ? BoundaryTag::Bottom : BoundaryTag::Top;
            const BoundaryLoop& loop = m_loops[i];
            std::cout << "[FootingCompare][r] loop " << i << " vertices=" << loop.vertexIds.size()
                << " centroid=(" << loop.centroid.x << ", " << loop.centroid.y << ", " << loop.centroid.z << ")"
                << " role=" << (loopCountOk ? (loop.tag == BoundaryTag::Bottom ? "bottom" : "top") : "unassigned") << std::endl;
        }

        if (!loopCountOk) {
            std::cout << "[FootingCompare][r] ERROR expected " << kExpectedLoopCount
                << " boundary loops, found " << m_loops.size() << std::endl;
            return;
        }
        m_haveLoops = true;
    }

    // ------------------------------------------------------------------ c: cap holes

    void capHoles()
    {
        if (!m_loaded || !m_haveLoops) {
            m_status = "Cannot cap: input missing or boundary loop count != 3.";
            std::cout << "[FootingCompare][c] " << m_status << std::endl;
            return;
        }

        clearDerived();
        m_cappedSoup = m_sideSoup;
        m_capVertexTags.assign(m_cappedSoup.positions.size(), static_cast<int>(BoundaryTag::Side));

        TriangleSoup bottomCap;
        TriangleSoup topCap;
        bottomCap.positions = m_sideSoup.positions;
        topCap.positions = m_sideSoup.positions;

        for (const BoundaryLoop& loop : m_loops) {
            const int centroidId = static_cast<int>(m_cappedSoup.positions.size());
            m_cappedSoup.positions.push_back(loop.centroid);
            m_capVertexTags.push_back(static_cast<int>(loop.tag));
            for (int vertexId : loop.vertexIds) m_capVertexTags[vertexId] = static_cast<int>(loop.tag);

            TriangleSoup& capSoup = (loop.tag == BoundaryTag::Bottom) ? bottomCap : topCap;
            const int capCentroidId = static_cast<int>(capSoup.positions.size());
            capSoup.positions.push_back(loop.centroid);

            // Boundary halfedges run start->end with the open side on their
            // left, so (start, end, centroid) matches the neighbouring faces.
            const int count = static_cast<int>(loop.vertexIds.size());
            for (int i = 0; i < count; ++i) {
                const int a = loop.vertexIds[i];
                const int b = loop.vertexIds[(i + 1) % count];
                m_cappedSoup.triangles.push_back({ a, b, centroidId });
                capSoup.triangles.push_back({ a, b, capCentroidId });
            }
        }

        createMeshFromSoup(m_cappedSoup, m_cappedMesh);
        createMeshFromSoup(bottomCap, m_bottomCapMesh);
        createMeshFromSoup(topCap, m_topCapMesh);
        buildLoopGraphs();

        int openHalfEdges = 0;
        for (zSpace::zItMeshHalfEdge he(m_cappedMesh); !he.end(); he++) {
            if (he.onBoundary()) ++openHalfEdges;
        }
        int bottomCount = 0;
        int topCount = 0;
        for (int tag : m_capVertexTags) {
            if (tag == static_cast<int>(BoundaryTag::Bottom)) ++bottomCount;
            else if (tag == static_cast<int>(BoundaryTag::Top)) ++topCount;
        }

        m_haveCaps = (openHalfEdges == 0);
        std::ostringstream out;
        out << "Capped mesh vertices=" << m_cappedSoup.positions.size()
            << " triangles=" << m_cappedSoup.triangles.size()
            << " tagged bottom/top=" << bottomCount << "/" << topCount
            << " open halfedges=" << openHalfEdges;
        m_status = out.str();
        m_debugMode = 1;
        std::cout << "[FootingCompare][c] " << m_status << std::endl;
        if (!m_haveCaps) std::cout << "[FootingCompare][c] ERROR capped mesh is not closed." << std::endl;
    }

    // Closed polylines of the bottom loop and both top loops; they act as
    // layer 0 and layer N when measuring print heights.
    void buildLoopGraphs()
    {
        clearGraph(m_bottomLoopGraph);
        clearGraph(m_topLoopGraph);
        zSpace::zPointArray bottomPositions;
        zSpace::zPointArray topPositions;
        zSpace::zIntArray bottomEdges;
        zSpace::zIntArray topEdges;
        for (const BoundaryLoop& loop : m_loops) {
            const bool isBottom = loop.tag == BoundaryTag::Bottom;
            zSpace::zPointArray& positions = isBottom ? bottomPositions : topPositions;
            zSpace::zIntArray& edges = isBottom ? bottomEdges : topEdges;
            const int base = static_cast<int>(positions.size());
            const int count = static_cast<int>(loop.vertexIds.size());
            for (int i = 0; i < count; ++i) {
                positions.push_back(m_sideSoup.positions[loop.vertexIds[i]]);
                edges.push_back(base + i);
                edges.push_back(base + (i + 1) % count);
            }
        }
        if (!bottomEdges.empty()) {
            zSpace::zFnGraph fn(m_bottomLoopGraph);
            fn.create(bottomPositions, bottomEdges);
        }
        if (!topEdges.empty()) {
            zSpace::zFnGraph fn(m_topLoopGraph);
            fn.create(topPositions, topEdges);
        }
    }

    // ------------------------------------------------------------------ g: heat geodesic slicing

    static bool buildSurfaceOperators(const TriangleSoup& soup, SurfaceOperators& ops, int& degenerateTriangles)
    {
        const int n = static_cast<int>(soup.positions.size());
        ops.mass = Eigen::VectorXd::Zero(n);
        std::vector<Eigen::Triplet<double>> triplets;
        triplets.reserve(soup.triangles.size() * 12);
        double edgeLengthSum = 0.0;
        int edgeCount = 0;
        degenerateTriangles = 0;

        for (const auto& tri : soup.triangles) {
            const Eigen::Vector3d p[3] = { toEigen(soup.positions[tri[0]]), toEigen(soup.positions[tri[1]]), toEigen(soup.positions[tri[2]]) };
            const double area2 = (p[1] - p[0]).cross(p[2] - p[0]).norm();
            if (area2 < 1e-14) {
                ++degenerateTriangles;
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                ops.mass[tri[c]] += area2 / 6.0;
                edgeLengthSum += (p[(c + 1) % 3] - p[c]).norm();
                ++edgeCount;

                // Cotangent at corner c weights the opposite edge (c+1, c+2).
                const int i = tri[(c + 1) % 3];
                const int j = tri[(c + 2) % 3];
                const double cot = (p[(c + 1) % 3] - p[c]).dot(p[(c + 2) % 3] - p[c]) / area2;
                const double w = 0.5 * cot;
                triplets.emplace_back(i, j, -w);
                triplets.emplace_back(j, i, -w);
                triplets.emplace_back(i, i, w);
                triplets.emplace_back(j, j, w);
            }
        }

        if (edgeCount == 0) return false;
        ops.laplacian.resize(n, n);
        ops.laplacian.setFromTriplets(triplets.begin(), triplets.end());
        ops.meanEdgeLength = edgeLengthSum / static_cast<double>(edgeCount);
        return true;
    }

    // Heat method (Crane, Weischedel, Wardetzky): diffuse from the sources,
    // normalise the negated gradient, then recover distance from its divergence.
    static bool computeHeatGeodesics(const TriangleSoup& soup, const SurfaceOperators& ops,
        const zSpace::zIntArray& sourceIds, zSpace::zFloatArray& outDistance, std::string& error)
    {
        const int n = static_cast<int>(soup.positions.size());
        if (n == 0 || sourceIds.empty()) {
            error = "empty mesh or source set";
            return false;
        }

        const double t = ops.meanEdgeLength * ops.meanEdgeLength;
        Eigen::SparseMatrix<double> heatMatrix = ops.laplacian * t;
        for (int i = 0; i < n; ++i) heatMatrix.coeffRef(i, i) += ops.mass[i];

        Eigen::VectorXd delta = Eigen::VectorXd::Zero(n);
        for (int id : sourceIds) delta[id] = 1.0;

        Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> heatSolver;
        heatSolver.compute(heatMatrix);
        if (heatSolver.info() != Eigen::Success) {
            error = "heat factorization failed";
            return false;
        }
        const Eigen::VectorXd u = heatSolver.solve(delta);
        if (heatSolver.info() != Eigen::Success || !u.allFinite()) {
            error = "heat solve failed";
            return false;
        }

        Eigen::VectorXd divergence = Eigen::VectorXd::Zero(n);
        for (const auto& tri : soup.triangles) {
            const Eigen::Vector3d p[3] = { toEigen(soup.positions[tri[0]]), toEigen(soup.positions[tri[1]]), toEigen(soup.positions[tri[2]]) };
            const Eigen::Vector3d normalScaled = (p[1] - p[0]).cross(p[2] - p[0]);
            const double area2 = normalScaled.norm();
            if (area2 < 1e-14) continue;
            const Eigen::Vector3d normal = normalScaled / area2;

            // grad u = (1 / 2A) * sum_i u_i (N x e_i), e_i opposite vertex i.
            Eigen::Vector3d gradient = Eigen::Vector3d::Zero();
            for (int c = 0; c < 3; ++c) {
                const Eigen::Vector3d opposite = p[(c + 2) % 3] - p[(c + 1) % 3];
                gradient += u[tri[c]] * normal.cross(opposite);
            }
            gradient /= area2;
            const double gradientNorm = gradient.norm();
            if (gradientNorm < 1e-14) continue;
            const Eigen::Vector3d X = -gradient / gradientNorm;

            for (int c = 0; c < 3; ++c) {
                const Eigen::Vector3d& pi = p[c];
                const Eigen::Vector3d& pj = p[(c + 1) % 3];
                const Eigen::Vector3d& pk = p[(c + 2) % 3];
                const Eigen::Vector3d e1 = pj - pi;
                const Eigen::Vector3d e2 = pk - pi;
                const double cotAtK = (pi - pk).dot(pj - pk) / area2;
                const double cotAtJ = (pi - pj).dot(pk - pj) / area2;
                divergence[tri[c]] += 0.5 * (cotAtK * e1.dot(X) + cotAtJ * e2.dot(X));
            }
        }

        // The cotangent matrix here is the positive semi-definite weak form of
        // -Laplace, so Laplace(phi) = div X becomes L phi = -div. The source
        // loop is a Dirichlet boundary (phi = 0) so every source vertex sits
        // exactly on the zero level, matching the harmonic cap constraints.
        std::vector<int> freeIndex(n, -1);
        int freeCount = 0;
        for (int id : sourceIds) freeIndex[id] = -2;
        for (int i = 0; i < n; ++i) {
            if (freeIndex[i] == -1) freeIndex[i] = freeCount++;
        }

        Eigen::VectorXd phi = Eigen::VectorXd::Zero(n);
        if (freeCount > 0) {
            std::vector<Eigen::Triplet<double>> triplets;
            Eigen::VectorXd rhs(freeCount);
            for (int k = 0; k < ops.laplacian.outerSize(); ++k) {
                for (Eigen::SparseMatrix<double>::InnerIterator it(ops.laplacian, k); it; ++it) {
                    const int row = freeIndex[static_cast<int>(it.row())];
                    const int column = freeIndex[static_cast<int>(it.col())];
                    if (row >= 0 && column >= 0) triplets.emplace_back(row, column, it.value());
                }
            }
            for (int i = 0; i < n; ++i) {
                if (freeIndex[i] >= 0) rhs[freeIndex[i]] = -divergence[i];
            }

            Eigen::SparseMatrix<double> poissonMatrix(freeCount, freeCount);
            poissonMatrix.setFromTriplets(triplets.begin(), triplets.end());
            Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> poissonSolver;
            poissonSolver.compute(poissonMatrix);
            if (poissonSolver.info() != Eigen::Success) {
                error = "poisson factorization failed";
                return false;
            }
            const Eigen::VectorXd solution = poissonSolver.solve(rhs);
            if (poissonSolver.info() != Eigen::Success || !solution.allFinite()) {
                error = "poisson solve failed";
                return false;
            }
            for (int i = 0; i < n; ++i) {
                if (freeIndex[i] >= 0) phi[i] = solution[freeIndex[i]];
            }
        }

        if (phi.mean() < 0.0) {
            std::cout << "[FootingCompare][g] WARNING distance field orientation flipped; negating." << std::endl;
            phi = -phi;
        }

        outDistance.assign(n, 0.0f);
        for (int i = 0; i < n; ++i) outDistance[i] = static_cast<float>(std::max(0.0, phi[i]));
        return true;
    }

    void computeHeatSlicing()
    {
        if (!m_haveCaps) {
            m_status = "No caps. Press 'c' first.";
            std::cout << "[FootingCompare][g] " << m_status << std::endl;
            return;
        }

        clearHeat();
        zSpace::zIntArray bottomIds;
        zSpace::zIntArray topIds;
        for (const BoundaryLoop& loop : m_loops) {
            zSpace::zIntArray& target = (loop.tag == BoundaryTag::Bottom) ? bottomIds : topIds;
            target.insert(target.end(), loop.vertexIds.begin(), loop.vertexIds.end());
        }

        SurfaceOperators ops;
        int degenerateTriangles = 0;
        if (!buildSurfaceOperators(m_sideSoup, ops, degenerateTriangles)) {
            m_status = "Heat method: failed to build surface operators.";
            std::cout << "[FootingCompare][g] ERROR " << m_status << std::endl;
            return;
        }

        std::string error;
        if (!computeHeatGeodesics(m_sideSoup, ops, bottomIds, m_distBottom, error)) {
            m_status = "Heat method (bottom): " + error;
            std::cout << "[FootingCompare][g] ERROR " << m_status << std::endl;
            return;
        }
        if (!computeHeatGeodesics(m_sideSoup, ops, topIds, m_distTop, error)) {
            m_status = "Heat method (top): " + error;
            std::cout << "[FootingCompare][g] ERROR " << m_status << std::endl;
            return;
        }

        createMeshFromSoup(m_sideSoup, m_heatBottomMesh);
        createMeshFromSoup(m_sideSoup, m_heatTopMesh);
        alice2::colorMesh(m_heatBottomMesh, m_distBottom);
        alice2::colorMesh(m_heatTopMesh, m_distTop);
        m_haveHeat = true;

        const auto bottomRange = std::minmax_element(m_distBottom.begin(), m_distBottom.end());
        const auto topRange = std::minmax_element(m_distTop.begin(), m_distTop.end());
        std::cout << "[FootingCompare][g] sources bottom/top=" << bottomIds.size() << "/" << topIds.size()
            << " t=" << ops.meanEdgeLength * ops.meanEdgeLength
            << " degenerate triangles=" << degenerateTriangles
            << " distBottom=[" << *bottomRange.first << ", " << *bottomRange.second << "]"
            << " distTop=[" << *topRange.first << ", " << *topRange.second << "]" << std::endl;

        extractHeatContours();
        m_debugMode = 4;
        if (m_haveVolumeHeat) buildVolumeDiffMesh();
    }

    void extractHeatContours()
    {
        m_heatContours.clear();
        if (!m_haveHeat) return;

        const int sliceCount = interiorSliceCount();
        m_heatContours.assign(sliceCount, zSpace::zObjGraph());
        zSpace::zFnMesh fnSide(m_sideMesh);
        const int vertexCount = fnSide.numVertices();
        int totalSegments = 0;

        for (int k = 0; k < sliceCount; ++k) {
            // Mirrors the reference weighted blend: the zero set of
            // (1-w)*dBottom - w*dTop sits where dBottom/dTop = w/(1-w).
            const float w = isoValue(k);
            zSpace::zScalarArray scalars(vertexCount, 0.0f);
            for (int i = 0; i < vertexCount; ++i) {
                scalars[i] = (1.0f - w) * m_distBottom[i] - w * m_distTop[i];
            }

            zSpace::zPointArray positions;
            zSpace::zIntArray edgeConnects;
            zSpace::zColorArray vertexColors;
            const int precision = 3;
            fnSide.getIsoContour(scalars, 0.0f, positions, edgeConnects, vertexColors, precision, static_cast<float>(std::pow(10.0, -precision)));

            if (!positions.empty() && !edgeConnects.empty()) {
                zSpace::zFnGraph fnGraph(m_heatContours[k]);
                fnGraph.create(positions, edgeConnects);
                totalSegments += fnGraph.numEdges();
            }
            else {
                std::cout << "[FootingCompare][g] WARNING empty heat contour for slice " << (k + 1)
                    << " w=" << w << std::endl;
            }
        }

        m_currentSlice = (sliceCount == 0) ? 0 : std::clamp(m_currentSlice, 0, sliceCount - 1);
        std::ostringstream out;
        out << "Heat contours=" << sliceCount << " segments=" << totalSegments << " layers=" << m_layerCount;
        m_status = out.str();
        std::cout << "[FootingCompare][g] " << m_status << std::endl;
    }

    // ------------------------------------------------------------------ h: harmonic tet-volume slicing

    static void setTetgenFacet(tetgenio::facet& facet, const std::array<int, 3>& triangle)
    {
        facet.numberofpolygons = 1;
        facet.polygonlist = new tetgenio::polygon[1];
        facet.numberofholes = 0;
        facet.holelist = nullptr;
        tetgenio::polygon& polygon = facet.polygonlist[0];
        polygon.numberofvertices = 3;
        polygon.vertexlist = new int[3];
        polygon.vertexlist[0] = triangle[0];
        polygon.vertexlist[1] = triangle[1];
        polygon.vertexlist[2] = triangle[2];
    }

    void buildTetWireGraph()
    {
        clearGraph(m_tetWireGraph);
        static constexpr int tetEdges[6][2] = { {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3} };
        std::set<EdgeKey> uniqueEdges;
        for (const auto& tet : m_tet.tets) {
            for (const auto& edge : tetEdges) uniqueEdges.insert(sortedEdge(tet[edge[0]], tet[edge[1]]));
        }
        zSpace::zIntArray edgeConnects;
        edgeConnects.reserve(uniqueEdges.size() * 2);
        for (const EdgeKey& edge : uniqueEdges) {
            edgeConnects.push_back(edge.first);
            edgeConnects.push_back(edge.second);
        }
        if (!edgeConnects.empty()) {
            zSpace::zFnGraph fn(m_tetWireGraph);
            zSpace::zPointArray positions = m_tet.vertices;
            fn.create(positions, edgeConnects);
        }
    }

    bool buildTetVolume()
    {
        clearHarmonic();

        tetgenio input;
        tetgenio output;
        input.firstnumber = 0;
        input.numberofpoints = static_cast<int>(m_cappedSoup.positions.size());
        input.pointlist = new REAL[input.numberofpoints * 3];
        input.pointmarkerlist = new int[input.numberofpoints];
        for (int i = 0; i < input.numberofpoints; ++i) {
            const zSpace::zPoint& point = m_cappedSoup.positions[i];
            input.pointlist[i * 3 + 0] = point.x;
            input.pointlist[i * 3 + 1] = point.y;
            input.pointlist[i * 3 + 2] = point.z;
            input.pointmarkerlist[i] = m_capVertexTags[i];
        }

        input.numberoffacets = static_cast<int>(m_cappedSoup.triangles.size());
        input.facetlist = new tetgenio::facet[input.numberoffacets];
        input.facetmarkerlist = new int[input.numberoffacets];
        for (int i = 0; i < input.numberoffacets; ++i) {
            setTetgenFacet(input.facetlist[i], m_cappedSoup.triangles[i]);
            input.facetmarkerlist[i] = static_cast<int>(BoundaryTag::Side);
        }

        std::cout << "[FootingCompare][h] PLC points=" << input.numberofpoints
            << " triangles=" << input.numberoffacets << std::endl;
        try {
            char switches[] = "pYQ";
            tetrahedralize(switches, &input, &output);
        }
        catch (...) {
            m_status = "TetGen failed while tetrahedralizing the capped mesh.";
            std::cout << "[FootingCompare][h] ERROR " << m_status << std::endl;
            return false;
        }

        if (output.numberofpoints <= 0 || output.numberoftetrahedra <= 0 || output.numberofcorners < 4) {
            m_status = "TetGen returned an empty or invalid tetrahedralization.";
            std::cout << "[FootingCompare][h] ERROR " << m_status << std::endl;
            return false;
        }

        m_tet.vertices.reserve(output.numberofpoints);
        m_tet.markers.assign(output.numberofpoints, static_cast<int>(BoundaryTag::Interior));
        for (int i = 0; i < output.numberofpoints; ++i) {
            m_tet.vertices.emplace_back(output.pointlist[i * 3 + 0], output.pointlist[i * 3 + 1], output.pointlist[i * 3 + 2]);
            const int marker = output.pointmarkerlist ? output.pointmarkerlist[i] : 0;
            m_tet.markers[i] = marker;
            if (marker == static_cast<int>(BoundaryTag::Bottom)) m_tet.bottomVertexIds.push_back(i);
            else if (marker == static_cast<int>(BoundaryTag::Top)) m_tet.topVertexIds.push_back(i);
        }
        m_tet.tets.reserve(output.numberoftetrahedra);
        for (int i = 0; i < output.numberoftetrahedra; ++i) {
            const int* tet = output.tetrahedronlist + (i * output.numberofcorners);
            m_tet.tets.push_back({ tet[0], tet[1], tet[2], tet[3] });
        }

        buildTetWireGraph();
        m_haveVolume = !m_tet.tets.empty();
        std::cout << "[FootingCompare][h] tet vertices=" << m_tet.vertices.size() << " tets=" << m_tet.tets.size()
            << " constrained bottom/top=" << m_tet.bottomVertexIds.size() << "/" << m_tet.topVertexIds.size() << std::endl;
        return m_haveVolume;
    }

    // Linear tet element shared by the harmonic solve (Method 2) and the
    // gradient-equalisation Poisson solve (Method 3).
    static bool computeTetElement(const zSpace::zPointArray& vertices, const std::array<int, 4>& tet, TetElement& element)
    {
        Eigen::Matrix4d interpolation;
        Eigen::Matrix3d edgeMatrix;
        for (int local = 0; local < 4; ++local) {
            const zSpace::zPoint& point = vertices[tet[local]];
            interpolation.row(local) << 1.0, point.x, point.y, point.z;
            if (local > 0) {
                const zSpace::zPoint& origin = vertices[tet[0]];
                edgeMatrix.col(local - 1) << point.x - origin.x, point.y - origin.y, point.z - origin.z;
            }
        }
        const double determinant = edgeMatrix.determinant();
        element.valid = std::fabs(determinant) >= 1e-14;
        if (!element.valid) return false;

        element.volume = std::fabs(determinant) / 6.0;
        const Eigen::Matrix4d coefficients = interpolation.inverse();
        for (int local = 0; local < 4; ++local) element.gradients.col(local) = coefficients.block<3, 1>(1, local);
        element.stiffness = element.volume * element.gradients.transpose() * element.gradients;
        return true;
    }

    void buildTetElements(int& degenerateTets)
    {
        m_tetElements.assign(m_tet.tets.size(), TetElement());
        degenerateTets = 0;
        for (size_t t = 0; t < m_tet.tets.size(); ++t) {
            if (!computeTetElement(m_tet.vertices, m_tet.tets[t], m_tetElements[t])) ++degenerateTets;
        }
    }

    // Dirichlet solve K x = b on the tet volume. fixedValue < 0 marks a free
    // vertex. Without targetGradients this is the Laplace (harmonic) problem;
    // with them b also carries sum_t vol_t grad(N_i) . F_t, the normal
    // equations of min sum_t vol_t |grad x - F_t|^2. clampToUnit keeps the
    // [0, 1] fields of Methods 2/3 in range; distances (Method 4) pass false.
    bool solveTetDirichlet(const std::vector<double>& fixedValue, const std::vector<Eigen::Vector3d>* targetGradients,
        zSpace::zFloatArray& outField, int& freeCount, std::string& error, bool clampToUnit = true) const
    {
        const int vertexCount = static_cast<int>(m_tet.vertices.size());
        outField.assign(vertexCount, 0.0f);
        std::vector<int> freeIndex(vertexCount, -1);
        freeCount = 0;
        for (int i = 0; i < vertexCount; ++i) {
            if (fixedValue[i] < 0.0) freeIndex[i] = freeCount++;
            else outField[i] = static_cast<float>(fixedValue[i]);
        }

        std::vector<Eigen::Triplet<double>> triplets;
        Eigen::VectorXd rhs = Eigen::VectorXd::Zero(freeCount);
        for (size_t t = 0; t < m_tet.tets.size(); ++t) {
            const TetElement& element = m_tetElements[t];
            if (!element.valid) continue;
            const std::array<int, 4>& tet = m_tet.tets[t];
            Eigen::Vector4d source = Eigen::Vector4d::Zero();
            if (targetGradients) source = element.volume * (element.gradients.transpose() * (*targetGradients)[t]);

            for (int i = 0; i < 4; ++i) {
                const int row = freeIndex[tet[i]];
                if (row < 0) continue;
                for (int j = 0; j < 4; ++j) {
                    const int globalJ = tet[j];
                    const double value = element.stiffness(i, j);
                    const int column = freeIndex[globalJ];
                    if (column >= 0) triplets.emplace_back(row, column, value);
                    else rhs[row] -= value * fixedValue[globalJ];
                }
                if (targetGradients) rhs[row] += source[i];
            }
        }

        if (freeCount == 0) return true;
        Eigen::SparseMatrix<double> stiffness(freeCount, freeCount);
        stiffness.setFromTriplets(triplets.begin(), triplets.end());
        Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
        solver.compute(stiffness);
        if (solver.info() != Eigen::Success) {
            error = "factorization failed";
            return false;
        }
        const Eigen::VectorXd solution = solver.solve(rhs);
        if (solver.info() != Eigen::Success || !solution.allFinite()) {
            error = "returned an invalid field";
            return false;
        }
        for (int i = 0; i < vertexCount; ++i) {
            if (freeIndex[i] < 0) continue;
            const float value = static_cast<float>(solution[freeIndex[i]]);
            outField[i] = clampToUnit ? std::clamp(value, 0.0f, 1.0f) : value;
        }
        return true;
    }

    std::vector<double> capDirichletValues() const
    {
        std::vector<double> fixedValue(m_tet.vertices.size(), -1.0);
        for (int id : m_tet.bottomVertexIds) fixedValue[id] = 0.0;
        for (int id : m_tet.topVertexIds) fixedValue[id] = 1.0;
        return fixedValue;
    }

    bool computeHarmonicField()
    {
        int degenerateTets = 0;
        buildTetElements(degenerateTets);

        int freeCount = 0;
        std::string error;
        if (!solveTetDirichlet(capDirichletValues(), nullptr, m_phi, freeCount, error)) {
            m_status = "Harmonic solve " + error + ".";
            std::cout << "[FootingCompare][h] ERROR " << m_status << std::endl;
            return false;
        }

        m_havePhi = !m_phi.empty();
        const auto range = std::minmax_element(m_phi.begin(), m_phi.end());
        std::cout << "[FootingCompare][h] phi min/max=" << *range.first << "/" << *range.second
            << " harmonic unknowns=" << freeCount << " degenerate tets skipped=" << degenerateTets << std::endl;
        return m_havePhi;
    }

    int crossingVertex(const zSpace::zFloatArray& field, int a, int b, float iso,
        std::map<EdgeKey, int>& edgeVertices, zSpace::zPointArray& outPositions)
    {
        const EdgeKey key = sortedEdge(a, b);
        const auto found = edgeVertices.find(key);
        if (found != edgeVertices.end()) return found->second;
        const float aPhi = field[a];
        const float bPhi = field[b];
        const float denominator = bPhi - aPhi;
        const float t = (std::fabs(denominator) < 1e-8f) ? 0.5f : (iso - aPhi) / denominator;
        const zSpace::zPoint point = m_tet.vertices[a] + (m_tet.vertices[b] - m_tet.vertices[a]) * std::clamp(t, 0.0f, 1.0f);
        const int id = static_cast<int>(outPositions.size());
        outPositions.push_back(point);
        edgeVertices[key] = id;
        return id;
    }

    static bool appendTriangle(zSpace::zIntArray& connects, int a, int b, int c,
        const zSpace::zPointArray& positions, const zSpace::zVector& lowToHigh,
        std::set<std::array<int, 3>>& uniqueTriangles, int& degenerateCount, int& duplicateCount)
    {
        if (a == b || b == c || c == a || triangleArea2(positions[a], positions[b], positions[c]) < 1e-9) {
            ++degenerateCount;
            return false;
        }

        // Orient every triangle from low phi toward high phi so shared
        // halfedges are consistently directed for zObjMesh.
        zSpace::zPoint pa = positions[a];
        zSpace::zPoint pb = positions[b];
        zSpace::zPoint pc = positions[c];
        zSpace::zVector normal = (pb - pa) ^ (pc - pa);
        zSpace::zVector direction = lowToHigh;
        if ((normal * direction) < 0.0f) std::swap(b, c);

        std::array<int, 3> triangleKey = { a, b, c };
        std::sort(triangleKey.begin(), triangleKey.end());
        if (!uniqueTriangles.insert(triangleKey).second) {
            ++duplicateCount;
            return false;
        }

        connects.push_back(a);
        connects.push_back(b);
        connects.push_back(c);
        return true;
    }

    int extractIsoMeshes(const zSpace::zFloatArray& field, const std::vector<float>& isos,
        zSpace::zObjMeshArray& meshes, zSpace::zObjGraphArray& graphs, const char* tag)
    {
        meshes.clear();
        graphs.clear();
        const int sliceCount = static_cast<int>(isos.size());
        meshes.assign(sliceCount, zSpace::zObjMesh());
        graphs.assign(sliceCount, zSpace::zObjGraph());
        int totalTriangles = 0;

        for (int k = 0; k < sliceCount; ++k) {
            const float iso = isos[k];
            zSpace::zPointArray positions;
            zSpace::zIntArray connects;
            zSpace::zIntArray counts;
            std::map<EdgeKey, int> edgeVertices;
            std::set<std::array<int, 3>> uniqueTriangles;
            int degenerateCount = 0;
            int duplicateCount = 0;

            for (const auto& tet : m_tet.tets) {
                zSpace::zIntArray inside;
                zSpace::zIntArray outside;
                for (int localId = 0; localId < 4; ++localId) {
                    if (field[tet[localId]] < iso) inside.push_back(tet[localId]);
                    else outside.push_back(tet[localId]);
                }
                if (inside.empty() || outside.empty()) continue;

                zSpace::zVector lowToHigh;
                for (int lowId : inside) {
                    for (int highId : outside) lowToHigh += m_tet.vertices[highId] - m_tet.vertices[lowId];
                }

                if (inside.size() == 1 || outside.size() == 1) {
                    const bool oneInside = inside.size() == 1;
                    const int lone = oneInside ? inside[0] : outside[0];
                    const zSpace::zIntArray& other = oneInside ? outside : inside;
                    const int a = crossingVertex(field, lone, other[0], iso, edgeVertices, positions);
                    const int b = crossingVertex(field, lone, other[1], iso, edgeVertices, positions);
                    const int c = crossingVertex(field, lone, other[2], iso, edgeVertices, positions);
                    if (appendTriangle(connects, a, oneInside ? b : c, oneInside ? c : b,
                        positions, lowToHigh, uniqueTriangles, degenerateCount, duplicateCount)) counts.push_back(3);
                }
                else {
                    const int a = crossingVertex(field, inside[0], outside[0], iso, edgeVertices, positions);
                    const int b = crossingVertex(field, inside[0], outside[1], iso, edgeVertices, positions);
                    const int c = crossingVertex(field, inside[1], outside[1], iso, edgeVertices, positions);
                    const int d = crossingVertex(field, inside[1], outside[0], iso, edgeVertices, positions);
                    if (appendTriangle(connects, a, b, c, positions, lowToHigh, uniqueTriangles, degenerateCount, duplicateCount)) counts.push_back(3);
                    if (appendTriangle(connects, a, c, d, positions, lowToHigh, uniqueTriangles, degenerateCount, duplicateCount)) counts.push_back(3);
                }
            }

            if (!counts.empty()) {
                zSpace::zFnMesh fn(meshes[k]);
                fn.create(positions, counts, connects);
                buildBoundaryGraph(meshes[k], graphs[k]);
            }
            else {
                std::cout << "[FootingCompare][" << tag << "] WARNING empty iso mesh for slice " << (k + 1) << " iso=" << iso << std::endl;
            }
            totalTriangles += static_cast<int>(counts.size());
            if (degenerateCount > 0 || duplicateCount > 0) {
                std::cout << "[FootingCompare][" << tag << "] iso=" << iso << " degenerate skipped=" << degenerateCount
                    << " duplicate skipped=" << duplicateCount << std::endl;
            }
        }
        return totalTriangles;
    }

    void extractHarmonicIsoMeshes()
    {
        m_harmonicIsoMeshes.clear();
        m_harmonicBoundaryGraphs.clear();
        m_isoValues.clear();
        if (!m_havePhi) return;

        const int sliceCount = interiorSliceCount();
        m_isoValues.reserve(sliceCount);
        for (int k = 0; k < sliceCount; ++k) m_isoValues.push_back(isoValue(k));

        const int totalTriangles = extractIsoMeshes(m_phi, m_isoValues, m_harmonicIsoMeshes, m_harmonicBoundaryGraphs, "h");
        m_currentSlice = (sliceCount == 0) ? 0 : std::clamp(m_currentSlice, 0, sliceCount - 1);
        std::ostringstream out;
        out << "Harmonic iso meshes=" << sliceCount << " triangles=" << totalTriangles << " layers=" << m_layerCount;
        m_status = out.str();
        std::cout << "[FootingCompare][h] " << m_status << std::endl;
    }

    void computeHarmonicSlicing()
    {
        if (!m_haveCaps) {
            m_status = "No caps. Press 'c' first.";
            std::cout << "[FootingCompare][h] " << m_status << std::endl;
            return;
        }
        if (!buildTetVolume()) return;
        if (!computeHarmonicField()) return;
        extractHarmonicIsoMeshes();
        m_debugMode = 7;
    }

    // ------------------------------------------------------------------ k: comparison metrics

    // Deviation of every vertex of `samples` from the nearest segment of
    // `reference`. Returns false when either graph is empty.
    static bool contourDeviation(zSpace::zObjGraph& samples, zSpace::zObjGraph& reference, double& meanDeviation, double& maxDeviation)
    {
        meanDeviation = 0.0;
        maxDeviation = 0.0;
        zSpace::zFnGraph fnSamples(samples);
        zSpace::zFnGraph fnReference(reference);
        if (fnSamples.numVertices() == 0 || fnReference.numEdges() == 0) return false;

        zSpace::zPointArray samplePositions;
        zSpace::zPointArray referencePositions;
        zSpace::zIntArray referenceEdges;
        fnSamples.getVertexPositions(samplePositions);
        fnReference.getVertexPositions(referencePositions);
        fnReference.getEdgeData(referenceEdges);
        int sampleCount = 0;
        for (const zSpace::zPoint& p : samplePositions) {
            double nearest = std::numeric_limits<double>::max();
            for (size_t e = 0; e + 1 < referenceEdges.size(); e += 2) {
                nearest = std::min(nearest, pointSegmentDistance(p, referencePositions[referenceEdges[e]], referencePositions[referenceEdges[e + 1]]));
            }
            meanDeviation += nearest;
            maxDeviation = std::max(maxDeviation, nearest);
            ++sampleCount;
        }
        if (sampleCount > 0) meanDeviation /= static_cast<double>(sampleCount);
        return true;
    }

    void printSliceMetrics()
    {
        const bool haveHeatSlices = m_haveHeat && !m_heatContours.empty();
        const bool haveHarmonicSlices = m_havePhi && !m_harmonicIsoMeshes.empty();
        if (!haveHeatSlices || !haveHarmonicSlices) {
            m_status = "Metrics need both methods. Press 'g' and 'h' first.";
            std::cout << "[FootingCompare][k] " << m_status << std::endl;
            return;
        }

        const int sliceCount = interiorSliceCount();
        std::cout << "[FootingCompare][k] layers=" << m_layerCount
            << " heat slices=" << m_heatContours.size()
            << " harmonic slices=" << m_harmonicIsoMeshes.size()
            << " equal=" << (m_heatContours.size() == m_harmonicIsoMeshes.size() ? "yes" : "no") << std::endl;

        double overallMax = 0.0;
        double overallMeanSum = 0.0;
        int comparedSlices = 0;
        for (int k = 0; k < sliceCount; ++k) {
            int heatSegments = 0;
            int harmonicSegments = 0;
            const float heatLength = graphLength(m_heatContours[k], &heatSegments);
            const float harmonicLength = graphLength(m_harmonicBoundaryGraphs[k], &harmonicSegments);
            zSpace::zFnMesh fnIso(m_harmonicIsoMeshes[k]);

            double meanDeviation = 0.0;
            double maxDeviation = 0.0;
            if (contourDeviation(m_heatContours[k], m_harmonicBoundaryGraphs[k], meanDeviation, maxDeviation)) {
                overallMax = std::max(overallMax, maxDeviation);
                overallMeanSum += meanDeviation;
                ++comparedSlices;
            }

            std::cout << "[FootingCompare][k] slice " << (k + 1) << "/" << sliceCount << " iso=" << isoValue(k)
                << " | heat segments=" << heatSegments << " length=" << heatLength
                << " | harmonic triangles=" << fnIso.numPolygons() << " boundary segments=" << harmonicSegments
                << " length=" << harmonicLength
                << " | deviation mean/max=" << meanDeviation << "/" << maxDeviation << std::endl;
        }

        std::ostringstream out;
        out << "Compared " << comparedSlices << "/" << sliceCount << " slices, deviation mean="
            << (comparedSlices > 0 ? overallMeanSum / comparedSlices : 0.0) << " max=" << overallMax;
        m_status = out.str();
        m_debugMode = 8;
        std::cout << "[FootingCompare][k] " << m_status << std::endl;

        if (m_haveVolumeHeat) printVolumeHeatMetrics(sliceCount);

        if (m_haveBand || m_haveVolumeHeat) {
            measureAllLayerHeights();
            printLayerHeightReport("k");
        }
    }

    // Method 4 wall curves against Method 1 (surface heat, same blend zero
    // set) and Method 2 (harmonic), slice by slice at the shared iso-values.
    void printVolumeHeatMetrics(int sliceCount)
    {
        const int volumeSlices = static_cast<int>(m_volumeHeatBoundaryGraphs.size());
        std::cout << "[FootingCompare][k] volume heat slices=" << volumeSlices
            << " equal=" << (volumeSlices == sliceCount ? "yes" : "no") << std::endl;

        double meanSum[2] = { 0.0, 0.0 };
        double maxAll[2] = { 0.0, 0.0 };
        int compared[2] = { 0, 0 };
        const int count = std::min(sliceCount, volumeSlices);
        for (int k = 0; k < count; ++k) {
            int segments = 0;
            const float length = graphLength(m_volumeHeatBoundaryGraphs[k], &segments);
            zSpace::zFnMesh fnIso(m_volumeHeatIsoMeshes[k]);

            double meanDeviation[2] = { 0.0, 0.0 };
            double maxDeviation[2] = { 0.0, 0.0 };
            zSpace::zObjGraph* references[2] = { &m_heatContours[k], &m_harmonicBoundaryGraphs[k] };
            for (int r = 0; r < 2; ++r) {
                if (contourDeviation(m_volumeHeatBoundaryGraphs[k], *references[r], meanDeviation[r], maxDeviation[r])) {
                    meanSum[r] += meanDeviation[r];
                    maxAll[r] = std::max(maxAll[r], maxDeviation[r]);
                    ++compared[r];
                }
            }

            std::cout << "[FootingCompare][k] volume heat slice " << (k + 1) << "/" << sliceCount << " iso=" << isoValue(k)
                << " | triangles=" << fnIso.numPolygons() << " boundary segments=" << segments << " length=" << length
                << " | vs heat deviation mean/max=" << meanDeviation[0] << "/" << maxDeviation[0]
                << " | vs harmonic deviation mean/max=" << meanDeviation[1] << "/" << maxDeviation[1] << std::endl;
        }

        std::cout << "[FootingCompare][k] volume heat vs heat: compared " << compared[0] << "/" << sliceCount
            << " slices, deviation mean=" << (compared[0] > 0 ? meanSum[0] / compared[0] : 0.0) << " max=" << maxAll[0]
            << " | vs harmonic: compared " << compared[1] << "/" << sliceCount
            << " slices, deviation mean=" << (compared[1] > 0 ? meanSum[1] / compared[1] : 0.0) << " max=" << maxAll[1] << std::endl;
        if (m_haveVolumeDiff) {
            std::cout << "[FootingCompare][k] wall distance from bottom |volume - surface| mean/max=" << m_volumeDiffMeanAbs << "/" << m_volumeDiffMax
                << " volume shorter at " << (100.0f * m_volumeDiffShorterFraction) << "% of side vertices" << std::endl;
        }
    }

    // ------------------------------------------------------------------ j: band-constrained harmonic slicing (Method 3)

    static Eigen::Vector3d tetFieldGradient(const TetElement& element, const std::array<int, 4>& tet, const zSpace::zFloatArray& field)
    {
        Eigen::Vector3d gradient = Eigen::Vector3d::Zero();
        for (int local = 0; local < 4; ++local) gradient += static_cast<double>(field[tet[local]]) * element.gradients.col(local);
        return gradient;
    }

    static Eigen::Vector3d triangleFieldGradient(const TriangleSoup& soup, const std::array<int, 3>& tri, const zSpace::zFloatArray& field)
    {
        const Eigen::Vector3d p[3] = { toEigen(soup.positions[tri[0]]), toEigen(soup.positions[tri[1]]), toEigen(soup.positions[tri[2]]) };
        const Eigen::Vector3d normalScaled = (p[1] - p[0]).cross(p[2] - p[0]);
        const double area2 = normalScaled.norm();
        if (area2 < 1e-14) return Eigen::Vector3d::Zero();
        const Eigen::Vector3d normal = normalScaled / area2;
        Eigen::Vector3d gradient = Eigen::Vector3d::Zero();
        for (int c = 0; c < 3; ++c) {
            const Eigen::Vector3d opposite = p[(c + 2) % 3] - p[(c + 1) % 3];
            gradient += static_cast<double>(field[tri[c]]) * normal.cross(opposite);
        }
        return gradient / area2;
    }

    static GradientStats weightedStats(const std::vector<double>& values, const std::vector<double>& weights)
    {
        GradientStats stats;
        double weightSum = 0.0;
        double weightedSum = 0.0;
        bool first = true;
        for (size_t i = 0; i < values.size(); ++i) {
            if (weights[i] <= 0.0) continue;
            const float v = static_cast<float>(values[i]);
            if (first) {
                stats.min = stats.max = v;
                first = false;
            }
            stats.min = std::min(stats.min, v);
            stats.max = std::max(stats.max, v);
            weightSum += weights[i];
            weightedSum += weights[i] * values[i];
        }
        stats.mean = weightSum > 0.0 ? static_cast<float>(weightedSum / weightSum) : 0.0f;
        stats.spread = stats.mean > 0.0f ? (stats.max - stats.min) / stats.mean : 0.0f;
        return stats;
    }

    GradientStats tetGradientStats(const zSpace::zFloatArray& field) const
    {
        std::vector<double> values(m_tet.tets.size(), 0.0);
        std::vector<double> weights(m_tet.tets.size(), 0.0);
        for (size_t t = 0; t < m_tet.tets.size(); ++t) {
            if (!m_tetElements[t].valid) continue;
            values[t] = tetFieldGradient(m_tetElements[t], m_tet.tets[t], field).norm();
            weights[t] = m_tetElements[t].volume;
        }
        return weightedStats(values, weights);
    }

    GradientStats sideGradientStats(const std::vector<float>& perTriangle) const
    {
        std::vector<double> values(perTriangle.begin(), perTriangle.end());
        std::vector<double> weights(perTriangle.size(), 0.0);
        for (size_t t = 0; t < m_sideSoup.triangles.size(); ++t) {
            const auto& tri = m_sideSoup.triangles[t];
            weights[t] = 0.5 * triangleArea2(m_sideSoup.positions[tri[0]], m_sideSoup.positions[tri[1]], m_sideSoup.positions[tri[2]]);
        }
        return weightedStats(values, weights);
    }

    static std::string statsText(const GradientStats& stats)
    {
        std::ostringstream out;
        out << "min/mean/max=" << stats.min << "/" << stats.mean << "/" << stats.max << " spread=" << stats.spread;
        return out.str();
    }

    // TetGen -Y keeps the PLC points first and in order; verify by position and
    // fall back to a nearest search so the transfer never silently misaligns.
    bool buildSideToTetMap(const char* tag = "j")
    {
        const int sideCount = static_cast<int>(m_sideSoup.positions.size());
        const int tetCount = static_cast<int>(m_tet.vertices.size());
        m_sideToTet.assign(sideCount, -1);
        int fallbacks = 0;
        int unmatched = 0;
        for (int i = 0; i < sideCount; ++i) {
            const zSpace::zPoint& p = m_sideSoup.positions[i];
            if (i < tetCount && (m_tet.vertices[i] - p).length() < 1e-7) {
                m_sideToTet[i] = i;
                continue;
            }
            double best = std::numeric_limits<double>::max();
            for (int j = 0; j < tetCount; ++j) {
                const double d = (m_tet.vertices[j] - p).length();
                if (d < best) {
                    best = d;
                    m_sideToTet[i] = j;
                }
            }
            if (best > 1e-5) ++unmatched;
            ++fallbacks;
        }
        std::cout << "[FootingCompare][" << tag << "] side->tet vertex map: direct=" << (sideCount - fallbacks)
            << " nearest-search=" << fallbacks << " unmatched=" << unmatched << std::endl;
        if (unmatched > 0) {
            m_status = "Side vertices could not be matched to tet vertices.";
            std::cout << "[FootingCompare][" << tag << "] ERROR " << m_status << std::endl;
            return false;
        }
        return true;
    }

    void transferToSide(const zSpace::zFloatArray& tetField, zSpace::zFloatArray& sideField) const
    {
        sideField.assign(m_sideToTet.size(), 0.0f);
        for (size_t i = 0; i < m_sideToTet.size(); ++i) sideField[i] = tetField[m_sideToTet[i]];
    }

    void computeSideGradients(const zSpace::zFloatArray& sideField, std::vector<float>& perTriangle) const
    {
        perTriangle.assign(m_sideSoup.triangles.size(), 0.0f);
        for (size_t t = 0; t < m_sideSoup.triangles.size(); ++t) {
            perTriangle[t] = static_cast<float>(triangleFieldGradient(m_sideSoup, m_sideSoup.triangles[t], sideField).norm());
        }
    }

    // Mean edge length over the valid tets: the length scale for the
    // direction-field glyphs (Methods 3 and 4).
    // Non-const because zVector::operator- is not const-qualified.
    float meanTetEdgeLength()
    {
        double edgeLengthSum = 0.0;
        int edgeCount = 0;
        static constexpr int tetEdges[6][2] = { {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3} };
        for (size_t t = 0; t < m_tet.tets.size(); ++t) {
            if (!m_tetElements[t].valid) continue;
            for (const auto& edge : tetEdges) {
                edgeLengthSum += (m_tet.vertices[m_tet.tets[t][edge[0]]] - m_tet.vertices[m_tet.tets[t][edge[1]]]).length();
                ++edgeCount;
            }
        }
        return edgeCount > 0 ? static_cast<float>(edgeLengthSum / edgeCount) : 1.0f;
    }

    // Stage A: pull |grad psi| toward a constant g along the harmonic flow
    // direction while keeping psi = 0 on the bottom cap and psi = 1 on the tops.
    bool computeEqualisedField()
    {
        const size_t tetCount = m_tet.tets.size();
        m_directionField.assign(tetCount, Eigen::Vector3d::Zero());
        m_psiIterations.clear();
        m_havePsi = false;

        double weightedGradient = 0.0;
        double volumeSum = 0.0;
        for (size_t t = 0; t < tetCount; ++t) {
            const TetElement& element = m_tetElements[t];
            if (!element.valid) continue;
            const Eigen::Vector3d gradient = tetFieldGradient(element, m_tet.tets[t], m_phi);
            const double magnitude = gradient.norm();
            if (magnitude > 1e-12) m_directionField[t] = gradient / magnitude;
            weightedGradient += element.volume * magnitude;
            volumeSum += element.volume;
        }
        m_meanTetEdgeLength = meanTetEdgeLength();
        // Least-squares optimal constant for the harmonic field itself; the
        // Dirichlet range [0, 1] fixes the scale so this is the target that
        // the boundaries can actually accommodate.
        m_gradientTarget = volumeSum > 0.0 ? static_cast<float>(weightedGradient / volumeSum) : 1.0f;

        std::cout << "[FootingCompare][j] stage A: gradient target g=" << m_gradientTarget
            << " (volume-weighted mean |grad phi|)";
        if (m_haveHeat && !m_distBottom.empty()) {
            double lengthSum = 0.0;
            for (size_t i = 0; i < m_distBottom.size(); ++i) lengthSum += m_distBottom[i] + m_distTop[i];
            std::cout << " 1/meanStreamline=" << (m_distBottom.size() / lengthSum);
        }
        std::cout << " iterations=" << kEqualisationIterations << std::endl;

        m_psiIterations.push_back(m_phi);
        std::cout << "[FootingCompare][j] stage A: iteration 0 (phi) |grad| tets " << statsText(tetGradientStats(m_phi)) << std::endl;

        const std::vector<double> fixedValue = capDirichletValues();
        std::vector<Eigen::Vector3d> targets(tetCount, Eigen::Vector3d::Zero());
        for (int iteration = 1; iteration <= kEqualisationIterations; ++iteration) {
            const zSpace::zFloatArray& current = m_psiIterations.back();
            for (size_t t = 0; t < tetCount; ++t) {
                if (!m_tetElements[t].valid) continue;
                Eigen::Vector3d direction = m_directionField[t];
                if (iteration > 1) {
                    const Eigen::Vector3d gradient = tetFieldGradient(m_tetElements[t], m_tet.tets[t], current);
                    const double magnitude = gradient.norm();
                    direction = Eigen::Vector3d::Zero();
                    if (magnitude > 1e-12) direction = gradient / magnitude;
                }
                targets[t] = static_cast<double>(m_gradientTarget) * direction;
            }

            zSpace::zFloatArray psi;
            int freeCount = 0;
            std::string error;
            if (!solveTetDirichlet(fixedValue, &targets, psi, freeCount, error)) {
                m_status = "Equalisation solve (iteration " + std::to_string(iteration) + ") " + error + ".";
                std::cout << "[FootingCompare][j] ERROR " << m_status << std::endl;
                return false;
            }
            m_psiIterations.push_back(psi);
            std::cout << "[FootingCompare][j] stage A: iteration " << iteration << " |grad| tets "
                << statsText(tetGradientStats(psi)) << " unknowns=" << freeCount << std::endl;
        }

        m_psi = m_psiIterations.back();
        m_equalisationView = static_cast<int>(m_psiIterations.size()) - 1;
        m_havePsi = true;
        return true;
    }

    static Color gradientColor(float value, float low, float high)
    {
        const float t = (high > low) ? std::clamp((value - low) / (high - low), 0.0f, 1.0f) : 0.0f;
        // steep (thin layers) = red, flat (thick layers) = green
        return Color(t, 1.0f - t, 0.0f, 1.0f);
    }

    void buildColoredTriangles(const std::vector<float>& perTriangle, ColoredTriangles& out) const
    {
        out.vertices.clear();
        out.colors.clear();
        out.vertices.reserve(perTriangle.size() * 3);
        out.colors.reserve(perTriangle.size() * 3);
        for (size_t t = 0; t < m_sideSoup.triangles.size() && t < perTriangle.size(); ++t) {
            const Color color = gradientColor(perTriangle[t], m_gradScaleLow, m_gradScaleHigh);
            for (int c = 0; c < 3; ++c) {
                out.vertices.push_back(toVec3(m_sideSoup.positions[m_sideSoup.triangles[t][c]]));
                out.colors.push_back(color);
            }
        }
    }

    void refreshEqualisationView()
    {
        if (m_psiIterations.empty()) return;
        m_equalisationView = std::clamp(m_equalisationView, 0, static_cast<int>(m_psiIterations.size()) - 1);
        zSpace::zFloatArray sideField;
        transferToSide(m_psiIterations[m_equalisationView], sideField);
        zSpace::zFnMesh fn(m_psiSideMesh);
        if (fn.numVertices() == 0) createMeshFromSoup(m_sideSoup, m_psiSideMesh);
        alice2::colorMesh(m_psiSideMesh, sideField);
    }

    // Gradient statistic on the side triangles crossed by the contour at iso.
    double contourGradient(double iso, zSpace::zIntArray& crossed) const
    {
        crossed.clear();
        double sum = 0.0;
        double weightSum = 0.0;
        double minimum = std::numeric_limits<double>::max();
        const double gradientFloor = 1e-4 * m_gradientTarget;
        for (size_t t = 0; t < m_sideSoup.triangles.size(); ++t) {
            const auto& tri = m_sideSoup.triangles[t];
            const float a = m_psiSide[tri[0]];
            const float b = m_psiSide[tri[1]];
            const float c = m_psiSide[tri[2]];
            const double low = std::min({ a, b, c });
            const double high = std::max({ a, b, c });
            if (iso < low || iso > high || high <= low) continue;
            crossed.push_back(static_cast<int>(t));
            const double g = m_gradPsiSide[t];
            if (g < gradientFloor) continue;
            const double area = 0.5 * triangleArea2(m_sideSoup.positions[tri[0]], m_sideSoup.positions[tri[1]], m_sideSoup.positions[tri[2]]);
            sum += area * g;
            weightSum += area;
            minimum = std::min(minimum, g);
        }
        if (weightSum <= 0.0) return sideGradientStats(m_gradPsiSide).mean;
        return kGradientStatUseMin ? minimum : sum / weightSum;
    }

    // One schedule pass. layerCount <= 0: free-running with d_psi = h g_k,
    // returns the fractional layer count needed to reach psi = 1. Otherwise the
    // TAGAS blend (1-a)(1-psi)/N_remaining + a h g_k for exactly layerCount
    // steps, returning the field value reached.
    double runSchedule(double stepHeight, int layerCount, ScheduleRun& run) const
    {
        run = ScheduleRun();
        double psi = 0.0;
        int k = 0;
        while (true) {
            zSpace::zIntArray crossed;
            const double g = contourGradient(psi, crossed);
            run.isoValues.push_back(static_cast<float>(psi));
            run.gradients.push_back(static_cast<float>(g));
            run.crossedTriangles.push_back(std::move(crossed));
            const double adaptive = std::max(stepHeight * g, 1e-9);
            if (layerCount <= 0) {
                if (psi + adaptive >= 1.0 || k >= kMaxLayerCount) return k + (1.0 - psi) / adaptive;
                psi += adaptive;
            }
            else {
                const int remaining = layerCount - k;
                if (remaining <= 0) return psi;
                const double uniform = (1.0 - psi) / remaining;
                psi += (1.0 - kCoverageBlend) * uniform + kCoverageBlend * adaptive;
            }
            ++k;
        }
    }

    // Stage B: adaptive iso-value schedule on the printed (side) surface.
    bool computeAdaptiveIsoValues()
    {
        zSpace::zFloatArray phiSide;
        transferToSide(m_phi, phiSide);
        transferToSide(m_psi, m_psiSide);
        computeSideGradients(phiSide, m_gradPhiSide);
        computeSideGradients(m_psiSide, m_gradPsiSide);

        const GradientStats phiStats = sideGradientStats(m_gradPhiSide);
        const GradientStats psiStats = sideGradientStats(m_gradPsiSide);
        m_gradScaleLow = std::min(phiStats.min, psiStats.min);
        m_gradScaleHigh = std::max(phiStats.max, psiStats.max);
        buildColoredTriangles(m_gradPhiSide, m_gradPhiTriangles);
        buildColoredTriangles(m_gradPsiSide, m_gradPsiTriangles);
        refreshEqualisationView();
        std::cout << "[FootingCompare][j] stage B: side |grad phi| " << statsText(phiStats) << std::endl;
        std::cout << "[FootingCompare][j] stage B: side |grad psi| " << statsText(psiStats) << std::endl;

        // Diagnostic only: how many layers the target height alone would ask for.
        ScheduleRun freeRun;
        const double rawLayerCount = runSchedule(kTargetLayerHeight, 0, freeRun);
        // Method 3 uses the same layer count as Methods 1 and 2 so all three
        // produce the same number of slices; the step height is fitted to N.
        m_bandLayerCount = m_layerCount;

        // Fit the step height so exactly N blended steps land on psi = 1.
        double low = 0.02 * kTargetLayerHeight;
        double high = 20.0 * kTargetLayerHeight;
        ScheduleRun run;
        double fitted = kTargetLayerHeight;
        for (int iteration = 0; iteration < 80; ++iteration) {
            fitted = 0.5 * (low + high);
            const double reached = runSchedule(fitted, m_bandLayerCount, run);
            if (std::fabs(reached - 1.0) < 1e-7) break;
            if (reached < 1.0) low = fitted;
            else high = fitted;
        }
        m_fittedStepHeight = static_cast<float>(fitted);
        if (static_cast<int>(run.isoValues.size()) < m_bandLayerCount + 1) {
            m_status = "Adaptive schedule did not produce the expected layer count.";
            std::cout << "[FootingCompare][j] ERROR " << m_status << " isoValues=" << run.isoValues.size() << std::endl;
            return false;
        }

        m_adaptiveIsoValues.clear();
        m_adaptiveSteps.clear();
        m_scheduleGradients.assign(run.gradients.begin(), run.gradients.begin() + m_bandLayerCount);
        m_scheduleTriangles.assign(run.crossedTriangles.begin(), run.crossedTriangles.begin() + m_bandLayerCount);
        const float uniformStep = 1.0f / static_cast<float>(m_bandLayerCount);
        std::cout << "[FootingCompare][j] stage B: h=" << kTargetLayerHeight << " (would need " << rawLayerCount << " layers)"
            << " N=" << m_bandLayerCount << " (same as Methods 1/2) fitted step height=" << m_fittedStepHeight
            << " coverage blend=" << kCoverageBlend << " stat=" << (kGradientStatUseMin ? "min" : "mean") << std::endl;
        for (int k = 0; k < m_bandLayerCount; ++k) {
            const float from = run.isoValues[k];
            const float to = (k + 1 == m_bandLayerCount) ? 1.0f : std::clamp(run.isoValues[k + 1], 0.0f, 1.0f);
            m_adaptiveSteps.push_back(to - from);
            if (k + 1 < m_bandLayerCount) m_adaptiveIsoValues.push_back(to);
            std::cout << "[FootingCompare][j] schedule k=" << k << " psi=" << from << " g_k=" << run.gradients[k]
                << " crossed=" << run.crossedTriangles[k].size() << " d_psi=" << (to - from)
                << " uniform=" << uniformStep << " ratio=" << ((to - from) / uniformStep) << std::endl;
        }
        m_currentBandSlice = 0;
        return true;
    }

    void extractBandContours()
    {
        m_bandContours.clear();
        const int sliceCount = static_cast<int>(m_adaptiveIsoValues.size());
        m_bandContours.assign(sliceCount, zSpace::zObjGraph());
        zSpace::zFnMesh fnSide(m_sideMesh);
        const int vertexCount = fnSide.numVertices();
        int totalSegments = 0;
        int emptyContours = 0;

        for (int k = 0; k < sliceCount; ++k) {
            const float iso = m_adaptiveIsoValues[k];
            zSpace::zScalarArray scalars(vertexCount, 0.0f);
            for (int i = 0; i < vertexCount; ++i) scalars[i] = m_psiSide[i] - iso;

            zSpace::zPointArray positions;
            zSpace::zIntArray edgeConnects;
            zSpace::zColorArray vertexColors;
            const int precision = 3;
            fnSide.getIsoContour(scalars, 0.0f, positions, edgeConnects, vertexColors, precision, static_cast<float>(std::pow(10.0, -precision)));

            if (!positions.empty() && !edgeConnects.empty()) {
                zSpace::zFnGraph fnGraph(m_bandContours[k]);
                fnGraph.create(positions, edgeConnects);
                totalSegments += fnGraph.numEdges();
            }
            else {
                ++emptyContours;
                std::cout << "[FootingCompare][j] WARNING empty band contour for slice " << (k + 1) << " psi=" << iso << std::endl;
            }
        }
        std::cout << "[FootingCompare][j] band contours=" << sliceCount << " segments=" << totalSegments
            << " empty=" << emptyContours << " layers=" << m_bandLayerCount << std::endl;
    }

    void extractBandIsoMeshes()
    {
        m_bandIsoMeshes.clear();
        if (!m_havePsi || m_adaptiveIsoValues.empty()) return;
        zSpace::zObjGraphArray unusedBoundaries;
        const int totalTriangles = extractIsoMeshes(m_psi, m_adaptiveIsoValues, m_bandIsoMeshes, unusedBoundaries, "j");
        std::cout << "[FootingCompare][j] band iso meshes=" << m_bandIsoMeshes.size()
            << " triangles=" << totalTriangles << " layers=" << m_bandLayerCount << std::endl;
    }

    // Stage C: print height between consecutive layers, sampled on the upper
    // layer against the nearest segment of the layer below. Layer 0 is the
    // bottom loop and the last layer is the pair of top loops.
    void measureLayerHeights(zSpace::zObjGraphArray& contours, LayerHeightReport& report)
    {
        report = LayerHeightReport();
        struct Layer {
            zSpace::zPointArray positions;
            zSpace::zIntArray edges;
        };
        auto readGraph = [](zSpace::zObjGraph& graph, Layer& layer) {
            zSpace::zFnGraph fn(graph);
            if (fn.numVertices() == 0) return false;
            fn.getVertexPositions(layer.positions);
            fn.getEdgeData(layer.edges);
            return !layer.edges.empty();
        };

        std::vector<Layer> layers;
        layers.emplace_back();
        if (!readGraph(m_bottomLoopGraph, layers.back())) return;
        for (zSpace::zObjGraph& contour : contours) {
            layers.emplace_back();
            if (!readGraph(contour, layers.back())) layers.pop_back();
        }
        layers.emplace_back();
        if (!readGraph(m_topLoopGraph, layers.back())) return;
        if (layers.size() < 2) return;

        double globalSum = 0.0;
        int globalCount = 0;
        for (size_t g = 1; g < layers.size(); ++g) {
            const Layer& below = layers[g - 1];
            const Layer& above = layers[g];
            LayerGapStats stats;
            std::vector<float> heights(above.positions.size(), 0.0f);
            double sum = 0.0;
            double worstDeviation = -1.0;
            for (size_t i = 0; i < above.positions.size(); ++i) {
                double nearest = std::numeric_limits<double>::max();
                for (size_t e = 0; e + 1 < below.edges.size(); e += 2) {
                    nearest = std::min(nearest, pointSegmentDistance(above.positions[i], below.positions[below.edges[e]], below.positions[below.edges[e + 1]]));
                }
                const float h = static_cast<float>(nearest);
                heights[i] = h;
                if (stats.sampleCount == 0) stats.minHeight = stats.maxHeight = h;
                stats.minHeight = std::min(stats.minHeight, h);
                stats.maxHeight = std::max(stats.maxHeight, h);
                sum += h;
                ++stats.sampleCount;
                const double deviation = std::fabs(h - kTargetLayerHeight);
                if (deviation > worstDeviation) {
                    worstDeviation = deviation;
                    stats.worstHeight = h;
                    stats.worstPoint = above.positions[i];
                }
            }
            stats.meanHeight = stats.sampleCount > 0 ? static_cast<float>(sum / stats.sampleCount) : 0.0f;
            stats.inBand = stats.sampleCount > 0 && stats.minHeight >= kLayerHeightBandMin && stats.maxHeight <= kLayerHeightBandMax;
            if (!stats.inBand) ++report.violations;
            if (report.gaps.empty()) {
                report.globalMin = stats.minHeight;
                report.globalMax = stats.maxHeight;
            }
            report.globalMin = std::min(report.globalMin, stats.minHeight);
            report.globalMax = std::max(report.globalMax, stats.maxHeight);
            globalSum += sum;
            globalCount += stats.sampleCount;
            report.gaps.push_back(stats);
            report.samplePositions.push_back(above.positions);
            report.sampleEdges.push_back(above.edges);
            report.sampleHeights.push_back(std::move(heights));
        }
        report.globalMean = globalCount > 0 ? static_cast<float>(globalSum / globalCount) : 0.0f;
        report.valid = !report.gaps.empty();
    }

    void measureAllLayerHeights()
    {
        if (m_haveHeat && !m_heatContours.empty()) measureLayerHeights(m_heatContours, m_heightReports[0]);
        if (m_havePhi && !m_harmonicBoundaryGraphs.empty()) measureLayerHeights(m_harmonicBoundaryGraphs, m_heightReports[1]);
        if (!m_bandContours.empty()) measureLayerHeights(m_bandContours, m_heightReports[2]);
        if (m_haveVolumeHeat && !m_volumeHeatBoundaryGraphs.empty()) measureLayerHeights(m_volumeHeatBoundaryGraphs, m_heightReports[3]);
    }

    // Feasibility: every streamline of length L must be split into N layers
    // inside [h_min, h_max], so ceil(L_max / h_max) <= N <= floor(L_min / h_min).
    void computeStreamlineDiagnostic()
    {
        m_streamlineLengthSide.clear();
        clearMesh(m_streamlineMesh);
        m_streamlineMin = m_streamlineMax = 0.0f;
        m_feasibleLayersLow = m_feasibleLayersHigh = 0;
        if (!m_haveHeat || m_distBottom.size() != m_sideSoup.positions.size()) {
            std::cout << "[FootingCompare][j] streamline diagnostic skipped: Method 1 distances missing (press g)." << std::endl;
            return;
        }
        m_streamlineLengthSide.resize(m_distBottom.size());
        for (size_t i = 0; i < m_distBottom.size(); ++i) m_streamlineLengthSide[i] = m_distBottom[i] + m_distTop[i];
        const auto range = std::minmax_element(m_streamlineLengthSide.begin(), m_streamlineLengthSide.end());
        m_streamlineMin = *range.first;
        m_streamlineMax = *range.second;
        m_feasibleLayersLow = static_cast<int>(std::ceil(m_streamlineMax / kLayerHeightBandMax));
        m_feasibleLayersHigh = static_cast<int>(std::floor(m_streamlineMin / kLayerHeightBandMin));
        createMeshFromSoup(m_sideSoup, m_streamlineMesh);
        alice2::colorMesh(m_streamlineMesh, m_streamlineLengthSide);
    }

    static constexpr int kMethodCount = 4;

    static const char* methodName(int method)
    {
        static const char* names[kMethodCount] = { "heat", "harmonic", "band", "volume heat" };
        return names[std::clamp(method, 0, kMethodCount - 1)];
    }

    void printLayerHeightReport(const char* tag) const
    {
        const std::string prefix = std::string("[FootingCompare][") + tag + "] ";
        std::cout << prefix << "print height target h=" << kTargetLayerHeight
            << " band=[" << kLayerHeightBandMin << ", " << kLayerHeightBandMax << "]" << std::endl;
        for (int method = 0; method < kMethodCount; ++method) {
            const LayerHeightReport& report = m_heightReports[method];
            if (!report.valid) {
                std::cout << prefix << methodName(method) << ": no layer height report." << std::endl;
                continue;
            }
            std::cout << prefix << methodName(method) << ": layers=" << report.gaps.size()
                << " violations=" << report.violations << "/" << report.gaps.size()
                << " height min/mean/max=" << report.globalMin << "/" << report.globalMean << "/" << report.globalMax << std::endl;
            for (size_t g = 0; g < report.gaps.size(); ++g) {
                const LayerGapStats& stats = report.gaps[g];
                std::cout << prefix << methodName(method) << " gap " << (g + 1) << "/" << report.gaps.size()
                    << " min/mean/max=" << stats.minHeight << "/" << stats.meanHeight << "/" << stats.maxHeight
                    << " in-band=" << (stats.inBand ? "yes" : "NO")
                    << " worst=" << stats.worstHeight << " at (" << stats.worstPoint.x << ", " << stats.worstPoint.y << ", " << stats.worstPoint.z << ")"
                    << std::endl;
            }
        }
        if (!m_streamlineLengthSide.empty()) {
            std::cout << prefix << "streamline length (d_bottom + d_top) min/max=" << m_streamlineMin << "/" << m_streamlineMax
                << " feasible N in [" << m_feasibleLayersLow << ", " << m_feasibleLayersHigh << "]";
            if (m_feasibleLayersLow > m_feasibleLayersHigh) {
                std::cout << " EMPTY: no conforming schedule satisfies the band; longest/shortest streamline ratio="
                    << (m_streamlineMin > 0.0f ? m_streamlineMax / m_streamlineMin : 0.0f)
                    << " exceeds h_max/h_min=" << (kLayerHeightBandMax / kLayerHeightBandMin);
            }
            else {
                std::cout << " chosen N=" << m_bandLayerCount
                    << (m_bandLayerCount >= m_feasibleLayersLow && m_bandLayerCount <= m_feasibleLayersHigh ? " (inside)" : " (outside)");
            }
            std::cout << std::endl;
        }
    }

    void computeBandSlicing()
    {
        if (!m_havePhi || m_tetElements.empty()) {
            m_status = "Method 3 needs the harmonic field. Press 'h' first.";
            std::cout << "[FootingCompare][j] " << m_status << std::endl;
            return;
        }
        clearBand();
        if (!buildSideToTetMap()) return;
        if (!computeEqualisedField()) return;
        if (!computeAdaptiveIsoValues()) return;
        extractBandContours();
        extractBandIsoMeshes();
        computeStreamlineDiagnostic();
        measureAllLayerHeights();
        m_haveBand = !m_bandContours.empty();
        printLayerHeightReport("j");

        const LayerHeightReport& report = m_heightReports[2];
        std::ostringstream out;
        out << "Band slicing: N=" << m_bandLayerCount << " contours=" << m_bandContours.size()
            << " fitted h=" << m_fittedStepHeight << " (target " << kTargetLayerHeight << ")"
            << " height min/mean/max=" << report.globalMin << "/" << report.globalMean << "/" << report.globalMax
            << " violations=" << report.violations << "/" << report.gaps.size();
        m_status = out.str();
        m_debugMode = 14;
        std::cout << "[FootingCompare][j] " << m_status << std::endl;
    }

    // ------------------------------------------------------------------ v: surface heat extended into the tet volume (Method 4)

    // Copy one surface heat distance onto the matching tet vertices and fill
    // every vertex that is not on the wall by Laplace. Source cap centroids
    // (bottom fan for D_b, top fans for D_t) are pinned to 0, the same value
    // the heat method assigns to the source loop. The wall is never solved,
    // so an iso-surface of F = D_b/(D_b+D_t) meets the wall on the Method 1 contour.
    bool extendSurfaceDistance(const zSpace::zFloatArray& surfaceDistance, int sourceTag, const char* label,
        zSpace::zFloatArray& distance, std::vector<Eigen::Vector3d>& direction, float& distanceMax,
        int& pinnedCount, int& freeCount, std::string& error)
    {
        const int vertexCount = static_cast<int>(m_tet.vertices.size());
        std::vector<double> fixedValue(vertexCount, -1.0);
        pinnedCount = 0;
        for (int i = 0; i < static_cast<int>(m_sideToTet.size()) && i < static_cast<int>(surfaceDistance.size()); ++i) {
            const int tetId = m_sideToTet[i];
            if (tetId < 0 || tetId >= vertexCount) continue;
            if (fixedValue[tetId] < 0.0) ++pinnedCount;
            fixedValue[tetId] = surfaceDistance[i];
        }
        int capPins = 0;
        for (int i = 0; i < vertexCount; ++i) {
            if (fixedValue[i] >= 0.0) continue;
            if (i < static_cast<int>(m_tet.markers.size()) && m_tet.markers[i] == sourceTag) {
                fixedValue[i] = 0.0;
                ++capPins;
            }
        }

        freeCount = 0;
        if (!solveTetDirichlet(fixedValue, nullptr, distance, freeCount, error, false)) return false;

        float rawMin = 0.0f;
        int negativeCount = 0;
        distanceMax = 0.0f;
        for (float& d : distance) {
            rawMin = std::min(rawMin, d);
            if (d < 0.0f) {
                ++negativeCount;
                d = 0.0f;
            }
            distanceMax = std::max(distanceMax, d);
        }

        direction.assign(m_tet.tets.size(), Eigen::Vector3d::Zero());
        int zeroGradients = 0;
        for (size_t t = 0; t < m_tet.tets.size(); ++t) {
            if (!m_tetElements[t].valid) continue;
            const Eigen::Vector3d gradient = tetFieldGradient(m_tetElements[t], m_tet.tets[t], distance);
            const double magnitude = gradient.norm();
            if (magnitude > 1e-12) direction[t] = gradient / magnitude;
            else ++zeroGradients;
        }

        std::cout << "[FootingCompare][v] " << label << ": wall vertices pinned to surface heat=" << pinnedCount
            << " source cap centroids pinned 0=" << capPins
            << " inner unknowns=" << freeCount
            << " D min/max=" << rawMin << "/" << distanceMax
            << " negative clamped=" << negativeCount
            << " zero-gradient tets=" << zeroGradients << std::endl;
        return true;
    }

    void extractVolumeHeatIsoMeshes()
    {
        m_volumeHeatIsoMeshes.clear();
        m_volumeHeatBoundaryGraphs.clear();
        if (m_volumeRatio.empty()) return;

        const int sliceCount = interiorSliceCount();
        std::vector<float> isos;
        isos.reserve(sliceCount);
        for (int k = 0; k < sliceCount; ++k) isos.push_back(isoValue(k));

        const int totalTriangles = extractIsoMeshes(m_volumeRatio, isos, m_volumeHeatIsoMeshes, m_volumeHeatBoundaryGraphs, "v");
        m_currentSlice = (sliceCount == 0) ? 0 : std::clamp(m_currentSlice, 0, sliceCount - 1);
        std::cout << "[FootingCompare][v] volume heat iso meshes=" << sliceCount << " triangles=" << totalTriangles
            << " layers=" << m_layerCount << std::endl;
    }

    // Wall comparison: the volume distances transferred to the side vertices
    // against the surface heat distances of Method 1 (same vertex indexing).
    void buildVolumeDiffMesh()
    {
        clearVolumeDiff();
        if (!m_haveHeat || m_distBottom.size() != m_sideSoup.positions.size() || m_distTop.size() != m_distBottom.size()) {
            std::cout << "[FootingCompare][v] wall comparison skipped: Method 1 distances missing (press g)." << std::endl;
            return;
        }
        const size_t count = m_distBottom.size();
        if (count == 0 || m_volumeDistSide[0].size() != count || m_volumeDistSide[1].size() != count) return;

        const zSpace::zFloatArray* surface[2] = { &m_distBottom, &m_distTop };
        zSpace::zFloatArray absDiffBottom(count, 0.0f);
        double sumAbs[2] = { 0.0, 0.0 };
        float maxAbs[2] = { 0.0f, 0.0f };
        int shorter[2] = { 0, 0 };
        for (int s = 0; s < 2; ++s) {
            for (size_t i = 0; i < count; ++i) {
                const float diff = m_volumeDistSide[s][i] - (*surface[s])[i];
                const float magnitude = std::fabs(diff);
                sumAbs[s] += magnitude;
                maxAbs[s] = std::max(maxAbs[s], magnitude);
                if (diff < -1e-6f) ++shorter[s];
                if (s == 0) absDiffBottom[i] = magnitude;
            }
        }
        m_volumeDiffMeanAbs = static_cast<float>(sumAbs[0] / static_cast<double>(count));
        m_volumeDiffMax = maxAbs[0];
        m_volumeDiffShorterFraction = static_cast<float>(shorter[0]) / static_cast<float>(count);
        createMeshFromSoup(m_sideSoup, m_volumeDiffMesh);
        alice2::colorMesh(m_volumeDiffMesh, absDiffBottom);
        m_haveVolumeDiff = true;

        std::cout << "[FootingCompare][v] wall |D_volume - d_surface|: bottom mean/max=" << m_volumeDiffMeanAbs << "/" << m_volumeDiffMax
            << " volume shorter at " << (100.0f * m_volumeDiffShorterFraction) << "% of side vertices"
            << " | top mean/max=" << (sumAbs[1] / static_cast<double>(count)) << "/" << maxAbs[1]
            << " volume shorter at " << (100.0 * shorter[1] / static_cast<double>(count)) << "%" << std::endl;
    }

    void computeVolumeHeatSlicing()
    {
        if (!m_haveCaps) {
            m_status = "No caps. Press 'c' first.";
            std::cout << "[FootingCompare][v] " << m_status << std::endl;
            return;
        }
        if (!m_haveHeat) {
            clearVolumeHeat();
            computeHeatSlicing();
            if (!m_haveHeat) {
                m_status = "Method 4 needs the surface heat distances. Press 'g' first.";
                std::cout << "[FootingCompare][v] " << m_status << std::endl;
                return;
            }
        }
        if (!m_haveVolume) {
            if (!buildTetVolume()) return; // clears Method 4 state through clearHarmonic
        }
        else {
            clearVolumeHeat();
        }
        if (m_tetElements.size() != m_tet.tets.size()) {
            int degenerateTets = 0;
            buildTetElements(degenerateTets);
            std::cout << "[FootingCompare][v] tet elements built, degenerate skipped=" << degenerateTets << std::endl;
        }
        if (!buildSideToTetMap("v")) {
            clearVolumeHeat();
            return;
        }

        m_volumeHeatEdgeLength = meanTetEdgeLength();
        std::cout << "[FootingCompare][v] surface heat extended into the tet volume: vertices=" << m_tet.vertices.size()
            << " tets=" << m_tet.tets.size() << " mean edge=" << m_volumeHeatEdgeLength
            << " (wall distances stay on the surface heat field)" << std::endl;

        std::string error;
        const zSpace::zFloatArray* surfaceDistance[2] = { &m_distBottom, &m_distTop };
        const int sourceTag[2] = { static_cast<int>(BoundaryTag::Bottom), static_cast<int>(BoundaryTag::Top) };
        const char* labels[2] = { "bottom", "top" };
        for (int s = 0; s < 2; ++s) {
            if (!extendSurfaceDistance(*surfaceDistance[s], sourceTag[s], labels[s], m_volumeDist[s], m_volumeHeatDirection[s],
                m_volumeDistMax[s], m_volumePinned[s], m_volumeFree[s], error)) {
                m_status = std::string("Surface heat extension (") + labels[s] + "): " + error + ".";
                std::cout << "[FootingCompare][v] ERROR " << m_status << std::endl;
                clearVolumeHeat();
                return;
            }
        }

        // Ratio field. On every wall vertex both distances are the surface
        // heat values, so F equals the Method 1 blend and the iso-surface
        // boundary is the heat contour. Inner vertices use the extended distances.
        const size_t vertexCount = m_tet.vertices.size();
        m_volumeRatio.assign(vertexCount, 0.0f);
        int degenerateRatios = 0;
        for (size_t i = 0; i < vertexCount; ++i) {
            const float sum = m_volumeDist[0][i] + m_volumeDist[1][i];
            if (sum < 1e-9f) {
                m_volumeRatio[i] = 0.5f;
                ++degenerateRatios;
                continue;
            }
            m_volumeRatio[i] = std::clamp(m_volumeDist[0][i] / sum, 0.0f, 1.0f);
        }
        const auto ratioRange = std::minmax_element(m_volumeRatio.begin(), m_volumeRatio.end());

        double ratioErrorSum = 0.0;
        float ratioErrorMax = 0.0f;
        int ratioSamples = 0;
        for (int i = 0; i < static_cast<int>(m_sideToTet.size()) && i < static_cast<int>(m_distBottom.size()); ++i) {
            const int tetId = m_sideToTet[i];
            if (tetId < 0 || tetId >= static_cast<int>(m_volumeRatio.size())) continue;
            const float sum = m_distBottom[i] + m_distTop[i];
            const float surfaceRatio = (sum < 1e-9f) ? 0.5f : m_distBottom[i] / sum;
            const float error = std::fabs(m_volumeRatio[tetId] - surfaceRatio);
            ratioErrorSum += error;
            ratioErrorMax = std::max(ratioErrorMax, error);
            ++ratioSamples;
        }
        std::cout << "[FootingCompare][v] ratio F = D_b / (D_b + D_t) min/max=" << *ratioRange.first << "/" << *ratioRange.second
            << " degenerate=" << degenerateRatios
            << " wall |F - surface heat ratio| mean/max="
            << (ratioSamples > 0 ? ratioErrorSum / ratioSamples : 0.0) << "/" << ratioErrorMax << std::endl;

        transferToSide(m_volumeDist[0], m_volumeDistSide[0]);
        transferToSide(m_volumeDist[1], m_volumeDistSide[1]);
        m_haveVolumeHeat = true;

        buildVolumeDiffMesh();
        extractVolumeHeatIsoMeshes();
        measureAllLayerHeights();
        printLayerHeightReport("v");

        const LayerHeightReport& report = m_heightReports[3];
        std::ostringstream out;
        out << "Volume heat: wall pinned to surface heat, inner vertices=" << m_volumeFree[0]
            << " slices=" << m_volumeHeatIsoMeshes.size() << " layers=" << m_layerCount
            << " wall ratio error max=" << ratioErrorMax;
        if (report.valid) {
            out << " height min/mean/max=" << report.globalMin << "/" << report.globalMean << "/" << report.globalMax
                << " violations=" << report.violations << "/" << report.gaps.size();
        }
        m_status = out.str();
        m_debugMode = 21;
        std::cout << "[FootingCompare][v] " << m_status << std::endl;
    }

    // ------------------------------------------------------------------ navigation

    void cycleDebugMode()
    {
        m_debugMode = (m_debugMode + 1) % kDebugModeCount;
        m_status = "Debug mode " + std::to_string(m_debugMode) + ": " + modeName();
        std::cout << "[FootingCompare][d] " << m_status << std::endl;
        // Method 3 modes carry per-layer numbers in the status line.
        if (isBandSliceMode() && !m_bandContours.empty()) changeBandSlice(0);
        else if (m_debugMode == 15 && (m_heightView == 3 || heightViewReport())) updateHeightStatus();
        // Method 4 modes: which source is shown, or the wall comparison numbers.
        else if (isVolumeHeatFieldMode() && m_haveVolumeHeat) updateVolumeHeatViewStatus();
        else if (m_debugMode == 20 && m_haveVolumeHeat) updateVolumeDiffStatus();
    }

    bool isBandSliceMode() const
    {
        return m_debugMode == 13 || m_debugMode == 14 || m_debugMode == 16;
    }

    // Modes 17-19 show one of the two volumetric heat solutions (n toggles).
    bool isVolumeHeatFieldMode() const
    {
        return m_debugMode >= 17 && m_debugMode <= 19;
    }

    // Height view 3 is the streamline-length field; view 4 maps to report 3.
    int heightViewReportIndex() const
    {
        if (m_heightView >= 0 && m_heightView <= 2) return m_heightView;
        if (m_heightView == 4) return 3;
        return -1;
    }

    const LayerHeightReport* heightViewReport() const
    {
        const int index = heightViewReportIndex();
        if (index < 0) return nullptr;
        const LayerHeightReport& report = m_heightReports[index];
        return report.valid ? &report : nullptr;
    }

    std::string sliceLabel() const
    {
        if (isBandSliceMode()) {
            return std::to_string(m_currentBandSlice + 1) + "/" + std::to_string(m_bandContours.size()) + " (band)";
        }
        if (m_debugMode == 15) {
            if (m_heightView == 3) return "- (field)";
            const LayerHeightReport* report = heightViewReport();
            return std::to_string(m_currentGap + 1) + "/" + std::to_string(report ? report->gaps.size() : 0) + " (gap)";
        }
        return std::to_string(m_currentSlice + 1) + "/" + std::to_string(std::max(0, m_layerCount - 1));
    }

    void changeSlice(int delta)
    {
        if (isBandSliceMode()) {
            changeBandSlice(delta);
            return;
        }
        if (m_debugMode == 15) {
            changeGap(delta);
            return;
        }

        const int count = interiorSliceCount();
        if (count <= 0 || (m_heatContours.empty() && m_harmonicIsoMeshes.empty() && m_volumeHeatIsoMeshes.empty())) {
            m_status = "No slices. Press 'g', 'h' or 'v' first.";
            std::cout << "[FootingCompare][slice] " << m_status << std::endl;
            return;
        }
        m_currentSlice = (m_currentSlice + delta + count) % count;
        m_showAll = false;
        if (m_debugMode != 4 && m_debugMode != 7 && m_debugMode != 8 && m_debugMode != 21) {
            m_debugMode = (m_heatContours.empty() && m_harmonicIsoMeshes.empty()) ? 21 : 8;
        }

        std::ostringstream out;
        out << "Slice " << (m_currentSlice + 1) << "/" << count << " iso=" << isoValue(m_currentSlice);
        m_status = out.str();
        std::cout << "[FootingCompare][slice] " << m_status << std::endl;
    }

    void changeBandSlice(int delta)
    {
        const int count = static_cast<int>(m_bandContours.size());
        if (count <= 0) {
            m_status = "No band contours. Press 'j' first.";
            std::cout << "[FootingCompare][slice] " << m_status << std::endl;
            return;
        }
        m_currentBandSlice = (m_currentBandSlice + delta + count) % count;
        if (delta != 0) m_showAll = false;

        // Contour c sits at psi_k with k = c + 1; the schedule arrays are indexed by k.
        const int k = m_currentBandSlice + 1;
        std::ostringstream out;
        out << "Band slice " << (m_currentBandSlice + 1) << "/" << count << " psi=" << m_adaptiveIsoValues[m_currentBandSlice];
        if (k < static_cast<int>(m_scheduleGradients.size())) {
            out << " g_k=" << m_scheduleGradients[k] << " d_psi_k=" << m_adaptiveSteps[k]
                << " crossed=" << m_scheduleTriangles[k].size()
                << " uniform=" << (1.0f / static_cast<float>(m_bandLayerCount));
        }
        if (m_debugMode == 16) out << " | mapped heat/harmonic slice=" << (mappedSliceIndex(m_currentBandSlice) + 1);
        m_status = out.str();
        std::cout << "[FootingCompare][slice] " << m_status << std::endl;
    }

    void changeGap(int delta)
    {
        const LayerHeightReport* report = heightViewReport();
        if (!report) {
            m_status = std::string("No layer height report for ") + (m_heightView == 3 ? "streamline view" : methodName(heightViewReportIndex()))
                + (m_heightView == 4 ? ". Press 'v' first." : ". Press 'j' first.");
            std::cout << "[FootingCompare][slice] " << m_status << std::endl;
            return;
        }
        const int count = static_cast<int>(report->gaps.size());
        m_currentGap = (m_currentGap + delta + count) % count;
        m_showAll = false;
        updateHeightStatus();
    }

    void updateHeightStatus()
    {
        std::ostringstream out;
        if (m_heightView == 3) {
            out << "Streamline length L min/max=" << m_streamlineMin << "/" << m_streamlineMax
                << " feasible N in [" << m_feasibleLayersLow << ", " << m_feasibleLayersHigh << "]"
                << (m_feasibleLayersLow > m_feasibleLayersHigh ? " (empty)" : "") << " chosen N=" << m_bandLayerCount;
        }
        else {
            const LayerHeightReport* report = heightViewReport();
            out << "Height view " << methodName(heightViewReportIndex());
            if (!report) {
                out << (m_heightView == 4 ? ": no report (press v)." : ": no report (press j).");
            }
            else {
                const int gap = std::clamp(m_currentGap, 0, static_cast<int>(report->gaps.size()) - 1);
                const LayerGapStats& stats = report->gaps[gap];
                out << " gap " << (gap + 1) << "/" << report->gaps.size()
                    << " min/mean/max=" << stats.minHeight << "/" << stats.meanHeight << "/" << stats.maxHeight
                    << (stats.inBand ? " in band" : " VIOLATES band")
                    << " | all: violations=" << report->violations << "/" << report->gaps.size()
                    << " feasible N in [" << m_feasibleLayersLow << ", " << m_feasibleLayersHigh << "]";
            }
        }
        m_status = out.str();
        std::cout << "[FootingCompare][b] " << m_status << std::endl;
    }

    // Proportional index mapping from a band contour to the Method 1/2 slices.
    int mappedSliceIndex(int bandSlice) const
    {
        const int bandCount = static_cast<int>(m_bandContours.size());
        const int count = interiorSliceCount();
        if (bandCount <= 1 || count <= 0) return std::clamp(bandSlice, 0, std::max(0, count - 1));
        const double t = static_cast<double>(bandSlice) / static_cast<double>(bandCount - 1);
        return std::clamp(static_cast<int>(std::lround(t * (count - 1))), 0, count - 1);
    }

    void stepEqualisationView()
    {
        if (m_psiIterations.empty()) {
            m_status = "No equalised field. Press 'j' first.";
            std::cout << "[FootingCompare][n] " << m_status << std::endl;
            return;
        }
        m_equalisationView = (m_equalisationView + 1) % static_cast<int>(m_psiIterations.size());
        refreshEqualisationView();
        m_debugMode = 11;
        const GradientStats stats = tetGradientStats(m_psiIterations[m_equalisationView]);
        std::ostringstream out;
        out << "psi iteration " << m_equalisationView << "/" << (m_psiIterations.size() - 1)
            << (m_equalisationView == 0 ? " (= phi)" : "") << " |grad| tets " << statsText(stats) << " target g=" << m_gradientTarget;
        m_status = out.str();
        std::cout << "[FootingCompare][n] " << m_status << std::endl;
    }

    void cycleHeightView()
    {
        m_heightView = (m_heightView + 1) % 5;
        m_debugMode = 15;
        m_currentGap = 0;
        updateHeightStatus();
    }

    // Overlay mode that shows the toggled method: 8 (heat vs harmonic) or 16
    // (all methods) for Methods 1-3; Method 4 falls back to its own mode 21.
    void jumpToOverlayFor(int method)
    {
        if (method == 3) {
            if (m_debugMode != 16 && m_debugMode != 21) m_debugMode = m_haveBand ? 16 : 21;
            return;
        }
        if (m_debugMode != 8 && m_debugMode != 16) m_debugMode = m_haveBand ? 16 : 8;
    }

    void toggleMethodCurves(int method)
    {
        bool* flag = nullptr;
        const char* name = methodName(method);
        if (method == 0) flag = &m_showHeatCurves;
        else if (method == 1) flag = &m_showHarmonicCurves;
        else if (method == 2) flag = &m_showBandCurves;
        else if (method == 3) flag = &m_showVolumeHeatCurves;
        if (!flag) return;
        *flag = !*flag;
        jumpToOverlayFor(method);
        std::ostringstream out;
        out << name << " boundary curves " << (*flag ? "shown" : "hidden")
            << " | heat=" << (m_showHeatCurves ? "on" : "off")
            << " harmonic=" << (m_showHarmonicCurves ? "on" : "off")
            << " band=" << (m_showBandCurves ? "on" : "off")
            << " volume heat=" << (m_showVolumeHeatCurves ? "on" : "off");
        m_status = out.str();
        std::cout << "[FootingCompare][curves] " << m_status << std::endl;
    }

    void toggleMethodSurfaces(int method)
    {
        bool* flag = nullptr;
        if (method == 1) flag = &m_showHarmonicSurfaces;
        else if (method == 2) flag = &m_showBandSurfaces;
        else if (method == 3) flag = &m_showVolumeHeatSurfaces;
        if (!flag) return;
        *flag = !*flag;
        jumpToOverlayFor(method);
        std::ostringstream out;
        out << methodName(method) << " iso surfaces " << (*flag ? "shown" : "hidden")
            << " | harmonic=" << (m_showHarmonicSurfaces ? "on" : "off")
            << " band=" << (m_showBandSurfaces ? "on" : "off")
            << " volume heat=" << (m_showVolumeHeatSurfaces ? "on" : "off")
            << " (faces only, no inner edges)";
        m_status = out.str();
        std::cout << "[FootingCompare][surfaces] " << m_status << std::endl;
    }

    void toggleVolumeHeatView()
    {
        if (!m_haveVolumeHeat) {
            m_status = "No volume heat field. Press 'v' first.";
            std::cout << "[FootingCompare][n] " << m_status << std::endl;
            return;
        }
        m_volumeHeatView = 1 - m_volumeHeatView;
        updateVolumeHeatViewStatus();
    }

    void updateVolumeHeatViewStatus()
    {
        const int s = std::clamp(m_volumeHeatView, 0, 1);
        std::ostringstream out;
        if (m_debugMode == 17) {
            out << "F = D_b/(D_b+D_t). Wall vertices hold the surface heat ratio; inner vertices are the Laplace extension.";
        }
        else {
            out << "Surface heat from " << (s == 0 ? "bottom" : "top") << " (n toggles): wall pinned=" << m_volumePinned[s]
                << " inner Laplace vertices=" << m_volumeFree[s]
                << " D max=" << m_volumeDistMax[s];
        }
        m_status = out.str();
        std::cout << "[FootingCompare][n] " << m_status << std::endl;
    }

    void updateVolumeDiffStatus()
    {
        std::ostringstream out;
        if (!m_haveVolumeDiff) {
            out << "Wall comparison needs Method 1 distances. Press 'g' (surface heat) with 'v' computed.";
        }
        else {
            out << "Wall |D_volume - d_surface| from bottom: mean=" << m_volumeDiffMeanAbs << " max=" << m_volumeDiffMax
                << " volume shorter at " << (100.0f * m_volumeDiffShorterFraction) << "% of side vertices";
        }
        m_status = out.str();
        std::cout << "[FootingCompare][v] " << m_status << std::endl;
    }

    void changeLayerCount(int delta)
    {
        const int next = std::clamp(m_layerCount + delta, kMinLayerCount, kMaxLayerCount);
        if (next == m_layerCount) return;
        m_layerCount = next;
        std::cout << "[FootingCompare][layers] layers=" << m_layerCount << " interior slices=" << interiorSliceCount() << std::endl;
        // Both fields are independent of the layer count; only re-extract.
        if (m_haveHeat) extractHeatContours();
        if (m_havePhi) extractHarmonicIsoMeshes();
        if (!m_haveHeat && !m_havePhi) m_status = "Layers=" + std::to_string(m_layerCount);
        // Method 3 shares the layer count: re-run the schedule (the equalised
        // field itself does not depend on N) and refresh the height reports.
        bool refreshHeights = false;
        if (m_haveBand && m_havePsi) {
            m_haveBand = computeAdaptiveIsoValues();
            if (m_haveBand) {
                extractBandContours();
                extractBandIsoMeshes();
                m_haveBand = !m_bandContours.empty();
            }
            refreshHeights = true;
        }
        // Method 4: the ratio field is independent of N as well; re-slice it.
        if (m_haveVolumeHeat) {
            extractVolumeHeatIsoMeshes();
            refreshHeights = true;
        }
        if (refreshHeights) measureAllLayerHeights();
    }

    std::string modeName() const
    {
        static const char* names[kDebugModeCount] = {
            "input mesh", "caps (bottom blue / top red)", "heat distance from bottom", "heat distance from top",
            "heat contours", "tet volume wire", "harmonic phi points", "harmonic iso slices", "overlay heat vs harmonic",
            "harmonic direction field", "|grad phi| on side (red steep / green flat)", "equalised psi (n steps iteration)",
            "|grad psi| on side (same scale)", "schedule: crossed triangles", "adaptive iso-values (bars: d_psi vs 1/N)",
            "layer height check (b cycles method)", "four-way overlay heat / harmonic / band / volume heat",
            "volume heat ratio F (wall = surface heat)", "extended distance direction grad D / |grad D| (n: bottom/top)",
            "surface heat distance extended inside (n: bottom/top)", "wall: |extended - surface| distance from bottom",
            "volume heat iso slices, boundary on the surface heat contours"
        };
        std::string name = names[std::clamp(m_debugMode, 0, kDebugModeCount - 1)];
        if (m_debugMode == 11) name += " [" + std::to_string(m_equalisationView) + "]";
        if (m_debugMode == 15) name += std::string(" [") + (m_heightView == 3 ? "streamline length" : methodName(heightViewReportIndex())) + "]";
        if (m_debugMode == 18 || m_debugMode == 19) name += (m_volumeHeatView == 0) ? " [bottom]" : " [top]";
        return name;
    }

    // ------------------------------------------------------------------ drawing

    static zDisplayMeshSetting meshDisplay(const Color& face, const Color& edge, float edgeWidth, bool showEdges = true)
    {
        zDisplayMeshSetting display;
        display.showFaces = true;
        display.showEdges = showEdges;
        display.showVertices = false;
        display.faceColor = face;
        display.edgeColor = edge;
        display.edgeWidth = edgeWidth;
        return display;
    }

    void drawInput()
    {
        if (!m_loaded) return;
        scene().draw(m_inputMesh, meshDisplay(Color(0.58f, 0.60f, 0.64f, 0.26f), Color(0.28f, 0.30f, 0.34f, 0.82f), 0.8f));
    }

    void drawSideGhost()
    {
        if (!m_loaded) return;
        scene().draw(m_sideMesh, meshDisplay(Color(0.58f, 0.60f, 0.64f, 0.12f), Color(0.28f, 0.30f, 0.34f, 0.35f), 0.6f));
    }

    void drawCaps()
    {
        if (!m_haveCaps) {
            drawInput();
            return;
        }
        drawSideGhost();
        scene().draw(m_bottomCapMesh, meshDisplay(Color(0.02f, 0.25f, 1.0f, 0.38f), Color(0.1f, 0.45f, 1.0f, 1.0f), 2.0f));
        scene().draw(m_topCapMesh, meshDisplay(Color(1.0f, 0.04f, 0.08f, 0.38f), Color(1.0f, 0.16f, 0.12f, 1.0f), 2.0f));
    }

    void drawHeatField(zSpace::zObjMesh& mesh)
    {
        if (!m_haveHeat) return;
        zDisplayMeshSetting display = meshDisplay(Color(1.0f, 1.0f, 1.0f, 1.0f), Color(0.2f, 0.2f, 0.2f, 0.5f), 0.6f);
        display.useMeshColors = true;
        scene().draw(mesh, display);
    }

    void drawHeatContours()
    {
        drawSideGhost();
        if (m_heatContours.empty()) return;
        const zDisplayGraphSetting lines = Display::lines(Color(0.0f, 0.62f, 0.15f, 1.0f), 3.0f);
        if (m_showAll) {
            for (zSpace::zObjGraph& graph : m_heatContours) scene().draw(graph, lines);
        }
        else if (m_currentSlice >= 0 && m_currentSlice < static_cast<int>(m_heatContours.size())) {
            scene().draw(m_heatContours[m_currentSlice], lines);
        }
    }

    void drawTetWire()
    {
        if (!m_haveVolume) return;
        scene().draw(m_tetWireGraph, Display::lines(Color(0.0f, 0.9f, 1.0f, 0.8f), 1.1f));
    }

    void drawPhiPoints(Renderer& renderer)
    {
        if (!m_havePhi) return;
        for (int i = 0; i < static_cast<int>(m_tet.vertices.size()); ++i) {
            const float phi = std::clamp(m_phi[i], 0.0f, 1.0f);
            renderer.drawPoint(toVec3(m_tet.vertices[i]), Color(phi, 0.08f, 1.0f - phi, 1.0f), 7.0f);
        }
    }

    static zDisplayMeshSetting isoSurfaceDisplay(const Color& face)
    {
        return meshDisplay(face, Color(0.0f, 0.0f, 0.0f, 0.0f), 0.1f, false);
    }

    void drawHarmonicIsos()
    {
        drawSideGhost();
        if (m_harmonicIsoMeshes.empty() || !m_showHarmonicSurfaces) return;
        if (m_showAll) {
            const zDisplayMeshSetting display = isoSurfaceDisplay(Color(1.0f, 0.05f, 0.72f, 0.28f));
            for (zSpace::zObjMesh& mesh : m_harmonicIsoMeshes) scene().draw(mesh, display);
        }
        else if (m_currentSlice >= 0 && m_currentSlice < static_cast<int>(m_harmonicIsoMeshes.size())) {
            scene().draw(m_harmonicIsoMeshes[m_currentSlice], isoSurfaceDisplay(Color(1.0f, 0.05f, 0.72f, 0.72f)));
        }
    }

    void drawOverlay()
    {
        drawSideGhost();
        const zDisplayGraphSetting heatLines = Display::lines(Color(0.0f, 0.62f, 0.15f, 1.0f), 3.0f);
        const zDisplayGraphSetting harmonicLines = Display::lines(Color(0.55f, 0.0f, 0.40f, 1.0f), 2.2f);
        const zDisplayMeshSetting isoFaces = isoSurfaceDisplay(Color(1.0f, 0.05f, 0.72f, 0.30f));

        auto drawSlice = [&](int k) {
            if (m_showHarmonicSurfaces && k >= 0 && k < static_cast<int>(m_harmonicIsoMeshes.size())) {
                scene().draw(m_harmonicIsoMeshes[k], isoFaces);
            }
            if (m_showHarmonicCurves && k >= 0 && k < static_cast<int>(m_harmonicBoundaryGraphs.size())) {
                scene().draw(m_harmonicBoundaryGraphs[k], harmonicLines);
            }
            if (m_showHeatCurves && k >= 0 && k < static_cast<int>(m_heatContours.size())) scene().draw(m_heatContours[k], heatLines);
        };

        if (m_showAll) {
            const int count = static_cast<int>(std::max(m_heatContours.size(), m_harmonicIsoMeshes.size()));
            for (int k = 0; k < count; ++k) drawSlice(k);
        }
        else {
            drawSlice(m_currentSlice);
        }
    }

    // ------------------------------------------------------------------ Method 3 drawing (modes 9-16)

    void drawSideWire()
    {
        if (!m_loaded) return;
        zDisplayMeshSetting display = meshDisplay(Color(0.0f, 0.0f, 0.0f, 0.0f), Color(0.28f, 0.30f, 0.34f, 0.35f), 0.6f);
        display.showFaces = false;
        scene().draw(m_sideMesh, display);
    }

    void drawFieldPoints(Renderer& renderer, const zSpace::zFloatArray& field, float size)
    {
        for (int i = 0; i < static_cast<int>(m_tet.vertices.size()) && i < static_cast<int>(field.size()); ++i) {
            const float value = std::clamp(field[i], 0.0f, 1.0f);
            renderer.drawPoint(toVec3(m_tet.vertices[i]), Color(value, 0.08f, 1.0f - value, 1.0f), size);
        }
    }

    // Mode 9: one segment per tet centroid along V = grad phi / |grad phi|.
    void drawDirectionField(Renderer& renderer)
    {
        drawUnitField(renderer, m_directionField, 0.35f * m_meanTetEdgeLength, Color(1.0f, 0.55f, 0.0f, 0.9f));
    }

    // One segment of half-length `half` per tet centroid along a unit field
    // (modes 9 and 18).
    void drawUnitField(Renderer& renderer, const std::vector<Eigen::Vector3d>& field, float half, const Color& color)
    {
        drawSideGhost();
        if (field.empty()) return;
        for (size_t t = 0; t < m_tet.tets.size() && t < field.size(); ++t) {
            const Eigen::Vector3d& direction = field[t];
            if (direction.squaredNorm() < 0.5) continue;
            zSpace::zPoint centroid;
            for (int local = 0; local < 4; ++local) centroid += m_tet.vertices[m_tet.tets[t][local]];
            centroid = centroid * 0.25;
            const Vec3 c = toVec3(centroid);
            const Vec3 d(static_cast<float>(direction.x()) * half, static_cast<float>(direction.y()) * half, static_cast<float>(direction.z()) * half);
            renderer.drawLine(Vec3(c.x - d.x, c.y - d.y, c.z - d.z), Vec3(c.x + d.x, c.y + d.y, c.z + d.z), color, 1.2f);
        }
    }

    // Modes 10 and 12: per-triangle gradient magnitude on the side surface.
    void drawSideGradient(Renderer& renderer, const ColoredTriangles& triangles)
    {
        if (triangles.vertices.empty()) {
            drawSideGhost();
            return;
        }
        renderer.drawMesh(triangles.vertices.data(), nullptr, triangles.colors.data(), static_cast<int>(triangles.vertices.size()), nullptr, 0, false);
        drawSideWire();
    }

    // Mode 11: the equalised field of the displayed iteration.
    void drawEqualisedField(Renderer& renderer)
    {
        if (m_psiIterations.empty()) {
            drawSideGhost();
            return;
        }
        zDisplayMeshSetting display = meshDisplay(Color(1.0f, 1.0f, 1.0f, 1.0f), Color(0.2f, 0.2f, 0.2f, 0.35f), 0.6f);
        display.useMeshColors = true;
        scene().draw(m_psiSideMesh, display);
        const int view = std::clamp(m_equalisationView, 0, static_cast<int>(m_psiIterations.size()) - 1);
        drawFieldPoints(renderer, m_psiIterations[view], 5.0f);
    }

    void drawScheduleLayer(Renderer& renderer, int k, const Color& faceColor)
    {
        if (k < 0 || k >= static_cast<int>(m_scheduleTriangles.size())) return;
        for (int t : m_scheduleTriangles[k]) {
            const auto& tri = m_sideSoup.triangles[t];
            renderer.drawTriangle(toVec3(m_sideSoup.positions[tri[0]]), toVec3(m_sideSoup.positions[tri[1]]), toVec3(m_sideSoup.positions[tri[2]]), faceColor);
        }
    }

    // Mode 13: side triangles crossed by the contour that sets g_k.
    void drawScheduleTriangles(Renderer& renderer)
    {
        drawSideGhost();
        if (m_scheduleTriangles.empty()) return;
        const zDisplayGraphSetting contourLines = Display::lines(Color(0.95f, 0.35f, 0.0f, 1.0f), 3.0f);
        if (m_showAll) {
            for (int k = 0; k < static_cast<int>(m_scheduleTriangles.size()); ++k) {
                drawScheduleLayer(renderer, k, (k % 2 == 0) ? Color(1.0f, 0.62f, 0.15f, 0.55f) : Color(0.15f, 0.55f, 1.0f, 0.55f));
            }
            for (zSpace::zObjGraph& graph : m_bandContours) scene().draw(graph, contourLines);
        }
        else if (m_currentBandSlice >= 0 && m_currentBandSlice < static_cast<int>(m_bandContours.size())) {
            drawScheduleLayer(renderer, m_currentBandSlice + 1, Color(1.0f, 0.62f, 0.15f, 0.7f));
            scene().draw(m_bandContours[m_currentBandSlice], contourLines);
        }
    }

    // Mode 14: all band contours on an iso-value ramp plus the step bar strip.
    void drawAdaptiveIsoValues(Renderer& renderer)
    {
        drawSideGhost();
        if (m_bandContours.empty()) return;
        if (m_showBandSurfaces) {
            const zDisplayMeshSetting faces = isoSurfaceDisplay(Color(1.0f, 0.50f, 0.05f, 0.32f));
            if (m_showAll) {
                for (zSpace::zObjMesh& mesh : m_bandIsoMeshes) scene().draw(mesh, faces);
            }
            else if (m_currentBandSlice >= 0 && m_currentBandSlice < static_cast<int>(m_bandIsoMeshes.size())) {
                scene().draw(m_bandIsoMeshes[m_currentBandSlice], faces);
            }
        }
        for (int k = 0; k < static_cast<int>(m_bandContours.size()); ++k) {
            const float t = m_adaptiveIsoValues[k];
            const bool current = (!m_showAll && k == m_currentBandSlice);
            scene().draw(m_bandContours[k], Display::lines(Color(t, 0.2f, 1.0f - t, current ? 1.0f : 0.75f), current ? 4.0f : 2.0f));
        }

        // Screen-space bars: d_psi_k scaled so the uniform step 1/N is 30 px.
        const float baseY = 220.0f;
        const float unitHeight = 30.0f;
        const float barPitch = 9.0f;
        const float x0 = 12.0f;
        const float n = static_cast<float>(std::max(1, m_bandLayerCount));
        for (int k = 0; k < static_cast<int>(m_adaptiveSteps.size()); ++k) {
            const float x = x0 + k * barPitch;
            const float height = m_adaptiveSteps[k] * n * unitHeight;
            const bool current = (!m_showAll && (k == m_currentBandSlice + 1));
            renderer.draw2dLine(Vec2(x, baseY), Vec2(x, baseY - height), current ? Color(0.95f, 0.35f, 0.0f, 1.0f) : Color(1.0f, 0.62f, 0.15f, 0.9f), 6.0f);
        }
        const float xEnd = x0 + m_adaptiveSteps.size() * barPitch;
        renderer.draw2dLine(Vec2(x0 - 4.0f, baseY - unitHeight), Vec2(xEnd, baseY - unitHeight), Color(0.25f, 0.25f, 0.3f, 1.0f), 1.0f);
        renderer.draw2dLine(Vec2(x0 - 4.0f, baseY), Vec2(xEnd, baseY), Color(0.25f, 0.25f, 0.3f, 1.0f), 1.0f);
        renderer.setColor(Color(0.08f, 0.09f, 0.12f, 1.0f));
        renderer.drawString("d_psi_k per layer (line = uniform 1/N, N=" + std::to_string(m_bandLayerCount) + ")", x0, baseY + 18.0f);
    }

    static Color heightColor(float height)
    {
        if (height < kLayerHeightBandMin) return Color(0.1f, 0.35f, 1.0f, 1.0f);
        if (height > kLayerHeightBandMax) return Color(1.0f, 0.1f, 0.1f, 1.0f);
        return Color(0.0f, 0.7f, 0.2f, 1.0f);
    }

    void drawHeightGap(Renderer& renderer, const LayerHeightReport& report, int gap, bool markWorst)
    {
        if (gap < 0 || gap >= static_cast<int>(report.gaps.size())) return;
        const zSpace::zPointArray& positions = report.samplePositions[gap];
        const zSpace::zIntArray& edges = report.sampleEdges[gap];
        const std::vector<float>& heights = report.sampleHeights[gap];
        for (size_t e = 0; e + 1 < edges.size(); e += 2) {
            const int a = edges[e];
            const int b = edges[e + 1];
            // Colour by the endpoint farther from the target so violations win.
            const float ha = heights[a];
            const float hb = heights[b];
            const float worse = (std::fabs(ha - kTargetLayerHeight) > std::fabs(hb - kTargetLayerHeight)) ? ha : hb;
            renderer.drawLine(toVec3(positions[a]), toVec3(positions[b]), heightColor(worse), 3.0f);
        }
        if (markWorst) renderer.drawPoint(toVec3(report.gaps[gap].worstPoint), Color(0.05f, 0.05f, 0.05f, 1.0f), 11.0f);
    }

    // Mode 15: contours coloured by measured print height, or the streamline length field.
    void drawLayerHeightCheck(Renderer& renderer)
    {
        if (m_heightView == 3) {
            zSpace::zFnMesh fn(m_streamlineMesh);
            if (fn.numVertices() == 0) {
                drawSideGhost();
                return;
            }
            zDisplayMeshSetting display = meshDisplay(Color(1.0f, 1.0f, 1.0f, 1.0f), Color(0.2f, 0.2f, 0.2f, 0.35f), 0.6f);
            display.useMeshColors = true;
            scene().draw(m_streamlineMesh, display);
            return;
        }

        drawSideGhost();
        const LayerHeightReport* report = heightViewReport();
        if (!report) return;
        if (m_showAll) {
            for (int gap = 0; gap < static_cast<int>(report->gaps.size()); ++gap) drawHeightGap(renderer, *report, gap, !report->gaps[gap].inBand);
        }
        else {
            drawHeightGap(renderer, *report, std::clamp(m_currentGap, 0, static_cast<int>(report->gaps.size()) - 1), true);
        }
    }

    // Mode 16: Method 1 (green), Method 2 boundary (magenta), Method 3 (orange),
    // Method 4 (blue) when computed.
    void drawThreeWayOverlay()
    {
        drawSideGhost();
        const zDisplayGraphSetting heatLines = Display::lines(Color(0.0f, 0.62f, 0.15f, 1.0f), 2.4f);
        const zDisplayGraphSetting harmonicLines = Display::lines(Color(0.55f, 0.0f, 0.40f, 1.0f), 2.0f);
        const zDisplayGraphSetting bandLines = Display::lines(Color(1.0f, 0.5f, 0.0f, 1.0f), 3.2f);
        const zDisplayGraphSetting volumeLines = Display::lines(kVolumeHeatCurveColor, 2.2f);

        const zDisplayMeshSetting harmonicFaces = isoSurfaceDisplay(Color(1.0f, 0.05f, 0.72f, 0.28f));
        const zDisplayMeshSetting bandFaces = isoSurfaceDisplay(Color(1.0f, 0.50f, 0.05f, 0.32f));
        const zDisplayMeshSetting volumeFaces = isoSurfaceDisplay(kVolumeHeatFaceColorAll);

        auto drawBandSlice = [&](int c) {
            if (c < 0 || c >= static_cast<int>(m_bandContours.size())) return;
            const int k = mappedSliceIndex(c);
            if (m_showHarmonicSurfaces && k < static_cast<int>(m_harmonicIsoMeshes.size())) {
                scene().draw(m_harmonicIsoMeshes[k], harmonicFaces);
            }
            if (m_showBandSurfaces && c < static_cast<int>(m_bandIsoMeshes.size())) {
                scene().draw(m_bandIsoMeshes[c], bandFaces);
            }
            if (m_showVolumeHeatSurfaces && k < static_cast<int>(m_volumeHeatIsoMeshes.size())) {
                scene().draw(m_volumeHeatIsoMeshes[k], volumeFaces);
            }
            if (m_showHeatCurves && k < static_cast<int>(m_heatContours.size())) scene().draw(m_heatContours[k], heatLines);
            if (m_showHarmonicCurves && k < static_cast<int>(m_harmonicBoundaryGraphs.size())) scene().draw(m_harmonicBoundaryGraphs[k], harmonicLines);
            if (m_showVolumeHeatCurves && k < static_cast<int>(m_volumeHeatBoundaryGraphs.size())) scene().draw(m_volumeHeatBoundaryGraphs[k], volumeLines);
            if (m_showBandCurves) scene().draw(m_bandContours[c], bandLines);
        };

        if (m_showAll) {
            for (int c = 0; c < static_cast<int>(m_bandContours.size()); ++c) drawBandSlice(c);
        }
        else {
            drawBandSlice(m_currentBandSlice);
        }
    }

    // ------------------------------------------------------------------ Method 4 drawing (modes 17-21)

    static inline const Color kVolumeHeatCurveColor = Color(0.0f, 0.45f, 0.95f, 1.0f);
    static inline const Color kVolumeHeatFaceColorAll = Color(0.05f, 0.55f, 1.0f, 0.28f);
    static inline const Color kVolumeHeatFaceColorOne = Color(0.05f, 0.55f, 1.0f, 0.72f);

    // Mode 17: slicing field F. Wall vertices are the surface heat ratio, so the
    // colour on the boundary matches Method 1; inner vertices are the extension.
    void drawVolumeHeatU(Renderer& renderer)
    {
        drawSideGhost();
        if (!m_haveVolumeHeat) return;
        drawFieldPoints(renderer, m_volumeRatio, 6.0f);
    }

    // Mode 18: grad D / |grad D| of the extended surface distance, pointing away from the source.
    void drawVolumeHeatDirection(Renderer& renderer)
    {
        if (!m_haveVolumeHeat) {
            drawSideGhost();
            return;
        }
        drawUnitField(renderer, m_volumeHeatDirection[std::clamp(m_volumeHeatView, 0, 1)], 0.35f * m_volumeHeatEdgeLength, Color(0.0f, 0.45f, 0.95f, 0.9f));
    }

    // Mode 19: surface heat distance of the selected source, copied onto the wall
    // and extended to inner vertices, normalised by its maximum.
    void drawVolumeDistance(Renderer& renderer)
    {
        drawSideGhost();
        if (!m_haveVolumeHeat) return;
        const int s = std::clamp(m_volumeHeatView, 0, 1);
        const zSpace::zFloatArray& distance = m_volumeDist[s];
        const float scale = m_volumeDistMax[s] > 0.0f ? 1.0f / m_volumeDistMax[s] : 1.0f;
        for (int i = 0; i < static_cast<int>(m_tet.vertices.size()) && i < static_cast<int>(distance.size()); ++i) {
            const float value = std::clamp(distance[i] * scale, 0.0f, 1.0f);
            renderer.drawPoint(toVec3(m_tet.vertices[i]), Color(value, 0.08f, 1.0f - value, 1.0f), 6.0f);
        }
    }

    // Mode 20: side coloured by |D_b (volume) - d_b (surface heat, Method 1)|.
    void drawVolumeDiff()
    {
        zSpace::zFnMesh fn(m_volumeDiffMesh);
        if (!m_haveVolumeDiff || fn.numVertices() == 0) {
            drawSideGhost();
            return;
        }
        zDisplayMeshSetting display = meshDisplay(Color(1.0f, 1.0f, 1.0f, 1.0f), Color(0.2f, 0.2f, 0.2f, 0.35f), 0.6f);
        display.useMeshColors = true;
        scene().draw(m_volumeDiffMesh, display);
    }

    // Mode 21: iso surfaces of F (faces only) with their wall curves, same
    // slice index as modes 4/7/8.
    void drawVolumeHeatIsos()
    {
        drawSideGhost();
        if (!m_haveVolumeHeat || m_volumeHeatIsoMeshes.empty()) return;
        const zDisplayGraphSetting curves = Display::lines(kVolumeHeatCurveColor, 2.6f);
        auto drawSlice = [&](int k, const zDisplayMeshSetting& faces) {
            if (k < 0 || k >= static_cast<int>(m_volumeHeatIsoMeshes.size())) return;
            if (m_showVolumeHeatSurfaces) scene().draw(m_volumeHeatIsoMeshes[k], faces);
            if (m_showVolumeHeatCurves && k < static_cast<int>(m_volumeHeatBoundaryGraphs.size())) scene().draw(m_volumeHeatBoundaryGraphs[k], curves);
        };
        if (m_showAll) {
            const zDisplayMeshSetting faces = isoSurfaceDisplay(kVolumeHeatFaceColorAll);
            for (int k = 0; k < static_cast<int>(m_volumeHeatIsoMeshes.size()); ++k) drawSlice(k, faces);
        }
        else {
            drawSlice(m_currentSlice, isoSurfaceDisplay(kVolumeHeatFaceColorOne));
        }
    }
};

ALICE2_REGISTER_SKETCH_AUTO(FootingSlicingComparisonSketch)

#endif // __MAIN__
