#include "SceneIO.h"

#if !defined(ALICE2_USD_LEGACY_TBB) && __has_include(<tbb/version.h>)
#include <tbb/version.h> // USD 25.11 expects this through an older transitive include.
#endif
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include <cmath>
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE

namespace alice2 {
namespace {
const TfToken graphPoints("alice2:graph:points"), graphEdges("alice2:graph:edges");
const TfToken objectName("alice2:name"), objectColor("alice2:color");
const TfToken vertexColors("alice2:vertexColors"), faceColors("alice2:faceColors");
const TfToken useFaceColors("alice2:useFaceColors");
const TfToken edgeColor("alice2:edgeColor");
const TfToken materialParameters("alice2:material:parameters"), linearColors("alice2:material:linearColors");
const TfToken materialBaseColor("alice2:material:baseColor");

void initializeUSD() {
    static std::once_flag once;
    std::call_once(once, [] {
        // SDK-relative discovery remains active. The staged path supports moved apps.
        const auto local = std::filesystem::current_path() / "usd";
        if (std::filesystem::exists(local)) PlugRegistry::GetInstance().RegisterPlugins(local.string());
#ifdef ALICE2_USD_PLUGIN_DIR
        PlugRegistry::GetInstance().RegisterPlugins(ALICE2_USD_PLUGIN_DIR);
#endif
    });
}

void require(bool valid, const std::string& message) {
    if (!valid) throw std::runtime_error("Scene I/O: " + message);
}

GfVec3f usdPoint(const Vec3& value) { return {value.x, value.y, value.z}; }
GfVec4f usdColor(const Color& value) { return {value.r, value.g, value.b, value.a}; }
Color color(const GfVec4f& value) { return {value[0], value[1], value[2], value[3]}; }
Vec3 point(const GfVec3f& value) { return {value[0], value[1], value[2]}; }

Color materialColor(const UsdPrim& prim) {
    const auto material = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
    if (!material) return {1, 1, 1, 1};
    const auto shader = material.ComputeSurfaceSource();
    if (!shader) return {1, 1, 1, 1};
    GfVec3f rgb(1);
    float alpha = 1;
    shader.GetInput(TfToken("diffuseColor")).Get(&rgb);
    shader.GetInput(TfToken("opacity")).Get(&alpha);
    return {rgb[0], rgb[1], rgb[2], alpha};
}

MeshMaterial readMaterial(const UsdPrim& prim) {
    MeshMaterial result;
    result.baseColor=materialColor(prim);
    // External USD colour inputs/primvars are linear. Older alice2 snapshots
    // carry exact display colours; retain their previous convention.
    result.linearColors=!prim.HasAttribute(vertexColors);
    const auto material=UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
    if (material) if (const auto shader=material.ComputeSurfaceSource()) {
        result.specular=.5f;
        shader.GetInput(TfToken("metallic")).Get(&result.metallic);
        shader.GetInput(TfToken("roughness")).Get(&result.roughness);
        shader.GetInput(TfToken("specular")).Get(&result.specular);
    }
    GfVec3f parameters(result.metallic,result.roughness,result.specular);
    prim.GetAttribute(materialParameters).Get(&parameters);
    prim.GetAttribute(linearColors).Get(&result.linearColors);
    // Explicit geometry colours override the material-colour fallback, as in
    // earlier imports. Native snapshots retain the two factors independently.
    const UsdGeomGprim gprim(prim);
    if (gprim.GetDisplayColorPrimvar().GetAttr().HasAuthoredValueOpinion() || prim.HasAttribute(vertexColors) || prim.HasAttribute(faceColors)) {
        result.baseColor.r=result.baseColor.g=result.baseColor.b=1;
    }
    if (gprim.GetDisplayOpacityPrimvar().GetAttr().HasAuthoredValueOpinion() || prim.HasAttribute(vertexColors) || prim.HasAttribute(faceColors))
        result.baseColor.a=1;
    GfVec4f base=usdColor(result.baseColor);
    prim.GetAttribute(materialBaseColor).Get(&base);
    result.baseColor=color(base);
    for (int i=0;i<4;++i) require(std::isfinite(base[i]) && base[i]>=0 && base[i]<=1,"invalid material colour");
    for (int i=0;i<3;++i) require(std::isfinite(parameters[i]) && parameters[i]>=0 && parameters[i]<=1,"invalid mesh material");
    result.metallic=parameters[0]; result.roughness=parameters[1]; result.specular=parameters[2];
    return result;
}

Mat4 matrix(const GfMatrix4d& value) {
    Mat4 result;
    for (int column = 0; column != 4; ++column)
        for (int row = 0; row != 4; ++row)
            result.m[column * 4 + row] = static_cast<float>(value[column][row]);
    return result;
}

GfMatrix4d matrix(const Mat4& value) {
    GfMatrix4d result(1);
    for (int column = 0; column != 4; ++column)
        for (int row = 0; row != 4; ++row)
            result[column][row] = value.m[column * 4 + row];
    return result;
}

void validate(const SceneItem& item) {
    for (float component : item.transform.m)
        require(std::isfinite(component), "non-finite transform: " + item.name);
    const auto validPoint = [&](const auto& vertex) {
        const auto& p = vertex.position;
        require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
                "non-finite point: " + item.name);
    };
    const auto index = [&](int value, std::size_t count) {
        require(value >= 0 && static_cast<std::size_t>(value) < count, "invalid topology: " + item.name);
    };
    if (const auto* mesh = std::get_if<MeshData>(&item.geometry)) {
        for (float value:{item.material.metallic,item.material.roughness,item.material.specular})
            require(std::isfinite(value) && value>=0 && value<=1,"invalid mesh material: "+item.name);
        for (float value:{item.material.baseColor.r,item.material.baseColor.g,item.material.baseColor.b,item.material.baseColor.a})
            require(std::isfinite(value) && value>=0 && value<=1,"invalid material colour: "+item.name);
        for (const auto& vertex : mesh->vertices) validPoint(vertex);
        for (const auto& face : mesh->faces) {
            require(face.vertices.size() >= 3, "face has fewer than three vertices: " + item.name);
            for (int value : face.vertices) index(value, mesh->vertices.size());
        }
    } else {
        const auto& graph = std::get<GraphData>(item.geometry);
        for (const auto& vertex : graph.vertices) validPoint(vertex);
        for (const auto& edge : graph.edges) {
            index(edge.vertexA, graph.vertices.size());
            index(edge.vertexB, graph.vertices.size());
        }
    }
}

template<class Vertices>
void readColors(const UsdGeomGprim& prim, Vertices& vertices, std::vector<MeshFace>* faces = nullptr, bool useMaterialFallback = true) {
    VtArray<GfVec3f> colors;
    VtArray<float> opacity;
    const auto pv = prim.GetDisplayColorPrimvar();
    const auto alpha = prim.GetDisplayOpacityPrimvar();
    pv.ComputeFlattened(&colors);
    alpha.ComputeFlattened(&opacity);
    auto fallback = useMaterialFallback ? materialColor(prim.GetPrim()) : Color(1,1,1,1);
    if (opacity.size() == 1) fallback.a = opacity[0];
    for (auto& vertex : vertices) vertex.color = fallback;
    if (faces) for (auto& face : *faces) face.color = fallback;
    const auto sample = [&](std::size_t i) {
        const auto rgb = colors[colors.size() == 1 ? 0 : i];
        const float a = opacity.size() == colors.size() ? opacity[colors.size() == 1 ? 0 : i]
                      : opacity.size() == 1 ? opacity[0] : 1.0f;
        return Color(rgb[0], rgb[1], rgb[2], a);
    };
    if (colors.size() == 1) {
        for (auto& vertex : vertices) vertex.color = sample(0);
        if (faces) for (auto& face : *faces) face.color = sample(0);
    } else if (pv.GetInterpolation() == UsdGeomTokens->uniform && faces && colors.size() == faces->size()) {
        for (std::size_t i = 0; i < faces->size(); ++i) (*faces)[i].color = sample(i);
    } else if ((pv.GetInterpolation() == UsdGeomTokens->vertex || pv.GetInterpolation() == UsdGeomTokens->varying)
               && colors.size() == vertices.size()) {
        for (std::size_t i = 0; i < vertices.size(); ++i) vertices[i].color = sample(i);
    }
    // Exact alice2 colors take precedence over the portable display primvars.
    VtArray<GfVec4f> exact;
    if (prim.GetPrim().GetAttribute(vertexColors).Get(&exact) && exact.size() == vertices.size())
        for (std::size_t i = 0; i < vertices.size(); ++i) vertices[i].color = color(exact[i]);
    if (faces && prim.GetPrim().GetAttribute(faceColors).Get(&exact) && exact.size() == faces->size())
        for (std::size_t i = 0; i < faces->size(); ++i) (*faces)[i].color = color(exact[i]);
}

Color portableColor(const Color& color, const MeshMaterial* material) {
    if (!material) return color;
    const auto linear=[&](float value) { return material->linearColors ? value : srgbToLinear(value); };
    return {linear(color.r)*linear(material->baseColor.r),linear(color.g)*linear(material->baseColor.g),
        linear(color.b)*linear(material->baseColor.b),color.a*material->baseColor.a};
}
template<class Vertices>
void writeColors(const UsdGeomGprim& prim, const Vertices& vertices, const MeshMaterial* material = nullptr) {
    VtArray<GfVec3f> rgb;
    VtArray<float> alpha;
    VtArray<GfVec4f> exact;
    for (const auto& vertex : vertices) {
        const auto& c = vertex.color;
        const auto portable=portableColor(c,material);
        rgb.push_back({portable.r,portable.g,portable.b}); alpha.push_back(portable.a); exact.push_back(usdColor(c));
    }
    prim.CreateDisplayColorPrimvar(UsdGeomTokens->vertex).Set(rgb);
    prim.CreateDisplayOpacityPrimvar(UsdGeomTokens->vertex).Set(alpha);
    prim.GetPrim().CreateAttribute(vertexColors, SdfValueTypeNames->Color4fArray, true).Set(exact);
}
} // namespace

