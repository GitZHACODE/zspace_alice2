#include "Orbit.h"
#include <cmath>

alice2::Vec3 Orbit::position(float time) const {
    return {std::cos(time), std::sin(time), 0.5f};
}
