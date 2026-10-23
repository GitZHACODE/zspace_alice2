#include <alice2/viewer.h>
#include "Orbit.h"

class MinimalSketch : public alice2::ISketch {
public:
    std::string getName() const override { return "Minimal research sketch"; }

    void setup() override { scene().setShowGrid(true); }
    void update(float dt) override { time_ += dt; }
    void draw(alice2::Renderer& renderer, alice2::Camera&) override {
        const auto point = orbit_.position(time_);
        renderer.drawLine(alice2::Vec3(0, 0, 0), point, alice2::Color(1, 0.5f, 0));
        renderer.drawPoint(point, alice2::Color(1, 0.5f, 0), 8.0f);
    }

private:
    Orbit orbit_;
    float time_ = 0;
};

ALICE2_REGISTER_SKETCH_AUTO(MinimalSketch)