std::vector<SceneItem> readSceneFile(const std::filesystem::path& path) {
    initializeUSD();
    const auto stage = UsdStage::Open(path.string());
    require(bool(stage), "cannot open " + path.string());
    const double units = UsdGeomGetStageMetersPerUnit(stage);
    require(std::isfinite(units) && units > 0, "invalid metersPerUnit");
    Mat4 conversion = Mat4::scale(Vec3(static_cast<float>(units), static_cast<float>(units), static_cast<float>(units)));
    const auto up = UsdGeomGetStageUpAxis(stage);
    require(up == UsdGeomTokens->y || up == UsdGeomTokens->z, "unsupported up axis");
    if (up == UsdGeomTokens->y) conversion = conversion * Mat4::rotation({1, 0, 0}, PI * 0.5f);
    std::vector<SceneItem> result;
    for (const auto& prim : stage->Traverse(UsdTraverseInstanceProxies())) {
        const UsdGeomMesh mesh(prim);
        const UsdGeomBasisCurves curves(prim);
        const UsdGeomPoints points(prim);
        if (!mesh && !curves && !points) continue;
        SceneItem item;
        item.name = prim.GetPath().GetString();
        prim.GetAttribute(objectName).Get(&item.name);
        // Keep the transform cache inside USD. USD 25.11 chooses different
        // TfHashMap layouts for GCC and Clang; constructing a cache here and
        // letting a differently compiled SDK populate it corrupts its storage.
        item.transform = conversion * matrix(UsdGeomXformable(prim).ComputeLocalToWorldTransform(UsdTimeCode::Default()));
        const UsdGeomGprim gprim(prim);
        item.visible = gprim.ComputeVisibility() != UsdGeomTokens->invisible;
        GfVec4f tint(1);
        if (prim.GetAttribute(objectColor).Get(&tint)) item.color = color(tint);
        VtArray<GfVec3f> positions;
        if (mesh) {
            VtArray<int> counts, indices;
            mesh.GetPointsAttr().Get(&positions);
            mesh.GetFaceVertexCountsAttr().Get(&counts);
            mesh.GetFaceVertexIndicesAttr().Get(&indices);
            MeshData data;
            item.material=readMaterial(prim);
            data.vertices.reserve(positions.size());
            for (const auto& p : positions) data.vertices.emplace_back(point(p));
            std::size_t offset = 0;
            TfToken orientation;
            mesh.GetOrientationAttr().Get(&orientation);
            for (int count : counts) {
                require(count >= 3 && static_cast<std::size_t>(count) <= indices.size() - offset,
                        "invalid mesh face counts at " + item.name);
                std::vector<int> face(indices.begin() + offset, indices.begin() + offset + count);
                if (orientation == UsdGeomTokens->leftHanded) std::reverse(face.begin(), face.end());
                data.faces.emplace_back(face);
                offset += count;
            }
            require(offset == indices.size(), "unused face indices at " + item.name);
            readColors(gprim, data.vertices, &data.faces, false);
            item.useFaceColors = gprim.GetDisplayColorPrimvar().GetInterpolation() == UsdGeomTokens->uniform;
            prim.GetAttribute(useFaceColors).Get(&item.useFaceColors);
            item.geometry = std::move(data);
        } else {
            GraphData data;
            VtArray<GfVec2i> edges;
            const bool hasPoints = prim.GetAttribute(graphPoints).Get(&positions);
            const bool hasEdges = prim.GetAttribute(graphEdges).Get(&edges);
            require(hasPoints == hasEdges, "incomplete graph attributes at " + item.name);
            if (hasPoints) {
                for (const auto& p : positions) data.vertices.emplace_back(point(p));
                for (const auto& edge : edges) data.edges.emplace_back(edge[0], edge[1]);
            } else {
                UsdGeomPointBased(prim).GetPointsAttr().Get(&positions);
                for (const auto& p : positions) data.vertices.emplace_back(point(p));
                if (curves) {
                    TfToken type, wrap;
                    curves.GetTypeAttr().Get(&type); curves.GetWrapAttr().Get(&wrap);
                    require(type == UsdGeomTokens->linear, "only linear curves can be imported as graphs: " + item.name);
                    VtArray<int> counts;
                    curves.GetCurveVertexCountsAttr().Get(&counts);
                    std::size_t offset = 0;
                    for (int count : counts) {
                        require(count >= 2 && static_cast<std::size_t>(count) <= positions.size() - offset,
                                "invalid curve counts at " + item.name);
                        for (int i = 1; i < count; ++i) data.edges.emplace_back(static_cast<int>(offset) + i - 1, static_cast<int>(offset) + i);
                        if (wrap == UsdGeomTokens->periodic) data.edges.emplace_back(static_cast<int>(offset) + count - 1, static_cast<int>(offset));
                        offset += count;
                    }
                    require(offset == positions.size(), "unused curve points at " + item.name);
                }
            }
            readColors(gprim, data.vertices);
            if (!data.vertices.empty()) item.edgeColor = data.vertices.front().color;
            if (prim.GetAttribute(edgeColor).Get(&tint)) item.edgeColor = color(tint);
            item.geometry = std::move(data);
        }
        validate(item);
        if (auto* data = std::get_if<MeshData>(&item.geometry)) {
            // USD stores polygon boundaries rather than a separate mesh-edge
            // table. Rebuild it for alice2's wireframe and geometry algorithms.
            std::vector<std::pair<int, int>> edges;
            for (const auto& face : data->faces)
                for (std::size_t i = 0; i < face.vertices.size(); ++i) {
                    const int a = face.vertices[i], b = face.vertices[(i + 1) % face.vertices.size()];
                    edges.emplace_back(std::min(a, b), std::max(a, b));
                }
            std::sort(edges.begin(), edges.end());
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
            data->edges.reserve(edges.size());
            for (const auto& edge : edges) data->edges.emplace_back(edge.first, edge.second);
            data->calculateNormals();
        }
        result.push_back(std::move(item));
    }
    return result;
}

