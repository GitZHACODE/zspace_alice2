#include "BrickLayout.h"
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace alice2::bricks {
void BrickGenerator::setAlignmentPlanes(const std::vector<AlignmentPlane>& planes) {
    std::vector<AlignmentPlane> validated;
    const double scale = vertices_.rows() ? (vertices_.colwise().maxCoeff()-vertices_.colwise().minCoeff()).norm() : 1;
    const double tolerance = std::max(scale,1.0)*1e-7;
    for (auto plane : planes) {
        if (!plane.origin.allFinite() || !plane.normal.allFinite() || plane.normal.norm()<1e-12)
            throw std::runtime_error("Alignment planes need finite origins and nonzero normals.");
        plane.normal.normalize();
        bool duplicate=false;
        for (const auto& previous:validated)
            if (std::abs(previous.normal.dot(plane.normal))>1-1e-10 &&
                std::abs(previous.normal.dot(plane.origin-previous.origin))<=tolerance) { duplicate=true; break; }
        if (!duplicate) validated.push_back(plane);
    }
    planes_=std::move(validated); bricks_.clear(); segments_.clear();
}

void BrickGenerator::loadPlanesObj(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read alignment planes: "+path);
    std::vector<Eigen::Vector3d> vertices;
    std::vector<AlignmentPlane> planes;
    std::string line,tag;
    int degree=1;
    const auto index = [&](const std::string& token) {
        const int value=std::stoi(token.substr(0,token.find('/')));
        const int id=value>0 ? value-1 : static_cast<int>(vertices.size())+value;
        if (value==0 || id<0 || id>=static_cast<int>(vertices.size())) throw std::runtime_error("Invalid index in planes.obj.");
        return id;
    };
    const auto linePlane = [&](int a,int b) {
        const Eigen::Vector3d x=vertices[b]-vertices[a];
        const Eigen::Vector3d n=x.cross(Eigen::Vector3d::UnitZ());
        if (n.norm()<x.norm()*1e-10 || x.norm()<1e-12)
            throw std::runtime_error("Plane line must have a nonzero direction transverse to world Z.");
        planes.push_back({(vertices[a]+vertices[b])/2,n.normalized()});
    };
    while (std::getline(input,line)) {
        std::istringstream stream(line); stream>>tag;
        if (tag=="v") {
            Eigen::Vector3d p;
            if (!(stream>>p.x()>>p.y()>>p.z()) || !p.allFinite()) throw std::runtime_error("Invalid vertex in planes.obj.");
            vertices.push_back(p);
        } else if (tag=="deg") { stream>>degree; }
        else if (tag=="l" || tag=="curv" || tag=="f") {
            if (tag=="curv") {
                double lo,hi;
                if (!(stream>>lo>>hi) || degree!=1) throw std::runtime_error("Export plane curves as degree-1 lines.");
            }
            std::vector<int> ids; std::string token;
            while (stream>>token) { if (token[0]=='#') break; ids.push_back(index(token)); }
            if (tag=="f") {
                if (ids.size()<3) throw std::runtime_error("Plane face has fewer than three vertices.");
                Eigen::Vector3d n=Eigen::Vector3d::Zero(),centre=Eigen::Vector3d::Zero();
                double extent=0;
                for (size_t k=0;k<ids.size();++k) {
                    n+=(vertices[ids[k]]-vertices[ids[0]]).cross(vertices[ids[(k+1)%ids.size()]]-vertices[ids[0]]);
                    centre+=vertices[ids[k]];
                    extent=std::max(extent,(vertices[ids[k]]-vertices[ids[0]]).norm());
                }
                if (n.norm()<1e-12) throw std::runtime_error("Degenerate plane face.");
                n.normalize(); centre/=static_cast<double>(ids.size());
                for (int id:ids) if (std::abs(n.dot(vertices[id]-centre))>std::max(extent,1.0)*1e-7)
                    throw std::runtime_error("Nonplanar face in planes.obj.");
                planes.push_back({centre,n});
            } else {
                if (ids.size()<2 || (tag=="curv" && ids.size()!=2)) throw std::runtime_error("Use straight two-point curves for plane axes.");
                for (size_t k=1;k<ids.size();++k) linePlane(ids[k-1],ids[k]);
            }
        }
        tag.clear();
    }
    if (planes.empty()) throw std::runtime_error("planes.obj contains no supported lines or planar faces.");
    setAlignmentPlanes(planes);
}

namespace detail {
namespace {
struct Cut { double distance; int plane; };
int divisionCount(double distance,double average) {
    const double ratio=distance/average;
    if(ratio>250000)throw std::runtime_error("Too many bricks on one contour path.");
    int n=static_cast<int>(std::ceil(ratio));
    if(ratio-std::floor(ratio)<=0.1)n=static_cast<int>(std::floor(ratio));
    if(n%2)++n;
    return std::max(n,2);
}
Eigen::Vector3d at(const ContourPath& path,const std::vector<double>& arc,double d) {
    if (path.closed) { d=std::fmod(d,path.length); if(d<0)d+=path.length; }
    d=std::clamp(d,0.0,path.length);
    const size_t i=std::min(static_cast<size_t>(std::upper_bound(arc.begin(),arc.end(),d)-arc.begin()),arc.size()-1);
    if (!i) return path.points.front();
    const double t=(d-arc[i-1])/(arc[i]-arc[i-1]);
    return (1-t)*path.points[i-1]+t*path.points[i];
}
}
EqualDivisions equalDivisionCounts(const std::vector<ContourSegment>& segments,const BrickOptions& options) {
    std::map<std::tuple<int,int,int>,std::pair<double,int>> groups;
    std::map<std::pair<int,int>,int> occurrences;
    int previousPath=-1;
    for(const auto& segment:segments) {
        if(segment.pathIndex!=previousPath){occurrences.clear();previousPath=segment.pathIndex;}
        const auto pair=std::make_pair(segment.startPlane,segment.endPlane);
        const int occurrence=occurrences[pair]++;
        if(pair.first<0 && pair.second<0)continue;
        auto& group=groups[{pair.first,pair.second,occurrence}];
        group.first+=segment.endDistance-segment.startDistance;++group.second;
    }
    EqualDivisions result;
    const double average=(options.lengthA+options.lengthB)/2+options.gap;
    for(const auto& [key,group]:groups)result[key]=divisionCount(group.first/group.second,average);
    return result;
}
std::vector<BrickSpan> layoutPath(const ContourPath& path,int pathIndex,
    const std::vector<double>& arc,const std::vector<AlignmentPlane>& planes,
    const BrickOptions& options,std::vector<ContourSegment>& segments,const EqualDivisions* equalDivisions) {
    const double tolerance=std::max(path.length,1.0)*1e-9;
    const double threshold=options.lengthA+2*options.lengthB;
    std::vector<Cut> cuts;
    const int count=static_cast<int>(path.points.size())-(path.closed?1:0);
    for (int p=0;p<static_cast<int>(planes.size());++p) {
        std::vector<double> distances(path.points.size());
        for (size_t i=0;i<path.points.size();++i) distances[i]=planes[p].normal.dot(path.points[i]-planes[p].origin);
        for (size_t i=1;i<path.points.size();++i) {
            const double a=distances[i-1],b=distances[i];
            if ((a>tolerance && b< -tolerance) || (a< -tolerance && b>tolerance))
                cuts.push_back({arc[i-1]+(arc[i]-arc[i-1])*a/(a-b),p});
        }
        for (int i=0;i<count;++i) if (std::abs(distances[i])<=tolerance) {
            // One cut per zero run, only if the contour actually crosses the plane.
            if (!path.closed && (i==0 || i==count-1)) continue;
            const int previous=(i+count-1)%count;
            if (std::abs(distances[previous])<=tolerance) continue;
            int next=(i+1)%count;
            while (next!=i && std::abs(distances[next])<=tolerance) {
                if (!path.closed && next==count-1) break;
                next=(next+1)%count;
            }
            if (distances[previous]*distances[next]< -tolerance*tolerance) cuts.push_back({arc[i],p});
        }
    }
    std::sort(cuts.begin(),cuts.end(),[](const Cut& a,const Cut& b){return a.distance<b.distance || (a.distance==b.distance && a.plane<b.plane);});
    cuts.erase(std::unique(cuts.begin(),cuts.end(),[&](const Cut& a,const Cut& b){return std::abs(a.distance-b.distance)<=tolerance;}),cuts.end());
    if (!path.closed) {
        cuts.erase(std::remove_if(cuts.begin(),cuts.end(),[&](const Cut& c){return c.distance<=tolerance || c.distance>=path.length-tolerance;}),cuts.end());
        cuts.insert(cuts.begin(),{0,-1}); cuts.push_back({path.length,-1});
        bool changed=true;
        while(changed && cuts.size()>2) {
            changed=false;
            for(size_t i=0;i+1<cuts.size();++i) if(cuts[i+1].distance-cuts[i].distance<=threshold+tolerance) {
                // Merge right, except the terminal fragment merges left.
                const size_t erase=i+1<cuts.size()-1?i+1:i;
                cuts.erase(cuts.begin()+erase); changed=true; break;
            }
        }
    } else {
        bool changed=true;
        while(changed && cuts.size()>1) {
            changed=false;
            for(size_t i=0;i<cuts.size();++i) {
                const size_t next=(i+1)%cuts.size();
                const double end=cuts[next].distance+(next==0?path.length:0);
                if(end-cuts[i].distance<=threshold+tolerance) {
                    cuts.erase(cuts.begin()+next); changed=true; break;
                }
            }
        }
        if(cuts.empty()) cuts.push_back({0,-1});
        cuts.push_back({cuts.front().distance+path.length,cuts.front().plane});
    }

    const int firstSegment=static_cast<int>(segments.size());
    for(size_t i=0;i+1<cuts.size();++i) {
        ContourSegment segment;
        segment.pathIndex=pathIndex; segment.levelIndex=path.levelIndex;
        segment.startDistance=cuts[i].distance; segment.endDistance=cuts[i+1].distance;
        segment.startPlane=cuts[i].plane; segment.endPlane=cuts[i+1].plane;
        segment.points.push_back(at(path,arc,segment.startDistance));
        for(int wrap=0;wrap<=(path.closed?1:0);++wrap) for(size_t k=1;k<arc.size();++k) {
            const double d=arc[k]+wrap*path.length;
            if(d>segment.startDistance+tolerance && d<segment.endDistance-tolerance) segment.points.push_back(path.points[k]);
        }
        segment.points.push_back(at(path,arc,segment.endDistance));
        segments.push_back(std::move(segment));
    }
    std::vector<BrickSpan> spans;
    const double repeat=options.lengthA+options.lengthB+2*options.gap;
    const bool equalLengths=std::abs(options.lengthA-options.lengthB)<=std::max(options.lengthA,options.lengthB)*1e-6;
    const bool anchorA=path.levelIndex%2==0;
    const auto push=[&](double begin,double end,bool a,int plane,int segment) {
        if(end-begin>tolerance) spans.push_back({begin,end,a,plane,segment});
        if(spans.size()>250000) throw std::runtime_error("Too many bricks on one contour path.");
    };
    const bool anchored=std::any_of(cuts.begin(),cuts.end(),[](const Cut& c){return c.plane>=0;});
    if(!anchored) {
        const double phase=path.levelIndex%2?repeat/2:0;
        double cell=-phase; bool a=true;
        while(cell<path.length) {
            const double length=a?options.lengthA:options.lengthB;
            push(std::max(options.gap/2,cell+options.gap/2),std::min(path.length-options.gap/2,cell+options.gap/2+length),a,-1,firstSegment);
            const double next=cell+length+options.gap;
            if(next<=cell)throw std::runtime_error("Brick length is too small for this mesh scale.");
            cell=next;a=!a;
        }
    } else {
        // Port of Shared/Utils.cs: BrickCount -> ComputeBricks -> CombineBrickes.
        // Equal center steps; B pitch stays fixed and A pitch fits the segment.
        // Endpoint records are halves until adjoining halves have been combined.
        const double shortPitch=options.lengthB+options.gap;
        const double longPitch=options.lengthA+options.gap;
        const double average=(shortPitch+longPitch)/2;
        std::vector<BrickSpan> raw;
        std::map<std::pair<int,int>,int> occurrences;
        for(size_t i=0;i+1<cuts.size();++i) {
            const double distance=cuts[i+1].distance-cuts[i].distance;
            int divisions=divisionCount(distance,average);
            const auto pair=std::make_pair(cuts[i].plane,cuts[i+1].plane);
            const int occurrence=occurrences[pair]++;
            if(equalLengths && equalDivisions) {
                const auto shared=equalDivisions->find({pair.first,pair.second,occurrence});
                if(shared!=equalDivisions->end())divisions=shared->second;
            }
            double step=distance/divisions;
            // Unlike Rhino's zero-length rectangles, report impossible dimensions.
            if((equalLengths?step:2*step-shortPitch)<=options.gap)
                throw std::runtime_error("Segment cannot fit these A/B lengths and gap; reduce gap or adjust lengths.");
            for(int k=0;k<=divisions;++k) {
                const bool isA=k%2==0?anchorA:!anchorA;
                // Equal inputs form an aligned bond: share the fitting adjustment
                // between both types so swapping course parity cannot shift joints.
                const double pitch=equalLengths?step:(isA?2*step-shortPitch:shortPitch);
                const double reference=k==divisions?cuts[i+1].distance:cuts[i].distance+k*step;
                BrickSpan brick{reference-(k==0?0:pitch/2),reference+(k==divisions?0:pitch/2),isA};
                brick.reference=reference; brick.segment=firstSegment+static_cast<int>(i);
                brick.plane=k==0?cuts[i].plane:(k==divisions?cuts[i+1].plane:-1);
                brick.boundaryHalf=k==0 || k==divisions;
                if(k==0 && !raw.empty()) {
                    // CombineBrickes: extend the previous end record by the
                    // next start half, then remove the next start record.
                    raw.back().end+=brick.end-brick.begin;
                    raw.back().boundaryHalf=false;
                } else raw.push_back(brick);
                if(raw.size()>250000)throw std::runtime_error("Too many bricks on one contour path.");
            }
        }
        if(path.closed && raw.size()>1) {
            raw.front().begin-=raw.back().end-raw.back().begin;
            raw.front().boundaryHalf=false;
            raw.pop_back();
        }
        for(auto brick:raw) {
            // InstantiateBricks applies mortar only after the halves are united.
            brick.begin=std::min(brick.begin+options.gap/2,brick.reference);
            brick.end=std::max(brick.end-options.gap/2,brick.reference);
            if(brick.end-brick.begin>tolerance)spans.push_back(brick);
        }
    }
    std::sort(spans.begin(),spans.end(),[](const BrickSpan& a,const BrickSpan& b){return a.begin<b.begin;});
    return spans;
}
}
}
