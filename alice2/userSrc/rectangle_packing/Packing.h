#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace packing {
constexpr double eps=1e-8;
struct Point { double x=0,y=0; };
using Polygon=std::vector<Point>;
struct Rect { double x=0,y=0,w=0,h=0; int type=0; bool rotated=false; double area() const {return w*h;} };
struct Type { std::string id; double w,h,share; int maxCount; };
struct Attractor { int type=0; Point point; double radius=20,strength=0.8; };
enum class Mode { Area, Type };
inline double gaussianInfluence(const Attractor& a,Point p) {
    double dx=(p.x-a.point.x)/a.radius,dy=(p.y-a.point.y)/a.radius;
    return a.strength*std::exp(-std::log(2.0)*(dx*dx+dy*dy));
}
inline double cross(Point a,Point b,Point c) {return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
inline bool onSegment(Point p,Point a,Point b) {
    return std::abs(cross(a,b,p))<eps && p.x>=std::min(a.x,b.x)-eps && p.x<=std::max(a.x,b.x)+eps && p.y>=std::min(a.y,b.y)-eps && p.y<=std::max(a.y,b.y)+eps;
}
inline bool contains(const Polygon& p,Point q) {
    bool in=false;
    for(size_t i=0,j=p.size()-1;i<p.size();j=i++) {
        if(onSegment(q,p[j],p[i])) return true;
        if((p[i].y>q.y)!=(p[j].y>q.y) && q.x<(p[j].x-p[i].x)*(q.y-p[i].y)/(p[j].y-p[i].y)+p[i].x) in=!in;
    } return in;
}
inline double area(const Polygon& p) {
    double a=0; for(size_t i=0;i<p.size();++i) a+=p[i].x*p[(i+1)%p.size()].y-p[i].y*p[(i+1)%p.size()].x;
    return std::abs(a)*0.5;
}
inline double boundaryDistance(const Polygon& p,Point q) {
    double distance=std::numeric_limits<double>::infinity();
    for(size_t i=0;i<p.size();++i) {
        Point a=p[i],b=p[(i+1)%p.size()];double dx=b.x-a.x,dy=b.y-a.y;
        double length2=dx*dx+dy*dy;
        double t=length2>eps*eps?std::clamp(((q.x-a.x)*dx+(q.y-a.y)*dy)/length2,0.0,1.0):0;
        distance=std::min(distance,std::hypot(q.x-a.x-t*dx,q.y-a.y-t*dy));
    }return distance;
}
// Clip a boundary segment to the open rectangle. Detects concave notches,
// mask edges and masks entirely enclosed by a candidate. Boundary contact is allowed.
inline bool enters(Point a,Point b,const Rect& r) {
    double lo=0,hi=1;
    const double v[2]={a.x,a.y}, d[2]={b.x-a.x,b.y-a.y};
    const double mn[2]={r.x+eps,r.y+eps}, mx[2]={r.x+r.w-eps,r.y+r.h-eps};
    for(int k=0;k<2;++k) {
        if(std::abs(d[k])<eps) {if(v[k]<=mn[k] || v[k]>=mx[k]) return false;}
        else {double s=(mn[k]-v[k])/d[k],t=(mx[k]-v[k])/d[k]; if(s>t) std::swap(s,t); lo=std::max(lo,s);hi=std::min(hi,t);}
    } return lo<hi;
}
inline bool boundaryEnters(const Polygon& p,const Rect& r) {
    for(size_t i=0;i<p.size();++i) if(enters(p[i],p[(i+1)%p.size()],r)) return true;
    return false;
}
inline bool valid(const Polygon& p,const Polygon& mask,const Rect& r) {
    for(Point q:std::array<Point,4>{{{r.x,r.y},{r.x+r.w,r.y},{r.x+r.w,r.y+r.h},{r.x,r.y+r.h}}}) if(!contains(p,q)) return false;
    return !boundaryEnters(p,r) && (mask.empty() || (!contains(mask,{r.x+r.w/2,r.y+r.h/2}) && !boundaryEnters(mask,r)));
}
inline bool overlaps(const Rect& a,const Rect& b) {
    return a.x<b.x+b.w-eps && b.x<a.x+a.w-eps && a.y<b.y+b.h-eps && b.y<a.y+a.h-eps;
}
inline bool intersects(Point a,Point b,Point c,Point d) {
    return (cross(a,b,c)*cross(a,b,d)<0 && cross(c,d,a)*cross(c,d,b)<0) || onSegment(a,c,d)||onSegment(b,c,d)||onSegment(c,a,b)||onSegment(d,a,b);
}
inline Polygon readPolygon(const nlohmann::json& j,bool optional=false) {
    Polygon p;
    for(const auto& q:j) {if(!q.is_array()||q.size()!=2) throw std::runtime_error("Points must be [x,y]"); Point v{q[0].get<double>(),q[1].get<double>()}; if(!std::isfinite(v.x)||!std::isfinite(v.y)) throw std::runtime_error("Non-finite point"); p.push_back(v);}
    if(p.size()>1 && std::abs(p.front().x-p.back().x)<eps && std::abs(p.front().y-p.back().y)<eps) p.pop_back();
    if(optional && p.empty()) return p;
    if(p.size()<3 || area(p)<eps) throw std::runtime_error("Polygon must have positive area");
    for(size_t i=0;i<p.size();++i) {
        size_t ni=(i+1)%p.size();
        if(std::hypot(p[i].x-p[ni].x,p[i].y-p[ni].y)<eps) throw std::runtime_error("Duplicate polygon vertex");
        for(size_t k=i+1;k<p.size();++k) if(k!=ni && (k+1)%p.size()!=i && intersects(p[i],p[ni],p[k],p[(k+1)%p.size()])) throw std::runtime_error("Polygon self-intersects");
    } return p;
}
struct Model {
    Mode mode=Mode::Area;
    Polygon polygon,mask; std::vector<Type> types; std::vector<Point> anchors;
    std::vector<Rect> candidates,best; Rect inner;
    double spacing=2,angle=0,usableArea=0,target=0.85,tolerance=0.05;
    double typeCoverageRetention=1;
    int attempts=30,seed=42; bool rotate=true; double coverage=0,error=0;
    // 0 outer, 1 inner mask, 2 both. Zero strength restores the original solver.
    int boundarySource=2;
    double influenceWidth=12,fieldStrength=0,fieldMismatch=0;
    std::vector<Attractor> attractors;
    double attractionScore=0,attractorDistancePenalty=0;
    // Unlike Gaussian influence, this cost keeps increasing far from an attractor.
    // Multiple active attractors provide alternative destinations for the same type.
    double distancePenalty(int type,Point p) const {
        double cost=std::numeric_limits<double>::infinity();
        for(const auto& a:attractors) if(a.type==type && a.strength>0) {
            double distance=std::min(1e6,std::hypot(p.x-a.point.x,p.y-a.point.y)/a.radius);
            cost=std::min(cost,a.strength*distance*distance);
        }
        return std::isfinite(cost)?cost:0;
    }
    double layoutDistancePenalty(const std::vector<Rect>& layout) const {
        double cost=0,total=0;
        for(const auto& r:layout) {cost+=r.area()*distancePenalty(r.type,{r.x+r.w/2,r.y+r.h/2});total+=r.area();}
        return total>0?cost/total:0;
    }
    double attraction(int type,Point p) const {
        double value=0;
        for(const auto& a:attractors) if(a.type==type) value=std::max(value,gaussianInfluence(a,p));
        return value;
    }
    double layoutAttraction(const std::vector<Rect>& layout) const {
        double value=0,total=0;
        for(const auto& r:layout) {value+=r.area()*attraction(r.type,{r.x+r.w/2,r.y+r.h/2});total+=r.area();}
        return total>0?value/total:0;
    }
    double influence(Point p) const {
        double d=std::numeric_limits<double>::infinity();
        if(boundarySource!=1) d=boundaryDistance(polygon,p);
        if(boundarySource!=0) d=std::min(d,boundaryDistance(mask,p));
        return std::exp(-d/influenceWidth);
    }
    double candidateMismatch(const Rect& r) const {
        double smallest=std::numeric_limits<double>::infinity(),largest=0;
        for(const auto& t:types) if(t.share>0 && t.maxCount>0) {smallest=std::min(smallest,t.w*t.h);largest=std::max(largest,t.w*t.h);}
        if(largest-smallest<eps) return 0;
        double size=(r.area()-smallest)/(largest-smallest);
        return std::abs(size-(1-influence({r.x+r.w/2,r.y+r.h/2})));
    }
    double layoutMismatch(const std::vector<Rect>& layout) const {
        double sum=0,total=0;
        for(const auto& r:layout) {sum+=r.area()*candidateMismatch(r);total+=r.area();}
        return total>0?sum/total:0;
    }
    Point world(Point p) const {return {p.x*std::cos(angle)-p.y*std::sin(angle),p.x*std::sin(angle)+p.y*std::cos(angle)};}
    Point local(Point p) const {return {p.x*std::cos(angle)+p.y*std::sin(angle),-p.x*std::sin(angle)+p.y*std::cos(angle)};}
    void setTargetShare(size_t selected,double share) {
        if(selected>=types.size() || !std::isfinite(share) || share<0 || share>1) throw std::runtime_error("Invalid target share");
        if(types.size()==1) {types[0].share=1;return;}
        double others=0;for(size_t i=0;i<types.size();++i) if(i!=selected) others+=types[i].share;
        for(size_t i=0;i<types.size();++i) if(i!=selected) types[i].share=others>eps?(1-share)*types[i].share/others:(1-share)/(types.size()-1);
        types[selected].share=share;
    }
    void load(const nlohmann::json& j) {
        *this=Model{};
        polygon=readPolygon(j.at("polygon")); mask=readPolygon(j.value("mask",nlohmann::json::array()),true);
        for(Point q:mask) if(!contains(polygon,q)) throw std::runtime_error("Mask must lie strictly inside polygon");
        for(size_t i=0;i<mask.size();++i) for(size_t k=0;k<polygon.size();++k) if(intersects(mask[i],mask[(i+1)%mask.size()],polygon[k],polygon[(k+1)%polygon.size()])) throw std::runtime_error("Mask must lie strictly inside polygon");
        auto s=j.value("settings",nlohmann::json::object());
        const std::string modeName=s.value("mode",std::string("area"));
        if(modeName!="area" && modeName!="type") throw std::runtime_error("Mode must be area or type");
        mode=modeName=="type"?Mode::Type:Mode::Area;
        spacing=s.value("gridSpacing",2.0);angle=s.value("gridAngleDegrees",0.0)*3.141592653589793/180;
        target=s.value("targetCoverage",0.85);tolerance=s.value("shareTolerance",0.05);
        typeCoverageRetention=s.value("typeCoverageRetention",1.0);
        if(!(typeCoverageRetention>0 && typeCoverageRetention<=1)) throw std::runtime_error("typeCoverageRetention must be greater than 0 and at most 1");
        attempts=s.value("attempts",30); seed=s.value("seed",42);rotate=s.value("allowRotation90",true);
        const auto field=s.value("boundaryField",nlohmann::json::object());
        std::string source=field.value("source",std::string("both"));
        if(source!="outer" && source!="inner" && source!="both") throw std::runtime_error("Boundary source must be outer, inner or both");
        boundarySource=source=="outer"?0:source=="inner"?1:2;
        influenceWidth=field.value("influenceWidth",12.0);fieldStrength=field.value("strength",0.0);
        if(!std::isfinite(influenceWidth)||influenceWidth<=0 || !(fieldStrength>=0&&fieldStrength<=1)) throw std::runtime_error("Invalid boundary field settings");
        if(!std::isfinite(spacing)||spacing<=0 || !std::isfinite(angle)|| !(target>0&&target<=1)|| !(tolerance>=0&&tolerance<=1)|| attempts<1||attempts>1000) throw std::runtime_error("Invalid settings");
        double sum=0;
        for(const auto& t:j.at("rectangles")) {
            Type v{t.at("id").get<std::string>(),t.at("w").get<double>(),t.at("h").get<double>(),t.at("targetShare").get<double>(),t.value("maxCount",1000000)};
            if(!std::isfinite(v.w)||!std::isfinite(v.h)||v.w<=eps||v.h<=eps||!(v.share>=0&&v.share<=1)||v.maxCount<0) throw std::runtime_error("Invalid rectangle type");
            for(const auto& old:types) if(old.id==v.id) throw std::runtime_error("Duplicate rectangle id");
            sum+=v.share;types.push_back(v);
        }
        if(types.empty()||std::abs(sum-1)>1e-6) throw std::runtime_error("targetShare values must sum to 1");
        for(const auto& v:j.value("attractors",nlohmann::json::array())) {
            auto it=std::find_if(types.begin(),types.end(),[&](const Type& t){return t.id==v.at("type").get<std::string>();});
            if(it==types.end()) throw std::runtime_error("Unknown attractor type");
            const auto& q=v.at("point");if(!q.is_array()||q.size()!=2) throw std::runtime_error("Attractor point must be [x,y]");
            Point p{q[0].get<double>(),q[1].get<double>()};
            Attractor a{static_cast<int>(it-types.begin()),local(p),v.value("radius",20.0),v.value("strength",0.8)};
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(a.radius)||a.radius<=0 || !(a.strength>=0&&a.strength<=1)) throw std::runtime_error("Invalid attractor settings");
            if(!contains(polygon,p)||(!mask.empty()&&contains(mask,p))) throw std::runtime_error("Attractor must lie in usable region");
            attractors.push_back(a);
        }
        usableArea=area(polygon)-area(mask);
        for(auto* p:{&polygon,&mask}) for(auto& q:*p) {double x=q.x*std::cos(angle)+q.y*std::sin(angle);q.y=-q.x*std::sin(angle)+q.y*std::cos(angle);q.x=x;}
    }
    void generate() {
        anchors.clear();candidates.clear();inner={};
        double lx=polygon[0].x,ly=polygon[0].y,hx=lx,hy=ly;
        for(auto p:polygon) {lx=std::min(lx,p.x);ly=std::min(ly,p.y);hx=std::max(hx,p.x);hy=std::max(hy,p.y);}
        double nx=std::ceil((hx-lx)/spacing),ny=std::ceil((hy-ly)/spacing);
        if(nx*ny>100000 || nx>100000 || ny>100000) throw std::runtime_error("Grid too dense; increase gridSpacing (100000 cell limit)");
        int cols=static_cast<int>(nx),rows=static_cast<int>(ny);
        // Largest rectangle in the conservative full-cell raster, using a histogram stack.
        std::vector<int> heights(cols,0);
        for(int y=0;y<rows;++y) {
            for(int x=0;x<cols;++x) heights[x]=valid(polygon,mask,{lx+x*spacing,ly+y*spacing,spacing,spacing})?heights[x]+1:0;
            std::vector<int> stack;
            for(int x=0;x<=cols;++x) {
                int h=x==cols?0:heights[x];
                while(!stack.empty() && heights[stack.back()]>h) {
                    int height=heights[stack.back()];stack.pop_back();int left=stack.empty()?0:stack.back()+1;
                    Rect r{lx+left*spacing,ly+(y+1-height)*spacing,(x-left)*spacing,height*spacing};
                    if(r.area()>inner.area()) inner=r;
                } if(x<cols) stack.push_back(x);
            }
        }
        if(inner.area()==0) throw std::runtime_error("No inner grid rectangle; decrease gridSpacing");
        // The inner rectangle lies on this lattice, so extending it preserves its corner as an anchor.
        for(int y=0;y<=rows;++y) for(int x=0;x<=cols;++x) {
            Point p{lx+x*spacing,ly+y*spacing};
            if(!contains(polygon,p)||(!mask.empty()&&contains(mask,p))) continue;
            anchors.push_back(p);
            for(size_t t=0;t<types.size();++t) for(int rot=0;rot<(rotate?2:1);++rot) {
                const auto& v=types[t]; if(rot && std::abs(v.w-v.h)<eps) continue;
                Rect r{p.x,p.y,rot?v.h:v.w,rot?v.w:v.h,static_cast<int>(t),rot!=0};
                if(valid(polygon,mask,r)) candidates.push_back(r);
                if(candidates.size()>500000) throw std::runtime_error("Too many candidates; increase gridSpacing");
            }
        }
    }
    double deviation(const std::vector<Rect>& layout) const {
        double total=0,e=0;std::vector<double> a(types.size(),0);
        for(auto r:layout) {total+=r.area();a[r.type]+=r.area();}
        for(size_t i=0;i<a.size();++i) e=std::max(e,std::abs((total?a[i]/total:0)-types[i].share));
        return e;
    }
    void refineTypeLocations() {
        auto merit=[&](const Rect& r) {
            Point p{r.x+r.w/2,r.y+r.h/2};
            return 0.75*attraction(r.type,p)-1.5*distancePenalty(r.type,p);
        };
        // Relocate and exchange footprints without changing counts, mix or coverage.
        for(int pass=0;pass<3;++pass) {
            bool changed=false;
            for(size_t i=0;i<best.size();++i) {
                Rect chosen=best[i];double value=merit(chosen);
                for(const auto& candidate:candidates) if(candidate.type==chosen.type && merit(candidate)>value+eps) {
                    bool free=true;
                    for(size_t k=0;k<best.size();++k) if(k!=i && overlaps(candidate,best[k])) {free=false;break;}
                    if(free) {chosen=candidate;value=merit(chosen);}
                }
                if(chosen.x!=best[i].x || chosen.y!=best[i].y || chosen.rotated!=best[i].rotated) {best[i]=chosen;changed=true;}
            }
            for(size_t i=0;i<best.size();++i) for(size_t j=i+1;j<best.size();++j) if(best[i].type!=best[j].type) {
                Rect a=best[i],b=best[j];std::swap(a.x,b.x);std::swap(a.y,b.y);
                if(a.area()*merit(a)+b.area()*merit(b)<=best[i].area()*merit(best[i])+best[j].area()*merit(best[j])+eps) continue;
                if(overlaps(a,b)||!valid(polygon,mask,a)||!valid(polygon,mask,b)) continue;
                bool free=true;
                for(size_t k=0;k<best.size();++k) if(k!=i && k!=j && (overlaps(a,best[k])||overlaps(b,best[k]))) {free=false;break;}
                if(free) {best[i]=a;best[j]=b;changed=true;}
            }
            if(!changed) break;
        }
        attractionScore=layoutAttraction(best);attractorDistancePenalty=layoutDistancePenalty(best);
    }
    void compactTypeGroups() {
        // A larger rectangle often cannot move until several smaller blockers move
        // together. Try complete rearrangements, accepting only valid improvements.
        auto merit=[&](const Rect& r) {
            Point p{r.x+r.w/2,r.y+r.h/2};
            return 0.75*attraction(r.type,p)-1.5*distancePenalty(r.type,p);
        };
        auto totalMerit=[&](const std::vector<Rect>& layout) {
            double value=0;for(const auto& r:layout) value+=r.area()*merit(r);return value;
        };
        std::vector<std::vector<size_t>> byType(types.size());
        std::vector<double> values(candidates.size());
        for(size_t i=0;i<candidates.size();++i) {byType[candidates[i].type].push_back(i);values[i]=merit(candidates[i]);}
        for(auto& order:byType) std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return values[a]>values[b];});
        int budget=1500000;
        for(int pass=0;pass<4 && budget>0;++pass) {
            bool improved=false;
            std::vector<size_t> farthest(best.size());std::iota(farthest.begin(),farthest.end(),0);
            std::stable_sort(farthest.begin(),farthest.end(),[&](size_t a,size_t b){return merit(best[a])<merit(best[b]);});
            for(size_t moving:farthest) {
                if(budget<=0) break;
                const Rect original=best[moving];int tried=0;
                const auto& targets=byType[original.type];
                for(size_t rank=0;rank<targets.size();++rank) {
                    size_t targetIndex=targets[rank];
                    if(budget<=0 || tried>=24 || values[targetIndex]<=merit(original)+eps) break;
                    // Spread later trials across the neighbourhood instead of repeatedly
                    // testing almost identical positions near the same blocking edge.
                    if(rank>=12 && rank%8!=0) continue;
                    ++tried;const Rect target=candidates[targetIndex];
                    std::vector<Rect> trial{target},displaced;
                    for(size_t k=0;k<best.size();++k) if(k!=moving) {
                        if(overlaps(target,best[k])) displaced.push_back(best[k]);else trial.push_back(best[k]);
                    }
                    if(displaced.size()>8) continue;
                    std::stable_sort(displaced.begin(),displaced.end(),[](const Rect& a,const Rect& b){return a.area()>b.area();});
                    int localBudget=5000;
                    auto reinsert=[&](auto&& self,size_t depth)->bool {
                        if(depth==displaced.size()) return true;
                        int branches=0;
                        for(size_t index:byType[displaced[depth].type]) {
                            if(--budget<=0 || --localBudget<=0) return false;
                            const auto& r=candidates[index];
                            if(std::any_of(trial.begin(),trial.end(),[&](const Rect& b){return overlaps(r,b);})) continue;
                            trial.push_back(r);
                            if(self(self,depth+1)) return true;
                            trial.pop_back();if(++branches>=4) break;
                        }
                        return false;
                    };
                    if(reinsert(reinsert,0) && totalMerit(trial)>totalMerit(best)+eps) {
                        best=std::move(trial);improved=true;break;
                    }
                }
            }
            if(!improved) break;
        }
        attractionScore=layoutAttraction(best);attractorDistancePenalty=layoutDistancePenalty(best);
    }
    void solve() {
        best.clear();coverage=0;error=1;fieldMismatch=0;attractionScore=0;attractorDistancePenalty=0;
        const double areaStrength=mode==Mode::Area?fieldStrength:0;
        const bool attracted=mode==Mode::Type && std::any_of(attractors.begin(),attractors.end(),[](const Attractor& a){return a.strength>0;});
        std::mt19937 rng(seed);
        std::vector<size_t> order(candidates.size());std::iota(order.begin(),order.end(),0);
        std::vector<double> mismatch(candidates.size(),0);
        if(areaStrength>0) for(size_t i=0;i<candidates.size();++i) mismatch[i]=candidateMismatch(candidates[i]);
        std::vector<double> attractionValues(candidates.size(),0);
        if(attracted) for(size_t i=0;i<candidates.size();++i) {const auto& r=candidates[i];attractionValues[i]=attraction(r.type,{r.x+r.w/2,r.y+r.h/2});}
        std::vector<double> distanceCosts(candidates.size(),0);
        if(attracted) for(size_t i=0;i<candidates.size();++i) {const auto& r=candidates[i];distanceCosts[i]=distancePenalty(r.type,{r.x+r.w/2,r.y+r.h/2});}
        double coverageFloor=0;
        auto consider=[&](const std::vector<Rect>& layout) {
            double a=0;for(auto r:layout) a+=r.area(); double c=a/usableArea,e=deviation(layout);
            double f=areaStrength>0?layoutMismatch(layout):0;
            double att=attracted?layoutAttraction(layout):0;
            double cost=attracted?layoutDistancePenalty(layout):0;
            double score=c-2*areaStrength*f+0.75*att-1.5*cost,oldScore=coverage-2*areaStrength*fieldMismatch+0.75*attractionScore-1.5*attractorDistancePenalty;
            bool feasible=e<=tolerance,old=error<=tolerance;
            bool meetsCoverage=c+eps>=coverageFloor,oldMeetsCoverage=coverage+eps>=coverageFloor;
            bool improvesScore=score>oldScore+eps || (std::abs(score-oldScore)<eps&&e<error);
            bool improvesFeasible=(meetsCoverage&&!oldMeetsCoverage) || (meetsCoverage==oldMeetsCoverage&&improvesScore);
            if((feasible&&!old) || (feasible==old && (feasible ? improvesFeasible : e<error-eps || (std::abs(e-error)<eps&&score>oldScore)))) {best=layout;coverage=c;error=e;fieldMismatch=f;attractionScore=att;attractorDistancePenalty=cost;}
        };
        // Retain a coverage-focused solution as a warm start and fallback.
        if(areaStrength>0 || attracted) {
            Model baseline=*this;baseline.fieldStrength=0;baseline.attractors.clear();baseline.solve();
            // Strong clustering must not win by retaining only a few rectangles.
            if(attracted && baseline.error<=tolerance) coverageFloor=std::min(target,baseline.coverage)*typeCoverageRetention;
            consider(baseline.best);
        }
        for(int run=0;run<attempts;++run) {
            std::vector<Rect> layout;
            if(run%3==2 && !best.empty()) {
                layout=best;std::shuffle(layout.begin(),layout.end(),rng);
                // Alternate random removal with removing the most distant placements.
                if(attracted && run%2==0) std::stable_sort(layout.begin(),layout.end(),[&](const Rect& a,const Rect& b) {
                    return distancePenalty(a.type,{a.x+a.w/2,a.y+a.h/2})<distancePenalty(b.type,{b.x+b.w/2,b.y+b.h/2});
                });
                layout.resize(layout.size()*3/4);
            }
            std::vector<double> areas(types.size(),0);std::vector<int> counts(types.size(),0);double total=0;
            for(auto r:layout) {areas[r.type]+=r.area();counts[r.type]++;total+=r.area();}
            // A few passes revisit types that were temporarily ahead of their target share.
            std::shuffle(order.begin(),order.end(),rng);
            if(attracted && run%3!=0) {
                std::vector<double> keys(candidates.size());
                // Logarithmic weighted shuffle avoids numerical underflow for distant points.
                for(size_t i=0;i<keys.size();++i) keys[i]=std::log(-std::log(std::max(1e-12,std::generate_canonical<double,32>(rng))))+2*distanceCosts[i]-4*attractionValues[i];
                std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return keys[a]<keys[b];});
            }
            // Structured sweeps reduce fragmentation; shuffled runs explore alternatives.
            if(run%3==0) std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b) {
                const auto& u=candidates[a];const auto& v=candidates[b];
                if(attracted && run!=0 && distanceCosts[a]!=distanceCosts[b]) return distanceCosts[a]<distanceCosts[b];
                if(u.y!=v.y) return u.y<v.y;
                if(u.x!=v.x) return run%2==0?u.x<v.x:u.x>v.x;
                double uPenalty=areaStrength*mismatch[a]-0.75*attractionValues[a]+1.5*distanceCosts[a],vPenalty=areaStrength*mismatch[b]-0.75*attractionValues[b]+1.5*distanceCosts[b];
                if((areaStrength>0 || attracted) && run!=0 && uPenalty!=vPenalty) return uPenalty<vPenalty;
                return u.area()>v.area();
            });
            for(int pass=0;pass<3;++pass) for(auto index:order) {
                const Rect& r=candidates[index];const auto& t=types[r.type];
                if(t.share==0 || counts[r.type]>=t.maxCount) continue;
                if(total>0 && areas[r.type]/total>t.share+tolerance*0.5) continue;
                // Soft sampling preference. Final pass can still fill gaps with any type.
                if(areaStrength>0 && run%3!=0 && pass<2 && std::generate_canonical<double,32>(rng)>std::exp(-5*areaStrength*mismatch[index])) continue;
                if(std::any_of(layout.begin(),layout.end(),[&](const Rect& b){return overlaps(r,b);})) continue;
                layout.push_back(r);total+=r.area();areas[r.type]+=r.area();counts[r.type]++;
                if(deviation(layout)<=tolerance) consider(layout);
            }
            consider(layout);
        }
        if(attracted) {refineTypeLocations();compactTypeGroups();refineTypeLocations();}
    }
    nlohmann::json result() const {
        nlohmann::json j={{"coverage",coverage},{"targetCoverage",target},{"targetMet",coverage+eps>=target},{"shareDeviation",error},{"sharesMet",error<=tolerance},{"optimalityProven",false},{"placements",nlohmann::json::array()},{"types",nlohmann::json::array()}};
        j["mode"]=mode==Mode::Area?"area":"type";
        j["typeCoverageRetention"]=typeCoverageRetention;
        j["boundaryField"]={{"source",boundarySource==0?"outer":boundarySource==1?"inner":"both"},{"influenceWidth",influenceWidth},{"strength",fieldStrength},{"areaWeightedMismatch",fieldMismatch}};
        j["attractionScore"]=attractionScore;
        j["attractorDistancePenalty"]=attractorDistancePenalty;
        j["attractors"]=attractorJson();
        std::vector<double> areas(types.size(),0);std::vector<int> counts(types.size(),0);double total=coverage*usableArea;
        for(auto r:best) {Point p=world({r.x,r.y});j["placements"].push_back({{"type",types[r.type].id},{"anchor",{p.x,p.y}},{"footprintWidth",r.w},{"footprintHeight",r.h},{"typeRotated90",r.rotated},{"gridAngleDegrees",angle*180/3.141592653589793}});areas[r.type]+=r.area();counts[r.type]++;}
        // Footprint dimensions are in the grid frame; anchor is its lower-left corner.
        for(size_t i=0;i<best.size();++i) {auto r=best[i];auto& corners=j["placements"][i]["corners"];corners=nlohmann::json::array();for(Point q:std::array<Point,4>{{{r.x,r.y},{r.x+r.w,r.y},{r.x+r.w,r.y+r.h},{r.x,r.y+r.h}}}) {q=world(q);corners.push_back({q.x,q.y});}}
        for(size_t i=0;i<types.size();++i) j["types"].push_back({{"id",types[i].id},{"count",counts[i]},{"targetShare",types[i].share},{"actualShare",total?areas[i]/total:0}});
        return j;
    }
    nlohmann::json attractorJson() const {
        auto j=nlohmann::json::array();
        for(const auto& a:attractors) {Point p=world(a.point);j.push_back({{"type",types[a.type].id},{"point",{p.x,p.y}},{"radius",a.radius},{"strength",a.strength}});}
        return j;
    }
};
}
