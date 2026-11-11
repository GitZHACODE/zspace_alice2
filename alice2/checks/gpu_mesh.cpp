#include <alice2/viewer.h>
#include "utils/Runtime.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace alice2;
namespace {
    void require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    void frame(Renderer& renderer, Camera& camera, MeshObject& mesh, int instances = 1) {
        renderer.beginFrame();
        renderer.setViewport(0, 0, 512, 512);
        renderer.setCamera(camera);
        renderer.clear();
        for (int i = 0; i < instances; ++i) {
            renderer.pushMatrix();
            if (i) renderer.multMatrix(Mat4::translation({0, 0, -0.015f * i}));
            mesh.render(renderer, camera);
            renderer.popMatrix();
        }
        require(glGetError() == GL_NO_ERROR, "OpenGL error while drawing mesh");
        renderer.endFrame();
        glFinish();
    }

    std::vector<unsigned char> pixels() {
        std::vector<unsigned char> result(512 * 512 * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, 512, 512, GL_RGB, GL_UNSIGNED_BYTE, result.data());
        return result;
    }

    std::size_t foregroundPixels(const std::vector<unsigned char>& image) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < image.size(); i += 3)
            if (std::abs(int(image[i])-image[0]) + std::abs(int(image[i+1])-image[1]) +
                std::abs(int(image[i+2])-image[2]) > 10) ++count;
        return count;
    }

    void comparePaths(Renderer& renderer, Camera& camera, MeshObject& mesh, const char* label) {
        renderer.setBufferedMeshesEnabled(false);
        frame(renderer, camera, mesh);
        const auto reference = pixels();
        renderer.setBufferedMeshesEnabled(true);
        frame(renderer, camera, mesh);
        const auto buffered = pixels();
        double error = 0;
        for (std::size_t i = 0; i < reference.size(); ++i) {
            error += std::abs(int(reference[i]) - int(buffered[i]));
        }
        const double meanError = error / reference.size();
        std::cout << label << " legacy/GPU mean pixel difference: " << meanError << '\n';
        require(foregroundPixels(buffered) > 1000, "Mesh did not produce visible pixels");
        require(meanError < 1.0, "Buffered rendering differs from the reference image");
    }

    std::shared_ptr<MeshData> grid(int resolution) {
        auto mesh = std::make_shared<MeshData>();
        const int side = resolution + 1;
        mesh->vertices.reserve(std::size_t(side) * side);
        mesh->triangleIndices.reserve(std::size_t(resolution) * resolution * 6);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            const float px = 2.0f * x / resolution - 1;
            const float py = 2.0f * y / resolution - 1;
            const float z = 0.08f * std::sin(5 * px) * std::cos(5 * py);
            const Vec3 normal(-0.4f * std::cos(5 * px) * std::cos(5 * py),
                               0.4f * std::sin(5 * px) * std::sin(5 * py), 1);
            mesh->vertices.emplace_back(Vec3(px, py, z), normal.normalized(), Color(0.7f, 0.8f, 0.9f));
        }
        for (int y = 0; y < resolution; ++y) for (int x = 0; x < resolution; ++x) {
            const int a = y * side + x, b = a + 1, c = a + side, d = c + 1;
            for (int id : {a, b, d, a, d, c}) mesh->triangleIndices.push_back(id);
        }
        mesh->triangulationDirty = false;
        mesh->markRenderDirty();
        return mesh;
    }

    void checkRawArrays(Renderer& renderer, Camera& camera) {
        Vec3 vertices[] = {{-0.8f,-0.7f,0}, {0.8f,-0.7f,0}, {0,0.8f,0}};
        Vec3 normals[] = {{0,0,1}, {0,0,1}, {0,0,1}};
        Color colors[] = {{1,0,0}, {0,1,0}, {0,0,1}};
        int indices[] = {0,1,2};
        const auto draw = [&](bool buffered, bool indexed, bool attributes, bool lighting) {
            renderer.setBufferedMeshesEnabled(buffered);
            renderer.beginFrame();
            renderer.setViewport(0,0,512,512);
            renderer.setCamera(camera);
            renderer.clear();
            renderer.setColor(Color(0.7f,0.5f,0.3f));
            glNormal3f(0,0,1);
            const auto uploads = renderer.getMeshRenderStats().uploads;
            renderer.drawMesh(vertices, attributes ? normals : nullptr, attributes ? colors : nullptr,
                              3, indexed ? indices : nullptr, indexed ? 3 : 0, lighting);
            require(glGetError() == GL_NO_ERROR, "Raw mesh OpenGL error");
            if (buffered) require(renderer.getMeshRenderStats().uploads > uploads,
                                  "Raw mesh did not use GPU buffers");
            renderer.endFrame();
            glFinish();
            return pixels();
        };
        for (auto mode : {SceneRenderMode::Regular, SceneRenderMode::MeshNormalShaded,
                          SceneRenderMode::MeshTransparent, SceneRenderMode::MeshGray}) {
            renderer.setSceneRenderMode(mode);
            for (bool indexed : {false,true}) for (bool attributes : {false,true})
                for (bool lighting : {false,true}) {
                    const auto reference = draw(false,indexed,attributes,lighting);
                    const auto buffered = draw(true,indexed,attributes,lighting);
                    double error = 0;
                    for (std::size_t i = 0; i < reference.size(); ++i)
                        error += std::abs(int(reference[i])-int(buffered[i]));
                    require(error/reference.size() < 1.0, "Raw GPU mesh differs from legacy rendering");
                }
        }
        renderer.setSceneRenderMode(SceneRenderMode::Regular);
        const auto before = draw(true,true,true,false);
        colors[0] = Color(0,0,0);
        vertices[2].x = 0.6f;
        indices[1] = 2; indices[2] = 1;
        const auto changed = draw(true,true,true,false);
        require(before != changed, "Raw mesh in-place edits were ignored");
        const auto reference = draw(false,true,true,false);
        double error = 0;
        for (std::size_t i = 0; i < reference.size(); ++i)
            error += std::abs(int(reference[i])-int(changed[i]));
        require(error/reference.size() < 1.0, "Edited raw mesh differs from legacy rendering");
        renderer.setBufferedMeshesEnabled(true);
        std::cout << "Raw array GPU paths, display modes, lighting and in-place edits passed\n";
    }

    void benchmark(Renderer& renderer, Camera& camera, int resolution, bool buffered, int instances = 1) {
        MeshObject mesh;
        auto data = grid(resolution);
        // Direct triangle data is intentional: keep the stress test's CPU memory
        // small and test the same indexed geometry uploaded by imported meshes.
        *mesh.getMeshData() = *data;
        data.reset();
        mesh.setRenderMode(MeshRenderMode::NormalShaded);
        renderer.setBufferedMeshesEnabled(buffered);
        frame(renderer, camera, mesh, instances); // Upload/warm up outside timing.
        require(foregroundPixels(pixels()) > 1000, "Benchmark mesh is outside the framebuffer");
        const auto before = renderer.getMeshRenderStats();
        std::vector<double> times;
        const int frames = buffered ? 24 : 4;
        for (int i = 0; i < frames; ++i) {
            camera.orbit({0, 0, 0}, 0.15f, 0.03f, 3.5f);
            const auto start = std::chrono::steady_clock::now();
            frame(renderer, camera, mesh, instances);
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        const auto after = renderer.getMeshRenderStats();
        const std::uint64_t triangles = std::uint64_t(resolution) * resolution * 2 * instances;
        if (buffered) {
            require(after.uploads == before.uploads, "Camera navigation reuploaded geometry");
            require(after.triangles - before.triangles == triangles * frames, "Triangle draw count mismatch");
        }
        std::sort(times.begin(), times.end());
        std::cout << (buffered ? "GPU" : "Legacy") << ", triangles=" << triangles
                  << ", instances=" << instances << ", median_ms=" << times[times.size()/2]
                  << ", p95_ms=" << times[std::min(times.size()-1, std::size_t(times.size()*0.95))]
                  << ", navigation_uploads=" << after.uploads - before.uploads << '\n';
    }
}

