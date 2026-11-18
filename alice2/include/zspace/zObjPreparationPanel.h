#pragma once
#include <zspace/interface.h>
#include <alice2.h>
#include <windows.h>
#include <commdlg.h>
#include <zspace/zToolsets/fabrication/stereotomy/ObjPreparation.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

// Reusable, non-repairing preparation panel; file conversion lives in toolsets.
class zObjPreparationPanel {
 using Mesh=zSpace::stereotomy::PreparedObj;
 using Options=zSpace::stereotomy::ObjOptions;
 using fs=std::filesystem::path;
 struct Overlay {std::string label;alice2::Color color;bool shown=true,hasEdges=false,hasPoints=false;zSpace::zObjectGraph edges;zSpace::zObjectPointCloud points;};
 alice2::InputManager& input;
 std::unique_ptr<alice2::SimpleUI> ui;
 std::function<void(const fs&,const std::string&)> assign;
 Mesh mesh;fs source,saved;zSpace::zObjectMesh surface;bool hasSurface=false;zSpace::zObjectGraph wire;zSpace::zObjectPointCloud vertices;
 std::vector<Overlay> overlays;alice2::Vec3 previewLow,previewHigh;
 bool opened=false,lastOpened=false,loaded=false,validSelection=false,redJoints=false;
 int role=0,unit=0,axis=0,lastRole=0,lastUnit=0,lastAxis=0,action=0,dummy=-1;
 bool lastRed=false;int screenWidth=0;float x=900;
 std::string status="Choose role, units and up axis, then Load OBJ.";
 const double scales[4]={1,10,1000,25.4};
 const char* unitNames[4]={"mm","cm","m","in"};
 const char* axisNames[3]={"Z","Y","X"};
 Options options()const{Options o;o.role=role==0?"pattern":"stereotomy";o.millimetersPerUnit=scales[unit];o.upAxis=axisNames[axis];o.useRedVertexJoints=redJoints;return o;}
 static alice2::Color color(const std::string&kind){if(kind=="boundary")return {.95f,.55f,.05f};if(kind=="nonmanifold")return {1,.05f,.05f};if(kind=="degenerate")return {1,0,.75f};if(kind=="winding")return {0,.65f,.85f};if(kind=="coincident")return {.6f,.15f,.9f};if(kind=="redJointEdges")return {.9f,.05f,.05f};return {.35f,.2f,.65f};}
 static std::string label(const std::string&kind){if(kind=="boundary")return "Boundary";if(kind=="nonmanifold")return "Nonmanifold";if(kind=="degenerate")return "Degenerate faces";if(kind=="winding")return "Winding";if(kind=="coincident")return "Coincident vertices";if(kind=="duplicateface")return "Duplicate faces";if(kind=="nonquad")return "Non-quad faces";if(kind=="redJointEdges")return "Red joint edges";return "Unused vertices";}
 void rebuild(){
  ui=std::make_unique<alice2::SimpleUI>(input);ui->setTheme(alice2::SimpleUI::UITheme::Dark);
  ui->addToggle("Prepare OBJ inputs",{x,108,270,28},opened);
  if(!opened)return;
  ui->addButtonGroup({"Pattern","Stereotomy"},{x,155},132,26,6,role);
  ui->addButtonGroup({"mm","cm","m","in"},{x,208},63,25,6,unit);
  ui->addButtonGroup({"Z up","Y up","X up"},{x,260},86,25,6,axis);
  if(role==1)ui->addToggle("Red vertices mark joints",{x,294,270,24},redJoints);
  ui->addButtonGroup({"Load OBJ","Save USD"},{x,330},132,27,6,dummy);
  ui->addButtonGroup({"Use saved USD as input"},{x,366},270,27,0,dummy);
  float y=450;for(auto&o:overlays){ui->addToggle(o.label,{x,y,270,18},o.shown);y+=20;}
 }
 void geometry(){
  overlays.clear();zSpace::zPointArray points;std::array<double,3>lo=mesh.pointsMM.front(),hi=lo;
  for(const auto&p:mesh.pointsMM)for(int k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}
  for(const auto&p:mesh.pointsMM)points.emplace_back(p[0]-(lo[0]+hi[0])*.5,p[1]-(lo[1]+hi[1])*.5,p[2]-(lo[2]+hi[2])*.5);
  std::set<std::array<int,2>> edges;
  for(const auto&f:mesh.faces)for(size_t k=0;k<f.size();++k){int a=f[k],b=f[(k+1)%f.size()];if(a!=b)edges.insert({std::min(a,b),std::max(a,b)});}
  zSpace::zIntArray connects;for(const auto&e:edges){connects.push_back(e[0]);connects.push_back(e[1]);}
  wire=zSpace::zObjectGraph();if(!connects.empty())zSpace::zFnGraph(wire).create(points,connects);
  vertices=zSpace::zObjectPointCloud();zSpace::zFnPointCloud fn(vertices);fn.create(points);
  zSpace::zColorArray colors;for(size_t i=0;i<points.size();++i){if(i<mesh.vertexColorPresent.size()&&mesh.vertexColorPresent[i])colors.emplace_back(float(mesh.colors[i][0]),float(mesh.colors[i][1]),float(mesh.colors[i][2]),1);else colors.emplace_back(.35f,.35f,.35f,1);}fn.setVertexColors(colors);
  hasSurface=false;surface=zSpace::zObjectMesh();
  bool safeSurface=true;for(const auto*key:{"degenerateFaces","nonmanifoldEdges","duplicateFaces","inconsistentWinding"})if(mesh.counts.count(key)&&mesh.counts.at(key)>0)safeSurface=false;
  if(safeSurface){zSpace::zIntArray fc,fi;for(const auto&f:mesh.faces){fc.push_back(int(f.size()));fi.insert(fi.end(),f.begin(),f.end());}zSpace::zFnMesh sf(surface);sf.create(points,fc,fi);sf.setVertexColors(colors);hasSurface=true;}

  for(const auto&issue:mesh.issues){Overlay o;o.label=label(issue.kind);o.color=color(issue.kind);std::set<int>ids(issue.vertexIds.begin(),issue.vertexIds.end());zSpace::zIntArray ec;for(const auto&e:issue.edges){if(e[0]!=e[1]){ec.push_back(e[0]);ec.push_back(e[1]);}}if(!ec.empty()){zSpace::zFnGraph(o.edges).create(points,ec);o.hasEdges=true;}zSpace::zPointArray marked;for(int v:ids)marked.push_back(points.at(v));if(!marked.empty()){zSpace::zFnPointCloud(o.points).create(marked);o.hasPoints=true;}const std::map<std::string,std::string> countKeys={{"boundary","boundaryEdges"},{"nonmanifold","nonmanifoldEdges"},{"degenerate","degenerateFaces"},{"winding","inconsistentWinding"},{"coincident","coincidentVertices"},{"duplicateface","duplicateFaces"},{"nonquad","nonQuadFaces"},{"redJointEdges","redEdges"},{"unusedVertices","unusedVertices"}};const auto key=countKeys.find(issue.kind);const size_t count=key!=countKeys.end()&&mesh.counts.count(key->second)?mesh.counts.at(key->second):ids.size();o.label+=": "+std::to_string(count);overlays.push_back(std::move(o));}
  previewLow=alice2::Vec3(float((lo[0]-hi[0])*.5),float((lo[1]-hi[1])*.5),float((lo[2]-hi[2])*.5));previewHigh=alice2::Vec3(float((hi[0]-lo[0])*.5),float((hi[1]-lo[1])*.5),float((hi[2]-lo[2])*.5));fitPreview();
 }
 void load(const fs&path){
  try{auto next=zSpace::stereotomy::readObj(path,options());mesh=std::move(next);source=path;saved.clear();loaded=true;validSelection=true;geometry();status=mesh.canExport()?"Preparation checks passed; Save USD.":"Export blocked: "+(mesh.blockers.empty()?std::string("check mesh"):mesh.blockers.front());rebuild();std::cout<<"[ObjPreparation] loaded "<<source.string()<<" vertices="<<mesh.pointsMM.size()<<" faces="<<mesh.faces.size()<<" blockers="<<mesh.blockers.size()<<std::endl;}
  catch(const std::exception&e){validSelection=false;status=e.what();std::cerr<<"[ObjPreparation] "<<status<<std::endl;}
 }
 void openFile(){wchar_t name[32768]{};OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=GetActiveWindow();d.lpstrFilter=L"Wavefront OBJ (*.obj)\0*.obj\0\0";d.lpstrFile=name;d.nMaxFile=32768;d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;d.lpstrTitle=L"Load mesh OBJ (vertex RGB supported)";if(GetOpenFileNameW(&d))load(fs(name));}
 void saveTo(fs destination){
  if(!loaded||!validSelection||!mesh.canExport())throw std::runtime_error("Resolve the listed blockers before saving pipeline input.");
  auto extension=destination.extension().wstring();for(auto&c:extension)c=wchar_t(towlower(c));if(extension!=L".usda")throw std::runtime_error("Choose a .usda filename for the prepared mesh.");const auto text=zSpace::stereotomy::writeUsdaString(mesh,options());auto temporary=destination;temporary+=L".alice2-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
  {std::ofstream out(temporary,std::ios::binary);out<<text;out.close();if(!out){DeleteFileW(temporary.c_str());throw std::runtime_error("Could not write the selected USD file.");}}
  if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());throw std::runtime_error("Could not finish saving USD.");}saved=destination;status="Saved USD. Use it as input if wanted.";std::cout<<"[ObjPreparation] saved "<<saved.string()<<std::endl;
 }
 void useSaved(){if(saved.empty())throw std::runtime_error("Save the checked USD first.");assign(saved,options().role);opened=false;lastOpened=false;rebuild();status="Prepared USD assigned to pipeline.";}
 void saveFile(){if(!loaded||!validSelection||!mesh.canExport())throw std::runtime_error("Resolve the listed blockers before saving pipeline input.");wchar_t name[32768]{};auto suggested=source.stem().wstring()+L"_prepared.usda";wcsncpy_s(name,suggested.c_str(),_TRUNCATE);OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=GetActiveWindow();d.lpstrFilter=L"USD ASCII (*.usda)\0*.usda\0\0";d.lpstrFile=name;d.nMaxFile=32768;d.lpstrDefExt=L"usda";d.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;d.lpstrTitle=L"Save checked pipeline input";if(GetSaveFileNameW(&d))saveTo(fs(name));}
