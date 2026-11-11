#include "Renderer.h"
#include "../geometry/MeshData.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace alice2 {
namespace {
    GLuint compileShader(GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        if (!shader) return 0;
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[2048] = {};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "Mesh shader compile failed: " << log << '\n';
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    // Compatibility GLSL keeps the existing matrix stack and immediate-mode
    // UI available. Only meshes use this program; shading runs on the GPU.
    constexpr const char* vertexShader = R"glsl(#version 120
uniform int normalShaded;
uniform vec4 frontColor;
uniform vec4 backColor;
uniform vec4 constantColor;
uniform float alphaOverride;
varying vec4 meshColor;
void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    meshColor = gl_Color;
    if (normalShaded == 1) {
        vec3 n = gl_NormalMatrix * gl_Normal;
        float magnitude = length(n);
        n = magnitude > 0.0 ? n / magnitude : vec3(0.0);
        // Camera forward is -Z in view space. Interpolate the vertex colours
        // just as the old CPU shading did, including rigid joint transforms.
        float t = clamp(0.5 - 0.5 * n.z, 0.0, 1.0);
        meshColor = mix(backColor, frontColor, t);
    }
    if (normalShaded == 2) meshColor = constantColor;
    if (alphaOverride >= 0.0) meshColor.a = alphaOverride;
}
)glsl";
    constexpr const char* fragmentShader = R"glsl(#version 120
varying vec4 meshColor;
void main() { gl_FragColor = meshColor; }
)glsl";
}

class GpuMeshRenderer {
public:
    struct Entry {
        std::weak_ptr<MeshData> owner;
        std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
        GLuint vertices = 0, indices = 0, faceVertices = 0, edgeVertices = 0;
        GLsizei indexCount = 0, faceVertexCount = 0, edgeVertexCount = 0;
        std::size_t vertexCount = 0, triangleIndexCount = 0, edgeCount = 0;
        bool transparent = false, faceTransparent = false;
    };
    GLFWwindow* context = glfwGetCurrentContext();
    GLuint program = 0;
    GLuint streamingBuffers[4] = {};
    GLint normalUniform = -1, frontUniform = -1, backUniform = -1, constantUniform = -1, alphaUniform = -1;
    std::unordered_map<const MeshData*, Entry> entries;

    static void release(Entry& entry) {
        const GLuint buffers[] = {entry.vertices, entry.indices, entry.faceVertices, entry.edgeVertices};
        glDeleteBuffers(4, buffers);
        entry = Entry{};
    }

