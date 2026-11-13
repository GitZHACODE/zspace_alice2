#include <alice2/viewer.h>
#include <input/CameraController.h>
#include <sketches/SketchRegistry.h>
#include "Packing.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace alice2;
class RectanglePackingSketch : public ISketch {
    packing::Model model_;
    bool ready_=false;
    float inset_=0.2f;
    int fieldSource_=2;
    float influenceWidth_=12,fieldStrength_=1.0f,repackDelay_=0;
    bool showHeatmap_=true;
    std::vector<double> heatValues_;
    std::unique_ptr<SimpleUI> ui_;
    std::vector<std::array<packing::Point,3>> maskTriangles_;
    std::string status_;
    std::filesystem::path input_;
    nlohmann::json inputJson_;
    int mode_=0,selectedType_=0,lastType_=0,selectedAttractor_=-1,draggingAttractor_=-1;
    float attractorRadius_=20,attractorStrength_=0.8f;
    bool editAttractors_=false,deleteAttractor_=false;
    int viewWidth_=1,viewHeight_=1;
    float coverageTarget_=85;
    std::vector<float> shareTargets_;
    float panelScroll_=0,pendingScroll_=0;
    float uiBase() const {return 130+20.0f*static_cast<float>(model_.types.size());}
    float targetsBase() const {return uiBase()-panelScroll_;}
    float targetsBottom() const {return targetsBase()+115+48.0f*static_cast<float>(model_.types.size());}
    float controlsBase() const {return targetsBottom()+35;}
    float panelBottom() const {return controlsBase()+(typeMode()?475:355);}
    float maxPanelScroll() const {return std::max(0.0f,panelBottom()+panelScroll_-viewHeight_+16);}
    bool typeMode() const {return mode_==1;}
    void drawControlPanel() const {
        glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);glDepthMask(GL_FALSE);
        glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,viewWidth_,viewHeight_,0,-1,1);
        glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
        float top=uiBase()-12,bottom=std::min(static_cast<float>(viewHeight_-8),panelBottom());
        glColor4f(0.97f,0.97f,0.97f,1);glBegin(GL_QUADS);
        glVertex2f(8,top);glVertex2f(400,top);glVertex2f(400,bottom);glVertex2f(8,bottom);glEnd();
        if(maxPanelScroll()>0) {
            float available=bottom-top,content=panelBottom()+panelScroll_-top;
            float thumb=std::max(24.0f,available*available/content),offset=panelScroll_/maxPanelScroll()*(available-thumb);
            glColor4f(0.5f,0.5f,0.5f,1);glBegin(GL_QUADS);
            glVertex2f(390,top+offset);glVertex2f(394,top+offset);glVertex2f(394,top+offset+thumb);glVertex2f(390,top+offset+thumb);glEnd();
        }
        glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopAttrib();
    }
    void selectTypeAttractor() {
        selectedAttractor_=-1;
        for(size_t i=0;i<model_.attractors.size();++i) if(model_.attractors[i].type==selectedType_) {selectedAttractor_=static_cast<int>(i);break;}
        if(selectedAttractor_>=0) {
            const auto& a=model_.attractors[selectedAttractor_];
            attractorRadius_=static_cast<float>(a.radius);attractorStrength_=static_cast<float>(a.strength);
        }
    }
    void buildUI() {
        ui_=std::make_unique<SimpleUI>(input());ui_->setTheme(SimpleUI::UITheme::Dark);
        float y=controlsBase(),targetY=targetsBase();
        ui_->addButtonGroup({"Area mode","Type mode"},Vec2(20,y),145,30,10,mode_);
        ui_->addSlider("Display inset",Vec2(20,y+60),300,0,10,inset_);
        ui_->addToggle("Show active field",{20,y+85,180,28},showHeatmap_);
        ui_->addSlider("Coverage target (%)",Vec2(20,targetY+50),300,1,100,coverageTarget_);
        if(model_.types.size()>1) for(size_t i=0;i<model_.types.size();++i) {
            ui_->addSlider(model_.types[i].id+" area share (%)",Vec2(20,targetY+125+48.0f*static_cast<float>(i)),300,0,100,shareTargets_[i]);
        }
        double span=std::sqrt(model_.usableArea);
        if(!typeMode()) {
            ui_->addButtonGroup({"Outer","Inner","Both"},Vec2(20,y+155),92,28,8,fieldSource_);
            ui_->addSlider("Influence width",Vec2(20,y+210),300,0.1f,static_cast<float>(std::max(100.0,std::max(span,model_.influenceWidth*2))),influenceWidth_);
            ui_->addSlider("Strength",Vec2(20,y+250),300,0,1,fieldStrength_);
        }else {
            std::vector<std::string> labels;for(const auto& type:model_.types) labels.push_back(type.id);
            ui_->addButtonGroup(labels,Vec2(20,y+155),80,28,8,selectedType_);
            ui_->addToggle("Edit attractors",{20,y+195,240,28},editAttractors_);
            double radiusMax=std::max(100.0,span*2);for(const auto& a:model_.attractors) radiusMax=std::max(radiusMax,a.radius*2);
            ui_->addSlider("Radius (half-strength distance)",Vec2(20,y+265),300,0.1f,static_cast<float>(radiusMax),attractorRadius_);
            ui_->addSlider("Attraction strength",Vec2(20,y+310),300,0,1,attractorStrength_);
            ui_->addToggle("Delete point",{20,y+345,240,28},deleteAttractor_);
        }
    }
    Color typeColor(int type) const {
        const std::array<Color,8> colors{{Color(0,0.35f,1),Color(1,0.32f,0),Color(0.65f,0.05f,0.95f),Color(0,0.7f,0.25f),Color(0.95f,0.05f,0.15f),Color(0,0.65f,0.85f),Color(1,0.05f,0.6f),Color(0.85f,0.6f,0)}};
        return colors[static_cast<size_t>(type)%colors.size()];
    }
    void changedAttractors() {refreshHeatmap();repackDelay_=0.25f;}
    void deleteSelected() {
        if(typeMode() && selectedAttractor_>=0) {model_.attractors.erase(model_.attractors.begin()+selectedAttractor_);draggingAttractor_=-1;selectTypeAttractor();changedAttractors();}
    }
    bool pickPlane(int x,int y,packing::Point& local) {
        // Invert the projected XY plane directly, including perspective and DPI scaling.
        auto vp=camera().getViewProjectionMatrix();const auto& m=vp.m;
        double nx=2.0*x/viewWidth_-1,ny=1-2.0*y/viewHeight_;
        double a=m[0]-nx*m[3],b=m[4]-nx*m[7],c=m[1]-ny*m[3],d=m[5]-ny*m[7];
        double u=nx*m[15]-m[12],v=ny*m[15]-m[13],det=a*d-b*c;
        if(std::abs(det)<1e-10) return false;
        packing::Point p{(u*d-b*v)/det,(a*v-u*c)/det};
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||m[3]*p.x+m[7]*p.y+m[15]<=0) return false;
        local=model_.local(p);return true;
    }
    bool usable(packing::Point p) const {return packing::contains(model_.polygon,p)&&(model_.mask.empty()||!packing::contains(model_.mask,p));}
    Vec3 point(packing::Point p,float z=0) const {p=model_.world(p);return Vec3(static_cast<float>(p.x),static_cast<float>(p.y),z);}
    void outline(Renderer& r,const packing::Polygon& p,Color c,float z=0,float width=2) {
        for(size_t i=0;i<p.size();++i) r.drawLine(point(p[i],z),point(p[(i+1)%p.size()],z),c,width);
    }
    void box(Renderer& r,const packing::Rect& b,Color c,float z=0) {
        outline(r,{{b.x,b.y},{b.x+b.w,b.y},{b.x+b.w,b.y+b.h},{b.x,b.y+b.h}},c,z);
    }
    void refreshHeatmap() {
        heatValues_.clear();heatValues_.reserve(model_.anchors.size());
        for(auto p:model_.anchors) heatValues_.push_back(typeMode()?model_.attraction(selectedType_,p):model_.influence(p));
    }
    void buildMaskFill() {
        maskTriangles_.clear();
        auto p=model_.mask;
        double signedArea=0;
        for(size_t i=0;i<p.size();++i) signedArea+=p[i].x*p[(i+1)%p.size()].y-p[i].y*p[(i+1)%p.size()].x;
        if(signedArea<0) std::reverse(p.begin(),p.end());
        // Ear clipping supports concave masks, unlike a triangle fan.
        while(p.size()>2) {
            bool clipped=false;
            for(size_t i=0;i<p.size();++i) {
                const size_t a=(i+p.size()-1)%p.size(),c=(i+1)%p.size();
                if(packing::cross(p[a],p[i],p[c])<=packing::eps) continue;
                bool occupied=false;
                for(size_t k=0;k<p.size();++k) if(k!=a&&k!=i&&k!=c &&
                    packing::cross(p[a],p[i],p[k])>=-packing::eps &&
                    packing::cross(p[i],p[c],p[k])>=-packing::eps &&
                    packing::cross(p[c],p[a],p[k])>=-packing::eps) {occupied=true;break;}
                if(occupied) continue;
                maskTriangles_.push_back({p[a],p[i],p[c]});p.erase(p.begin()+i);clipped=true;break;
            }
            if(!clipped) throw std::runtime_error("Cannot triangulate mask fill");
        }
    }
    void reload(bool frameCamera=false) {
        ready_=false;
        try {
            // alice2's runtime sets the working directory to the executable's folder.
            input_=std::filesystem::current_path()/"data"/"packing.json";
            std::ifstream f(input_);if(!f) throw std::runtime_error("Cannot open "+input_.string());
            nlohmann::json j;f>>j;inputJson_=j;
            packing::Model next;next.load(j);next.generate();next.solve();model_=std::move(next);
            buildMaskFill();ready_=true;
            panelScroll_=0;pendingScroll_=0;
            mode_=model_.mode==packing::Mode::Type?1:0;
            selectedType_=0;lastType_=0;selectedAttractor_=-1;draggingAttractor_=-1;
            fieldSource_=model_.boundarySource;influenceWidth_=static_cast<float>(model_.influenceWidth);fieldStrength_=static_cast<float>(model_.fieldStrength);repackDelay_=0;
            coverageTarget_=static_cast<float>(model_.target*100);shareTargets_.clear();
            for(const auto& type:model_.types) shareTargets_.push_back(static_cast<float>(type.share*100));
            selectTypeAttractor();refreshHeatmap();buildUI();
            std::cout<<"Loaded "<<input_.string()<<std::endl;
            status_.clear();
            if(frameCamera) {
            double lx=1e30,ly=1e30,hx=-1e30,hy=-1e30;
            for(auto p:model_.polygon) {p=model_.world(p);lx=std::min(lx,p.x);ly=std::min(ly,p.y);hx=std::max(hx,p.x);hy=std::max(hy,p.y);}
            auto& controls=Application::getInstance()->getCameraController();
            controls.setOrbitCenter(Vec3(static_cast<float>((lx+hx)/2),static_cast<float>((ly+hy)/2),0));
            controls.setOrbitDistance(static_cast<float>(std::hypot(hx-lx,hy-ly)*1.5));
            camera().setFarPlane(static_cast<float>(std::max(1000.0,std::hypot(hx-lx,hy-ly)*20)));
            }
        } catch(const std::exception& e) {status_=e.what();}
        std::cout<<status_<<std::endl;
    }
