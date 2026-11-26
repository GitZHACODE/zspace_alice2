#include <alice2.h>
#include <differential/DiscreteExteriorCalculus.h>
#include <differential/LaboratoryMeshes.h>
#include <input/CameraController.h>
#include <sketches/SketchRegistry.h>
#include <ui/SimpleUI.h>
#include <utils/OpenGL.h>

#include <igl/readOBJ.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>

namespace {
using namespace alice2;
using namespace alice2::differential;
using namespace alice2::differential::laboratory;
using Cochain = DiscreteExteriorCalculus::Vector;

constexpr int panelWidth = 340;
const Color backdrop(.055f,.071f,.095f);
const Color panel(.091f,.115f,.151f);
const Color muted(.61f,.68f,.76f);
const Color white(.89f,.93f,.97f);
const Color teal(.32f,.91f,.81f);
const Color amber(.98f,.66f,.34f);
const Color lilac(.66f,.62f,.99f);
const std::array<std::string,10> geometries{
    "Plane","Disk","Annulus","Cylinder","Sphere","Saddle","Torus","Genus two","Cut annulus","Import OBJ"};
const std::array<std::string,7> fields{"Demo","Constant","Radial","Rotational","Angular","Random","Import CSV"};
const std::array<std::string,6> displays{"Scalar","Vectors","Edge form","Contours","Curl","Divergence"};
enum Field { Demo, Constant, Radial, Rotational, Angular, Random, ImportedField };
enum Display { Scalar, Vectors, EdgeForm, Contours, Curl, Divergence };

Vec3 direction(const Eigen::RowVector3d& row) { return Vec3(row.x(),row.y(),row.z()); }

std::string number(double x, const char* format = "%.2e") {
    char buffer[32];
    std::snprintf(buffer,sizeof(buffer),format,x);
    return buffer;
}

std::filesystem::path inputFile(const char* name) {
    const auto local = std::filesystem::path("data")/name;
    return std::filesystem::exists(local) ? local : std::filesystem::path(name);
}

MeshData importMesh() {
    const auto path = inputFile("input.obj");
    std::vector<std::vector<double>> v, normals, uv;
    std::vector<std::vector<int>> faces, normalIds, uvIds;
    if (!igl::readOBJ(path.string(),v,uv,normals,faces,uvIds,normalIds))
        throw std::runtime_error("Place input.obj in the runtime data folder.");
    MeshData mesh;
    for (const auto& p : v) mesh.vertices.emplace_back(Vec3(p[0],p[1],p[2]));
    for (const auto& face : faces) mesh.faces.emplace_back(face);
    return mesh;
}

Eigen::MatrixXd importField(size_t count) {
    std::ifstream input(inputFile("field.csv"));
    if (!input) throw std::runtime_error("Place field.csv in the runtime data folder.");
    Eigen::MatrixXd field(count,3);
    size_t f = 0;
    for (std::string line; std::getline(input,line);) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line);
        std::string extra;
        double x,y,z;
        if (!(row>>x>>y>>z) || (row>>extra) || f>=count)
            throw std::runtime_error("field.csv needs one x,y,z row per face.");
        field.row(f++) = Eigen::RowVector3d(x,y,z);
    }
    if (f!=count) throw std::runtime_error("field.csv needs one x,y,z row per face.");
    return field;
}

Color heat(double t) {
    const Color low(.16f,.38f,.78f), middle(.81f,.87f,.90f), high(.87f,.27f,.23f);
    const auto mix = [](Color a, Color b, float s) {
        return Color(a.r+(b.r-a.r)*s,a.g+(b.g-a.g)*s,a.b+(b.b-a.b)*s);
    };
    return t < 0.5 ? mix(low,middle,2*t) : mix(middle,high,2*t-1);
}

class DifferentialSketch : public ISketch {
public:
    std::string getName() const override { return "Differential Laboratory"; }
    std::string getDescription() const override { return "Whitney-form discrete exterior calculus on triangle surfaces"; }

    void setup() override {
        scene().setBackgroundColor(backdrop);
        scene().setShowGrid(false);
        scene().setShowAxes(false);
        ui_ = std::make_unique<SimpleUI>(input());
        // SimpleUI's light foreground palette is meant for a dark background.
        ui_->setTheme(SimpleUI::UITheme::Dark);
        ui_->setTheme(SimpleUI::UITheme::Light);
        update(0);
    }

