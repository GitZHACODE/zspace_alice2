#pragma once
#include "../utils/Math.h"
#include <algorithm>
#include <cmath>

namespace alice2 {
// Flat geometric normals with a compact studio material, not a full PBR pipeline.
// Procedural vertex colours historically use display/sRGB values; USD colours
// are linear. Material edits are uniforms and require no geometry invalidation.
struct MeshMaterial {
    float metallic = 0;
    float roughness = .5f;
    float specular = 0; // Procedural geometry stays matte unless requested.
    bool linearColors = false;
    Color baseColor{1,1,1,1};
};
inline float srgbToLinear(float value) {
    value=std::max(0.0f,value);
    return value<=.04045f ? value/12.92f : std::pow((value+.055f)/1.055f,2.4f);
}
inline float linearToSrgb(float value) {
    value=std::clamp(value,0.0f,1.0f);
    return value<=.0031308f ? 12.92f*value : 1.055f*std::pow(value,1/2.4f)-.055f;
}
}
