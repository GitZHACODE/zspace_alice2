#pragma once
#include "BestFitForceDensitySolver.h"
#include <map>
#include <set>
#include <stdexcept>
#include <cmath>
#include <tuple>

namespace alice2 {

// OBJ exports often duplicate every face corner. Restore shared topology before
// deciding which yellow vertices are on the boundary. Do not average labels.
inline MeshData weldOffsetCableMesh(const MeshData& source) {
    constexpr float tolerance = 1e-3f;
    using Key = std::tuple<long long, long long, long long>;
    std::map<Key, int> lookup;
    MeshData mesh;
    std::vector<int> remap;
    auto role = [](const Color& c) {
        if (c.r <= .02f && c.g <= .02f && c.b <= .02f) return 1;
        if (c.r >= .8f && c.g <= .2f && c.b <= .2f) return 2;
        if (c.r >= .8f && c.g >= .8f && c.b <= .2f) return 3;
        return 0;
    };
    for (const auto& v : source.vertices) {
        const auto p = v.position;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) > 1e12f)
            throw std::runtime_error("Invalid mesh coordinate");
        Key key{std::llround(p.x / tolerance), std::llround(p.y / tolerance), std::llround(p.z / tolerance)};
        auto [it, inserted] = lookup.emplace(key, static_cast<int>(mesh.vertices.size()));
        if (inserted) mesh.vertices.push_back(v);
        else {
            auto& color = mesh.vertices[it->second].color;
            const int oldRole = role(color), newRole = role(v.color);
            if (oldRole && newRole && oldRole != newRole)
                throw std::runtime_error("Coincident vertices have conflicting black/red/yellow constraints");
            if (newRole) color = v.color;
        }
        remap.push_back(it->second);
    }
    for (const auto& face : source.faces) {
        std::vector<int> vertices;
        for (int v : face.vertices) {
            if (v < 0 || v >= static_cast<int>(remap.size())) throw std::runtime_error("Invalid face vertex");
            if (vertices.empty() || vertices.back() != remap[v]) vertices.push_back(remap[v]);
        }
        if (vertices.size() > 1 && vertices.front() == vertices.back()) vertices.pop_back();
        if (vertices.size() < 3) throw std::runtime_error("Face collapsed at weld tolerance 0.001");
        mesh.faces.emplace_back(vertices);
    }
    return mesh;
}

struct OffsetCableNetwork {
    TnaFormDiagram form;
    std::vector<int> sliding;
    std::vector<int> yellowSupports;
    // Original mesh vertex -> cable node, or -1 when not selected.
    std::vector<int> cableNode;
    std::vector<std::string> memberKinds;
    int surfaceVertexCount{0};
};