    bool initialize() {
        if (!context || !glfwGetProcAddress("glCreateShader") || !glfwGetProcAddress("glGenBuffers")) return false;
        const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexShader);
        const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentShader);
        if (!vertex || !fragment) {
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            return false;
        }
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint success = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char log[2048] = {};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::cerr << "Mesh shader link failed: " << log << '\n';
            glDeleteProgram(program);
            program = 0;
            return false;
        }
        normalUniform = glGetUniformLocation(program, "normalShaded");
        frontUniform = glGetUniformLocation(program, "frontColor");
        backUniform = glGetUniformLocation(program, "backColor");
        constantUniform = glGetUniformLocation(program, "constantColor");
        alphaUniform = glGetUniformLocation(program, "alphaOverride");
        return true;
    }

    bool upload(Entry& entry, const std::shared_ptr<MeshData>& mesh, MeshRenderStats& stats) {
        release(entry);
        entry.owner = mesh;
        std::vector<GLuint> indices;
        indices.reserve(mesh->triangleIndices.size());
        const auto valid = [&](int i) { return i >= 0 && static_cast<std::size_t>(i) < mesh->vertices.size(); };
        for (std::size_t i = 0; i + 2 < mesh->triangleIndices.size(); i += 3) {
            if (valid(mesh->triangleIndices[i]) && valid(mesh->triangleIndices[i+1]) && valid(mesh->triangleIndices[i+2]))
                for (int j = 0; j < 3; ++j) indices.push_back(static_cast<GLuint>(mesh->triangleIndices[i+j]));
        }
        if (indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
        glGenBuffers(1, &entry.vertices);
        glGenBuffers(1, &entry.indices);
        glBindBuffer(GL_ARRAY_BUFFER, entry.vertices);
        glBufferData(GL_ARRAY_BUFFER, mesh->vertices.size() * sizeof(MeshVertex), mesh->vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
        if (glGetError() != GL_NO_ERROR) {
            std::cerr << "Mesh buffer upload failed; using compatibility rendering\n";
            release(entry);
            return false;
        }
        entry.indexCount = static_cast<GLsizei>(indices.size());
        entry.vertexCount = mesh->vertices.size();
        entry.triangleIndexCount = mesh->triangleIndices.size();
        entry.edgeCount = mesh->edges.size();
        entry.revision = mesh->renderRevision;
        entry.transparent = std::any_of(mesh->vertices.begin(), mesh->vertices.end(),
            [](const MeshVertex& v) { return v.color.a < 1.0f; });
        ++stats.uploads;
        stats.uploadedBytes += mesh->vertices.size() * sizeof(MeshVertex) + indices.size() * sizeof(GLuint);
        return true;
    }

    bool uploadFaceColors(Entry& entry, const MeshData& mesh, MeshRenderStats& stats) {
        std::vector<MeshVertex> vertices;
        vertices.reserve(entry.indexCount);
        for (const auto& face : mesh.faces) {
            entry.faceTransparent |= face.color.a < 1.0f;
            for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                const int ids[] = {face.vertices[0], face.vertices[i], face.vertices[i+1]};
                if (std::any_of(std::begin(ids), std::end(ids), [&](int id) {
                    return id < 0 || static_cast<std::size_t>(id) >= mesh.vertices.size();
                })) continue;
                for (int id : ids) {
                    auto vertex = mesh.vertices[id];
                    vertex.color = face.color;
                    vertices.push_back(vertex);
                }
            }
        }
        if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
        glGenBuffers(1, &entry.faceVertices);
        glBindBuffer(GL_ARRAY_BUFFER, entry.faceVertices);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(MeshVertex), vertices.data(), GL_STATIC_DRAW);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteBuffers(1, &entry.faceVertices);
            entry.faceVertices = 0;
            return false;
        }
        entry.faceVertexCount = static_cast<GLsizei>(vertices.size());
        ++stats.uploads;
        stats.uploadedBytes += vertices.size() * sizeof(MeshVertex);
        return true;
    }

    struct EdgeVertex { Vec3 position; Color color; };
    bool uploadEdges(Entry& entry, const MeshData& mesh, MeshRenderStats& stats) {
        std::vector<EdgeVertex> vertices;
        vertices.reserve(mesh.edges.size() * 2);
        for (const auto& edge : mesh.edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                static_cast<std::size_t>(edge.vertexA) >= mesh.vertices.size() ||
                static_cast<std::size_t>(edge.vertexB) >= mesh.vertices.size()) continue;
            vertices.push_back({mesh.vertices[edge.vertexA].position, edge.color});
            vertices.push_back({mesh.vertices[edge.vertexB].position, edge.color});
        }
        if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
        glGenBuffers(1, &entry.edgeVertices);
        glBindBuffer(GL_ARRAY_BUFFER, entry.edgeVertices);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(EdgeVertex), vertices.data(), GL_STATIC_DRAW);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteBuffers(1, &entry.edgeVertices);
            entry.edgeVertices = 0;
            return false;
        }
        entry.edgeVertexCount = static_cast<GLsizei>(vertices.size());
        ++stats.uploads;
        stats.uploadedBytes += vertices.size() * sizeof(EdgeVertex);
        return true;
    }
};

void Renderer::initializeMeshBuffers() {
    m_meshRenderStats = {};
    auto buffers = std::make_shared<GpuMeshRenderer>();
    if (buffers->initialize()) {
        m_gpuMeshes = std::move(buffers);
        std::cout << "GPU buffered mesh rendering enabled\n";
    } else {
        std::cerr << "GPU mesh buffers unavailable; using compatibility rendering\n";
    }
}

void Renderer::shutdownMeshBuffers() {
    if (!m_gpuMeshes) return;
    if (glfwGetCurrentContext() == m_gpuMeshes->context) {
        for (auto& item : m_gpuMeshes->entries) GpuMeshRenderer::release(item.second);
        glDeleteProgram(m_gpuMeshes->program);
        glDeleteBuffers(4, m_gpuMeshes->streamingBuffers);
    }
    m_gpuMeshes.reset();
    m_meshRenderStats.cachedMeshes = 0;
}