    void update(float) override {
        const int resolution = static_cast<int>(std::lround(resolution_));
        const auto geometry = static_cast<Geometry>(geometry_);
        const bool rebuild = geometry_!=lastGeometry_ || resolution!=lastResolution_ || (recompute_ && geometry==Geometry::Imported);
        const bool metric = geometry==Geometry::Saddle && bend_!=lastBend_;
        const bool solve = rebuild || metric || recompute_ || field_!=lastField_ || rotation_!=lastRotation_;
        const bool view = solve || display_!=lastDisplay_ || boundary_!=lastBoundary_ || interval_!=lastInterval_ ||
            exact_!=lastExact_ || coexact_!=lastCoexact_ || harmonic_!=lastHarmonic_;
        if (rebuild || metric) {
            valid_ = false;
            formsReady_ = false;
            try {
                auto next = geometry==Geometry::Imported ? importMesh() : makeMesh(geometry,resolution,bend_);
                if (rebuild) dec_.build(next);
                else dec_.updateMetric(next);
                mesh_ = std::move(next);
                mesh_.updateBounds(low_,high_);
                scale_ = (high_-low_).length();
                makeGeometry();
                valid_ = true;
                if (geometry_!=lastGeometry_ || (recompute_ && geometry==Geometry::Imported)) frame();
            } catch (const std::exception& e) {
                status_ = e.what();
            }
        }
        if (solve && valid_) {
            formsReady_ = false;
            const auto start = std::chrono::steady_clock::now();
            try {
                source_ = makeField(geometry);
                decomposition_ = dec_.decompose(source_);
                formsReady_ = true;
                status_.clear();
            } catch (const std::exception& e) {
                status_ = e.what();
            }
            solveMs_ = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
        if (view && formsReady_) {
            try { makeView(); }
            catch (const std::exception& e) {
                formsReady_ = false;
                status_ = e.what();
            }
        }
        lastGeometry_=geometry_; lastResolution_=resolution; lastBend_=bend_; lastField_=field_;
        lastRotation_=rotation_; lastDisplay_=display_; lastBoundary_=boundary_; lastInterval_=interval_;
        lastExact_=exact_; lastCoexact_=coexact_; lastHarmonic_=harmonic_;
        recompute_=false;
    }

    void draw(Renderer& r, Camera& c) override {
        int x,y,w,h;
        r.getViewport(x,y,w,h);
        const float scale = r.getContentScale(), height = h/scale;
        panelX_ = std::max(0.0f,w/scale-panelWidth);
        const int space = static_cast<int>(panelX_*scale);
        if (space>32 && h>0 && valid_) {
            const float oldAspect = c.getAspectRatio();
            c.setAspectRatio(static_cast<float>(space)/h);
            r.setViewport(x,y,space,h);
            r.setCamera(c);
            drawScene(r);
            c.setAspectRatio(oldAspect);
            r.setViewport(x,y,w,h);
            r.setCamera(c);
        }
        drawPanel(r,w/scale,height);
    }

    bool onMousePress(int button,int state,int x,int y) override {
        const int px = x-static_cast<int>(panelX_);
        const bool consumed = ui_ && ui_->onMousePress(button,state,x,y);
        if (button==0 && state==0 && px>=0) {
            for (const auto& choice : choices_)
                if (choice.rect.contains(px,y)) *choice.binding = choice.value;
            input().setMouseButton(MouseButton::Left,KeyState::Released);
        }
        return consumed || px>=0;
    }

    bool onMouseMove(int x,int y) override {
        return ui_ && ui_->onMouseMove(x,y);
    }

    bool onKeyPress(unsigned char key,int,int) override {
        if (key=='r' || key=='R') { recompute_=true; return true; }
        if (key=='f' || key=='F') { if (valid_) frame(); return true; }
        if (key=='t' || key=='T') { traces_=!traces_; return true; }
        return false;
    }

private:
    struct Choice { UIRect rect; int* binding; int value; };
    struct Triangle {
        std::array<int,3> v, adjacent{-1,-1,-1};
        Vec3 center, normal;
    };

    void frame() {
        auto& controls = Application::getInstance()->getCameraController();
        const bool flat = geometry_==static_cast<int>(Geometry::Plane) || geometry_==static_cast<int>(Geometry::Disk) ||
            geometry_==static_cast<int>(Geometry::Annulus) || geometry_==static_cast<int>(Geometry::CutAnnulus);
        camera().setOrbitRotation(Quaternion::fromAxisAngle(Vec3(1,0,0),flat ? -1.56f : -.86f));
        controls.setOrbitCenter((low_+high_)*.5f);
        const bool wide = geometry_==static_cast<int>(Geometry::GenusTwo);
        controls.setOrbitDistance(scale_*(wide ? 1.62f : flat ? 1.45f : 1.24f));
        camera().setNearPlane(scale_*.004f);
        camera().setFarPlane(scale_*50);
    }

    void makeGeometry() {
        wireLines_.clear();
        boundaryLines_.clear();
        triangles_.resize(mesh_.faces.size());
        std::map<std::pair<int,int>,std::pair<int,int>> incidence;
        for (int f = 0; f < triangles_.size(); ++f) {
            Triangle t;
            for (int k=0;k<3;++k) t.v[k]=mesh_.faces[f].vertices[k];
            const Vec3 a=pos(t.v[0]), b=pos(t.v[1]), c=pos(t.v[2]);
            t.center=(a+b+c)/3;
            t.normal=(b-a).cross(c-a).normalized();
            for (int k=0;k<3;++k) {
                const auto key=std::minmax(t.v[(k+1)%3],t.v[(k+2)%3]);
                const auto [it, inserted]=incidence.try_emplace({key.first,key.second},std::pair{f,k});
                if (inserted) continue;
                const auto [g,j]=it->second;
                triangles_[g].adjacent[j]=f;
                t.adjacent[k]=g;
                incidence.erase(it);
            }
            triangles_[f]=t;
        }
        for (const auto& [a,b] : dec_.edges()) {
            wireLines_.push_back(pos(a));
            wireLines_.push_back(pos(b));
        }
        for (const auto& [edge,face] : incidence) {
            const Vec3 offset=triangles_[face.first].normal*(scale_*.001f);
            boundaryLines_.push_back(pos(edge.first)+offset);
            boundaryLines_.push_back(pos(edge.second)+offset);
        }
    }

    Vec3 pos(int i) const { return mesh_.vertices[i].position; }

    Cochain makeField(Geometry geometry) {
        if (field_==ImportedField) return dec_.projectFaceField(importField(mesh_.faces.size()));
        const Vec3 center=(low_+high_)*.5f;
        if (field_==Angular || (field_==Demo && (geometry==Geometry::Torus || geometry==Geometry::Annulus ||
                geometry==Geometry::CutAnnulus || geometry==Geometry::Cylinder))) {
            Cochain angle(mesh_.vertices.size()), minor(angle.size()), potential(angle.size());
            for (int v=0;v<angle.size();++v) {
                const Vec3 p=pos(v)-center;
                angle[v]=std::atan2(p.y,p.x);
                minor[v]=std::atan2(p.z,std::hypot(p.x,p.y)-1);
                potential[v]=.22*p.x*p.y;
            }
            // The cut duplicates coincident seam positions. Both copies of the
            // seam use the same angle and retain the same wrapped increments.
            auto wrap=[](double value){return std::remainder(value,tau);};
            Cochain w=dec_.derivative0(angle).unaryExpr(wrap);
            if (field_==Demo && geometry==Geometry::Torus) {
                w += .26*dec_.derivative0(minor).unaryExpr(wrap) + dec_.derivative0(potential);
                Eigen::MatrixXd rotation(triangles_.size(),3);
                for (int f=0;f<rotation.rows();++f) {
                    const Vec3 p=triangles_[f].center-center;
                    rotation.row(f)=Eigen::RowVector3d(.27*p.z,0,-.27*p.x);
                }
                w += dec_.projectFaceField(rotation);
            }
            return w;
        }
        Eigen::MatrixXd target(triangles_.size(),3);
        std::mt19937 seed(1901);
        std::uniform_real_distribution<double> noise(-1,1);
        for (int f=0;f<target.rows();++f) {
            const Vec3 p=triangles_[f].center-center;
            switch (field_) {
            case Constant: target.row(f)=Eigen::RowVector3d(1,0,0); break;
            case Radial: target.row(f)=Eigen::RowVector3d(p.x,p.y,p.z); break;
            case Rotational: target.row(f)=Eigen::RowVector3d(1-rotation_*p.y,rotation_*p.x,0); break;
            case Random: target.row(f)=Eigen::RowVector3d(noise(seed),noise(seed),noise(seed)); break;
            default:
                target.row(f)=geometry==Geometry::Disk ? Eigen::RowVector3d(p.x-.75*p.y,p.y+.75*p.x,0) :
                    Eigen::RowVector3d(1-.6*p.y,.6*p.x,0);
                break;
            }
        }
        return dec_.projectFaceField(target);
    }

    void makeView() {
        const Cochain combined=exact_*decomposition_.exact+coexact_*decomposition_.coexact+harmonic_*decomposition_.harmonic;
        Cochain potential=exact_*decomposition_.potential;
        if (boundary_==1) {
            std::vector<int> fixed;
            std::vector<double> values;
            const double tolerance=(high_.x-low_.x)*1e-6;
            if (tolerance>0) {
                for (int v=0;v<potential.size();++v) {
                    const double x=pos(v).x;
                    if (x-low_.x<=tolerance || high_.x-x<=tolerance) {
                        fixed.push_back(v);
                        values.push_back(x-low_.x<=tolerance ? 0.0 : 1.0);
                    }
                }
            }
            potential=dec_.harmonicField(fixed,Eigen::Map<Cochain>(values.data(),values.size()));
        }
        const Cochain curl=dec_.H2()*dec_.derivative1(combined);
        const Cochain divergence=-dec_.codifferential1(combined);
        const double area=dec_.H0().diagonal().sum();
        curlRms_=std::sqrt(curl.dot(curl.cwiseQuotient(dec_.H2().diagonal()))/area);
        divergenceRms_=std::sqrt(divergence.dot(dec_.H0()*divergence)/area);
        energy_=std::pow(dec_.edgeNorm(combined),2);
        const double norm=dec_.edgeNorm(combined);
        residual_=norm>0 ? dec_.edgeNorm(combined-exact_*decomposition_.exact)/norm : 0;
        componentNorms_={dec_.edgeNorm(exact_*decomposition_.exact),dec_.edgeNorm(coexact_*decomposition_.coexact),
                         dec_.edgeNorm(harmonic_*decomposition_.harmonic)};

        drawPositions_.clear(); drawColors_.clear(); marks_.clear(); traceLines_.clear();
        const bool colored=display_==Scalar || display_==Curl || display_==Divergence;
        const auto& vertexScalar=display_==Divergence ? divergence : potential;
        double min=vertexScalar.minCoeff(), max=vertexScalar.maxCoeff();
        if (display_==Divergence || display_==Curl) {
            max=display_==Curl ? curl.cwiseAbs().maxCoeff() : std::max(std::abs(min),std::abs(max));
            min=-max;
        }
        if (display_==Curl && max<1e-10) { min=-1; max=1; }
        for (int f=0;f<triangles_.size();++f) {
            const auto& t=triangles_[f];
            const float light=.55f+.45f*std::abs(t.normal.dot(Vec3(.32f,-.46f,.827f)));
            for (int v : t.v) {
                drawPositions_.push_back(pos(v));
                const double q=display_==Curl ? curl[f] : vertexScalar[v];
                drawColors_.push_back(colored ? heat(max-min>1e-12 ? (q-min)/(max-min) : .5) :
                                                 Color(.15f*light,.24f*light,.32f*light));
            }
        }
        range_={min,max};
        if (display_==Contours) makeContours(potential);
        if (display_==Vectors) makeVectors(dec_.reconstructFaceField(combined));
        if (display_==EdgeForm) makeEdges(combined);
    }

    void makeContours(const Cochain& scalar) {
        const double low=scalar.minCoeff(), high=scalar.maxCoeff();
        const double step=std::max<double>(interval_,(high-low)/100);
        shownInterval_=step;
        for (const auto& t : triangles_) {
            const auto& [a,b,c]=t.v;
            const double from=std::min({scalar[a],scalar[b],scalar[c]});
            const double to=std::max({scalar[a],scalar[b],scalar[c]});
            if (to-from<=1e-13) continue;
            const int first=std::max(1,static_cast<int>(std::ceil((from-low)/step)));
            const int last=std::min(100,static_cast<int>(std::floor((to-low)/step)));
            for (int k=first;k<=last;++k) {
                const double level=low+k*step;
                if (level>=high) break;
                std::array<Vec3,3> hits;
                int count=0;
                for (int e=0;e<3;++e) {
                    const int p=t.v[e], q=t.v[(e+1)%3];
                    const double x=scalar[p]-level, y=scalar[q]-level;
                    if ((x<0)==(y<0)) continue;
                    const Vec3 hit=pos(p)+(pos(q)-pos(p))*(x/(x-y));
                    if (count && (hit-hits[0]).lengthSquared()<=scale_*scale_*1e-14f) continue;
                    hits[count++]=hit;
                }
                if (count==2) {
                    const Vec3 offset=t.normal*(scale_*.0015f);
                    marks_.push_back(hits[0]+offset);
                    marks_.push_back(hits[1]+offset);
                }
            }
        }
    }

    void arrow(Vec3 start, Vec3 delta, Vec3 normal) {
        const Vec3 end=start+delta;
        const Vec3 base=end-delta*.30f, side=normal.cross(delta)*.16f;
        marks_.insert(marks_.end(),{start,end,base+side,end,base-side,end});
    }

    void makeEdges(const Cochain& form) {
        const double greatest=form.cwiseAbs().maxCoeff();
        if (greatest<1e-12) return;
        // Use one incident triangle for each edge's local tangent plane.
        std::map<std::pair<int,int>,Vec3> normal;
        for (const auto& t : triangles_) {
            for (int k=0;k<3;++k) {
                const auto key=std::minmax(t.v[k],t.v[(k+1)%3]);
                normal.try_emplace({key.first,key.second},t.normal);
            }
        }
        for (int e=0;e<form.size();++e) {
            const auto [a,b]=dec_.edges()[e];
            const auto n=normal.at({a,b});
            const double weight=form[e]/greatest;
            if (std::abs(weight)<.04) continue;
            const Vec3 delta=(pos(b)-pos(a))*(.80*weight);
            arrow((pos(a)+pos(b)-delta)*.5f+n*(scale_*.002f),delta,n);
        }
    }

    void makeVectors(const Eigen::MatrixXd& vectors) {
        const double largest=vectors.rowwise().norm().maxCoeff();
        if (largest<1e-10) return;
        const int stride=std::max(1,static_cast<int>(triangles_.size()/360));
        for (int f=0;f<vectors.rows();f+=stride) {
            const auto& t=triangles_[f];
            const float edgeSize=std::min({(pos(t.v[0])-pos(t.v[1])).length(),
                (pos(t.v[1])-pos(t.v[2])).length(),(pos(t.v[2])-pos(t.v[0])).length()});
            const Vec3 delta=direction(vectors.row(f))*(std::min<float>(edgeSize*1.4f,scale_*.055f)/largest);
            if (delta.length()<scale_*.0015f) continue;
            arrow(t.center-delta*.45f+t.normal*(scale_*.002f),delta,t.normal);
        }
        const int steps=std::min(220,static_cast<int>(triangles_.size()));
        if (geometry_==static_cast<int>(Geometry::Torus)) {
            const int resolution=static_cast<int>(std::lround(resolution_));
            for (int i=0;i<resolution;i+=std::max(1,resolution/10))
                trace(vectors,i*resolution*4,steps);
        } else {
            const int seedStride=std::max(1,static_cast<int>(triangles_.size()/12));
            for (int seed=seedStride/2;seed<vectors.rows();seed+=seedStride) trace(vectors,seed,steps);
        }
    }

    void trace(const Eigen::MatrixXd& vectors,int seed,int steps) {
        for (float sign : {-1.f,1.f}) {
            int f=seed, previous=-1;
            Vec3 p=triangles_[f].center;
            for (int step=0;step<steps && f>=0;++step) {
                const auto& t=triangles_[f];
                const Vec3 flow=direction(vectors.row(f))*sign;
                if (flow.lengthSquared()<1e-16f) break;
                float earliest=std::numeric_limits<float>::infinity();
                int side=-1;
                for (int k=0;k<3;++k) {
                    const Vec3 a=pos(t.v[(k+1)%3]), b=pos(t.v[(k+2)%3]);
                    const Vec3 inward=t.normal.cross(b-a);
                    const float velocity=inward.dot(flow);
                    if (velocity>=0) continue;
                    const float time=inward.dot(a-p)/velocity;
                    if (time<earliest) { earliest=time; side=k; }
                }
                if (side<0 || earliest<=0 || (previous>=0 && t.adjacent[side]==previous)) break;
                const Vec3 end=p+flow*earliest;
                const Vec3 offset=t.normal*(scale_*.0018f);
                traceLines_.insert(traceLines_.end(),{p+offset,end+offset});
                previous=f;
                f=t.adjacent[side];
                if (f==seed) break;
                if (f>=0) p=end+(triangles_[f].center-end)*.00015f;
            }
        }
    }

    void drawScene(Renderer& r) {
        if (formsReady_)
            r.drawMesh(drawPositions_.data(),nullptr,drawColors_.data(),static_cast<int>(drawPositions_.size()));
        if (wire_ || !formsReady_) lines(r,wireLines_,Color(.27f,.36f,.45f),.8f);
        lines(r,boundaryLines_,Color(.56f,.67f,.78f),1.25f);
        if (!formsReady_) return;
        if (traces_ && display_==Vectors) lines(r,traceLines_,teal,1.65f);
        lines(r,marks_,display_==EdgeForm ? lilac : teal,display_==Contours ? 2.f : 1.2f);
    }

    static void lines(Renderer& r,const std::vector<Vec3>& points,const Color& color,float width) {
        if (!points.empty()) r.drawLines(points.data(),static_cast<int>(points.size()),color,width*r.getContentScale());
    }

    static void rectangle(float x,float y,float w,float h,Color color) {
        glColor4f(color.r,color.g,color.b,color.a);
        glBegin(GL_QUADS);
        glVertex2f(x,y); glVertex2f(x+w,y); glVertex2f(x+w,y+h); glVertex2f(x,y+h);
        glEnd();
    }

    void text(Renderer& r,const std::string& label,float x,float y,Color color=white) const {
        r.setColor(color);
        r.drawString(label,x,y);
    }

    template<size_t N>
    void choices(Renderer& r,const std::array<std::string,N>& labels,float y,int& binding,
                 int columns=3,float width=96,float pitch=19) {
        for (int i=0;i<N;++i) {
            const UIRect rect{18+(i%columns)*(width+8),y+(i/columns)*pitch,width,18};
            const bool active=binding==i;
            rectangle(panelX_+rect.x,rect.y,rect.w,rect.h,
                      active ? Color(.13f,.32f,.34f) : Color(.13f,.166f,.215f));
            text(r,labels[i],panelX_+rect.x+6,rect.y+14,active ? teal : white);
            choices_.push_back({rect,&binding,i});
        }
    }

    void makeControls() {
        ui_->clear();
        controlX_=panelX_;
        const float x=controlX_;
        ui_->addSlider("Subdivisions", {x+18,151},234,6,36,resolution_);
        ui_->addSlider("Exact", {x+18,373},234,0,2,exact_);
        ui_->addSlider("Coexact", {x+18,399},234,0,2,coexact_);
        ui_->addSlider("Harmonic", {x+18,425},234,0,2,harmonic_);
        ui_->addSlider("Saddle bend", {x+18,451},234,0,1.5f,bend_);
        ui_->addSlider("Contour interval", {x+18,477},234,.025f,1.0f,interval_);
        ui_->addSlider("Rotation", {x+18,503},234,0,3,rotation_);
        ui_->addToggle("Wire",{x+18,295,96,22},wire_);
        ui_->addToggle("Traces",{x+122,295,96,22},traces_);
        ui_->addToggle("Recompute",{x+226,295,96,22},recompute_);
    }

    void drawPanel(Renderer& r,float width,float height) {
        if (controlX_!=panelX_) makeControls();
        const float x=panelX_;
        const Color previous=r.getCurrentColor();
        glPushAttrib(GL_ENABLE_BIT|GL_DEPTH_BUFFER_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDepthMask(GL_FALSE);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0,width,height,0,-1,1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

        rectangle(x,0,panelWidth,height,panel);
        rectangle(x,0,1,height,Color(.20f,.26f,.34f));
        rectangle(x+18,13,3,17,teal);
        text(r,"Differential Laboratory",x+28,26);
        text(r,"GEOMETRY",x+18,48,muted);
        choices_.clear();
        choices(r,geometries,56,geometry_);
        text(r,"FIELD",x+18,173,muted);
        choices(r,fields,181,field_);
        text(r,"DISPLAY",x+18,249,muted);
        choices(r,displays,256,display_);
        const std::array<std::string,2> boundaryLabels{"Natural BC","X extrema 0 / 1"};
        choices(r,boundaryLabels,322,boundary_,2,148);
        text(r,"HODGE COMPONENTS",x+18,354,muted);
        if (height>=632 || x<350) drawDiagnostics(r,x+18,527);
        else drawDiagnostics(r,24,height-125);
        if (x>260) drawLegend(r,height);

        glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
        if (ui_) ui_->draw(r);
        glPopAttrib();
        r.setColor(previous);
    }

    void drawDiagnostics(Renderer& r,float x,float y) const {
        text(r,"DIAGNOSTICS",x,y,muted);
        if (formsReady_) {
            text(r,"Genus "+std::to_string(dec_.totalGenus())+"   Modes "+std::to_string(dec_.harmonicModeCount())+
                "   Loops "+std::to_string(dec_.boundaryLoopCount()),x,y+17);
            text(r,"Vertices "+std::to_string(mesh_.vertices.size())+"   Faces "+std::to_string(mesh_.faces.size()),x,y+34,muted);
            text(r,"Integration residual "+number(residual_*100,"%.2f")+"%",x,y+51);
            text(r,"RMS curl "+number(curlRms_,"%.1e")+"  div "+number(divergenceRms_,"%.1e"),x,y+68);
            text(r,"Energy "+number(energy_,"%.3g")+"  Solve "+number(solveMs_,"%.1f")+" ms",x,y+85);
            text(r,"Hodge norms "+number(componentNorms_[0],"%.2g")+" / "+
                number(componentNorms_[1],"%.2g")+" / "+number(componentNorms_[2],"%.2g"),x,y+102,muted);
        } else if (!status_.empty()) {
            std::istringstream words(status_);
            std::string word,line;
            float lineY=y+17;
            while (words>>word) {
                if (line.size()+word.size()>33) { text(r,line,x,lineY,amber); line.clear(); lineY+=17; }
                line+=(line.empty()?"":" ")+word;
            }
            text(r,line,x,lineY,amber);
        }
    }

    void drawLegend(Renderer& r,float height) {
        text(r,geometries[geometry_]+"  /  "+displays[display_],24,32);
        if (formsReady_ && (display_==Scalar || display_==Curl || display_==Divergence)) {
            for (int i=0;i<80;++i) rectangle(24+2*i,50,2,6,heat(i/79.0));
            text(r,number(range_[0],"%.3g"),24,72,muted);
            text(r,number(range_[1],"%.3g"),145,72,muted);
        } else if (formsReady_ && display_==Contours) {
            text(r,"Interval "+number(shownInterval_,"%.3f"),24,55,teal);
        }
        text(r,"Drag to orbit   /   F to frame",24,height-22,muted);
    }

    DiscreteExteriorCalculus dec_;
    MeshData mesh_;
    std::vector<Triangle> triangles_;
    std::vector<Vec3> wireLines_,boundaryLines_,drawPositions_,marks_,traceLines_;
    std::vector<Color> drawColors_;
    std::vector<Choice> choices_;
    std::unique_ptr<SimpleUI> ui_;
    Cochain source_;
    HodgeResult decomposition_;
    Vec3 low_,high_;
    float scale_=1, panelX_=0,controlX_=-1;
    int geometry_=static_cast<int>(Geometry::Torus), field_=Demo, display_=Vectors, boundary_=0;
    int lastGeometry_=-1,lastResolution_=-1,lastField_=-1,lastDisplay_=-1,lastBoundary_=-1;
    float resolution_=16,bend_=.8f,exact_=.60f,coexact_=.35f,harmonic_=1.f,interval_=.10f,rotation_=1.f;
    float lastBend_=-1,lastExact_=-1,lastCoexact_=-1,lastHarmonic_=-1,lastInterval_=-1,lastRotation_=-1;
    bool valid_=false,formsReady_=false,wire_=true,traces_=true,recompute_=false;
    double residual_=0,curlRms_=0,divergenceRms_=0,energy_=0,solveMs_=0,shownInterval_=0;
    std::array<double,2> range_{};
    std::array<double,3> componentNorms_{};
    std::string status_;
};
} // namespace

ALICE2_REGISTER_SKETCH_AUTO(DifferentialSketch)