void writeSceneFile(const std::filesystem::path& path, const std::vector<SceneItem>& items) {
    initializeUSD();
    // Validate everything before touching the destination file.
    for (const auto& item : items) validate(item);
    const auto stage = UsdStage::CreateInMemory();
    require(bool(stage), "cannot create stage");
    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->z);
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    const auto root = stage->DefinePrim(SdfPath("/World"), TfToken("Xform"));
    stage->SetDefaultPrim(root);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const SdfPath primPath("/World/Object_" + std::to_string(i));
        UsdGeomGprim gprim;
        if (const auto* data = std::get_if<MeshData>(&item.geometry)) {
            auto mesh = UsdGeomMesh::Define(stage, primPath);
            gprim = UsdGeomGprim(mesh);
            gprim.GetPrim().CreateAttribute(materialParameters,SdfValueTypeNames->Float3,true).Set(
                GfVec3f(item.material.metallic,item.material.roughness,item.material.specular));
            gprim.GetPrim().CreateAttribute(linearColors,SdfValueTypeNames->Bool,true).Set(item.material.linearColors);
            gprim.GetPrim().CreateAttribute(materialBaseColor,SdfValueTypeNames->Color4f,true).Set(usdColor(item.material.baseColor));
            VtArray<GfVec3f> positions, normals;
            VtArray<int> counts, indices;
            VtArray<GfVec4f> colors;
            // VtArray::insert grows to the exact new size. Reserve once to
            // avoid copying all previous indices for every face of a large mesh.
            std::size_t indexCount = 0;
            for (const auto& face : data->faces) indexCount += face.vertices.size();
            indices.reserve(indexCount);
            for (const auto& vertex : data->vertices) {
                positions.push_back(usdPoint(vertex.position)); normals.push_back(usdPoint(vertex.normal));
            }
            for (const auto& face : data->faces) {
                counts.push_back(static_cast<int>(face.vertices.size()));
                indices.insert(indices.end(), face.vertices.begin(), face.vertices.end());
                colors.push_back(usdColor(face.color));
            }
            mesh.CreatePointsAttr().Set(positions);
            mesh.CreateFaceVertexCountsAttr().Set(counts);
            mesh.CreateFaceVertexIndicesAttr().Set(indices);
            mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
            mesh.CreateNormalsAttr().Set(normals);
            mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
            writeColors(gprim, data->vertices, &item.material);
            if (item.useFaceColors) {
                VtArray<GfVec3f> rgb;
                VtArray<float> opacity;
                for (const auto& face : data->faces) {
                    const auto portable=portableColor(face.color,&item.material);
                    rgb.push_back({portable.r,portable.g,portable.b});
                    opacity.push_back(portable.a);
                }
                gprim.CreateDisplayColorPrimvar(UsdGeomTokens->uniform).Set(rgb);
                gprim.CreateDisplayOpacityPrimvar(UsdGeomTokens->uniform).Set(opacity);
            }
            gprim.GetPrim().CreateAttribute(faceColors, SdfValueTypeNames->Color4fArray, true).Set(colors);
            gprim.GetPrim().CreateAttribute(useFaceColors, SdfValueTypeNames->Bool, true).Set(item.useFaceColors);
        } else {
            const auto& graph = std::get<GraphData>(item.geometry);
            auto curves = UsdGeomBasisCurves::Define(stage, primPath);
            gprim = UsdGeomGprim(curves);
            VtArray<GfVec3f> positions, original;
            VtArray<GfVec2i> edges;
            VtArray<int> counts;
            for (const auto& vertex : graph.vertices) original.push_back(usdPoint(vertex.position));
            for (const auto& edge : graph.edges) {
                edges.push_back({edge.vertexA, edge.vertexB}); counts.push_back(2);
                for (int id : {edge.vertexA, edge.vertexB}) {
                    positions.push_back(usdPoint(graph.vertices[id].position));
                }
            }
            curves.CreateTypeAttr().Set(UsdGeomTokens->linear);
            curves.CreateWrapAttr().Set(UsdGeomTokens->nonperiodic);
            curves.CreateCurveVertexCountsAttr().Set(counts);
            curves.CreatePointsAttr().Set(positions);
            curves.CreateWidthsAttr().Set(VtArray<float>{0.001f});
            curves.SetWidthsInterpolation(UsdGeomTokens->constant);
            writeColors(gprim, graph.vertices);
            gprim.CreateDisplayColorPrimvar(UsdGeomTokens->constant).Set(
                VtArray<GfVec3f>{{item.edgeColor.r, item.edgeColor.g, item.edgeColor.b}});
            gprim.CreateDisplayOpacityPrimvar(UsdGeomTokens->constant).Set(VtArray<float>{item.edgeColor.a});
            gprim.GetPrim().CreateAttribute(edgeColor, SdfValueTypeNames->Color4f, true).Set(usdColor(item.edgeColor));
            gprim.GetPrim().CreateAttribute(graphPoints, SdfValueTypeNames->Point3fArray, true).Set(original);
            gprim.GetPrim().CreateAttribute(graphEdges, SdfValueTypeNames->Int2Array, true).Set(edges);
        }
        const auto prim = gprim.GetPrim();
        prim.CreateAttribute(objectName, SdfValueTypeNames->String, true).Set(item.name);
        prim.CreateAttribute(objectColor, SdfValueTypeNames->Color4f, true).Set(usdColor(item.color));
        UsdGeomXformable(prim).AddTransformOp().Set(matrix(item.transform));
        gprim.CreateVisibilityAttr().Set(item.visible ? UsdGeomTokens->inherited : UsdGeomTokens->invisible);
    }
    require(stage->GetRootLayer()->Export(path.string()), "cannot export " + path.string());
}
} // namespace alice2
