// sketch_field3d.cpp
// alice2 3D Scalar Field with Marching Cubes Sketch
// Now demonstrates the 6-term nodal surface
#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/ScalarField3D.h>
#include <objects/MeshObject.h>
#include <memory>
#include <iostream>
#include <vector>
#include <cmath>

using namespace alice2;

class ScalarField3DSketch : public ISketch {
private:
    // Core field & mesh
    std::unique_ptr<ScalarField3D> m_scalarField;
    std::shared_ptr<MeshObject> m_meshObject;
    std::shared_ptr<MeshObject> m_meshObject_x;
    std::shared_ptr<MeshObject> m_meshObject_y;

    // Which field to compute
    bool b_computeSphere;
    bool b_computeBox;
    bool b_computeTorus;
    bool b_computeNoise;   // now “nodal” surface
    bool b_recreateField;
    bool b_compute;
    bool b_export;

    std::string m_path_export = "C:/Users/Taizh/source/repos/zspace_alice2/alice2/data/nodal_srf.obj";

    // Draw toggles
    bool d_drawField;
    bool d_drawMesh;
    bool d_drawWireframe;
    bool d_drawSlice;

    // Parameters
    float m_sphereRadius;
    float m_boxSize;
    float m_torusRadius;
    float m_isolevel;
    int   m_currentSlice;
    int   m_fieldResolution;

    // Animation timer
    float m_time;
    float m_freq;
    float m_iso;

    // Nodal-surface coefficients
    float m_a1, m_a2, m_a3, m_a4, m_a5, m_a6;

public:
    ScalarField3DSketch()
        : m_scalarField(nullptr)
        , m_meshObject(nullptr)
        , m_meshObject_x(nullptr)
        , m_meshObject_y(nullptr)
        , b_compute(true)
        , b_computeSphere(true)
        , b_computeBox(false)
        , b_computeTorus(false)
        , b_computeNoise(false)
        , b_recreateField(true)
        , b_export(false)
        , d_drawField(false)
        , d_drawMesh(true)
        , d_drawWireframe(false)
        , d_drawSlice(false)
        , m_sphereRadius(20.0f)
        , m_boxSize(15.0f)
        , m_torusRadius(15.0f)
        , m_isolevel(0.0f)
        , m_currentSlice(25)
        , m_fieldResolution(32)
        , m_time(0.0f)
        // set your desired nodal-surface parameters here:
        , m_a1(0.8f)
        , m_a2(0.8f)
        , m_a3(0.8f)
        , m_a4(0.8f)
        , m_a5(0.8f)
        , m_a6(0.8f)
        // other params
        , m_freq(2.0f)
        , m_iso(0.0f)
    {}

    // Sketch information
    std::string getName() const override {
        return "Nodal Surface Sketch";
    }

    std::string getDescription() const override {
        return "Nodal surface demonstration";
    }

    std::string getAuthor() const override {
        return "alice2";
    }

