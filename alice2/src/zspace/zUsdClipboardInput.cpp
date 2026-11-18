#ifdef ALICE2_BATCH_PATTERN_SKETCH
#include <zspace/zUsdClipboardInput.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4d.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <locale>
namespace alice2 {
namespace {
using namespace pxr;
[[noreturn]] void fail(const std::string& s) { throw std::runtime_error("USD input: " + s); }
UsdClipboardSummary normalize(const UsdStageRefPtr& stage, const std::filesystem::path& output, const std::string& role) {
    if (!stage) fail("could not open stage.");
    if (role != "pattern" && role != "stereotomy") fail("role must be pattern or stereotomy.");
    UsdGeomMesh mesh;
    for (const auto& prim : stage->Traverse()) {
        if (prim.IsInstance() || prim.IsInstanceProxy() || prim.IsA<UsdGeomPointInstancer>()) fail("instances are unsupported; make a mesh copy first.");
        for (const auto& attr : prim.GetAttributes()) if (attr.GetNumTimeSamples() > 0) fail("animated attributes are unsupported: " + attr.GetPath().GetString());
        if (!prim.IsA<UsdGeomMesh>()) continue;
        UsdGeomMesh candidate(prim);
        if (candidate.ComputeVisibility() == UsdGeomTokens->invisible) continue;
        if (mesh) fail("multiple visible meshes; copy one colored mesh at a time.");
        mesh = candidate;
    }
    if (!mesh) fail("no visible mesh found.");
    VtVec3fArray points;
    VtIntArray counts, indices, holes;
    if (!mesh.GetPointsAttr().Get(&points) || !mesh.GetFaceVertexCountsAttr().Get(&counts) || !mesh.GetFaceVertexIndicesAttr().Get(&indices) || points.empty() || counts.empty()) fail("missing mesh points or faces.");
    mesh.GetHoleIndicesAttr().Get(&holes);
    if (!holes.empty()) fail("mesh holes are unsupported; resolve hole faces explicitly first.");
    std::size_t corners = 0;
    for (int n : counts) { if (n < 3) fail("face has fewer than three vertices."); corners += n; }
    if (corners != indices.size()) fail("face counts do not match indices.");
    for (int i : indices) if (i < 0 || static_cast<std::size_t>(i) >= points.size()) fail("face index out of range.");
    UsdGeomPrimvarsAPI api(mesh);
    UsdGeomPrimvar color;
    for (const char* name : {"vertexColors", "colorSet1", "displayColor"}) {
        auto candidate = api.FindPrimvarWithInheritance(TfToken(name));
        // An explicitly authored invalid/blocking value must not select a lower-priority color.
        if (candidate && candidate.GetAttr().HasAuthoredValueOpinion()) { color = candidate; break; }
    }
    if (!color || !color.HasValue()) fail("vertex RGB colors are required; material and texture colors are not imported.");
    if (color.GetElementSize() != 1) fail("RGB primvar elementSize must be one.");
    VtValue flat;
    if (!color.ComputeFlattened(&flat)) fail("cannot flatten indexed RGB colors.");
    std::vector<GfVec3d> values;
    if (flat.IsHolding<VtVec3fArray>()) for (auto c : flat.UncheckedGet<VtVec3fArray>()) values.emplace_back(c);
    else if (flat.IsHolding<VtVec3dArray>()) for (auto c : flat.UncheckedGet<VtVec3dArray>()) values.emplace_back(c);
    else if (flat.IsHolding<VtVec4fArray>()) for (auto c : flat.UncheckedGet<VtVec4fArray>()) {
        // Alpha is not a slicing annotation; only RGB participates.
        values.emplace_back(c[0],c[1],c[2]);
    }
    else if (flat.IsHolding<VtVec4dArray>()) for (auto c : flat.UncheckedGet<VtVec4dArray>()) {
        // Alpha is not a slicing annotation; only RGB participates.
        values.emplace_back(c[0],c[1],c[2]);
    }
    else fail("RGB/RGBA primvar must be an array of three- or four-component floats or doubles.");
    for (const auto& c : values) for (int j=0;j<3;++j) if (!std::isfinite(c[j]) || c[j]<0 || c[j]>1) fail("RGB values must be finite and normalized to 0..1.");
    std::vector<GfVec3d> rgb(points.size());
    std::vector<bool> assigned(points.size(), false);
    auto assign = [&](int vertex, const GfVec3d& c) {
        if (assigned[vertex] && rgb[vertex] != c) fail("incident face colors disagree at a shared vertex; topology is preserved, so color seams cannot be split automatically.");
        assigned[vertex] = true; rgb[vertex] = c;
    };
    auto interpolation = color.GetInterpolation();
    if (interpolation == UsdGeomTokens->constant) {
        if (values.size()!=1) fail("constant RGB requires one color.");
        std::fill(rgb.begin(),rgb.end(),values[0]); std::fill(assigned.begin(),assigned.end(),true);
    } else if (interpolation == UsdGeomTokens->vertex || interpolation == UsdGeomTokens->varying) {
        if (values.size()!=points.size()) fail("vertex RGB count differs from point count.");
        rgb=values; std::fill(assigned.begin(),assigned.end(),true);
    } else if (interpolation == UsdGeomTokens->uniform || interpolation == UsdGeomTokens->faceVarying) {
        bool uniform = interpolation == UsdGeomTokens->uniform;
        if (values.size() != (uniform ? counts.size() : indices.size())) fail("face RGB count differs from interpolation domain.");
        std::size_t k=0;
        for (std::size_t f=0;f<counts.size();++f) for (int j=0;j<counts[f];++j,++k) assign(indices[k],values[uniform?f:k]);
        if (std::find(assigned.begin(),assigned.end(),false)!=assigned.end()) fail("face colors do not define RGB for unused vertices.");
    } else fail("unsupported RGB interpolation: " + interpolation.GetString());
    UsdClipboardSummary result;
    result.vertices=points.size(); result.faces=counts.size(); result.sourcePrim=mesh.GetPath().GetString();
    result.sourceColorPrimvar=color.GetPrimvarName().GetString();
    result.sourceUpAxis=UsdGeomGetStageUpAxis(stage).GetString(); result.sourceMetersPerUnit=UsdGeomGetStageMetersPerUnit(stage);
    if (!(result.sourceMetersPerUnit>0) || !std::isfinite(result.sourceMetersPerUnit)) fail("invalid metres-per-unit.");
    if (result.sourceUpAxis!="Y" && result.sourceUpAxis!="Z") fail("only USD Y-up and Z-up stages are supported.");
    UsdGeomXformCache cache;
    auto world=cache.GetLocalToWorldTransform(mesh.GetPrim());
    double determinant=world.GetDeterminant();
    if (!std::isfinite(determinant) || std::abs(determinant)<1e-20) fail("singular or nonfinite world transform.");
    result.mirrored=determinant<0;
    TfToken orientation; mesh.GetOrientationAttr().Get(&orientation);
    result.windingReversed=result.mirrored != (orientation==UsdGeomTokens->leftHanded);
    if (result.windingReversed) { std::size_t k=0; for (int n:counts) { std::reverse(indices.begin()+k,indices.begin()+k+n); k+=n; } }
    // Older Alice2 exports store their Y-up offset in metres as layer metadata.
    GfVec3d sourceTranslation(0);
    const auto layerData=stage->GetRootLayer()->GetCustomLayerData();
    const auto translation=layerData.find("sourceTranslation");
    if(translation!=layerData.end()){
        if(!translation->second.IsHolding<GfVec3d>())fail("sourceTranslation must be a double3 in metres.");
        sourceTranslation=translation->second.UncheckedGet<GfVec3d>();
    }
    if (result.sourceUpAxis=="Z")
        sourceTranslation=GfVec3d(sourceTranslation[0],-sourceTranslation[2],sourceTranslation[1]);
    const GfVec3d translationInSourceUnits=sourceTranslation/result.sourceMetersPerUnit;
    std::vector<GfVec3d> normalized;
    for (const auto& point:points) {
        const GfVec3d p=world.Transform(GfVec3d(point))+translationInSourceUnits;
        for (int j=0;j<3;++j) if (!std::isfinite(p[j])) fail("nonfinite transformed point.");
        normalized.push_back(p);
    }
    std::ostringstream s; s.imbue(std::locale::classic()); s<<std::setprecision(17);
    s<<"#usda 1.0\n(\n    defaultPrim = \"Input\"\n    metersPerUnit = "<<result.sourceMetersPerUnit
     <<"\n    upAxis = \""<<result.sourceUpAxis<<"\"\n)\ndef Mesh \"Input\"\n{\n    uniform token subdivisionScheme = \"none\"\n    uniform token orientation = \"rightHanded\"\n    custom string alice2:inputRole = \""<<role<<"\"\n";
    auto vectors=[&](const char* declaration,const auto& data,bool primvar) { s<<"    "<<declaration<<" = ["; for(std::size_t i=0;i<data.size();++i) { if(i) s<<", "; s<<"("<<data[i][0]<<", "<<data[i][1]<<", "<<data[i][2]<<")"; } s<<"]"; if(primvar)s<<" (\n        interpolation = \"vertex\"\n    )"; s<<"\n"; };
    vectors("point3f[] points",normalized,false);
    auto integers=[&](const char* name,const auto& data) { s<<"    int[] "<<name<<" = ["; for(std::size_t i=0;i<data.size();++i){ if(i)s<<", ";s<<data[i]; }s<<"]\n"; };
    integers("faceVertexCounts",counts); integers("faceVertexIndices",indices);
    vectors("color3f[] primvars:vertexColors",rgb,true); vectors("color3f[] primvars:displayColor",rgb,true); s<<"}\n";
    std::ofstream out(output,std::ios::binary|std::ios::trunc); if(!out)fail("cannot create normalized output."); out<<s.str(); out.close(); if(!out)fail("could not finish writing normalized output.");
    return result;
}
}
UsdClipboardSummary normalizeUsdInputText(const std::string& input,const std::filesystem::path& output,const std::string& role) {
    std::string text=input;
    if(text.size()>=3 && text.compare(0,3,"\xEF\xBB\xBF")==0)text.erase(0,3);
    if(text.find('\0')!=std::string::npos)fail("clipboard text contains NUL bytes; decode UTF-16 to UTF-8 before import.");
    auto layer=pxr::SdfLayer::CreateAnonymous("clipboard.usda");
    if(!layer->ImportFromString(text))fail("clipboard does not contain valid USDA text.");
    return normalize(pxr::UsdStage::Open(layer),output,role);
}
UsdClipboardSummary normalizeUsdInputFile(const std::filesystem::path& input,const std::filesystem::path& output,const std::string& role) {
    const auto utf8 = input.u8string();
    return normalize(pxr::UsdStage::Open(std::string(utf8.begin(), utf8.end())),output,role);
}
}
#endif
