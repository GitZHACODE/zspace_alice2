// alice2 Modified SDF Tower Sketch with Configurable Corner Circles
// - Parameters for corner positions & radii centralized
// - Smooth interpolation from corner-ops rect to pure circle
// - Scaled down by 1/10

#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <array>

using namespace alice2;

class SdfTowerSketch : public ISketch {
public:
    // -- Configuration --
    const Vec3 rectHalfSize   = {2.0f, 3.0f, 0.0f};
    const float circleRadius  = 2.0f;
    // Corner circles: {position, radius, subtractFlag}
    struct CornerOp { Vec3 pos; float r; bool subtract; };
    const std::array<CornerOp,4> cornerParams = {
        CornerOp{{ 2.0f,  3.0f, 0.0f}, 1.0f, true},   // top-right: subtract
        CornerOp{{-2.0f, -3.0f, 0.0f}, 1.0f, true},   // bottom-left: subtract
        CornerOp{{-2.0f,  3.0f, 0.0f}, 1.5f, false},  // top-left: union
        CornerOp{{ 2.0f, -3.0f, 0.0f}, 1.5f, false}   // bottom-right: union
    };

    // Fields and contours
    ScalarField2D fieldRect, fieldCircle, fieldCorner, fieldEnd, fieldBlend;
    std::vector<std::vector<std::pair<Vec3, Vec3>>> m_contours;
    std::vector<float> m_levels;

    void setup() override {
        Vec3 bmin(-5, -5, 0), bmax(5, 5, 0);
        int resX = 200, resY = 200;
        fieldRect   = ScalarField2D(bmin, bmax, resX, resY);
        fieldCircle = ScalarField2D(bmin, bmax, resX, resY);
        fieldCorner = ScalarField2D(bmin, bmax, resX, resY);
        fieldEnd    = ScalarField2D(bmin, bmax, resX, resY);
        fieldBlend  = ScalarField2D(bmin, bmax, resX, resY);

        // Base rectangle
        fieldRect.clear_field();
        fieldRect.apply_scalar_rect({0,0,0}, rectHalfSize, 0.0f);

        // Base circle (for end shape)
        fieldCircle.clear_field();
        fieldCircle.apply_scalar_circle({0,0,0}, circleRadius);

        // Build corner-modified field
        fieldCorner = fieldRect;
        for (auto &cp : cornerParams) {
            fieldCircle.clear_field();
            fieldCircle.apply_scalar_circle(cp.pos, cp.r);
            if (cp.subtract)
                fieldCorner.boolean_subtract(fieldCircle);
            else
                fieldCorner.boolean_union(fieldCircle);
        }

        // End shape is interpolation to full circle
        fieldEnd = fieldRect;
        fieldEnd.interpolate(fieldCircle, 1.0f);

        // Layers & contours
        const int numFloors = 100;
        const float spacing = 0.4f;
        m_levels.resize(numFloors);
        m_contours.resize(numFloors);
        for (int i = 0; i < numFloors; ++i)
            m_levels[i] = float(i) / float(numFloors - 1);

        for (int i = 0; i < numFloors; ++i) {
            fieldBlend = fieldCorner;
            fieldBlend.interpolate(fieldEnd, m_levels[i]);
            m_contours[i] = std::move(fieldBlend.get_contours(0.0f).line_segments);
        }
    }

    void update(float dt) override {}

    void draw(Renderer& renderer, Camera& cam) override {
        fieldRect.draw_points(renderer, 1);
        const float spacing = 0.4f;
        for (size_t i = 0; i < m_contours.size(); ++i) {
            float z = float(i) * spacing;
            for (auto& seg : m_contours[i]) {
                Vec3 A = seg.first  + Vec3(0,0,z);
                Vec3 B = seg.second + Vec3(0,0,z);
                renderer.drawLine(A, B, Vec3(1,1,1), 1.0f);
            }
        }
    }

    std::string getName() const override { return "SDF Tower Sketch"; }
    std::string getDescription() const override { return "Configurable corner circles & smooth blend"; }
    std::string getAuthor() const override { return "alice2 User"; }
};

//ALICE2_REGISTER_SKETCH_AUTO(SdfTowerSketch)

#endif // __MAIN__