    void setup() override {
        //scene().setBackgroundColor(Color(0.1f, 0.1f, 0.1f));
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f));
        scene().setShowGrid(true);
        scene().setGridSize(50.0f);
        scene().setGridDivisions(10);
        scene().setShowAxes(true);
        scene().setAxesLength(25.0f);

        // Create the scalar field
        m_scalarField = std::make_unique<ScalarField3D>(
            Vec3(-25, -25, -5), Vec3(25, 25, 25),
            m_fieldResolution, m_fieldResolution, m_fieldResolution);

        // Create mesh object
        m_meshObject = std::make_shared<MeshObject>();
        scene().addObject(m_meshObject);

        m_meshObject_x = std::make_shared<MeshObject>();
        m_meshObject_y = std::make_shared<MeshObject>();
        scene().addObject(m_meshObject_x);
        scene().addObject(m_meshObject_y);

        generateField();

        std::cout << "3D Scalar Field: Setup complete" << std::endl;
    }

    void update(float deltaTime) override {
        m_time += deltaTime;

        if(b_compute){
            // If in “nodal” mode, update periodically
            if (b_computeNoise) {
                m_a1 = 1.0f * std::sin(m_time + 0.0f);
                m_a2 = 1.0f * std::sin(m_time + 0.0f);
                m_a3 = 1.0f * std::sin(m_time + 1.0f);
                m_a4 = 1.0f * std::sin(m_time + 1.0f);
                m_a5 = 1.0f * std::sin(m_time + 1.0f);
                m_a6 = 1.0f * std::sin(m_time + 1.0f);

                m_iso = 0.35f * std::sin(m_time + 0.0f);

                generateField();
            }
        }

        if(b_export)
        {
            auto m_exportMesh = std::make_unique<MeshObject>(*m_meshObject);
            m_exportMesh->combineWith(*m_meshObject_x);
            m_exportMesh->combineWith(*m_meshObject_y);
            m_exportMesh->weld();
            m_exportMesh->writeToObj(m_path_export);

            b_export = false;
        }
    }

    void draw(Renderer& renderer, Camera& camera) override {
        if (d_drawField && m_scalarField) {
            m_scalarField->draw_points(renderer, 4);
        }
        if (d_drawSlice && m_scalarField) {
            m_scalarField->draw_slice(renderer, m_currentSlice, 3.0f);
        }

        if (m_meshObject) {
            m_meshObject->setShowFaces(d_drawMesh);
            m_meshObject->setShowEdges(d_drawWireframe);
            m_meshObject->setRenderMode(MeshRenderMode::NormalShaded);
            //m_meshObject->setNormalShadingColors(Color(0.8f, 0.2f, 0.8f),Color(1.0f, 1.0f, 1.0f)); // Magentq to white
            m_meshObject->setNormalShadingColors(Color(1.0f, 1.0f, 1.0f), Color(0.8f, 0.2f, 0.8f)); // White to magenta

            m_meshObject_x->setShowFaces(d_drawMesh);
            m_meshObject_x->setShowEdges(d_drawWireframe);
            m_meshObject_x->setRenderMode(MeshRenderMode::NormalShaded);
            m_meshObject_x->setNormalShadingColors(Color(1.0f, 1.0f, 1.0f), Color(0.8f, 0.2f, 0.8f)); // White to magenta

            m_meshObject_y->setShowFaces(d_drawMesh);
            m_meshObject_y->setShowEdges(d_drawWireframe);
            m_meshObject_y->setRenderMode(MeshRenderMode::NormalShaded);
            m_meshObject_y->setNormalShadingColors(Color(1.0f, 1.0f, 1.0f), Color(0.8f, 0.2f, 0.8f)); // White to magenta

            // for(auto v : m_meshObject->getMeshData()->vertices)
            // {
            //     renderer.drawLine(v.position,v.position+v.normal * 0.1,Color(0,0,0));
            // }
        }

        drawUI(renderer);
    }

    void drawUI(Renderer& renderer) {
        float y = 20.0f;
        renderer.setColor(Color(0.0f, 0.0f, 0.0f));
        // renderer.drawString("3D Scalar Field Controls:", 10.0f, y); y += 20;
        // renderer.drawString("1: Sphere   2: Box   3: Torus   4: Nodal Surface", 10.0f, y); y += 20;
        // renderer.drawString("F: Toggle field points   M: Toggle mesh   W: Toggle wireframe", 10.0f, y); y += 20;
        // renderer.drawString("S: Toggle slice   l/k: Adjust isolevel (" + std::to_string(m_isolevel) + ")", 10.0f, y); y += 20;
        // renderer.drawString("[/]: Change slice (" + std::to_string(m_currentSlice) + ")", 10.0f, y); y += 30;

        renderer.drawString("Nodal Surface", 10, y); y += 20;
        renderer.drawString("FPS: " + std::to_string((Application::getInstance()->getFPS())), 10, y); y += 20;
        renderer.drawString("Nodal Surface Formula: \n a1*sin(x)*sin(2y)*sin(3z) \n + a2*sin(2x)*sin(y)*sin(3z) \n + a3*sin(2x)*sin(3y)*sin(z) \n + a4*sin(3x)*sin(y)*sin(2z) \n + a5*sin(x)*sin(3y)*sin(2z) \n + a6*sin(3x)*sin(2y)*sin(z) = 0", 10.0f, y); y += 160;


        // Show which mode we’re in
        const char* mode = b_computeSphere ? "Sphere"
                        : b_computeBox    ? "Box"
                        : b_computeTorus  ? "Torus"
                        : "Nodal Surface";
        // renderer.drawString(std::string("Mode: ") + mode, 10.0f, y); y += 30;

        // If nodal, display parameter values
        if (b_computeNoise) {
            renderer.drawString("a1=" + std::to_string(m_a1), 10.0f, y); y += 15;
            renderer.drawString("a2=" + std::to_string(m_a2), 10.0f, y); y += 15;
            renderer.drawString("a3=" + std::to_string(m_a3), 10.0f, y); y += 15;
            renderer.drawString("a4=" + std::to_string(m_a4), 10.0f, y); y += 15;
            renderer.drawString("a5=" + std::to_string(m_a5), 10.0f, y); y += 15;
            renderer.drawString("a6=" + std::to_string(m_a6), 10.0f, y); y += 15;
        }

        int width, height;
        Application::getInstance()->getWindowSize(width, height);
        renderer.setColor(Color(0.75f, 0.75f, 0.75f));
        renderer.drawString("Michael Trott (2007), Nodal Surfaces of Degenerate States. \n Wolfram Demonstrations Project. \n demonstrations.wolfram.com/NodalSurfacesOfDegenerateStates/", 10, height - 100); y += 20;

    }

    bool onKeyPress(unsigned char key, int x, int y) override {
        switch (key) {
            case '1': b_computeSphere = true;  b_computeBox = b_computeTorus = b_computeNoise = false; generateField(); return true;
            case '2': b_computeBox    = true;  b_computeSphere = b_computeTorus = b_computeNoise = false; generateField(); return true;
            case '3': b_computeTorus  = true;  b_computeSphere = b_computeBox = b_computeNoise = false; generateField(); return true;
            case '4': b_computeNoise  = true;  b_computeSphere = b_computeBox = b_computeTorus = false; generateField(); return true;

            case 'f': d_drawField     = !d_drawField;     return true;
            case 'm': d_drawMesh      = !d_drawMesh;      return true;
            case 'w': d_drawWireframe = !d_drawWireframe; return true;
            case 's': d_drawSlice     = !d_drawSlice;     return true;

            case 'l': m_isolevel += 0.05f; generateField(); return true;
            case 'k': m_isolevel -= 0.05f; generateField(); return true;
            case '[': m_currentSlice = std::max(0, m_currentSlice - 1); return true;
            case ']': m_currentSlice = std::min(m_fieldResolution-1, m_currentSlice + 1); return true;

            case 'p': b_compute = !b_compute; return true;
            case 'x': b_export = !b_export; return true;
        }
        return false;
    }

    void cleanup() override {
        scene().removeObject(m_meshObject);
        std::cout << "Scalar Field Sketch cleanup" << std::endl;
    }

    void generateMesh() {
        auto meshData = m_scalarField->generate_mesh(m_isolevel);
        m_meshObject->setMeshData(meshData);
    }

    void generateField() {
        if (!m_scalarField) return;
        m_scalarField->clear_field();

        m_scalarField = std::make_unique<ScalarField3D>(
        Vec3(-25, -25, -25), Vec3(25, 25, 25),
        m_fieldResolution, m_fieldResolution, m_fieldResolution);
        b_recreateField = true;

        if (b_computeSphere) {
            m_scalarField->apply_scalar_sphere(Vec3(0, 0, 0), m_sphereRadius);
        }
        else if (b_computeBox) {
            m_scalarField->apply_scalar_box(Vec3(0, 0, 0), Vec3(m_boxSize, m_boxSize, m_boxSize));
        }
        else if (b_computeTorus) {
            m_scalarField->apply_scalar_torus(Vec3(0, 0, 0), m_torusRadius, m_torusRadius * 0.3f);
        }
        else if (b_computeNoise) {
            if(b_recreateField){
                m_scalarField = std::make_unique<ScalarField3D>(
                //Vec3(0, 0, 0), Vec3(6, 6, 2),
                Vec3(-0.8f, -0.8f, -0.8f), Vec3(0.8f, 0.8f, 0.8f),
                m_fieldResolution, m_fieldResolution, m_fieldResolution);
                b_recreateField = false;
            }

            // const auto &pts = m_scalarField->get_points();
            // std::vector<float> vals(pts.size());

            // // Morph line endpoints
            // const Vec3 A(0, 0, 0);
            // const Vec3 B(20, 20, 0);

            // const float HALF_PI = 1.57079632679f;

            // for (size_t i = 0; i < pts.size(); ++i)
            // {
            //     // 0..1 along the segment
            //     float t = paramOnSegment(pts[i], A, B);

            //     // optional easing to make the transition softer
            //     float s = smoothstep01(t);

            //     // map s → θ in [0, π/2]
            //     float theta = s * HALF_PI;

            //     vals[i] = tpmsField(pts[i], theta, m_freq, m_iso);
            // }

            // m_scalarField->set_values(vals); // run MC at iso=0

            // --- Nodal Surface Formula ---
            // a1*sin(x)*sin(2y)*sin(3z)
            // + a2*sin(2x)*sin(y)*sin(3z)
            // + a3*sin(2x)*sin(3y)*sin(z)
            // + a4*sin(3x)*sin(y)*sin(2z)
            // + a5*sin(x)*sin(3y)*sin(2z)
            // + a6*sin(3x)*sin(2y)*sin(z) = 0
            const auto& pts = m_scalarField->get_points();
            std::vector<float> vals(pts.size());
            for (size_t i = 0; i < pts.size(); ++i) {
                // const float xmin = 0.0f, xmax = alice2::PI;
                // const float ymin = 0.0f, ymax = alice2::PI;
                // const float zmin = 0.0f, zmax = alice2::PI;

                // const int ix = i % m_fieldResolution;
                // const int iy = (i / m_fieldResolution) % m_fieldResolution;
                // const int iz = i / (m_fieldResolution * m_fieldResolution);

                // const float dx = (xmax - xmin) / (m_fieldResolution - 1);
                // const float dy = (ymax - ymin) / (m_fieldResolution - 1);
                // const float dz = (zmax - zmin) / (m_fieldResolution - 1);

                // float x = xmin + ix * dx;
                // float y = ymin + iy * dy;
                // float z = zmin + iz * dz;

                // float x = pts[i].x;
                // float y = pts[i].y;
                // float z = pts[i].z;

                float x = mapToPi(pts[i].x, -1, 1);
                float y = mapToPi(pts[i].y, -1, 1);
                float z = mapToPi(pts[i].z, -1, 1);

                // Precompute to avoid duplicated sin calls (optional micro-opt)
                float s1x = std::sin(x), s2x = std::sin(2 * x), s3x = std::sin(3 * x);
                float s1y = std::sin(y), s2y = std::sin(2 * y), s3y = std::sin(3 * y);
                float s1z = std::sin(z), s2z = std::sin(2 * z), s3z = std::sin(3 * z);

                float v = 
                  m_a1 * s1x * s2y * s3z
                + m_a2 * s2x * s1y * s3z 
                + m_a3 * s2x * s3y * s1z 
                + m_a4 * s3x * s1y * s2z 
                + m_a5 * s1x * s3y * s2z 
                + m_a6 * s3x * s2y * s1z;

                vals[i] = v;

                // vals[i] = gyroid(pts[i], m_freq, m_iso);
                //vals[i] = schwarzP(pts[i], m_freq, m_iso);
                // vals[i] = simpleTrig(pts[i]);
            }
            m_scalarField->set_values(vals);
        }

        generateMesh();

        if(b_computeNoise)
        {
            auto m = m_meshObject->duplicate();
            // std::cout << m.getMeshData()->vertices.size() << std::endl;
            // std::cout << m.getMeshData()->edges.size() << std::endl;
            // std::cout << m.getMeshData()->faces.size() << std::endl;
            // mirror meshes
            // m_meshObject_x = std::make_shared<MeshObject>(m_meshObject->duplicate());
            // m_meshObject_y = std::make_shared<MeshObject>(m_meshObject->duplicate());

            // auto data = std::make_shared<MeshData>(*m_meshObject->getMeshData());
            // m_meshObject_x->setMeshData(data);
            // std::cout << m_meshObject_x->getMeshData()->vertices.size() << std::endl;
            // std::cout << m_meshObject_x->getMeshData()->edges.size() << std::endl;
            // std::cout << m_meshObject_x->getMeshData()->faces.size() << std::endl;

            *m_meshObject_x = m_meshObject->duplicate();
            *m_meshObject_y = m_meshObject->duplicate();

            // m_meshObject_x->getTransform().setScale(Vec3(-1, 1, 1));
            // m_meshObject_y->getTransform().setScale(Vec3(1, -1, 1));
            // m_meshObject_x->getTransform().setTranslation(Vec3(-1.6f, 0, 0));
            // m_meshObject_y->getTransform().setTranslation(Vec3(0, -1.6f, 0));

            m_meshObject_x->scaleMesh(Vec3(-1, 1, 1));
            m_meshObject_y->scaleMesh(Vec3(1, -1, 1));
            m_meshObject_x->translateMesh(Vec3(-1.6f, 0, 0));
            m_meshObject_y->translateMesh(Vec3(0, -1.6f, 0));
        }
    }

    float gyroid(const Vec3 &p, float f, float iso)
    {
        Vec3 q = p * f;
        return std::sin(q.x) * std::cos(q.y) +
               std::sin(q.y) * std::cos(q.z) +
               std::sin(q.z) * std::cos(q.x) - iso;
    }

    float schwarzP(const Vec3 &p, float f, float iso)
    {
        Vec3 q = p * f;
        return std::cos(q.x) + std::cos(q.y) + std::cos(q.z) - iso;
    }

    float simpleTrig(const Vec3 &p)
    {
        return std::sin(p.x) * std::sin(2 * p.y) +
               std::sin(3 * p.z) * 0.5f; // iso = 0
    }

    struct Blob
    {
        Vec3 c;
        float r2;
    }; // center & radius^2
    float blobs(const Vec3 &p, const std::vector<Blob> &B, float iso)
    {
        float sum = 0.0f;
        for (auto &b : B)
        {
            float d2 = (p.x - b.c.x) * (p.x - b.c.x) +
                       (p.y - b.c.y) * (p.y - b.c.y) +
                       (p.z - b.c.z) * (p.z - b.c.z);
            sum += std::exp(-d2 / b.r2);
        }
        return sum - iso;
    }
    // -------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------
    inline float saturate(float x) { return std::max(0.0f, std::min(1.0f, x)); }

    inline float smoothstep01(float x)
    { // optional smoothing
        x = saturate(x);
        return x * x * (3.0f - 2.0f * x);
    }

    // Project point p onto the segment [a,b] and return 0..1
    inline float paramOnSegment(const Vec3 &p, const Vec3 &a, const Vec3 &b)
    {
        Vec3 d = b - a;
        float dd = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dd == 0.0f)
            return 0.0f;
        Vec3 ap = p - a;
        float t = (ap.x * d.x + ap.y * d.y + ap.z * d.z) / dd;
        return saturate(t);
    }

    // Schoen family morph (θ: 0→P, π/2→Gyroid)
    inline float tpmsField(const Vec3 &p, float theta, float freq, float iso)
    {
        Vec3 q = p * freq;
        float P = std::cos(q.x) + std::cos(q.y) + std::cos(q.z);
        float G = std::sin(q.x) * std::cos(q.y) +
                  std::sin(q.y) * std::cos(q.z) +
                  std::sin(q.z) * std::cos(q.x);
        return std::cos(theta) * P + std::sin(theta) * G - iso;
    }

    inline float mapToPi(float v, float srcMin, float srcMax)
    {
        return (v - srcMin) * (float)alice2::PI / (srcMax - srcMin); // 0 .. π
    }
};

// Register the sketch
ALICE2_REGISTER_SKETCH_AUTO(ScalarField3DSketch)

#endif // __MAIN__
