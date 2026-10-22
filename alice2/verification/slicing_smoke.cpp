#define ALICE2_SLICER_IO_ONLY
#include "../userSrc/zspace/slicer/slicer.h"
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
    out["widths"]=p.widths; out["heights"]=p.heights; out["normals"]=normals; out["trackIds"]=p.trackIds;
    return out;
}
static Json result(zTsSlicing& slicer,int mode) {
    zObjMeshArray* slices = mode ? &slicer.getHarmonicSlicer()->getIsoMeshes() : &slicer.getTopologySlicer()->getIsoMeshes();
    Json layers=Json::array(); const auto& paths=slicer.get3DPrintPaths();
    require(paths.size()==slices->size(),"Path/slice layer count mismatch");
    for(size_t i=0;i<slices->size();++i) {
        Json ps=Json::array(); require(!paths[i].empty(),"Empty path layer");
        for(const auto& p:paths[i]) ps.push_back(path(p));
        layers.push_back({{"slice",mesh((*slices)[i])},{"paths",ps}});
    }
    return {{"mode",mode},{"layers",layers},{"combined",path(slicer.getResultantPrintPath())}};
}
// Check exported arrays in source units, independent of graph traversal order.
static void exportedArray(const std::string& text,const std::string& name,Json expected,double scale) {
    const auto scope=text.find("def Scope \"TDP_gcode\"");
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
    std::sort(actual.begin(),actual.end());std::sort(reference.begin(),reference.end());
    for(size_t i=0;i<actual.size();++i)
        require(std::abs(actual[i]-reference[i])<=1e-6*std::max(1.0,std::abs(reference[i])),"Exported units/values changed: "+name);
}
int main(int argc,char** argv) {
  try {
    require(argc==4,"Usage: alice2_slicing_smoke fixture.usda output.json topology|harmonic");
    const std::string modeName=argv[3]; require(modeName=="topology"||modeName=="harmonic","Invalid mode");
    const int mode=modeName=="harmonic";
    aliceSlicingUSD::SlicerUSDInput input; std::string error;
    const bool imported=aliceSlicingUSD::importUSD(argv[1],input,&error);
    require(imported,"USD import: "+error);
    auto settings=input.Settings;
    auto print=input.PrintSettings;
    require(input.UnknownSettings.empty(),"Unknown fixture settings");
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
    auto& bracing = input.Bracing;
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
    // Harmonic currently has no layer-height table and therefore reports zero volume.
    if (!mode) require(slicer.getPrintVolume()>0,"Topology volume must be positive");
    Json first=result(slicer,mode); require(first["layers"].size()==size_t(legacy?53:5),"Unexpected layer count");
    require(slicer.computeSlicer(false),"Repeated slicing failed");
    require(slicer.computePostProcess(false),"Repeated PrintSyn failed");
    require(first==result(slicer,mode),"Repeated solve changed owned numeric result");
    const std::filesystem::path output=argv[2];
    if(!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    const auto exported=output.parent_path()/(output.stem().string()+"-export.usda");
    const bool didExport=aliceSlicingUSD::exportUSD(slicer.getResultantPrintPath(),argv[1],exported.string(),static_cast<int>(first["layers"].size()),&error);
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
    aliceSlicingUSD::SlicerUSDInput reread;
    const bool reimported=aliceSlicingUSD::importUSD(exported.string(),reread,&error);
    require(reimported,"USD exported input reimport: "+error);
    require(reread.FaceTags==input.FaceTags&&reread.Settings==input.Settings,"Export changed source settings/tags");
    require(mesh(reread.Block)==mesh(input.Block),"Export changed source mesh");
    require(reread.MetersPerUnit==input.MetersPerUnit,"Export changed source units");
    // A failed read must clear an earlier successful import and report why.
    const auto malformed=output.parent_path()/(output.stem().string()+"-malformed.usda");
    require(!std::filesystem::exists(malformed),"Malformed-input temporary path already exists");
    {
        std::ofstream bad(malformed); require(bool(bad),"Cannot create malformed USD fixture");
        bad<<"not a USD stage\n"; require(bool(bad),"Cannot write malformed USD fixture");
    }
    const bool acceptedMalformed=aliceSlicingUSD::importUSD(malformed.string(),reread,&error);
    std::filesystem::remove(malformed);
    require(!acceptedMalformed && !error.empty(),"Malformed USD must fail with a diagnostic");
    require(zFnMesh(reread.Block).numVertices()==0 && reread.FaceTags.empty() && reread.Settings.empty(),
            "Failed USD import retained stale input");
    Json report={{"input",request},{"result",first},{"metersPerUnit",input.MetersPerUnit},{"repeated",true},{"printLength",slicer.getPrintLength()},{"printVolume",slicer.getPrintVolume()},{"exported",exported.string()}};
    std::ofstream file(output); require(bool(file),"Cannot open report"); file<<report.dump(); require(bool(file),"Cannot write report");
    std::cout<<"PASS "<<modeName<<" layers="<<first["layers"].size()<<" vertices="<<first["combined"]["vertexPositions"].size()/3<<"\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n"; return 1; }
}
