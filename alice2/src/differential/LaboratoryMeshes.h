#pragma once

#include <geometry/MeshData.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>

namespace alice2::differential::laboratory {

inline constexpr double tau = 2 * std::numbers::pi;

enum class Geometry { Plane, Disk, Annulus, Cylinder, Sphere, Saddle, Torus, GenusTwo, CutAnnulus, Imported };

inline void quad(MeshData& mesh, int a, int b, int c, int d) {
    mesh.faces.emplace_back(std::vector<int>{a, b, c});
    mesh.faces.emplace_back(std::vector<int>{a, c, d});
}

inline MeshData plane(int n, double bend = 0, bool irregular = false) {
    MeshData mesh;
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            const bool interior = i > 0 && i < n && j > 0 && j < n;
            const double x = (i + (irregular && interior ? 0.24 * std::sin(2.3*i + 1.9*j) : 0)) / n;
            const double y = (j + (irregular && interior ? 0.25 * std::cos(1.4*i - 2.7*j) : 0)) / n;
            mesh.vertices.emplace_back(Vec3(x, y, bend * ((x-.5)*(x-.5) - (y-.5)*(y-.5))));
        }
    }
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const int a = j * (n+1) + i, b = a+1, d = a+n+1, c = d+1;
            if ((i+j)%2 == 0) quad(mesh,a,b,c,d);
            else quad(mesh,b,c,d,a);
        }
    }
    return mesh;
}

inline MeshData genusTwo(int resolution) {
    MeshData mesh;
    const int k = std::max(2, resolution / 6), nx = 5*k, ny = 3*k;
    const auto filled = [=](int i, int j) {
        return i >= 0 && i < nx && j >= 0 && j < ny &&
            !(j >= k && j < 2*k && ((i >= k && i < 2*k) || (i >= 3*k && i < 4*k)));
    };
    std::map<std::array<int,3>, int> vertices;
    const auto vertex = [&](int i, int j, int layer) {
        const auto [it, inserted] = vertices.try_emplace({i,j,layer}, static_cast<int>(mesh.vertices.size()));
        if (inserted) mesh.vertices.emplace_back(Vec3(2.0*i/nx-1, 1.2*j/ny-.6, layer == 0 ? -.13 : .13));
        return it->second;
    };
    for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
        if (!filled(i,j)) continue;
        const int a = vertex(i,j,0), b = vertex(i+1,j,0), c = vertex(i+1,j+1,0), d = vertex(i,j+1,0);
        const int e = vertex(i,j,1), f = vertex(i+1,j,1), g = vertex(i+1,j+1,1), h = vertex(i,j+1,1);
        quad(mesh, d,c,b,a);
        quad(mesh, e,f,g,h);
        if (!filled(i,j-1)) quad(mesh,a,b,f,e);
        if (!filled(i+1,j)) quad(mesh,b,c,g,f);
        if (!filled(i,j+1)) quad(mesh,c,d,h,g);
        if (!filled(i-1,j)) quad(mesh,d,a,e,h);
    }
    return mesh;
}

inline MeshData makeMesh(Geometry geometry, int n, double bend = 1) {
    if (geometry == Geometry::Plane || geometry == Geometry::Saddle)
        return plane(n, geometry == Geometry::Saddle ? bend : 0);
    if (geometry == Geometry::GenusTwo) return genusTwo(n);

    MeshData mesh;
    const int angular = 2*n;
    const bool cut = geometry == Geometry::CutAnnulus, sphere = geometry == Geometry::Sphere;
    const bool disk = geometry == Geometry::Disk, torus = geometry == Geometry::Torus;
    const int columns = angular + (cut ? 1 : 0), layers = n + (torus || sphere || disk ? 0 : 1) - (sphere ? 1 : 0);
    const auto id = [=](int i, int j) { return j * columns + i % columns; };
    for (int j = 0; j < layers; ++j) for (int i = 0; i < columns; ++i) {
        const double u = tau * (i % angular) / angular, v = tau * j / n, t = 1.0*j/n;
        const double cu = std::cos(u), su = std::sin(u);
        Vec3 p;
        switch (geometry) {
        case Geometry::Sphere: {
            const double latitude = std::numbers::pi * (j+1)/n;
            p = Vec3(std::sin(latitude)*cu, std::sin(latitude)*su, -std::cos(latitude));
            break;
        }
        case Geometry::Cylinder: p = Vec3(.7*cu, .7*su, 2*t-1); break;
        case Geometry::Torus: p = Vec3((1+.38*std::cos(v))*cu, (1+.38*std::cos(v))*su, .38*std::sin(v)); break;
        case Geometry::Disk: p = Vec3((j+1.0)/n*cu, (j+1.0)/n*su, 0); break;
        default: p = Vec3((.40+.60*t)*cu, (.40+.60*t)*su, 0); break;
        }
        mesh.vertices.emplace_back(p);
    }
    for (int j = 0; j < (torus ? layers : layers-1); ++j) for (int i = 0; i < angular; ++i) {
        const int next = (j+1) % layers;
        if (sphere || torus || geometry == Geometry::Cylinder)
            quad(mesh,id(i,j),id(i+1,j),id(i+1,next),id(i,next));
        else
            quad(mesh,id(i+1,j),id(i,j),id(i,next),id(i+1,next));
    }
    if (disk || sphere) {
        const int pole = static_cast<int>(mesh.vertices.size());
        mesh.vertices.emplace_back(Vec3(0,0,sphere ? -1 : 0));
        for (int i = 0; i < angular; ++i) {
            const int a = id(i,0), b = id(i+1,0);
            mesh.faces.emplace_back(std::vector<int>{pole,disk ? a : b,disk ? b : a});
        }
        if (sphere) {
            mesh.vertices.emplace_back(Vec3(0,0,1));
            for (int i = 0; i < angular; ++i)
                mesh.faces.emplace_back(std::vector<int>{pole+1,id(i,layers-1),id(i+1,layers-1)});
        }
    }
    return mesh;
}

} // namespace alice2::differential::laboratory
