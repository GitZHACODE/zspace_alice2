// Isolated build opt-in: does not add a second active sketch to ordinary Alice2 builds.
#ifdef ALICE2_BATCH_PATTERN_SKETCH
#include <zspace/interface.h>
#include <windows.h>
#include <commdlg.h>
#include <alice2.h>
#include <utils/OpenGL.h>
#include <sketches/SketchRegistry.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <map>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <zspace/zToolsets/fabrication/stereotomy/BatchPipeline.h>
#include <zspace/zObjPreparationPanel.h>
#include <zspace/zUsdClipboard.h>
#include <zspace/zUsdClipboardInput.h>
#include <iostream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace alice2;
namespace {
using Json=nlohmann::json;
namespace fs=std::filesystem;
Json readJson(const fs::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("Cannot read "+p.string());Json j;f>>j;return j;}
zSpace::zPointArray positions(const Json& a){zSpace::zPointArray p;for(const auto&v:a){if(!v.is_array()||v.size()!=3)throw std::runtime_error("Display point must have three coordinates");float x=v[0].get<float>(),y=v[1].get<float>(),z=v[2].get<float>();if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))throw std::runtime_error("Non-finite display coordinate");p.emplace_back(x,y,z);}return p;}
struct SurfaceDirectionDisplay { int region=-1,selectedAxis=-1; double lengths[2]{}; zSpace::zObjectGraph previews[2]; bool hasPreview[2]{}; };
struct BlockDisplay{std::string name,error;zSpace::zObjectMesh mesh,printMesh;bool hasPrintMesh=false;zSpace::zObjectGraph paths;zSpace::zObjectPointCloud targets,features;size_t pathCount=0,targetCount=0,featureCount=0;};
using BatchResult=zSpace::stereotomy::BatchResult;
const Color colors[]={Color(.08f,.27f,.75f),Color(.02f,.48f,.68f),Color(.23f,.39f,.78f),Color(.18f,.56f,.57f),Color(.34f,.25f,.69f),Color(.12f,.43f,.84f)};
}
class zBatchPatternSketch:public ISketch {
public:
 std::string getName()const override{return "Stereotomy - native pipeline";}
 std::string getDescription()const override{return "Compute every block and apply the blue-driven staggered pattern in one operation.";}
 std::string getAuthor()const override{return "alice2 + zspace_toolsets";}
 void setup()override{
  scene().setBackgroundColor(Color(.97f,.98f,1.f));scene().setShowGrid(false);scene().setShowAxes(false);
  wchar_t modulePath[32768]{};GetModuleFileNameW(nullptr,modulePath,32768);m_package=fs::path(modulePath).parent_path();
  const char* autoRun=std::getenv("ALICE2_BATCH_AUTORUN");
  const char* loadConfig=std::getenv("ALICE2_BATCH_LOAD_CONFIG");
  const bool runOnStartup=autoRun&&std::string(autoRun)=="1";
  const bool loadSavedInputs=runOnStartup||(loadConfig&&std::string(loadConfig)=="1");
  m_config=Json::object();
  try{
   const auto configFile=m_package/"batch_config.json";
   if(fs::exists(configFile)){auto saved=readJson(configFile);if(saved.is_object())m_config=std::move(saved);}
  }catch(const std::exception&e){std::cerr<<"[BatchPattern] Settings could not be loaded: "<<e.what()<<std::endl;}
  for(const char* key:{"sourceMesh","patternMesh"})
   if(!loadSavedInputs||!m_config.contains(key)||!m_config[key].is_string())m_config[key]="";
  if(!loadSavedInputs){m_config.erase("surfaceCourseAxes");m_config.erase("surfaceSignature");}
  m_config.erase("adaptiveSampling");
  auto setting=[this](const char* key,float fallback,float low,float high){if(!m_config.contains(key)||!m_config[key].is_number())return fallback;const double value=m_config[key].get<double>();return std::isfinite(value)&&value>=low&&value<=high?float(value):fallback;};
  m_layerHeight=setting("layerHeightMm",20,1,60);m_spacing=setting("targetSpacingMm",20,5,60);m_amplitude=setting("amplitudeMm",10,0,40);
  m_ui=std::make_unique<SimpleUI>(input());m_ui->setTheme(SimpleUI::UITheme::Dark);
  m_ui->addButtonGroup({"Compute all blocks"},{12,108},235,30,0,m_computeAction);
  m_ui->addSlider("Layer height (mm)",{12,170},235,1,60,m_layerHeight,1);
  m_ui->addSlider("Resample target (mm)",{12,206},235,5,60,m_spacing,1);
  m_ui->addSlider("Pattern amplitude (mm)",{12,242},235,0,40,m_amplitude,1);
  m_ui->addToggle("Input blocks",{12,258,235,26},m_showMeshes);
  m_ui->addToggle("Patterned paths",{12,290,235,26},m_showPaths);
  m_ui->addToggle("Sample targets",{12,322,235,26},m_showTargets);
  m_ui->addToggle("Features",{12,354,235,26},m_showFeatures);
  m_ui->addToggle("Pattern mesh",{12,386,235,26},m_showPattern);
  m_ui->addToggle("Stereotomy mesh",{12,418,235,26},m_showStereotomy);
  m_ui->addToggle("Print mesh",{12,450,235,26},m_showPrintMesh);
  m_ui->addButtonGroup({"Fit all"},{12,494},112,26,0,m_fitAction);
  m_ui->addButtonGroup({"Choose stereotomy mesh"},{12,534},235,26,0,m_sourceAction);
  m_ui->addButtonGroup({"Choose pattern mesh"},{12,572},235,26,0,m_patternAction);
  m_ui->addButtonGroup({"Export USD"},{12,610},235,26,0,m_exportAction);
  m_status="Choose or paste an RGB stereotomy mesh.";
  if(loadSavedInputs){
   try{loadDisplay(inspectSelection(m_config));m_showStereotomy=m_hasStereotomy;m_showPattern=m_hasPattern;if(m_hasStereotomy)m_status="Inputs loaded. Compute all blocks to process.";}
   catch(const std::exception&e){m_status=e.what();}
  }
  fitAll();
  m_objPrep=std::make_unique<zObjPreparationPanel>(input(),[this](const fs::path&path,const std::string&role){assignInput(path,role);});
  const char* objInput=std::getenv("ALICE2_OBJ_INPUT");if(objInput&&*objInput){const char* objOutput=std::getenv("ALICE2_OBJ_OUTPUT");const char* objRole=std::getenv("ALICE2_OBJ_ROLE");const std::string role=objRole?objRole:"pattern";m_objPrep->validationRun(fs::path(objInput),objOutput?fs::path(objOutput):fs::path{},role,role=="stereotomy",std::getenv("ALICE2_OBJ_ASSIGN")!=nullptr);m_captureFrame=Application::getInstance()->getFrameCount()+8;}
  const char* meshView=std::getenv("ALICE2_BATCH_MESH_VIEW");if(meshView){const std::string view(meshView);if(view=="stereotomy"||view=="pattern"){m_showStereotomy=view=="stereotomy";m_showPattern=view=="pattern";m_showPaths=false;m_showFeatures=false;m_showPrintMesh=false;}else if(view=="print"){m_showPrintMesh=true;m_showMeshes=false;m_showPaths=false;m_showFeatures=false;m_showPattern=false;m_showStereotomy=false;}}if(const char* focus=std::getenv("ALICE2_BATCH_FOCUS_REGION")){try{m_focusBlock=std::stoi(focus)-1;}catch(...){m_focusBlock=-1;}}
  const char* previewCapture=std::getenv("ALICE2_BATCH_PREVIEW_CAPTURE");if(previewCapture&&std::string(previewCapture)=="1")m_captureFrame=Application::getInstance()->getFrameCount()+8;
  const char* clipInput=std::getenv("ALICE2_CLIPBOARD_INPUT_FILE");if(clipInput&&*clipInput){const char* role=std::getenv("ALICE2_CLIPBOARD_ROLE");try{std::ifstream in(clipInput,std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});pasteUsdText(text,role?role:"pattern");}catch(const std::exception&e){m_status=e.what();std::cerr<<"[Clipboard] FAILED "<<m_status<<std::endl;}}
  const char* pasteTest=std::getenv("ALICE2_CLIPBOARD_PASTE_TEST_ROLE");if(pasteTest&&*pasteTest){try{pasteUsdText(readUsdClipboardText(),pasteTest);}catch(const std::exception&e){m_status=e.what();std::cerr<<"[Clipboard] FAILED "<<m_status<<std::endl;}}
  if(std::getenv("ALICE2_CLEAR_PATTERN_TEST"))clearPattern();
  const char* axisTest=std::getenv("ALICE2_SURFACE_AXIS_TEST");if(axisTest&&*axisTest){try{const std::string value(axisTest);const auto colon=value.find(':');if(colon==std::string::npos)throw std::runtime_error("Surface axis test must be region:axis.");const int region=std::stoi(value.substr(0,colon)),axis=std::stoi(value.substr(colon+1));if(axis!=0&&axis!=1)throw std::runtime_error("Surface axis test must select 0 or 1.");bool found=false;for(int index=0;index<int(m_surfaceDirections.size());++index)if(m_surfaceDirections[index].region==region){m_surfacePreviewRow=index;selectSurfaceAxis(index,axis);found=true;std::cout<<"[SurfaceDirection] callback region="<<region<<" axis="<<axis<<std::endl;break;}if(!found)throw std::runtime_error("Surface axis test region not found.");}catch(const std::exception&e){m_status=e.what();std::cerr<<"[SurfaceDirection] FAILED "<<m_status<<std::endl;}}
  if(runOnStartup)m_computePending=true;
 }
 void cleanup()override{m_cancel=true;if(m_worker.valid())m_worker.wait();m_ui.reset();}
 bool onMousePress(int b,int s,int x,int y)override{
  if(m_objPrep&&m_objPrep->onMousePress(b,s,x,y))return true;
  if(b==0&&s==0&&!(m_objPrep&&m_objPrep->active())){int w=0,h=0;Application::getInstance()->getWindowSize(w,h);if(x>=w-286&&x<=w-14){if(y>=148&&y<=176){m_pasteRole="stereotomy";return true;}if(y>=184&&y<=212){m_pasteRole="pattern";return true;}if(y>=220&&y<=248){m_copyPending=true;return true;}if(y>=256&&y<=284){m_clearPatternPending=true;return true;}}}
  if(b==0&&s==0&&m_openSurface&&!(m_objPrep&&m_objPrep->active())){int w=0,h=0;Application::getInstance()->getWindowSize(w,h);if(x>=w-286&&x<=w-14){const int row=(y-320)/32;if(y>=320&&row>=0&&row<8&&y<320+row*32+26){const int index=m_surfacePage*8+row;if(index<int(m_surfaceDirections.size())){m_surfacePreviewRow=index;if(x>=w-214&&x<=w-118)selectSurfaceAxis(index,0);else if(x>=w-112&&x<=w-16)selectSurfaceAxis(index,1);return true;}}const int footer=320+32*int((std::min)(std::size_t(8),m_surfaceDirections.size()-std::size_t(m_surfacePage*8)));if(m_surfaceDirections.size()>8&&y>=footer+48&&y<=footer+74){if(x<w-150&&m_surfacePage>0)--m_surfacePage;else if(x>=w-150&&(m_surfacePage+1)*8<int(m_surfaceDirections.size()))++m_surfacePage;m_surfaceUi.reset();return true;}}}
  if(b==0&&s==0&&x>=12&&x<=247){if(y>=108&&y<=138){m_computePending=true;return true;}if(y>=494&&y<=520&&x<=124){m_fitPending=true;return true;}if(y>=534&&y<=560){m_sourcePending=true;return true;}if(y>=572&&y<=598){m_patternPending=true;return true;}if(y>=610&&y<=636){m_exportPending=true;return true;}}
  return m_ui&&m_ui->onMousePress(b,s,x,y);
 }
 bool onMouseMove(int x,int y)override{if(m_objPrep&&m_objPrep->onMouseMove(x,y))return true;return m_ui&&m_ui->onMouseMove(x,y);}
 bool onKeyPress(unsigned char key,int,int)override{if(key=='c'||key=='C'){if(!m_running)startBatch();return true;}if(key=='e'||key=='E'){m_exportPending=true;return true;}if(key=='f'||key=='F'){fitAll();return true;}return false;}
 void update(float dt)override{
  if(m_objPrep){const bool was=m_objPrep->active();m_objPrep->update(m_running);if(was&&!m_objPrep->active())fitAll();}
  if(m_computePending){m_computePending=false;if(!m_running)startBatch();}
  if(m_fitPending){m_fitPending=false;fitAll();}
  if(m_sourcePending){m_sourcePending=false;chooseMesh("stereotomy",L"Choose RGB stereotomy mesh");}
  if(m_patternPending){m_patternPending=false;chooseMesh("pattern",L"Choose optional RGB pattern mesh");}
  if(m_exportPending){m_exportPending=false;exportUsd();}
  if(!m_pasteRole.empty()){auto role=std::move(m_pasteRole);m_pasteRole.clear();try{pasteUsdText(readUsdClipboardText(),role);}catch(const std::exception&e){m_status=e.what();std::cerr<<"[Clipboard] FAILED "<<m_status<<std::endl;}}
  if(m_copyPending){m_copyPending=false;copyUsd();}
  if(m_clearPatternPending){m_clearPatternPending=false;clearPattern();}
  if(m_running){m_progressClock+=dt;if(m_progressClock>1){m_progressClock=0;std::cout<<"[BatchPattern] working frame="<<Application::getInstance()->getFrameCount()<<std::endl;{std::lock_guard<std::mutex> lock(m_progressMutex);if(!m_workerProgress.empty())m_status=m_workerProgress;}}
   if(m_worker.valid()&&m_worker.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){m_running=false;
    try{auto result=m_worker.get();loadDisplay(result.display);m_lastUsd=result.combinedUsd;m_status=m_failedBlocks?"Computed valid blocks; "+std::to_string(m_failedBlocks)+" block(s) retained for geometry review.":m_jointNeedsReview?"Computed paths; shared-joint staggering needs review.":"Completed native pipeline. Outputs saved in the latest results folder.";std::cout<<"[BatchPattern] timings "<<result.timings.dump()<<std::endl;fitAll();std::cout<<"[BatchPattern] complete blocks="<<m_blocks.size()<<" paths="<<m_paths<<" targets="<<m_targets<<" printMeshes="<<m_printMeshes<<" failedBlocks="<<m_failedBlocks<<" jointStaggerComplete="<<(!m_jointNeedsReview)<<" exit=0"<<" output="<<m_run.string()<<std::endl;
     const char* copyTest=std::getenv("ALICE2_CLIPBOARD_COPY_TEST");if(copyTest&&*copyTest){copyUsd();std::ofstream copied(copyTest,std::ios::binary);copied<<readUsdClipboardText();}
     const char* autoExport=std::getenv("ALICE2_BATCH_EXPORT_TEST");if(autoExport&&*autoExport)saveUsd(fs::path(autoExport));
     const char* capture=std::getenv("ALICE2_BATCH_CAPTURE_EXIT");if(capture&&std::string(capture)=="1")m_captureFrame=Application::getInstance()->getFrameCount()+8;
    }catch(const std::exception&e){m_status=std::string("Run failed; previous display retained. ")+e.what();std::cerr<<"[BatchPattern] FAILED "<<m_status<<std::endl;const char* capture=std::getenv("ALICE2_BATCH_CAPTURE_EXIT");if(capture&&std::string(capture)=="1")m_captureFrame=Application::getInstance()->getFrameCount()+8;}
   }
  }
  if(m_captureFrame>=0&&Application::getInstance()->getFrameCount()>=m_captureFrame){m_captureFrame=-1;Application::getInstance()->takeScreenshot();Application::getInstance()->quit();}
 }
 void draw(Renderer&r,Camera&)override{
  if(!(m_objPrep&&m_objPrep->drawGeometry(scene()))){
  auto drawColoredMesh=[this](zSpace::zObjectMesh&mesh){zDisplayMeshSetting style;style.showFaces=true;style.showEdges=false;style.showVertices=false;style.useMeshColors=true;glPushAttrib(GL_ENABLE_BIT|GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);scene().draw(mesh,style);glPopAttrib();};
  if(m_showPattern&&m_hasPattern)drawColoredMesh(m_patternMesh);
  if(m_showStereotomy&&m_hasStereotomy)drawColoredMesh(m_stereotomyMesh);
  for(size_t i=0;i<m_blocks.size();++i){if(m_focusBlock>=0&&int(i)!=m_focusBlock)continue;auto&b=m_blocks[i];if(m_showPrintMesh&&b.hasPrintMesh){zDisplayMeshSetting style;style.showFaces=true;style.showEdges=m_focusBlock>=0;style.showVertices=false;style.faceColor=m_focusBlock>=0?Color(.82f,.83f,.85f):colors[i%6];style.edgeWidth=.65f;scene().draw(b.printMesh,style);}if(m_showMeshes)scene().draw(b.mesh,Display::wireframe(b.error.empty()?Color(.65f,.69f,.74f):Color(.92f,.27f,.05f),b.error.empty()?.65f:1.5f));if(m_showPaths)scene().draw(b.paths,Display::lines(colors[i%6],1.5f));if(m_showTargets)scene().draw(b.targets,Display::points(Color(.12f,.18f,.25f),3.f));if(m_showFeatures)scene().draw(b.features,Display::points(Color(.78f,.12f,.35f),5.f));}
  }
  if(m_openSurface&&m_surfacePreviewRow>=0&&m_surfacePreviewRow<int(m_surfaceDirections.size())&&!(m_objPrep&&m_objPrep->active())){auto&direction=m_surfaceDirections[m_surfacePreviewRow];glPushAttrib(GL_ENABLE_BIT|GL_DEPTH_BUFFER_BIT);glDisable(GL_DEPTH_TEST);for(int axis=0;axis<2;++axis)if(direction.hasPreview[axis])scene().draw(direction.previews[axis],Display::lines(axis==0?Color(.95f,.35f,.04f):Color(.02f,.55f,.77f),direction.selectedAxis==axis?4.f:2.f));glPopAttrib();}
  r.setColor(Color(.06f,.10f,.17f));r.drawString(getName(),12,26);
  r.drawString(std::to_string(m_blocks.size())+" blocks | "+std::to_string(m_paths)+(m_openSurface?" open courses | ":" layer loops | ")+std::to_string(m_targets)+" targets | "+std::to_string(m_printMeshes)+" print meshes",12,48);
  std::ostringstream settings;settings<<"Layer: "<<m_layerHeight<<" mm | target: "<<m_spacing<<" mm (min > "<<m_spacing*.5<<")";if(m_minimumBaseChord>=0)settings<<" | last min ~"<<std::fixed<<std::setprecision(3)<<m_minimumBaseChord<<" mm";settings<<" | fixed counts";if(m_hasStereotomy)settings<<(m_openSurface?" | open paths":" | closed loops");
  r.drawString(m_running?"Computing native topology + pattern...":settings.str(),12,70);
  r.drawString(m_objPrep&&m_objPrep->active()?"OBJ preparation preview. Save USD, then Use saved USD to assign it.":m_status.substr(0,145),12,92);
  r.drawString("Stereotomy: "+(m_hasStereotomy?fs::path(m_config.at("sourceMesh").get<std::string>()).filename().string().substr(0,38):std::string("none")),12,650);
  r.drawString("Pattern: "+(hasPatternSelection()?fs::path(m_config["patternMesh"].get<std::string>()).filename().string().substr(0,38):std::string(m_hasStereotomy?"using stereotomy blue":"none (optional)")),12,672);
  if(m_reviewLines.size()<3)r.drawString("C compute | E export USD | F fit | F9 screenshot",12,708);
  int windowW=0,windowH=0;Application::getInstance()->getWindowSize(windowW,windowH);
  r.setColor(Color(.62f,.28f,.08f));for(size_t i=0;i<m_reviewLines.size();++i)r.drawString(m_reviewLines[i],12,windowH-40-int(m_reviewLines.size()-1-i)*22);r.setColor(Color(.06f,.10f,.17f));
  if(!m_summary.empty())r.drawString(m_summary.substr(0,145),12,windowH-18);
  if(m_ui)m_ui->draw(r);
  if(m_openSurface&&!(m_objPrep&&m_objPrep->active()))drawSurfaceDirections(r,windowW);
  if(m_objPrep)m_objPrep->draw(r);
  if(!(m_objPrep&&m_objPrep->active())){if(m_clipboardWidth!=windowW){m_clipboardWidth=windowW;m_clipboardUi=std::make_unique<SimpleUI>(input());m_clipboardUi->setTheme(SimpleUI::UITheme::Dark);m_clipboardUi->addButtonGroup({"Paste stereotomy USD"},{float(windowW-286),148},272,28,0,m_clipboardDummy);m_clipboardUi->addButtonGroup({"Paste optional pattern USD"},{float(windowW-286),184},272,28,0,m_clipboardDummy);m_clipboardUi->addButtonGroup({"Copy computed USD"},{float(windowW-286),220},272,28,0,m_clipboardDummy);m_clipboardUi->addButtonGroup({"Clear pattern override"},{float(windowW-286),256},272,28,0,m_clipboardDummy);}m_clipboardUi->draw(r);}
 }