// Pointer-only callers provide no ownership or revision information. Stream the
// current arrays on every draw so in-place edits and reused addresses stay safe.
// Fixed-function arrays retain drawMesh's existing lighting and colour semantics.
bool Renderer::drawMeshArraysBuffered(const Vec3* vertices, const Vec3* normals,
                                      const Color* colors, int vertexCount,
                                      const int* indices, int indexCount) {
    if (!m_initialized || !m_bufferedMeshesEnabled || m_vectorExportMode || !m_gpuMeshes ||
        glfwGetCurrentContext() != m_gpuMeshes->context || vertexCount <= 0) return false;
    const bool indexed = indices && indexCount > 0;
    if (indexed && (indexCount % 3 || std::any_of(indices, indices + indexCount,
        [vertexCount](int i) { return i < 0 || i >= vertexCount; }))) return false;

    GLint oldVertices = 0, oldIndices = 0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldVertices);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &oldIndices);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    const auto restore = [&] {
        glPopClientAttrib();
        glBindBuffer(GL_ARRAY_BUFFER, oldVertices);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oldIndices);
    };
    auto& buffers = m_gpuMeshes->streamingBuffers;
    if (!buffers[0]) glGenBuffers(4, buffers);
    std::uint64_t bytes = 0;
    const auto upload = [&](int slot, GLenum target, const void* data, std::size_t size) {
        glBindBuffer(target, buffers[slot]);
        glBufferData(target, size, data, GL_STREAM_DRAW);
        bytes += size;
    };
    upload(0, GL_ARRAY_BUFFER, vertices, std::size_t(vertexCount) * sizeof(Vec3));
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Vec3), nullptr);
    glDisableClientState(GL_NORMAL_ARRAY);
    if (normals) {
        upload(1, GL_ARRAY_BUFFER, normals, std::size_t(vertexCount) * sizeof(Vec3));
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, sizeof(Vec3), nullptr);
    }
    glDisableClientState(GL_COLOR_ARRAY);
    if (colors) {
        upload(2, GL_ARRAY_BUFFER, colors, std::size_t(vertexCount) * sizeof(Color));
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_FLOAT, sizeof(Color), nullptr);
    }
    if (indexed) upload(3, GL_ELEMENT_ARRAY_BUFFER, indices, std::size_t(indexCount) * sizeof(int));
    if (glGetError() != GL_NO_ERROR) { restore(); return false; }
    if (indexed) glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    else glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    if (normals) {
        const auto& last = normals[indexed ? indices[indexCount-1] : vertexCount-1];
        glNormal3f(last.x, last.y, last.z);
    }
    ++m_meshRenderStats.uploads;
    m_meshRenderStats.uploadedBytes += bytes;
    ++m_meshRenderStats.drawCalls;
    m_meshRenderStats.triangles += (indexed ? indexCount : vertexCount) / 3;
    restore();
    return true;
}

void Renderer::collectMeshBuffers() {
    if (!m_gpuMeshes || glfwGetCurrentContext() != m_gpuMeshes->context) return;
    auto& entries = m_gpuMeshes->entries;
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->second.owner.expired()) {
            GpuMeshRenderer::release(it->second);
            it = entries.erase(it);
        } else ++it;
    }
    m_meshRenderStats.cachedMeshes = entries.size();
}