int main(int argc, char** argv) {
    GLFWwindow* window = nullptr;
    try {
        initializeRuntime(argv[0]);
        require(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_SAMPLES, 0);
        window = glfwCreateWindow(512, 512, "alice2 GPU mesh checks", nullptr, nullptr);
        require(window != nullptr, "OpenGL context creation failed");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(0);
#ifdef _WIN32
        glewExperimental = GL_TRUE;
        require(glewInit() == GLEW_OK, "GLEW initialization failed");
#endif
        while (glGetError() != GL_NO_ERROR) {}
        std::cout << "GPU: " << glGetString(GL_RENDERER) << '\n';
        Renderer renderer;
        require(renderer.initialize(), "Renderer initialization failed");
        glfwSwapInterval(0);
        Camera camera;
        camera.setPerspective(45, 1, 0.1f, 100);
        camera.setOrbitCenter({0, 0, 0});
        camera.setOrbitDistance(3.5f);
        checkRawArrays(renderer, camera);
        {
            MeshObject mesh;
            mesh.createSphere(0.8f, 48, 24);
            mesh.generateEdgesFromFaces();
            mesh.setRenderMode(MeshRenderMode::Lit);
            comparePaths(renderer, camera, mesh, "Vertex colours");
            for (std::size_t i = 0; i < mesh.getMeshData()->faces.size(); ++i)
                mesh.getMeshData()->faces[i].color = i % 2 ? Color(0.8f, 0.2f, 0.1f) : Color(0.1f, 0.4f, 0.9f);
            mesh.getMeshData()->markRenderDirty();
            mesh.setUseFaceColors(true);
            comparePaths(renderer, camera, mesh, "Face colours");
            mesh.setRenderMode(MeshRenderMode::NormalShaded);
            mesh.setNormalShadingColors({0.9f, 0.8f, 0.7f}, {0.15f, 0.2f, 0.25f});
            comparePaths(renderer, camera, mesh, "Normal shading");
            mesh.setShowEdges(true);
            mesh.setShowVertices(true);
            comparePaths(renderer, camera, mesh, "Edge and vertex overlays");
            mesh.setShowEdges(false);
            mesh.setShowVertices(false);
            const auto initialUploads = renderer.getMeshRenderStats().uploads;
            mesh.getTransform().setMatrix(Mat4::translation({0.1f, 0, 0}));
            camera.orbit({0, 0, 0}, 5, 2, 3.5f);
            frame(renderer, camera, mesh);
            require(renderer.getMeshRenderStats().uploads == initialUploads, "Transform change reuploaded geometry");
            mesh.translateMesh({0, 0, 0.1f});
            frame(renderer, camera, mesh);
            require(renderer.getMeshRenderStats().uploads == initialUploads + 1, "Geometry edit was not uploaded");
            mesh.setRenderMode(MeshRenderMode::Lit);
            mesh.setUseFaceColors(false);
            for (auto& vertex : mesh.getMeshData()->vertices) vertex.color = {0.2f, 0.8f, 0.3f, 0.6f};
            mesh.getMeshData()->markRenderDirty();
            comparePaths(renderer, camera, mesh, "Edited transparent colours");
            for (auto mode : {SceneRenderMode::MeshWireframe, SceneRenderMode::MeshWireframeWithVertices,
                              SceneRenderMode::MeshGray, SceneRenderMode::MeshTransparent}) {
                renderer.setSceneRenderMode(mode);
                comparePaths(renderer, camera, mesh, "Scene mode override");
                const auto uploads = renderer.getMeshRenderStats().uploads;
                camera.orbit({0, 0, 0}, 0.1f, 0, 3.5f);
                frame(renderer, camera, mesh);
                require(renderer.getMeshRenderStats().uploads == uploads, "Overlay navigation uploaded geometry");
            }
            renderer.setSceneRenderMode(SceneRenderMode::Regular);
            const auto beforeExport = renderer.getMeshRenderStats().drawCalls;
            renderer.setVectorExportMode(true);
            frame(renderer, camera, mesh);
            renderer.setVectorExportMode(false);
            require(renderer.getMeshRenderStats().drawCalls == beforeExport, "Vector export used GPU buffers");
            mesh.getMeshData()->clear();
            frame(renderer, camera, mesh);
            require(renderer.getMeshRenderStats().cachedMeshes == 0, "Clearing mesh retained GPU buffers");
            require(glGetError() == GL_NO_ERROR, "OpenGL error after buffered mesh drawing");
        }
        renderer.endFrame();
        require(renderer.getMeshRenderStats().cachedMeshes == 0, "Destroyed mesh retained GPU buffers");
        {
            MeshObject mesh;
            mesh.createCube(1);
            frame(renderer, camera, mesh);
            require(renderer.getMeshRenderStats().cachedMeshes == 1, "Cube did not enter the GPU cache");
        }
        renderer.endFrame();
        require(renderer.getMeshRenderStats().cachedMeshes == 0, "Expired mesh retained GPU buffers");
        benchmark(renderer, camera, 512, false);
        benchmark(renderer, camera, 512, true);
        benchmark(renderer, camera, 1024, true);
        benchmark(renderer, camera, 1536, true);
        benchmark(renderer, camera, 1024, true, 8);
        if (argc > 1 && std::string(argv[1]) == "--large") benchmark(renderer, camera, 2048, true);
        renderer.shutdown();
        require(renderer.getMeshRenderStats().cachedMeshes == 0, "Renderer shutdown retained mesh buffers");
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        std::cout << "GPU mesh image, invalidation, export, lifetime and scale checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
