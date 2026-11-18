#include <zspace/zUsdClipboardInput.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace alice2;
void check(bool ok){if(!ok)throw std::runtime_error("check failed");}
std::string fixture(std::string interp="vertex",std::string colors="[(1,0,0),(0,0,1),(0,1,0)]",std::string indices="") {
return "#usda 1.0\n( metersPerUnit=0.001\n upAxis=\"Y\" )\ndef Xform \"Parent\" {\n double3 xformOp:translate=(1000,2000,3000)\n uniform token[] xformOpOrder=[\"xformOp:translate\"]\n def Mesh \"Shape\" {\n point3f[] points=[(0,0,0),(1000,0,0),(0,1000,0)]\n int[] faceVertexCounts=[3]\n int[] faceVertexIndices=[0,1,2]\n color3f[] primvars:displayColor="+colors+" ( interpolation=\""+interp+"\" )\n"+indices+"\n}\n}\n";
}
int main(int argc, char** argv){try { if(argc>2) { auto r=normalizeUsdInputFile(argv[1],argv[2],argc>3?argv[3]:"pattern");std::cout<<r.vertices<<" vertices, "<<r.faces<<" faces; source "<<r.sourceUpAxis<<" units "<<r.sourceMetersPerUnit<<" mirrored "<<r.mirrored<<" reversed "<<r.windingReversed<<"\n";return 0;}
 auto s=normalizeUsdInputText(fixture(),"test.usda","pattern");check(s.vertices==3&&s.faces==1&&s.sourceUpAxis=="Y");
 auto stage=pxr::UsdStage::Open("test.usda");
 check(pxr::UsdGeomGetStageUpAxis(stage).GetString()=="Y");
 check(pxr::UsdGeomGetStageMetersPerUnit(stage)==0.001);
 pxr::VtVec3fArray p;
 check(pxr::UsdGeomMesh(stage->GetPrimAtPath(pxr::SdfPath("/Input"))).GetPointsAttr().Get(&p));
 check(p[0]==pxr::GfVec3f(1000,2000,3000));
 normalizeUsdInputText(fixture("vertex","[(0,0,1),(1,0,0)]","int[] primvars:displayColor:indices=[1,0,1]"),"indexed.usda","stereotomy");
 normalizeUsdInputText(fixture("constant","[(0.1234567,0,0)]"),"constant.usda","pattern");
 normalizeUsdInputText(fixture("uniform","[(1,0,0)]"),"uniform.usda","pattern");
 normalizeUsdInputText(fixture("faceVarying"),"facevarying.usda","pattern");
 auto mirror=fixture(); auto pos=mirror.find("uniform token[] xformOpOrder"); mirror.insert(pos,"double3 xformOp:scale=(-1,1,1)\n "); auto order=mirror.find("[\"xformOp:translate\"]"); mirror.replace(order,21,"[\"xformOp:translate\",\"xformOp:scale\"]");
 auto mirrored=normalizeUsdInputText(mirror,"mirror.usda","pattern");check(mirrored.mirrored&&mirrored.windingReversed);
 auto reject=[&](const std::string& text){bool caught=false;try{normalizeUsdInputText(text,"bad.usda","pattern");}catch(...){caught=true;}check(caught);};
 auto rgba=fixture("constant","[(0.4,0.2,0.1)]","float4[] primvars:colorSet1=[(1,0,0,1),(0,0,1,0.5)] (interpolation=\"vertex\")\n int[] primvars:colorSet1:indices=[1,0,1]");
 auto rgbaSummary=normalizeUsdInputText(rgba,"rgba.usda","pattern");check(rgbaSummary.sourceColorPrimvar=="colorSet1");
 auto rgbaStage=pxr::UsdStage::Open("rgba.usda");pxr::VtVec3fArray rgb;check(rgbaStage->GetPrimAtPath(pxr::SdfPath("/Input")).GetAttribute(pxr::TfToken("primvars:vertexColors")).Get(&rgb));check(rgb.size()==3&&rgb[0]==pxr::GfVec3f(0,0,1)&&rgb[1]==pxr::GfVec3f(1,0,0));
 auto rgbaDouble=rgba;auto type=rgbaDouble.find("float4[]");rgbaDouble.replace(type,8,"double4[]");normalizeUsdInputText(rgbaDouble,"rgba_double.usda","pattern");
 auto preferred=rgba;auto insertion=preferred.find("float4[]");preferred.insert(insertion,"color3f[] primvars:vertexColors=[(0,1,0)] (interpolation=\"constant\")\n ");check(normalizeUsdInputText(preferred,"preferred.usda","pattern").sourceColorPrimvar=="vertexColors");
 auto invalidIndex=rgba;auto ix=invalidIndex.find("indices=[1,0,1]");invalidIndex.replace(ix,15,"indices=[2,0,1]");reject(invalidIndex);
 auto partial=rgba;ix=partial.find("indices=[1,0,1]");partial.replace(ix,15,"indices=[1,0]");reject(partial);
 for(const char* alpha:{"2.0)","3.0)","-1.0)","nan)"}){auto unusedAlpha=rgba;ix=unusedAlpha.find("0.5)");unusedAlpha.replace(ix,4,alpha);normalizeUsdInputText(unusedAlpha,"ignored_alpha.usda","pattern");auto stage=pxr::UsdStage::Open("ignored_alpha.usda");pxr::VtVec3fArray actual;check(stage->GetPrimAtPath(pxr::SdfPath("/Input")).GetAttribute(pxr::TfToken("primvars:vertexColors")).Get(&actual));check(actual==rgb);auto doubleAlpha=unusedAlpha;auto t=doubleAlpha.find("float4[]");doubleAlpha.replace(t,8,"double4[]");normalizeUsdInputText(doubleAlpha,"ignored_double_alpha.usda","pattern");}
 auto invalidRGB=rgba;ix=invalidRGB.find("(1,0,0,1)");invalidRGB.replace(ix,9,"(2,0,0,1)");reject(invalidRGB);
 auto invalidType=fixture("constant","[(0.4,0.2,0.1)]","float[] primvars:colorSet1=[1,0,1] (interpolation=\"vertex\")");reject(invalidType);
 auto missing=fixture(); auto begin=missing.find(" color3f[]");auto end=missing.find("\n",begin);missing.erase(begin,end-begin);reject(missing);
 auto seams=fixture("faceVarying","[(1,0,0),(1,0,0),(1,0,0),(0,0,1),(0,0,1),(0,0,1)]"); auto n=seams.find("faceVertexCounts=[3]");seams.replace(n,20,"faceVertexCounts=[3,3]");n=seams.find("faceVertexIndices=[0,1,2]");seams.replace(n,25,"faceVertexIndices=[0,1,2,2,1,0]");reject(seams);
 auto animated=fixture(); auto close=animated.find("point3f[] points=");animated.insert(close,"float test.timeSamples={0:1, 1:2}\n ");reject(animated);
 bool rejected=false;try{normalizeUsdInputText(fixture("vertex","[(1,0,0)]"),"bad.usda","pattern");}catch(...){rejected=true;}check(rejected);
 std::cout<<"USD clipboard normalization tests PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