bool Renderer::drawMeshBuffered(const std::shared_ptr<MeshData>& mesh, bool normalShaded,
                                const Color& front, const Color& back, bool faceColors,
                                bool showFaces, bool showEdges, bool showVertices,
                                float edgeWidth, float vertexSize) {
    if (!m_initialized || !m_bufferedMeshesEnabled || m_vectorExportMode || !m_gpuMeshes ||
        glfwGetCurrentContext() != m_gpuMeshes->context || !mesh) return false;
    if (mesh->vertices.empty()) {
        auto it = m_gpuMeshes->entries.find(mesh.get());
        if (it != m_gpuMeshes->entries.end()) {
            GpuMeshRenderer::release(it->second);
            m_gpuMeshes->entries.erase(it);
            m_meshRenderStats.cachedMeshes = m_gpuMeshes->entries.size();
        }
        return true;
    }
    if (mesh->vertices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
    static_assert(std::is_standard_layout_v<MeshVertex>);
    const bool overrideMode = m_sceneRenderMode != SceneRenderMode::Regular;
    const bool globalNormals = m_sceneRenderMode == SceneRenderMode::MeshNormalShaded;
    const bool gray = m_sceneRenderMode == SceneRenderMode::MeshGray;
    const bool transparentMode = m_sceneRenderMode == SceneRenderMode::MeshTransparent;
    if (overrideMode) {
        showFaces = globalNormals || gray || transparentMode;
        showEdges = !globalNormals;
        showVertices = m_sceneRenderMode == SceneRenderMode::MeshWireframeWithVertices;
        normalShaded = globalNormals;
    }

    GLint oldProgram = 0, oldVertices = 0, oldIndices = 0, oldDepthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &oldDepthFunc);
    GLboolean oldDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldVertices);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &oldIndices);
    glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_POINT_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    auto restore = [&] {
        glUseProgram(oldProgram);
        glPopClientAttrib();
        glBindBuffer(GL_ARRAY_BUFFER, oldVertices);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oldIndices);
        glPopAttrib();
    };

    auto& entry = m_gpuMeshes->entries[mesh.get()];
    const auto previous = entry.owner.lock();
    if (previous != mesh || entry.revision != mesh->renderRevision ||
        entry.vertexCount != mesh->vertices.size() || entry.triangleIndexCount != mesh->triangleIndices.size() ||
        entry.edgeCount != mesh->edges.size()) {
        if (!m_gpuMeshes->upload(entry, mesh, m_meshRenderStats)) { restore(); return false; }
    }
    const bool useFaces = faceColors && !normalShaded && !overrideMode;
    if (showFaces && useFaces && !entry.faceVertices && !m_gpuMeshes->uploadFaceColors(entry, *mesh, m_meshRenderStats)) {
        restore(); return false;
    }
    if (showEdges && !mesh->edges.empty() && !entry.edgeVertices &&
        !m_gpuMeshes->uploadEdges(entry, *mesh, m_meshRenderStats)) { restore(); return false; }
    const bool transparent = gray || transparentMode || m_currentColor.a < 1.0f || (normalShaded
        ? front.a < 1.0f || back.a < 1.0f : useFaces ? entry.faceTransparent : entry.transparent);
    if (transparent) { glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); }
    glDisable(GL_LIGHTING);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glUseProgram(m_gpuMeshes->program);
    glUniform1i(m_gpuMeshes->normalUniform, gray ? 2 : normalShaded ? 1 : 0);
    glUniform4f(m_gpuMeshes->constantUniform, 0.5f, 0.5f, 0.5f, 0.18f);
    glUniform1f(m_gpuMeshes->alphaUniform, gray || transparentMode ? 0.18f : -1.0f);
    const auto frontColor = globalNormals ? Color(1, 1, 1, 1) : front;
    const auto backColor = globalNormals ? Color(0.18f, 0.18f, 0.18f, 1) : back;
    glUniform4f(m_gpuMeshes->frontUniform, frontColor.r, frontColor.g, frontColor.b, frontColor.a);
    glUniform4f(m_gpuMeshes->backUniform, backColor.r, backColor.g, backColor.b, backColor.a);
    glBindBuffer(GL_ARRAY_BUFFER, useFaces ? entry.faceVertices : entry.vertices);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<const void*>(offsetof(MeshVertex, position)));
    glNormalPointer(GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<const void*>(offsetof(MeshVertex, normal)));
    glColorPointer(4, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<const void*>(offsetof(MeshVertex, color)));
    if (overrideMode) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (showFaces && useFaces && entry.faceVertexCount) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, entry.faceVertexCount);
        ++m_meshRenderStats.drawCalls;
        m_meshRenderStats.triangles += entry.faceVertexCount / 3;
    } else if (showFaces && entry.indexCount) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.indices);
        glDrawElements(GL_TRIANGLES, entry.indexCount, GL_UNSIGNED_INT, nullptr);
        ++m_meshRenderStats.drawCalls;
        m_meshRenderStats.triangles += entry.indexCount / 3;
    }
    // Overlays use their own cached streams, preserving polygon boundary edges
    // rather than showing triangulation diagonals. Materials and camera changes
    // are uniforms/state changes, never uploads.
    glDepthMask(oldDepthMask);
    glUniform1f(m_gpuMeshes->alphaUniform, -1.0f);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    auto drawVertices = [&] {
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glBindBuffer(GL_ARRAY_BUFFER, entry.vertices);
        glVertexPointer(3, GL_FLOAT, sizeof(MeshVertex), reinterpret_cast<const void*>(offsetof(MeshVertex, position)));
        glUniform1i(m_gpuMeshes->normalUniform, 2);
        const auto color = overrideMode ? Color(1, 0, 120.0f/255.0f, 1) : m_currentColor;
        glUniform4f(m_gpuMeshes->constantUniform, color.r, color.g, color.b, color.a);
        glPointSize(overrideMode ? std::max(vertexSize, 5.0f) : vertexSize);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(entry.vertexCount));
        ++m_meshRenderStats.drawCalls;
    };
    if (showVertices && !overrideMode) drawVertices();
    if (showEdges && entry.edgeVertexCount) {
        using EdgeVertex = GpuMeshRenderer::EdgeVertex;
        glBindBuffer(GL_ARRAY_BUFFER, entry.edgeVertices);
        glVertexPointer(3, GL_FLOAT, sizeof(EdgeVertex), reinterpret_cast<const void*>(offsetof(EdgeVertex, position)));
        glColorPointer(4, GL_FLOAT, sizeof(EdgeVertex), reinterpret_cast<const void*>(offsetof(EdgeVertex, color)));
        glEnableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glUniform1i(m_gpuMeshes->normalUniform, overrideMode ? 2 : 0);
        glUniform4f(m_gpuMeshes->constantUniform, 0, 0, 0, 1);
        glLineWidth(overrideMode ? std::max(edgeWidth, 1.0f) : edgeWidth);
        glDepthFunc(GL_LEQUAL);
        glDrawArrays(GL_LINES, 0, entry.edgeVertexCount);
        ++m_meshRenderStats.drawCalls;
        glDepthFunc(oldDepthFunc);
    }
    if (showVertices && overrideMode) drawVertices();
    m_meshRenderStats.cachedMeshes = m_gpuMeshes->entries.size();
    restore();
    return true;
}
} // namespace alice2
