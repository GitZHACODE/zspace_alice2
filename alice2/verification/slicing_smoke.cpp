#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <zspace/slicing.h>
#include <zspace/zSlicingSettings.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <algorithm>
#include <stdexcept>
using namespace zSpace;
using Json = nlohmann::json;
static void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
static Json points(const zPointArray& values) {
    Json out=Json::array();
    for(const auto& p:values) for(double v:{double(p.x),double(p.y),double(p.z)}) {
        require(std::isfinite(v),"Non-finite coordinate"); out.push_back(v);
    }
    return out;
}
static Json mesh(zObjMesh& object) {
    zPointArray p; zIntArray counts,connects; zFnMesh fn(object);
    fn.getVertexPositions(p); fn.getPolygonData(connects,counts);
    return {{"vertexPositions",points(p)},{"faceCounts",counts},{"faceConnects",connects}};
}
static Json graph(zObjGraph& object) {
    zPointArray p; zIntArray edges; zFnGraph fn(object);
    fn.getVertexPositions(p); fn.getEdgeData(edges);
    return {{"vertexPositions",points(p)},{"edgeConnects",edges}};
}
static Json path(const PrintPath& p) {
    Json out=graph(const_cast<zObjGraph&>(p.graph));
    const size_t n=out["vertexPositions"].size()/3;
    require(n>0 && p.widths.size()==n && p.heights.size()==n && p.normals.size()==n,"Incomplete print attributes");
    for(float v:p.widths) require(std::isfinite(v)&&v>=0,"Invalid width");
    for(float v:p.heights) require(std::isfinite(v)&&v>=0,"Invalid height");
    Json normals=Json::array();
    for(const auto& v:p.normals) for(double c:{double(v.x),double(v.y),double(v.z)}) {
        require(std::isfinite(c),"Invalid normal"); normals.push_back(c);
    }
    out["widths"]=p.widths; out["heights"]=p.heights; out["normals"]=normals; out["trackIds"]=p.trackIds;out["featureFlags"]=p.featureFlags;
    return out;
}
static Json result(zTsSlicing& slicer,int mode) {
    zObjMeshArray* slices = mode ? &slicer.getHarmonicSlicer()->getIsoMeshes() : &slicer.getTopologySlicer()->getIsoMeshes();
    Json layers=Json::array(); const auto& paths=slicer.get3DPrintPaths();
    require(paths.size()+1==slices->size(),"Exact top reference mesh must not be a print layer");
    for(size_t i=0;i<paths.size();++i) {
        Json ps=Json::array(); require(!paths[i].empty(),"Empty path layer");
        for(const auto& p:paths[i]) ps.push_back(path(p));
        layers.push_back({{"slice",mesh((*slices)[i])},{"paths",ps}});
    }
    return {{"mode",mode},{"layers",layers},{"topReference",mesh(slices->back())},{"combined",path(slicer.getResultantPrintPath())}};
}
// Check exported arrays in source units, independent of graph traversal order.
static void exportedArray(const std::string& text,const std::string& name,Json expected,double scale,
                          const std::string& declaration="def Scope \"TDP_gcode\"",bool ordered=false) {
    const auto scope=text.find(declaration);
    require(scope!=std::string::npos,"Missing exported prim: "+declaration);
    const auto attr=text.find(" " + name + " = [",scope);
    require(attr!=std::string::npos,"Missing exported array: "+name);
    const auto begin=text.find('[',attr),end=text.find(']',begin);
    require(end!=std::string::npos,"Unterminated exported array");
    std::string data=text.substr(begin+1,end-begin-1);
    for(char& c:data) if(c==',' || c=='(' || c==')') c=' ';
    std::istringstream stream(data); std::vector<double> actual,reference;
    double value;
    while(stream>>value) {require(std::isfinite(value),"Non-finite export");actual.push_back(value);}
    require(stream.eof(),"Invalid numeric export");
    for(const auto& v:expected) reference.push_back(v.get<double>()*scale);
    require(actual.size()==reference.size(),"Exported attribute count changed");
    if(!ordered) {std::sort(actual.begin(),actual.end());std::sort(reference.begin(),reference.end());}
    for(size_t i=0;i<actual.size();++i)
        require(std::abs(actual[i]-reference[i])<=1e-6*std::max(1.0,std::abs(reference[i])),"Exported units/values changed: "+name);
}
static void checkGeometryFile(const std::string& file,const Json& expectedMesh,const Json& expectedPoints,
                              const Json& expectedCounts,const Json& expectedWidths,double scale) {
    const auto stage=pxr::UsdStage::Open(pxr::SdfLayer::OpenAsAnonymous(file));
    require(bool(stage),"Cannot reopen exported stage");
    const pxr::UsdGeomMesh surface(stage->GetPrimAtPath(pxr::SdfPath("/TDP_printmesh")));
    const pxr::UsdGeomBasisCurves curves(stage->GetPrimAtPath(pxr::SdfPath("/TDP_printpath")));
    require(bool(surface)&&bool(curves),"Exported geometry schemas missing");
    require(surface.GetPrim().GetDisplayName()=="print mesh"&&curves.GetPrim().GetDisplayName()=="print path","Geometry display names changed");
    pxr::TfToken token;
    require(surface.GetSubdivisionSchemeAttr().Get(&token)&&token==pxr::UsdGeomTokens->none,"Mesh must not subdivide");
    require(curves.GetTypeAttr().Get(&token)&&token==pxr::UsdGeomTokens->linear,"Print curves must be linear");
    require(curves.GetWrapAttr().Get(&token)&&token==pxr::UsdGeomTokens->nonperiodic,"Unexpected curve wrap");
    require(curves.GetWidthsInterpolation()==pxr::UsdGeomTokens->vertex,"Widths must use vertex interpolation");
    const auto checkPoints=[&](const pxr::UsdAttribute& attr,const Json& expected) {
      pxr::VtArray<pxr::GfVec3f> actual; require(attr.Get(&actual)&&actual.size()*3==expected.size(),"Wrong geometry point count");
      for(size_t i=0;i<actual.size();++i) for(int j=0;j<3;++j) {
        const double reference=expected[3*i+j].get<double>()*scale;
        require(std::isfinite(actual[i][j])&&std::abs(actual[i][j]-reference)<=1e-6*std::max(1.0,std::abs(reference)),"Geometry coordinates/units changed");
      }
    };
    const auto checkIds=[&](const pxr::UsdAttribute& attr,const Json& expected) {
      pxr::VtArray<int> actual; require(attr.Get(&actual)&&actual.size()==expected.size(),"Wrong topology size");
      for(size_t i=0;i<actual.size();++i) require(actual[i]==expected[i].get<int>(),"Topology order changed");
    };
    checkPoints(surface.GetPointsAttr(),expectedMesh["vertexPositions"]);
    checkIds(surface.GetFaceVertexCountsAttr(),expectedMesh["faceCounts"]);
    checkIds(surface.GetFaceVertexIndicesAttr(),expectedMesh["faceConnects"]);
    checkPoints(curves.GetPointsAttr(),expectedPoints);
    checkIds(curves.GetCurveVertexCountsAttr(),expectedCounts);
    pxr::VtArray<float> widths; require(curves.GetWidthsAttr().Get(&widths)&&widths.size()==expectedWidths.size(),"Curve widths missing");
    for(size_t i=0;i<widths.size();++i) require(std::abs(widths[i]-expectedWidths[i].get<double>()*scale)<1e-5,"Curve width units changed");
}
int main(int argc,char** argv) {
  try {
    require(argc==4,"Usage: alice2_slicing_smoke fixture.usda output.json topology|harmonic");
    const std::string modeName=argv[3]; require(modeName=="topology"||modeName=="harmonic","Invalid mode");
    const int mode=modeName=="harmonic";
    zTsIO::SlicerUSDInput input; std::string error;
    const bool imported=zTsIO::readSlicerInputUSD(argv[1],input,&error);
    require(imported,"USD import: "+error);
    const Json authored=Json::parse(input.SettingsJSON);
    auto settings=alice2::slicing::defaultSlicerSettings();
    auto print=alice2::slicing::defaultPrintSynSettings(); std::vector<std::string> unknown;
    alice2::slicing::applySlicerJson(authored.at("slicer"),settings,unknown);
    alice2::slicing::applyPrintSynJson(authored.at("printSyn"),print,unknown);
    require(unknown.empty(),"Unknown fixture settings");
    require(!input.BracingPrintWidths.empty(),"Missing bracing widths");
    settings.BracingCurveWidths=input.BracingPrintWidths; settings.BracingWidth=input.BracingPrintWidths.front();
    const bool legacy=std::filesystem::path(argv[1]).filename().string().find("legacy-mm")!=std::string::npos;
    require(std::abs(input.MetersPerUnit-(std::string(argv[1]).find("metres")!=std::string::npos?1.0:.001))<1e-12,"Source units changed");
    require(std::abs(settings.LayerSpacing-(legacy?10:.5))<1e-6,"Layer spacing not preserved");
    require(settings.BoundaryWidth.size()==2,"Missing width groups");
    require(std::abs(settings.BoundaryWidth[0]-(legacy?60:.4))<1e-6,"Authored width changed");
    require(std::abs(print.ResampleLength-(legacy?10:.2))<1e-6,"Authored resample changed");
    if (!legacy) {
        zPoint lo,hi; zFnMesh(input.Block).getBounds(lo,hi);
        require(std::abs(hi.x-lo.x-4)<1e-5 && std::abs(hi.y-lo.y-4)<1e-5 && std::abs(hi.z-lo.z-2)<1e-5,"Input geometry not normalized to mm");
        require(std::abs(settings.BracingWidth-.3)<1e-6 && std::abs(print.SeamOffset-.04)<1e-6 && std::abs(print.SeamSize-.02)<1e-6,"Lengths not normalized to mm");
    }
    zObjGraph bracing; zTsUtils::combineGraphObjects(input.BracingCurves,bracing);
    Json wasmSettings={{"layerSpacing",settings.LayerSpacing},{"boundaryWidths",settings.BoundaryWidth},
      {"bracingWidth",settings.BracingWidth},{"bracingCurveWidths",settings.BracingCurveWidths},
      {"overlapWidth",settings.OverlapWidth},{"doubleBoundary",settings.DoubleBoundary},
      {"slotOffset",settings.SlotOffset},{"bracingSlot",settings.BracingSlot},
      {"fieldScaleUp",settings.FieldScaleUp},{"fieldCellSize",settings.FieldCellSize},
      {"traceStepFraction",settings._HTraceStepFraction},{"traceMaxSteps",settings._HTraceMaxSteps},
      {"equaliseIterations",settings._HEqualiseIterations},{"arapIterations",settings._HArapIterations},{"unrollStart",settings._HUnrollStart},
      {"resampleLength",print.ResampleLength},{"featureAngle",print.FeatureAngleTol},{"seamOffset",print.SeamOffset},{"seamSize",print.SeamSize}};
    Json request={{"mesh",mesh(input.Block)},{"faceTags",input.FaceTags},{"faceWidthIds",input.FaceWidthIds},
      {"bracing",graph(bracing)},{"anchor",{input.StartPoint.x,input.StartPoint.y,input.StartPoint.z}},
      {"settings",wasmSettings},{"mode",mode},{"postProcess",true}};
    zTsSlicing slicer; slicer.setSlicerMode(mode?zTsSlicerMode::Harmonic:zTsSlicerMode::Topology);
    slicer.setInputMesh(input.Block,input.FaceTags,input.FaceWidthIds); slicer.setBracingGraph(bracing); slicer.setSlotAnchor(input.StartPoint);
    slicer.getSettings()=settings; slicer.getPrintSynSetting()=print;
    const bool sliced=slicer.computeSlicer(false);
    require(sliced,"Slicing: "+slicer.getStatus());
    const bool processed=slicer.computePostProcess(false);
    require(processed,"PrintSyn: "+slicer.getStatus());
    require(slicer.hasPrintStatistics() && std::isfinite(slicer.getPrintLength()) && slicer.getPrintLength()>0 && std::isfinite(slicer.getPrintVolume()) && slicer.getPrintVolume()>=0,"Invalid print statistics");
    require(slicer.getPrintVolume()>0,"Printed volume must remain positive with the existing height method");
    Json first=result(slicer,mode); require(first["layers"].size()==size_t(legacy?52:4),"Unexpected printed layer count");
    require(slicer.computeSlicer(false),"Repeated slicing failed");
    require(slicer.computePostProcess(false),"Repeated PrintSyn failed");
    require(first==result(slicer,mode),"Repeated solve changed owned numeric result");
    const std::filesystem::path output=argv[2];
    if(!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    const auto exported=output.parent_path()/(output.stem().string()+"-export.usda");
    const bool didExport=zTsIO::exportPrintPathUSD(slicer.getResultantPrintPath(),argv[1],exported.string(),static_cast<int>(first["layers"].size()),&error,0,&slicer.get3DPrintPaths());
    require(didExport,"USD export: "+error);
    std::ifstream exportedFile(exported);
    const std::string exportedText((std::istreambuf_iterator<char>(exportedFile)),std::istreambuf_iterator<char>());
    require(exportedText.find("def Scope \"TDP_gcode\"")!=std::string::npos,"Missing exported Gcode scope");
    require(std::regex_search(exportedText,std::regex("int layercount = " + std::to_string(first["layers"].size()) + "\\b")),"Incorrect export layer count");
    const double toSourceUnits=1.0/(input.MetersPerUnit*1000.0);
    exportedArray(exportedText,"points",first["combined"]["vertexPositions"],toSourceUnits);
    exportedArray(exportedText,"printWidths",first["combined"]["widths"],toSourceUnits);
    exportedArray(exportedText,"printHeight",first["combined"]["heights"],toSourceUnits);
    exportedArray(exportedText,"printNormals",first["combined"]["normals"],1.0);
    zObjMesh printMesh;
    require(zTsUtils::buildPrintMesh(slicer.get3DPrintPaths(),printMesh,&error),"Print mesh: "+error);
    const Json expectedMesh=mesh(printMesh);
    exportedArray(exportedText,"points",expectedMesh["vertexPositions"],toSourceUnits,"def Mesh \"TDP_printmesh\"",true);
    exportedArray(exportedText,"faceVertexCounts",expectedMesh["faceCounts"],1,"def Mesh \"TDP_printmesh\"",true);
    exportedArray(exportedText,"faceVertexIndices",expectedMesh["faceConnects"],1,"def Mesh \"TDP_printmesh\"",true);
    require(exportedText.find("displayName = \"print mesh\"")!=std::string::npos,"Missing print mesh display name");
    Json curvePoints=Json::array(),curveCounts=Json::array(),curveWidths=Json::array();
    for(const auto& layer:slicer.get3DPrintPaths()) for(const auto& p:layer) {
      zPointArray positions; zFnGraph(const_cast<zObjGraph&>(p.graph)).getVertexPositions(positions);
      std::vector<zTsUtils::GraphPolyline> curves;
      require(zTsUtils::graphPolylines(p.graph,curves),"Invalid layer path");
      for(const auto& curve:curves) {
        auto ids=curve.Vertices; if(curve.Closed) ids.push_back(ids.front());
        curveCounts.push_back(ids.size());
        for(int id:ids) {
          for(double v:{double(positions[id].x),double(positions[id].y),double(positions[id].z)}) curvePoints.push_back(v);
          curveWidths.push_back(p.widths[id]);
        }
      }
    }
    exportedArray(exportedText,"points",curvePoints,toSourceUnits,"def BasisCurves \"TDP_printpath\"",true);
    exportedArray(exportedText,"curveVertexCounts",curveCounts,1,"def BasisCurves \"TDP_printpath\"",true);
    exportedArray(exportedText,"widths",curveWidths,toSourceUnits,"def BasisCurves \"TDP_printpath\"",true);
    require(exportedText.find("displayName = \"print path\"")!=std::string::npos,"Missing print path display name");
    checkGeometryFile(exported.string(),expectedMesh,curvePoints,curveCounts,curveWidths,toSourceUnits);
    for(const std::string extension:{".usd",".usdc"}) {
      const auto binary=output.parent_path()/(output.stem().string()+"-export"+extension);
      require(zTsIO::exportPrintPathUSD(slicer.getResultantPrintPath(),argv[1],binary.string(),static_cast<int>(first["layers"].size()),&error,0,&slicer.get3DPrintPaths()),"Binary export: "+error);
      checkGeometryFile(binary.string(),expectedMesh,curvePoints,curveCounts,curveWidths,toSourceUnits);
    }
    // Mixed open/closed paths share a nonperiodic BasisCurves prim, with an
    // explicit closing point only on the loop and no inter-layer connector.
    if(!mode&&!legacy) {
      const auto makePath=[](zPointArray positions,zIntArray edges) {
        PrintPath p; zFnGraph(p.graph).create(positions,edges);
        p.widths.assign(positions.size(),2); p.heights.assign(positions.size(),1);
        p.normals.assign(positions.size(),zVector(0,0,1)); return p;
      };
      const auto loop=makePath({{0,0,0},{10,0,0},{10,10,0},{0,10,0}},{0,1,1,2,2,3,3,0});
      const auto open=makePath({{0,0,10},{10,0,10}},{0,1});
      const PrintPathArray mixed{{loop},{open}};
      PrintPath linked; std::string status;
      require(zPrintSyn::linkPrintPaths(mixed,linked,status),"Mixed paths did not link");
      zObjMesh mixedMesh; require(zTsUtils::buildPrintMesh(mixed,mixedMesh,&error),"Mixed path loft failed");
      const auto mixedFile=output.parent_path()/(output.stem().string()+"-mixed.usda");
      require(zTsIO::exportPrintPathUSD(linked,argv[1],mixedFile.string(),2,&error,0,&mixed),"Mixed export: "+error);
      checkGeometryFile(mixedFile.string(),mesh(mixedMesh),
        Json{0,0,0,10,0,0,10,10,0,0,10,0,0,0,0,0,0,10,10,0,10},Json{5,2},Json{2,2,2,2,2,2,2},toSourceUnits);
    }
    // Re-export must replace the previous mesh, rather than accumulate stale prims.
    require(zTsIO::exportPrintPathUSD(slicer.getResultantPrintPath(),exported.string(),exported.string(),static_cast<int>(first["layers"].size()),&error,0,&slicer.get3DPrintPaths()),"Re-export: "+error);
    checkGeometryFile(exported.string(),expectedMesh,curvePoints,curveCounts,curveWidths,toSourceUnits);
    zTsIO::SlicerUSDInput reread;
    const bool reimported=zTsIO::readSlicerInputUSD(exported.string(),reread,&error);
    require(reimported,"USD exported input reimport: "+error);
    require(reread.FaceTags==input.FaceTags&&reread.SettingsJSON==input.SettingsJSON,"Export changed source settings/tags");
    const auto heightRange=std::minmax_element(slicer.getResultantPrintPath().heights.begin(),slicer.getResultantPrintPath().heights.end());
    require(reread.PrintHeightDomain.size()==2,"Measured output printheightdomain was not imported");
    require(std::abs(reread.PrintHeightDomain[0]-*heightRange.first)<1e-5&&std::abs(reread.PrintHeightDomain[1]-*heightRange.second)<1e-5,
      "Output domain lost source-unit precision or did not overwrite the measured range");
    const auto domainStage=pxr::UsdStage::Open(pxr::SdfLayer::OpenAsAnonymous(exported.string()));
    require(bool(domainStage),"Cannot prepare domain-input fixture");
    auto metadata=domainStage->GetRootLayer()->GetCustomLayerData();
    metadata["printheightdomain"]=pxr::VtValue(pxr::VtArray<double>{6*toSourceUnits,12*toSourceUnits});
    domainStage->GetRootLayer()->SetCustomLayerData(metadata);
    const auto domainFile=output.parent_path()/(output.stem().string()+"-domain-input.usda");
    require(domainStage->GetRootLayer()->Export(domainFile.string()),"Cannot author domain-input fixture");
    zTsIO::SlicerUSDInput domainInput;
    require(zTsIO::readSlicerInputUSD(domainFile.string(),domainInput,&error),error);
    require(domainInput.PrintHeightDomain.size()==2&&std::abs(domainInput.PrintHeightDomain[0]-6)<1e-9&&std::abs(domainInput.PrintHeightDomain[1]-12)<1e-9,
      "Root double[] printheightdomain is not normalized to mm");
    const auto rejectDomain=[&](const pxr::VtValue& value){metadata["printheightdomain"]=value;domainStage->GetRootLayer()->SetCustomLayerData(metadata);
      const auto invalidDomain=output.parent_path()/(output.stem().string()+"-invalid-domain.usda");
      require(domainStage->GetRootLayer()->Export(invalidDomain.string()),"Cannot author invalid domain");
      require(!zTsIO::readSlicerInputUSD(invalidDomain.string(),domainInput,&error),"Invalid USD height-domain schema accepted");
      require(domainInput.PrintHeightDomain.empty()&&domainInput.SettingsJSON.empty(),"Failed domain import retained stale input");};
    rejectDomain(pxr::VtValue(pxr::VtArray<double>{6}));
    rejectDomain(pxr::VtValue(pxr::VtArray<float>{6,12}));
    rejectDomain(pxr::VtValue(pxr::VtArray<double>{12,6}));
    rejectDomain(pxr::VtValue(pxr::VtArray<double>{-1,12}));
    require(mesh(reread.Block)==mesh(input.Block),"Export changed source mesh");
    require(reread.MetersPerUnit==input.MetersPerUnit,"Export changed source units");
    // A failed read must clear an earlier successful import and report why.
    const auto malformed=output.parent_path()/(output.stem().string()+"-malformed.usda");
    require(!std::filesystem::exists(malformed),"Malformed-input temporary path already exists");
    {
        std::ofstream bad(malformed); require(bool(bad),"Cannot create malformed USD fixture");
        bad<<"not a USD stage\n"; require(bool(bad),"Cannot write malformed USD fixture");
    }
    const bool acceptedMalformed=zTsIO::readSlicerInputUSD(malformed.string(),reread,&error);
    std::filesystem::remove(malformed);
    require(!acceptedMalformed && !error.empty(),"Malformed USD must fail with a diagnostic");
    require(zFnMesh(reread.Block).numVertices()==0 && reread.FaceTags.empty() && reread.SettingsJSON.empty(),
            "Failed USD import retained stale input");
    Json report={{"input",request},{"result",first},{"metersPerUnit",input.MetersPerUnit},{"repeated",true},{"printLength",slicer.getPrintLength()},{"printVolume",slicer.getPrintVolume()},{"exported",exported.string()}};
    std::ofstream file(output); require(bool(file),"Cannot open report"); file<<report.dump(); require(bool(file),"Cannot write report");
    std::cout<<"PASS "<<modeName<<" layers="<<first["layers"].size()<<" vertices="<<first["combined"]["vertexPositions"].size()/3<<"\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}