public:
 zObjPreparationPanel(alice2::InputManager&i,std::function<void(const fs&,const std::string&)>use):input(i),assign(std::move(use)){int h;alice2::Application::getInstance()->getWindowSize(screenWidth,h);x=float(std::max(340,screenWidth-285));rebuild();}
 bool onMousePress(int b,int state,int px,int py){if(opened&&b==0&&state==0&&px>=x&&px<=x+270){if(py>=330&&py<=357){action=px<x+135?1:2;return true;}if(py>=366&&py<=393){action=3;return true;}}return ui&&ui->onMousePress(b,state,px,py);}
 bool onMouseMove(int px,int py){return ui&&ui->onMouseMove(px,py);}
 void update(bool busy){
  int w,h;alice2::Application::getInstance()->getWindowSize(w,h);if(w!=screenWidth){screenWidth=w;x=float(std::max(340,w-285));rebuild();}
  if(opened!=lastOpened){lastOpened=opened;rebuild();}
  if(role!=lastRole||unit!=lastUnit||axis!=lastAxis||redJoints!=lastRed){lastRole=role;lastUnit=unit;lastAxis=axis;lastRed=redJoints;if(loaded)load(source);else rebuild();}
  if(action){int next=action;action=0;try{if(busy)throw std::runtime_error("Wait for computation before preparing inputs.");if(next==1)openFile();if(next==2)saveFile();if(next==3)useSaved();}catch(const std::exception&e){status=e.what();}}
 }
 void fitPreview(){auto*app=alice2::Application::getInstance();app->getCamera().setNearPlane(.01f);app->getCamera().setFarPlane(10000000.f);app->getCameraController().focusOnBounds(previewLow,previewHigh);}
 bool active()const{return opened&&loaded;}
 bool drawGeometry(alice2::Scene&scene){if(!opened||!loaded)return false;if(hasSurface){alice2::zDisplayMeshSetting style;style.showVertices=false;style.showEdges=false;style.showFaces=true;style.useMeshColors=true;scene.draw(surface,style);}scene.draw(wire,alice2::Display::lines(alice2::Color(.45f,.48f,.53f),.7f));alice2::zDisplayPointCloudSetting c;c.useMeshColors=true;c.vertexSize=2;if(!hasSurface)scene.draw(vertices,c);for(auto&o:overlays)if(o.shown){if(o.hasEdges)scene.draw(o.edges,alice2::Display::lines(o.color,3));if(o.hasPoints)scene.draw(o.points,alice2::Display::points(o.color,6));}return true;}
 void draw(alice2::Renderer&r){if(ui)ui->draw(r);if(!opened)return;r.setColor(alice2::Color(.08f,.12f,.19f));r.drawString("Input role",x,149);r.drawString("OBJ units (unitless): "+std::string(unitNames[unit]),x,201);r.drawString("Source up axis",x,252);if(role==0)r.drawString("Pattern needs vertex RGB",x,310);r.drawString(loaded?source.filename().string().substr(0,29):"No OBJ loaded",x,416);if(loaded)r.drawString(std::to_string(mesh.pointsMM.size())+" vertices | "+std::to_string(mesh.faces.size())+" faces",x,438);
  float overlayY=459;for(const auto&o:overlays){r.draw2dPoint({x-7,overlayY},o.color,6);overlayY+=20;}
  float y=450+float(overlays.size())*20+15;const std::string note=loaded&&!mesh.canExport()?status:status;size_t at=0;for(int line=0;line<4&&at<note.size();++line){size_t length=std::min<size_t>(29,note.size()-at);if(at+length<note.size()){const auto space=note.rfind(' ',at+length);if(space!=std::string::npos&&space>at)length=space-at;}r.drawString(note.substr(at,length),x,y);at+=length;while(at<note.size()&&note[at]==' ')++at;y+=18;}
  r.drawString("No repair or welding.",x,y+4);r.drawString("Self-intersections unchecked.",x,y+22);r.drawString("Textures/materials not read.",x,y+40);
 }
 // Same import/export operations as the interactive controls, for repeatable QA.
 void validationRun(const fs&inputPath,const fs&outputPath,const std::string&inputRole,bool red,bool assignAfter=false){opened=lastOpened=true;role=lastRole=inputRole=="stereotomy"?1:0;redJoints=lastRed=red;load(inputPath);if(!outputPath.empty())saveTo(outputPath);if(assignAfter)useSaved();}
};