public:
    std::string getName() const override {return "Rectangle Packing";}
    std::string getDescription() const override {return "Point-anchored rectangle packing inside a polygon with an exclusion mask";}
    void setup() override {
        scene().setBackgroundColor(Color(1,1,1));scene().setShowGrid(false);scene().setShowAxes(false);reload(true);
        input().setMouseWheelCallback([this](float delta) {
            const auto mouse=input().getMousePosition();
            if(ready_ && mouse.x<400 && mouse.y>=uiBase()-12) {pendingScroll_-=delta*40;input().setMouseWheel(0);}
        });
    }
    void cleanup() override {input().setMouseWheelCallback({});}
    void update(float dt) override {
        if(!ready_) return;
        float nextScroll=std::clamp(panelScroll_+pendingScroll_,0.0f,maxPanelScroll());pendingScroll_=0;
        if(nextScroll!=panelScroll_) {panelScroll_=nextScroll;buildUI();}
        if(std::abs(coverageTarget_-model_.target*100)>1e-4) {model_.target=coverageTarget_/100.0;repackDelay_=0.25f;}
        for(size_t i=0;i<shareTargets_.size();++i) if(std::abs(shareTargets_[i]-model_.types[i].share*100)>1e-4) {
            model_.setTargetShare(i,shareTargets_[i]/100.0);
            for(size_t k=0;k<shareTargets_.size();++k) shareTargets_[k]=static_cast<float>(model_.types[k].share*100);
            repackDelay_=0.25f;break;
        }
        const auto requestedMode=typeMode()?packing::Mode::Type:packing::Mode::Area;
        if(requestedMode!=model_.mode) {
            model_.mode=requestedMode;draggingAttractor_=-1;editAttractors_=false;deleteAttractor_=false;
            selectTypeAttractor();buildUI();refreshHeatmap();repackDelay_=0.25f;
        }
        // Selection changes affect the preview, not the layout.
        if(typeMode() && selectedType_!=lastType_) {lastType_=selectedType_;selectTypeAttractor();refreshHeatmap();}
        if(deleteAttractor_) {deleteAttractor_=false;deleteSelected();}
        if(typeMode() && selectedAttractor_>=0) {
            auto& a=model_.attractors[selectedAttractor_];
            if(std::abs(a.radius-attractorRadius_)>1e-5 || std::abs(a.strength-attractorStrength_)>1e-5) {
                a.radius=attractorRadius_;a.strength=attractorStrength_;changedAttractors();
            }
        }
        if(!typeMode() && (fieldSource_!=model_.boundarySource || std::abs(influenceWidth_-model_.influenceWidth)>1e-5 || std::abs(fieldStrength_-model_.fieldStrength)>1e-5)) {
            model_.boundarySource=fieldSource_;model_.influenceWidth=influenceWidth_;model_.fieldStrength=fieldStrength_;
            refreshHeatmap();repackDelay_=0.25f;
        }else if(repackDelay_>0 && draggingAttractor_<0) {
            repackDelay_-=dt;
            if(repackDelay_<=0) model_.solve();
        }
    }
    void draw(Renderer& r,Camera&) override {
        int vx,vy,vw,vh;r.getViewport(vx,vy,vw,vh);
        viewWidth_=std::max(1,static_cast<int>(vw/r.getContentScale()));viewHeight_=std::max(1,static_cast<int>(vh/r.getContentScale()));
        if(ready_) {
            for(const auto& t:maskTriangles_) r.drawTriangle(point(t[0]),point(t[1]),point(t[2]),Color(0.65f,0.65f,0.65f));
            const Color boundary(0.15f,0.15f,0.15f);
            outline(r,model_.polygon,boundary,0.02f,4);
            outline(r,model_.mask,boundary,0.02f,4);
            if(showHeatmap_) for(size_t i=0;i<model_.anchors.size();++i) {
                float v=static_cast<float>(heatValues_[i]);
                const Color color(0.1f+0.9f*v,0.3f+0.35f*(1-v),1.0f-0.95f*v);
                r.drawPoint(point(model_.anchors[i],0.01f),color,5.0f);
            }
            for(auto b:model_.best) {
                // Inset each edge in model units; collapsed footprints are hidden.
                b.x+=inset_;b.y+=inset_;b.w-=2*inset_;b.h-=2*inset_;
                if(b.w<=packing::eps || b.h<=packing::eps) continue;
                box(r,b,typeColor(b.type),0.05f);
            }
            if(typeMode()) for(size_t i=0;i<model_.attractors.size();++i) {
                const auto& a=model_.attractors[i];Color color=typeColor(a.type);
                packing::Polygon ring;
                for(int k=0;k<64;++k) {double t=k*2*3.141592653589793/64;ring.push_back({a.point.x+a.radius*std::cos(t),a.point.y+a.radius*std::sin(t)});}
                outline(r,ring,color,0.08f,static_cast<int>(i)==selectedAttractor_?3.0f:1.0f);
                r.drawPoint(point(a.point,0.1f),color,static_cast<int>(i)==selectedAttractor_?15.0f:10.0f);
                r.setColor(color);r.drawText(model_.types[a.type].id,point(a.point,0.12f),1);
            }
        }
        if(ready_) drawControlPanel();
        r.setColor(Color(0.1f,0.1f,0.1f));r.drawString(getName()+" | R reload | S export | W save config",10,30);
        if(!status_.empty()) r.drawString(status_,420,30);
        if(ready_) {
            std::ostringstream s;s<<std::fixed<<std::setprecision(1)<<"Coverage "<<model_.coverage*100<<"% (target "<<model_.target*100<<"%) | "<<(model_.error<=model_.tolerance?"Mix OK":"Mix outside tolerance");
            r.drawString(s.str(),10,55);
            r.drawString("Type / size | count | area (target)",10,80);
            int y=100;
            double total=model_.coverage*model_.usableArea;
            for(size_t i=0;i<model_.types.size();++i) {int n=0;double a=0;for(auto b:model_.best) if(b.type==static_cast<int>(i)) {++n;a+=b.area();}
                std::ostringstream row;row<<model_.types[i].id<<" "<<model_.types[i].w<<"x"<<model_.types[i].h<<" | "<<n<<" | "<<std::fixed<<std::setprecision(1)<<(total?a/total*100:0)<<"% ("<<model_.types[i].share*100<<"%)";
                r.setColor(typeColor(static_cast<int>(i)));r.drawString(row.str(),10,y);y+=20;
            }
            r.setColor(Color(0.1f,0.1f,0.1f));
            glPushAttrib(GL_SCISSOR_BIT);glEnable(GL_SCISSOR_TEST);
            float scale=r.getContentScale();int clipTop=static_cast<int>((uiBase()-12)*scale),margin=static_cast<int>(8*scale);
            glScissor(vx+margin,vy+margin,static_cast<int>(392*scale),std::max(0,vh-clipTop-margin));
            int yBase=static_cast<int>(controlsBase()),targetY=static_cast<int>(targetsBase());
            if(repackDelay_>0) r.drawString("Repacking...",20,static_cast<int>(panelBottom()-10));
            r.drawString(typeMode()?"Type":"Boundary",20,yBase+140);
            r.drawString("TARGETS",20,targetY+12);
            r.drawString("Area mix totals 100%",20,targetY+88);
            r.drawString("Other shares rebalance automatically.",20,static_cast<int>(targetsBottom()-12));
            if(model_.types.size()==1) r.drawString(model_.types[0].id+": 100%",20,targetY+125);
            r.drawString("CONTROLS",20,yBase-12);
            if(typeMode()) {
                r.drawString("Groups gather near their own attractors.",20,yBase+390);
                r.drawString(editAttractors_?"Click to add | Drag to move":"Enable Edit attractors to place points.",20,yBase+410);
                if(showHeatmap_) r.drawString("Field: red = stronger | blue = weaker",20,yBase+430);
                r.drawString("Coverage retention: "+std::to_string(static_cast<int>(model_.typeCoverageRetention*100))+"%",20,yBase+450);
            }else {
                r.drawString("Smaller rectangles near chosen boundaries.",20,yBase+285);
                if(showHeatmap_) r.drawString("Field: red = small | blue = large",20,yBase+305);
                if(fieldSource_==1 && model_.mask.empty()) r.drawString("No inner mask",20,yBase+325);
            }
            if(ui_) ui_->draw(r);
            glPopAttrib();
        }
    }
    bool onMousePress(int button,int state,int x,int y) override {
        if(ui_ && (state!=0 || (x<400 && y>=uiBase()-12)) && ui_->onMousePress(button,state,x,y)) return true;
        if(button!=0 || !ready_) return false;
        if(state!=0 && draggingAttractor_>=0) {draggingAttractor_=-1;return true;}
        if(!typeMode() || !editAttractors_ || state!=0) return false;
        input().setMouseButton(MouseButton::Left,KeyState::Released);
        const int controlsBottom=static_cast<int>(panelBottom());
        if(x<400 && y<controlsBottom) return true;
        selectedAttractor_=-1;double closest=14;
        for(size_t i=0;i<model_.attractors.size();++i) {
            auto pos=point(model_.attractors[i].point);
            Vec3 pixel=camera().worldToScreen(pos,viewWidth_,viewHeight_);
            double distance=std::hypot(pixel.x-x,pixel.y-y);
            if(pixel.z>=-1 && pixel.z<=1 && distance<closest) {closest=distance;selectedAttractor_=static_cast<int>(i);}
        }
        if(selectedAttractor_<0) {
            packing::Point p;
            if(!pickPlane(x,y,p)||!usable(p)) return true;
            model_.attractors.push_back({selectedType_,p,attractorRadius_,attractorStrength_});selectedAttractor_=static_cast<int>(model_.attractors.size())-1;changedAttractors();
        }
        const auto& a=model_.attractors[selectedAttractor_];selectedType_=a.type;lastType_=a.type;
        attractorRadius_=static_cast<float>(a.radius);attractorStrength_=static_cast<float>(a.strength);
        draggingAttractor_=selectedAttractor_;refreshHeatmap();return true;
    }
    bool onMouseMove(int x,int y) override {
        if(typeMode() && draggingAttractor_>=0) {
            input().setMouseButton(MouseButton::Left,KeyState::Released);
            packing::Point p;if(pickPlane(x,y,p)&&usable(p)) {model_.attractors[draggingAttractor_].point=p;changedAttractors();}
            return true;
        }
        return ui_ && ui_->onMouseMove(x,y);
    }
    bool onKeyPress(unsigned char key,int,int) override {
        if(key=='r'||key=='R') {reload();return true;}
        if((key=='d'||key=='D')&&ready_) {deleteSelected();return true;}
        if((key=='w'||key=='W')&&ready_) {
            try {
                auto config=inputJson_;config["attractors"]=model_.attractorJson();
                config["settings"]["mode"]=typeMode()?"type":"area";
                config["settings"]["targetCoverage"]=model_.target;
                for(size_t i=0;i<model_.types.size();++i) config["rectangles"][i]["targetShare"]=model_.types[i].share;
                config["settings"]["boundaryField"]={{"source",fieldSource_==0?"outer":fieldSource_==1?"inner":"both"},{"influenceWidth",influenceWidth_},{"strength",fieldStrength_}};
                std::ofstream out("packing_config.json");out.exceptions(std::ios::failbit|std::ios::badbit);out<<config.dump(2);status_="Saved packing_config.json";
            }catch(const std::exception& e) {status_=e.what();}return true;
        }
        if((key=='s'||key=='S')&&ready_) {
            if(repackDelay_>0) {model_.solve();repackDelay_=0;}
            try {std::ofstream out("packing_result.json");out.exceptions(std::ios::failbit|std::ios::badbit);out<<model_.result().dump(2);status_="Saved packing_result.json";}
            catch(const std::exception& e) {status_=e.what();} return true;
        }return false;
    }
};
ALICE2_REGISTER_SKETCH_AUTO(RectanglePackingSketch)