// Face edges define the physical compression mesh; no reciprocal/exterior faces.
inline OffsetCableNetwork makeOffsetCableNetwork(const MeshData& source, float offset, bool slideYellow = false) {
    if (source.faces.empty() || !std::isfinite(offset) || std::abs(offset) < 1e-5f)
        throw std::runtime_error("Need mesh faces and a nonzero cable offset");
    OffsetCableNetwork result;
    auto& form = result.form;
    form.mesh = source;
    const int n = static_cast<int>(source.vertices.size());
    result.surfaceVertexCount = n;
    result.cableNode.assign(n, -1);
    std::map<std::pair<int, int>, int> incidence;
    for (const auto& face : source.faces) {
        if (face.vertices.size() < 3) throw std::runtime_error("Degenerate input face");
        std::set<int> unique;
        for (int v : face.vertices)
            if (v < 0 || v >= n || !unique.insert(v).second)
                throw std::runtime_error("Invalid or repeated face vertex");
        for (size_t i = 0; i < face.vertices.size(); ++i) {
            int a = face.vertices[i], b = face.vertices[(i + 1) % face.vertices.size()];
            if (++incidence[std::minmax(a, b)] > 2)
                throw std::runtime_error("Nonmanifold mesh edge");
        }
    }
    std::vector<bool> boundary(n, false), yellow(n, false), selected(n, false);
    for (const auto& [edge, count] : incidence) {
        if (count == 1) boundary[edge.first] = boundary[edge.second] = true;
    }
    // Use every polygon corner, so nonplanar quads do not acquire a normal
    // biased toward whichever three vertices happen to appear first.
    for (auto& vertex : form.mesh.vertices) vertex.normal = Vec3(0,0,0);
    for (auto& face : form.mesh.faces) {
        Vec3 normal(0,0,0);
        const auto origin = source.vertices[face.vertices.front()].position;
        for (size_t i = 0; i < face.vertices.size(); ++i) {
            const auto a = source.vertices[face.vertices[i]].position - origin;
            const auto b = source.vertices[face.vertices[(i+1) % face.vertices.size()]].position - origin;
            normal = normal + a.cross(b);
        }
        const float length = normal.length();
        if (!std::isfinite(length) || length < 1e-8f)
            throw std::runtime_error("Degenerate face normal");
        face.normal = normal * (1.f / length);
        for (int v : face.vertices) form.mesh.vertices[v].normal = form.mesh.vertices[v].normal + normal;
    }
    for (auto& vertex : form.mesh.vertices) {
        const float length = vertex.normal.length();
        if (length > 1e-8f) vertex.normal = vertex.normal * (1.f / length);
    }
    for (int v = 0; v < n; ++v) {
        const auto& c = source.vertices[v].color;
        yellow[v] = c.r >= .8f && c.g >= .8f && c.b <= .2f;
        if (c.r <= .02f && c.g <= .02f && c.b <= .02f)
            form.supportVertices.push_back(v);
        else if (c.r >= .8f && c.g <= .2f && c.b <= .2f)
            result.sliding.push_back(v);
        else if (yellow[v] && boundary[v]) {
            result.yellowSupports.push_back(v);
            if (!slideYellow) form.supportVertices.push_back(v);
        }
    }
    for (const auto& [edge, count] : incidence)
        if (yellow[edge.first] && yellow[edge.second])
            selected[edge.first] = selected[edge.second] = true;
    auto add = [&](int a, int b, bool tension, const char* kind) {
        form.activeFormEdges.push_back({a, b, tension});
        result.memberKinds.emplace_back(kind);
    };
    for (const auto& [edge, count] : incidence) add(edge.first, edge.second, false, "compression");
    for (int v = 0; v < n; ++v) {
        if (!selected[v]) continue;
        if (boundary[v]) { result.cableNode[v] = v; continue; }
        auto node = form.mesh.vertices[v];
        const float length = node.normal.length();
        if (!std::isfinite(length) || length < 1e-6f)
            throw std::runtime_error("Interior cable vertex has an undefined normal");
        node.position = node.position + node.normal * (offset / length);
        result.cableNode[v] = static_cast<int>(form.mesh.vertices.size());
        form.mesh.vertices.push_back(node);
        add(v, result.cableNode[v], true, "connector");
    }
    int cables = 0;
    for (const auto& [edge, count] : incidence) {
        if (!yellow[edge.first] || !yellow[edge.second]) continue;
        add(result.cableNode[edge.first], result.cableNode[edge.second], true, "cable");
        ++cables;
    }
    if (!cables) throw std::runtime_error("No mesh edge has two yellow endpoints");
    // Keep explicit members for rendering/export, including coincident cable/mesh edges.
    form.mesh.edges.clear();
    for (const auto& e : form.activeFormEdges) form.mesh.edges.emplace_back(e.vertexA, e.vertexB);
    form.success = true;
    return result;
}

inline BestFitForceDensitySettings offsetCableSettings(const OffsetCableNetwork& network,
                                                      float nodalLoad, float density) {
    BestFitForceDensitySettings settings;
    settings.allowSlidingTensionSupports = false;
    settings.slidingSupportPenalty = 0;
    settings.loads.additionalFixedVertices = network.sliding;
    for (int v : network.yellowSupports)
        if (std::find(network.form.supportVertices.begin(), network.form.supportVertices.end(), v) == network.form.supportVertices.end())
            settings.loads.additionalFixedVertices.push_back(v);
    settings.loads.density = density;
    settings.loads.nodalLoads.assign(network.form.mesh.vertices.size(), 0.f);
    for (int i = 0; i < network.surfaceVertexCount; ++i) settings.loads.nodalLoads[i] = nodalLoad;
    return settings;
}
} // namespace alice2
