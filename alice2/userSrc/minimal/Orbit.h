#pragma once
#include <alice2/core.h>

// An experiment-local class needs only the geometry core.
class Orbit {
public:
    alice2::Vec3 position(float time) const;
};
