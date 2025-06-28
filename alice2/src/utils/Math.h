#pragma once

#ifndef ALICE2_MATH_H
#define ALICE2_MATH_H

// alice2 Math Library - Unified Header
// This header provides backward compatibility by including all math components
// The math library has been refactored into separate files for better organization:
// - Vector.h/cpp: Vec3 class and Z-up coordinate system constants
// - Matrix.h/cpp: Mat4 class and matrix operations
// - Quaternion.h/cpp: Quaternion class and rotation operations
// - MathUtils.h: Utility functions and Z-up coordinate system helpers

#include "Vector.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "MathUtils.h"

#endif // ALICE2_MATH_H