private:
 static std::map<int,int> surfaceAxes(const Json& config){
  std::map<int,int> axes;
  if(!config.contains("surfaceCourseAxes")||!config.at("surfaceCourseAxes").is_object())return axes;
  for(auto it=config.at("surfaceCourseAxes").begin();it!=config.at("surfaceCourseAxes").end();++it){try{std::size_t used=0;int region=std::stoi(it.key(),&used);if(used!=it.key().size()||region<0||!it.value().is_number_integer())continue;int axis=it.value().get<int>();if(axis==0||axis==1)axes[region]=axis;}catch(const std::exception&){}}
  return axes;
 }
 void selectSurfaceAxis(int index,int axis){
  if(m_running){m_status="Calculation running. Change course directions after it finishes.";return;}
  try{auto selected=m_config;if(!selected.contains("surfaceCourseAxes")||!selected["surfaceCourseAxes"].is_object())selected["surfaceCourseAxes"]=Json::object();selected["surfaceCourseAxes"][std::to_string(m_surfaceDirections[index].region)]=axis;persistConfig(selected);m_config=std::move(selected);m_surfaceDirections[index].selectedAxis=axis;m_surfaceUi.reset();m_lastUsd.clear();m_status="Region "+std::to_string(m_surfaceDirections[index].region+1)+": direction "+(axis==0?"A":"B")+" saved. Compute to apply.";}catch(const std::exception&e){m_status=e.what();}
 }
 void drawSurfaceDirections(Renderer&r,int width){
  const int first=m_surfacePage*8,count=int((std::min)(std::size_t(8),m_surfaceDirections.size()-std::size_t(first)));
  if(!m_surfaceUi||m_surfaceWidth!=width){m_surfaceWidth=width;m_surfaceUi=std::make_unique<SimpleUI>(input());m_surfaceUi->setTheme(SimpleUI::UITheme::Dark);for(int row=0;row<count;++row){auto&d=m_surfaceDirections[first+row];m_surfaceUi->addButtonGroup({"",""},{float(width-214),float(320+row*32)},96,26,6,d.selectedAxis);}}
  r.setColor(Color(.06f,.10f,.17f));r.drawString("Course direction",width-286,306);
  for(int row=0;row<count;++row){const auto&d=m_surfaceDirections[first+row];r.drawString(std::string(first+row==m_surfacePreviewRow?"> ":"  ")+"R"+std::to_string(d.region+1),width-284,338+row*32);}
  m_surfaceUi->draw(r);r.setColor(Color(.06f,.10f,.17f));for(int row=0;row<count;++row){const auto&d=m_surfaceDirections[first+row];r.drawString(d.selectedAxis==0?"* A":"A",width-184,338+row*32);r.drawString(d.selectedAxis==1?"* B":"B",width-82,338+row*32);}const int footer=320+count*32;r.setColor(Color(.95f,.35f,.04f));r.drawString("A: orange",width-286,footer+16);r.setColor(Color(.02f,.55f,.77f));r.drawString("B: blue",width-150,footer+16);r.setColor(Color(.06f,.10f,.17f));r.drawString("* selected | choose every region",width-286,footer+36);
  if(m_surfaceDirections.size()>8)r.drawString("< Previous     Next >",width-280,footer+65);
 }
 bool hasPatternSelection()const{return m_config.contains("patternMesh")&&m_config.at("patternMesh").is_string()&&!m_config.at("patternMesh").get<std::string>().empty();}
 void applyInputSelection(Json selected){
  auto preview=inspectSelection(selected);
  persistConfig(selected);
  try{loadDisplay(preview);}catch(...){persistConfig(m_config);throw;}
  m_config=std::move(selected);
  m_lastUsd.clear();
  m_showStereotomy=m_hasStereotomy;
  m_showPattern=m_hasPattern;
  fitAll();
 }
 void assignInput(const fs::path&path,const std::string&role){
  auto selected=m_config;
  selected[role=="stereotomy"?"sourceMesh":"patternMesh"]=path.string();
  if(role=="stereotomy"){selected.erase("surfaceCourseAxes");selected.erase("surfaceSignature");}
  applyInputSelection(std::move(selected));
  m_status=m_hasStereotomy?"Inputs loaded. Compute all blocks to process.":"Pattern loaded. Choose or paste the RGB stereotomy mesh.";
 }
 void clearPattern(){
  if(m_running){m_status="Calculation running. Clear the pattern after it finishes.";return;}
  try{
   if(hasPatternSelection()){auto selected=m_config;selected["patternMesh"]="";applyInputSelection(std::move(selected));}
   m_status=m_hasStereotomy?"Using stereotomy blue. Compute all blocks to apply.":"Choose or paste an RGB stereotomy mesh.";
  }
  catch(const std::exception&e){m_status=e.what();}
 }
 static std::string readTextFile(const fs::path&path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read USD text.");return {std::istreambuf_iterator<char>(in),{}};}
 void pasteUsdText(const std::string&text,const std::string&role){
  if(m_running)throw std::runtime_error("Calculation running. Paste inputs after it finishes.");
  const auto stamp=std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  const auto folder=m_package/"clipboard_inputs"/(role+"_"+stamp);fs::create_directories(folder);
  {std::ofstream original(folder/"original.usda",std::ios::binary);original<<text;if(!original)throw std::runtime_error("Cannot preserve copied USD input.");}
  const auto destination=folder/"mesh.usda";const auto summary=normalizeUsdInputText(text,destination,role);
  assignInput(destination,role);
  m_status="Pasted "+role+" USD: "+std::to_string(summary.vertices)+" vertices, "+std::to_string(summary.faces)+" faces. "+
   (m_hasStereotomy?"Compute all blocks to process.":"Choose or paste the RGB stereotomy mesh.");
  std::ofstream report(folder/"import_report.json");report<<Json({{"role",role},{"sourcePrim",summary.sourcePrim},{"vertices",summary.vertices},{"faces",summary.faces},{"sourceUpAxis",summary.sourceUpAxis},{"sourceMetersPerUnit",summary.sourceMetersPerUnit},{"mirrored",summary.mirrored},{"windingReversed",summary.windingReversed}}).dump(2);
  std::cout<<"[Clipboard] imported "<<destination.string()<<" "<<summary.vertices<<" vertices "<<summary.faces<<" faces"<<std::endl;
 }
 void copyUsd(){try{
  if(m_lastUsd.empty()||!fs::is_regular_file(m_lastUsd))throw std::runtime_error("Compute all blocks successfully before copying USD.");
  const auto text=readTextFile(m_lastUsd);const auto folder=m_package/"clipboard_backups";fs::create_directories(folder);
  std::string previous;bool hasText=false;try{previous=readUsdClipboardText();hasText=true;}catch(const std::exception&){ }
  if(hasText){const auto stamp=std::to_string(GetTickCount64());std::ofstream backup(folder/("before_copy_"+stamp+".txt"),std::ios::binary);backup<<previous;if(!backup)throw std::runtime_error("Cannot preserve previous clipboard.");}
  writeUsdClipboardText(text);m_status="Copied computed USD. Use Paste in the zSpace USD shelf of the other app.";std::cout<<"[Clipboard] copied computed USD bytes="<<text.size()<<std::endl;
 }catch(const std::exception&e){m_status=e.what();std::cerr<<"[Clipboard] copy failed: "<<m_status<<std::endl;}}
 void saveUsd(const fs::path& destination){
  if(m_lastUsd.empty()||!fs::is_regular_file(m_lastUsd))throw std::runtime_error("Compute all blocks successfully before exporting USD.");
  std::error_code ec;if(fs::equivalent(m_lastUsd,destination,ec)){m_status="USD already saved at the selected location.";return;}
  // Stage alongside the destination, then atomically replace it. The original
  // remains intact if copying or replacement fails; metadata is copied verbatim.
  auto temporary=destination;temporary+=L".alice2-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tmp";
  if(!CopyFileW(m_lastUsd.c_str(),temporary.c_str(),TRUE))throw std::runtime_error("Could not copy USD to the selected folder (Windows error "+std::to_string(GetLastError())+").");
  if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DWORD error=GetLastError();DeleteFileW(temporary.c_str());throw std::runtime_error("Could not finish saving USD (Windows error "+std::to_string(error)+").");}
  m_status="Exported USD: "+destination.string();std::cout<<"[BatchPattern] exported USD "<<destination.string()<<std::endl;
 }
 void exportUsd(){
  try{
   if(m_lastUsd.empty()||!fs::is_regular_file(m_lastUsd)){m_status="Compute all blocks successfully before exporting USD.";return;}
   wchar_t filename[32768]=L"all_blocks_staggered_pattern.usda";OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();dialog.lpstrFilter=L"USD ASCII (*.usda)\0*.usda\0\0";dialog.lpstrFile=filename;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"usda";dialog.lpstrTitle=L"Export all patterned blocks as USD";dialog.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
   if(GetSaveFileNameW(&dialog))saveUsd(fs::path(filename));
  }catch(const std::exception&e){m_status=e.what();std::cerr<<"[BatchPattern] USD export failed: "<<m_status<<std::endl;}
 }
 Json inspectSelection(Json& config){
  auto selected=config;zSpace::stereotomy::BatchRequest request;request.sourceMesh=fs::path(selected.at("sourceMesh").get<std::string>());request.patternMesh=fs::path(selected.value("patternMesh",std::string{}));request.surfaceCourseAxes=surfaceAxes(selected);request.useSourceBlue=true;
  auto preview=zSpace::stereotomy::inspectInputs(request);const std::string signature=preview.value("surfaceSignature",std::string{});
  if(!signature.empty()){
   const std::string previous=selected.contains("surfaceSignature")&&selected.at("surfaceSignature").is_string()?selected.at("surfaceSignature").get<std::string>():std::string{};
   if(!previous.empty()&&previous!=signature){selected["surfaceCourseAxes"]=Json::object();request.surfaceCourseAxes.clear();preview=zSpace::stereotomy::inspectInputs(request);}
   // Explicit axes without a signature are adopted once, then tied to source data.
   selected["surfaceSignature"]=preview.value("surfaceSignature",signature);
  }
  config=std::move(selected);return preview;
 }
 void persistConfig(const Json& config){
  const auto destination=m_package/"batch_config.json";auto temporary=destination;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
  {std::ofstream out(temporary);out<<config.dump(2);out.close();if(!out){DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot save selected input files.");}}
  if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot update saved input selections.");}
 }
 void chooseMesh(const std::string& role,const wchar_t* title){
  if(m_running){m_status="Calculation running. Select inputs after it finishes.";return;}
  wchar_t filename[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();dialog.lpstrFilter=L"USDA mesh (*.usda)\0*.usda\0\0";dialog.lpstrFile=filename;dialog.nMaxFile=32768;dialog.lpstrTitle=title;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
  if(!GetOpenFileNameW(&dialog))return;
  try{
   const auto folder=m_package/"file_inputs"/(role+"_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));
   fs::create_directories(folder);
   const auto destination=folder/"mesh.usda";
   normalizeUsdInputFile(fs::path(filename),destination,role);
   assignInput(destination,role);
  }catch(const std::exception&e){m_status=e.what();}
 }
 void startBatch(){
  try{
   if(m_config.value("sourceMesh",std::string{}).empty())throw std::runtime_error("Choose or paste an RGB stereotomy mesh before computing.");
   auto refreshed=m_config;auto inputPreview=inspectSelection(refreshed);
   const bool sourceChanged=refreshed.value("surfaceSignature",std::string{})!=m_config.value("surfaceSignature",std::string{})||inputPreview.value("openSurface",false)!=m_openSurface;
   if(sourceChanged){loadDisplay(inputPreview);persistConfig(refreshed);m_config=std::move(refreshed);m_lastUsd.clear();fitAll();}else m_config=std::move(refreshed);
   if(m_openSurface){if(m_surfaceDirections.empty())throw std::runtime_error("Surface direction preview unavailable; reload the stereotomy mesh.");for(const auto&d:m_surfaceDirections)if(d.selectedAxis!=0&&d.selectedAxis!=1)throw std::runtime_error("Choose course direction A or B for every surface region before computing (region "+std::to_string(d.region+1)+" is unset).");}
   auto settings=m_config;settings["layerHeightMm"]=m_layerHeight;settings["targetSpacingMm"]=m_spacing;settings["amplitudeMm"]=m_amplitude;settings["colorRemap"]={{"enabled",false},{"function","linear"}};persistConfig(settings);m_config=std::move(settings);
   const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();m_run=m_package/"results"/("run_"+std::to_string(stamp));fs::create_directories(m_run);
   zSpace::stereotomy::BatchRequest request;request.sourceMesh=fs::path(m_config.at("sourceMesh").get<std::string>());request.patternMesh=fs::path(m_config.value("patternMesh",std::string{}));request.outputDirectory=m_run;request.layerHeightMm=m_layerHeight;request.targetSpacingMm=m_spacing;request.amplitudeMm=m_amplitude;request.featureAngleDeg=120;request.colorRemap.Enabled=false;request.adaptiveSampling=false;request.useSourceBlue=true;request.surfaceCourseAxes=surfaceAxes(m_config);
   Json requestJson={{"sourceMesh",request.sourceMesh.string()},{"patternMesh",request.patternMesh.string()},{"outputDirectory",m_run.string()},{"layerHeightMm",request.layerHeightMm},{"targetSpacingMm",request.targetSpacingMm},{"amplitudeMm",request.amplitudeMm},{"featureAngleDeg",request.featureAngleDeg}};requestJson["adaptiveSampling"]=false;requestJson["blueSource"]=request.patternMesh.empty()?"stereotomy":"pattern";requestJson["colorRemap"]=m_config.at("colorRemap");requestJson["surfaceCourseAxes"]=m_config.value("surfaceCourseAxes",Json::object());requestJson["surfaceSignature"]=m_config.value("surfaceSignature",std::string{});std::ofstream(m_run/"request.json")<<requestJson.dump(2);
   m_cancel=false;m_running=true;m_status="Preparing stereotomy and computing all blocks...";m_progressClock=0;{std::lock_guard<std::mutex> lock(m_progressMutex);m_workerProgress.clear();}
   m_worker=std::async(std::launch::async,[this,request](){
#ifdef _OPENMP
    omp_set_num_threads(4);
#endif
    return zSpace::stereotomy::runBatch(request,m_cancel,[this](const std::string& progress){std::lock_guard<std::mutex> lock(m_progressMutex);m_workerProgress=progress;std::cout<<"[BatchPattern] "<<progress<<std::endl;});});std::cout<<"[BatchPattern] compute requested "<<m_run.string()<<std::endl;
  }catch(const std::exception&e){m_running=false;m_status=e.what();}
 }
 void loadDisplay(const Json&j){
  std::vector<SurfaceDirectionDisplay> surfaceDirections;const bool openSurface=j.value("openSurface",false);
  if(openSurface&&j.contains("surfaceDirections"))for(const auto&entry:j.at("surfaceDirections")){SurfaceDirectionDisplay d;d.region=entry.at("region").get<int>();d.selectedAxis=entry.value("selectedAxis",-1);if(d.selectedAxis!=0&&d.selectedAxis!=1)d.selectedAxis=-1;const auto&lengths=entry.at("meanAxisLengthsMM");if(lengths.size()!=2)throw std::runtime_error("Surface direction lengths must contain two values.");for(int axis=0;axis<2;++axis){d.lengths[axis]=lengths[axis].get<double>();if(!std::isfinite(d.lengths[axis])||d.lengths[axis]<0)throw std::runtime_error("Invalid surface direction length.");auto p=positions(entry.at(axis==0?"previewA":"previewB"));zSpace::zIntArray edges;for(int k=0;k+1<int(p.size());++k){edges.push_back(k);edges.push_back(k+1);}if(p.size()>1){zSpace::zFnGraph(d.previews[axis]).create(p,edges);d.hasPreview[axis]=true;}}surfaceDirections.push_back(std::move(d));}
  std::map<std::string,std::string> reviewGroups;std::vector<BlockDisplay> blocks;size_t paths=0,targets=0,printMeshes=0,failedBlocks=0;zSpace::zPoint low(1e20f,1e20f,1e20f),high(-1e20f,-1e20f,-1e20f);
  for(const auto&entry:j.at("blocks")){BlockDisplay b;b.name=entry.value("name","block");b.error=entry.value("error",std::string{});if(!b.error.empty()){++failedBlocks;auto&names=reviewGroups["slice generation failed"];names+=(names.empty()?"":", ")+b.name;}else if(entry.value("provisional",false)&&!openSurface){const auto reason=entry.value("reviewReason",std::string{});const bool reversed=reason.find("reversed")!=std::string::npos||reason.find("folded")!=std::string::npos;const std::string category=reversed?"reversed terminal-cap faces":"joint/terminal coverage";auto&names=reviewGroups[category];names+=(names.empty()?"":", ")+b.name;}const auto&m=entry.at("mesh");auto p=positions(m.at("points"));auto counts=m.at("counts").get<zSpace::zIntArray>();auto indices=m.at("indices").get<zSpace::zIntArray>();zSpace::zFnMesh(b.mesh).create(p,counts,indices);
   for(const auto&v:p){low.x=(std::min)(low.x,v.x);low.y=(std::min)(low.y,v.y);low.z=(std::min)(low.z,v.z);high.x=(std::max)(high.x,v.x);high.y=(std::max)(high.y,v.y);high.z=(std::max)(high.z,v.z);}
   if(entry.contains("printMesh")){const auto&pm=entry.at("printMesh");auto pp=positions(pm.at("points"));auto pc=pm.at("counts").get<zSpace::zIntArray>();auto pi=pm.at("indices").get<zSpace::zIntArray>();if(!pp.empty()){zSpace::zFnMesh(b.printMesh).create(pp,pc,pi);b.hasPrintMesh=true;++printMeshes;}}
   zSpace::zPointArray gp,tp,fp;zSpace::zIntArray edges;
   if(entry.contains("paths"))for(const auto&path:entry["paths"]){auto v=positions(path.at("points"));if(v.size()<2)continue;bool closed=path.value("closed",true);if(closed&&v.size()>2){auto d=v.front()-v.back();if(d.x*d.x+d.y*d.y+d.z*d.z<1e-8f)v.pop_back();}int base=int(gp.size());gp.insert(gp.end(),v.begin(),v.end());for(int k=0;k+1<int(v.size());++k){edges.push_back(base+k);edges.push_back(base+k+1);}if(closed){edges.push_back(base+int(v.size())-1);edges.push_back(base);}auto tv=path.contains("targets")?positions(path["targets"]):v;tp.insert(tp.end(),tv.begin(),tv.end());if(path.contains("features")){auto fv=positions(path["features"]);fp.insert(fp.end(),fv.begin(),fv.end());}++b.pathCount;}
   if(!gp.empty())zSpace::zFnGraph(b.paths).create(gp,edges);if(!tp.empty())zSpace::zFnPointCloud(b.targets).create(tp);if(!fp.empty())zSpace::zFnPointCloud(b.features).create(fp);b.targetCount=tp.size();b.featureCount=fp.size();paths+=b.pathCount;targets+=b.targetCount;blocks.push_back(std::move(b));
  }
  auto loadColoredMesh=[&](const Json&m,zSpace::zObjectMesh&mesh){auto p=positions(m.at("points"));for(const auto&v:p){low.x=(std::min)(low.x,v.x);low.y=(std::min)(low.y,v.y);low.z=(std::min)(low.z,v.z);high.x=(std::max)(high.x,v.x);high.y=(std::max)(high.y,v.y);high.z=(std::max)(high.z,v.z);}auto counts=m.at("counts").get<zSpace::zIntArray>();auto indices=m.at("indices").get<zSpace::zIntArray>();zSpace::zColorArray colors;for(const auto&c:m.at("colors")){if(c.size()!=3)throw std::runtime_error("Mesh vertex color must be RGB.");colors.emplace_back(c[0].get<float>(),c[1].get<float>(),c[2].get<float>(),.35f);}if(colors.size()!=p.size())throw std::runtime_error("Mesh vertex/color count mismatch.");zSpace::zObjectMesh colored;zSpace::zFnMesh fn(colored);fn.create(p,counts,indices);fn.setVertexColors(colors);mesh=std::move(colored);};
  zSpace::zObjectMesh patternMesh,stereotomyMesh;const bool hasPattern=j.contains("patternMesh"),hasStereotomy=j.contains("stereotomyMesh");
  if(hasPattern)loadColoredMesh(j.at("patternMesh"),patternMesh);
  if(hasStereotomy)loadColoredMesh(j.at("stereotomyMesh"),stereotomyMesh);
  if(low.x>high.x){low=zSpace::zPoint();high=zSpace::zPoint();}
  if(m_focusBlock>=0&&m_focusBlock<int(j.at("blocks").size())){low=zSpace::zPoint(1e20f,1e20f,1e20f);high=zSpace::zPoint(-1e20f,-1e20f,-1e20f);for(const auto&v:positions(j.at("blocks").at(m_focusBlock).at("mesh").at("points"))){low.x=(std::min)(low.x,v.x);low.y=(std::min)(low.y,v.y);low.z=(std::min)(low.z,v.z);high.x=(std::max)(high.x,v.x);high.y=(std::max)(high.y,v.y);high.z=(std::max)(high.z,v.z);}}
  if(m_focusBlock>=0){const float center=.5f*(low.z+high.z),radius=.16f*(high.z-low.z);low.z=center-radius;high.z=center+radius;}
  m_surfaceUi.reset();m_surfaceDirections=std::move(surfaceDirections);m_openSurface=openSurface;m_surfacePage=0;m_surfacePreviewRow=m_surfaceDirections.empty()?-1:0;
  m_patternMesh=std::move(patternMesh);m_stereotomyMesh=std::move(stereotomyMesh);m_hasPattern=hasPattern;m_hasStereotomy=hasStereotomy;m_blocks=std::move(blocks);m_paths=paths;m_targets=targets;m_printMeshes=printMeshes;m_failedBlocks=failedBlocks;m_low=Vec3(low.x,low.y,low.z);m_high=Vec3(high.x,high.y,high.z);m_summary=j.value("summary",std::string{});m_reviewLines.clear();for(const auto&group:reviewGroups)m_reviewLines.push_back("Geometry review - "+group.first+": "+group.second);
  m_minimumBaseChord=j.value("samplingActualMinimumChordMM",-1.);if(j.contains("samplingMinimumComplete")&&!j.value("samplingMinimumComplete",true)){std::string note="Sampling review: fixed features prevent the requested minimum; see report.";if(j.contains("samplingReview")&&!j.at("samplingReview").empty())note="Sampling review: "+j.at("samplingReview").front().get<std::string>();m_reviewLines.push_back(note.substr(0,145));}
  m_jointNeedsReview=j.contains("jointStaggerComplete")&&!j.value("jointStaggerComplete",true);
  std::vector<std::string> jointReview;if(j.contains("jointStaggerReview"))for(const auto&message:j.at("jointStaggerReview"))jointReview.push_back(message.get<std::string>());
  if(m_jointNeedsReview||!jointReview.empty()){std::string note="Shared-joint stagger review: ";size_t shown=0;for(const auto&message:jointReview){if(shown&&note.size()+message.size()>125)break;note+=(shown?"; ":"")+message;++shown;}if(jointReview.empty())note+="not fully verified; see output report";else if(shown<jointReview.size())note+=" (+"+std::to_string(jointReview.size()-shown)+" more in report)";m_reviewLines.push_back(note.substr(0,145));}
 }
 void fitAll(){if(m_objPrep&&m_objPrep->active()){m_objPrep->fitPreview();return;}if(!m_blocks.empty()||m_hasPattern||m_hasStereotomy){auto*app=Application::getInstance();app->getCamera().setNearPlane(.5f);app->getCamera().setFarPlane(100000.f);app->getCameraController().focusOnBounds(m_low,m_high);}}
 std::vector<SurfaceDirectionDisplay>m_surfaceDirections;std::unique_ptr<SimpleUI>m_surfaceUi;bool m_openSurface=false;int m_focusBlock=-1,m_surfaceWidth=-1,m_surfacePage=0,m_surfacePreviewRow=-1;
 std::unique_ptr<zObjPreparationPanel>m_objPrep;
 std::unique_ptr<SimpleUI>m_clipboardUi;int m_clipboardWidth=-1,m_clipboardDummy=-1;std::string m_pasteRole;bool m_copyPending=false,m_clearPatternPending=false;
 zSpace::zObjectMesh m_patternMesh,m_stereotomyMesh;bool m_jointNeedsReview=false,m_showPattern=true,m_hasPattern=false,m_showStereotomy=false,m_hasStereotomy=false;std::unique_ptr<SimpleUI>m_ui;std::vector<BlockDisplay>m_blocks;Json m_config;fs::path m_package,m_run,m_lastUsd;std::future<BatchResult>m_worker;std::mutex m_progressMutex;std::string m_workerProgress;std::atomic<bool>m_cancel{false};bool m_computePending=false,m_fitPending=false,m_sourcePending=false,m_patternPending=false,m_exportPending=false,m_running=false,m_showPrintMesh=true,m_showMeshes=true,m_showPaths=true,m_showTargets=false,m_showFeatures=true;double m_minimumBaseChord=-1;float m_layerHeight=20,m_spacing=20,m_amplitude=10,m_progressClock=0;int m_computeAction=-1,m_fitAction=-1,m_sourceAction=-1,m_patternAction=-1,m_exportAction=-1,m_captureFrame=-1;size_t m_paths=0,m_targets=0,m_printMeshes=0,m_failedBlocks=0;Vec3 m_low,m_high;std::string m_status,m_summary;std::vector<std::string> m_reviewLines;
};
ALICE2_REGISTER_SKETCH_AUTO(zBatchPatternSketch)
#endif
