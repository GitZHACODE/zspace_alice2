#include "Packing.h"
#include <iostream>
#include <fstream>
using namespace packing;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main() {
    try {
        Polygon p{{0,0},{10,0},{10,10},{0,10}},mask{{4,4},{6,4},{6,6},{4,6}};
        check(valid(p,{}, {0,0,10,10}),"Boundary touching must be valid");
        check(!valid(p,mask,{2,2,6,6}),"Enclosed mask must be rejected");
        check(!valid(p,mask,{3,3,2,2}),"Mask crossing must be rejected");
        check(valid(p,mask,{0,0,4,4}),"Mask boundary touching must be valid");
        Polygon notch{{0,0},{10,0},{10,10},{6,10},{6,4},{4,4},{4,10},{0,10}};
        check(!valid(notch,{}, {2,2,6,6}),"Concave notch must be rejected even when corners are inside");
        check(!overlaps({0,0,2,2},{2,0,2,2}),"Touching rectangles must not overlap");
        check(std::abs(boundaryDistance(p,{5,1})-1)<eps,"Distance must use boundary segments rather than vertices");
        Model field;field.polygon=p;field.mask=mask;field.influenceWidth=2;
        field.boundarySource=0;check(field.influence({5,0})>field.influence({5,2}),"Outer influence must decay inwards");
        field.boundarySource=1;check(field.influence({3,5})>field.influence({0,5}),"Inner influence must decay away from mask");
        field.boundarySource=2;double both=field.influence({1,5});
        field.boundarySource=0;double outer=field.influence({1,5});
        field.boundarySource=1;double inner=field.influence({1,5});
        check(std::abs(both-std::max(outer,inner))<eps,"Both must use nearest selected boundary");
        field.mask.clear();check(field.influence({5,5})==0,"Missing inner mask should have zero influence");
        nlohmann::json j={{"polygon",{{0,0},{10,0},{10,10},{0,10}}},{"mask",nlohmann::json::array()},
            {"rectangles",{{{"id","A"},{"w",2},{"h",2},{"targetShare",1}}}},
            {"settings",{{"gridSpacing",1},{"attempts",5},{"targetCoverage",1}}}};
        Model m;m.load(j);m.generate();m.solve();
        check(m.inner.area()==100,"Largest inner raster rectangle");
        check(m.coverage>0.7 && m.error==0,"Packing must cover most of simple square");
        for(size_t i=0;i<m.best.size();++i) {
            auto r=m.best[i];check(valid(m.polygon,m.mask,r),"Output footprint invalid");
            check(std::abs(r.x-std::round(r.x))<eps && std::abs(r.y-std::round(r.y))<eps,"Anchor off lattice");
            for(size_t k=0;k<i;++k) check(!overlaps(r,m.best[k]),"Output overlap");
        }
        auto first=m.result();m.solve();check(first==m.result(),"Fixed seed must reproduce layout");
        m.attractors={{0,{5,5},3,0}};m.solve();check(first["placements"]==m.result()["placements"],"Zero-strength attractors must preserve original packing");
        j["rectangles"][0]["maxCount"]=2;m.load(j);m.generate();m.solve();check(m.best.size()==2,"Inventory cap");
        j["rectangles"][0]["targetShare"]=0.5;bool rejected=false;
        try {m.load(j);}catch(const std::exception&) {rejected=true;}check(rejected,"Invalid shares accepted");
        std::ifstream input(std::string(PACKING_DATA_DIR)+"/packing.json");input>>j;
        check(j.at("settings").at("mode")=="type" && j.at("attractors").size()==j.at("rectangles").size(),"Example must start in type mode with initial attractors");
        Model example;example.load(j);example.generate();example.solve();
        check(example.mode==Mode::Type && !example.best.empty(),"Type example must load and pack");
        Model shares=example;shares.setTargetShare(0,0.4);
        check(std::abs(shares.types[0].share-0.4)<eps,"Share slider must preserve the edited percentage");
        double shareSum=0;for(const auto& t:shares.types) shareSum+=t.share;
        check(std::abs(shareSum-1)<eps && std::abs(shares.types[1].share/shares.types[2].share-example.types[1].share/example.types[2].share)<eps,"Other shares must retain their proportions and total 100 percent");
        shares.setTargetShare(0,1);shares.setTargetShare(0,0.5);
        check(std::abs(shares.types[1].share-0.25)<eps && std::abs(shares.types[2].share-0.25)<eps,"Share sliders must recover from zeroed remaining shares");
        shares.setTargetShare(1,0);shareSum=0;for(const auto& t:shares.types) shareSum+=t.share;
        check(shares.types[1].share==0 && std::abs(shareSum-1)<eps,"Zero target share must remain valid");
        Model reference=example;reference.attractors.clear();reference.solve();
        std::cout<<"JSON example distance cost: "<<example.layoutDistancePenalty(reference.best)<<" -> "<<example.attractorDistancePenalty<<", coverage "<<example.coverage*100<<"%\n";
        check(example.layoutDistancePenalty(example.best)<example.layoutDistancePenalty(reference.best),"Example attractors must visibly improve type locations");
        check(example.coverage+eps>=std::min(example.target,reference.coverage)*example.typeCoverageRetention && example.error<=example.tolerance,"Example must respect configured coverage retention and mix");
        j["settings"]["mode"]="area";
        for(double angle:{0.0,23.0}) {
            j["settings"]["gridAngleDegrees"]=angle;m.load(j);m.generate();m.solve();
            check(!m.best.empty() && m.coverage>0.5,"Sample packing unexpectedly sparse");
            check(m.error<=m.tolerance,"Sample mix target not met");
            for(size_t i=0;i<m.best.size();++i) {
                check(valid(m.polygon,m.mask,m.best[i]),"Sample footprint invalid");
                for(size_t k=0;k<i;++k) check(!overlaps(m.best[i],m.best[k]),"Sample overlap");
            }
            std::cout<<"Sample angle "<<angle<<": coverage "<<m.coverage*100<<"%, mix deviation "<<m.error*100<<" points\n";
        }
        j["settings"]["gridAngleDegrees"]=0;
        bool spatialImprovement=false;
        for(const char* source:{"outer","inner","both"}) {
            j["settings"]["boundaryField"]={{"source",source},{"influenceWidth",12},{"strength",0}};
            m.load(j);m.generate();m.solve();double baseline=m.layoutMismatch(m.best);
            m.fieldStrength=1;m.solve();double guided=m.layoutMismatch(m.best);
            std::cout<<source<<" field mismatch: "<<baseline<<" -> "<<guided<<", coverage "<<m.coverage*100<<"%\n";
            check(guided<=baseline+eps,"Guidance must not worsen sample spatial preference");
            spatialImprovement=spatialImprovement || guided<baseline-eps;
            check(m.coverage>0.5 && m.error<=m.tolerance,"Guidance must retain useful coverage and valid mix");
            for(size_t i=0;i<m.best.size();++i) {
                check(valid(m.polygon,m.mask,m.best[i]),"Guided footprint invalid");
                for(size_t k=0;k<i;++k) check(!overlaps(m.best[i],m.best[k]),"Guided overlap");
            }
            auto guidedResult=m.result();m.solve();check(guidedResult==m.result(),"Guided solve must be deterministic");
        }
        check(spatialImprovement,"Guidance must improve spatial preference for at least one boundary source");
        Attractor a{0,{0,0},10,0.8};
        check(std::abs(gaussianInfluence(a,{0,0})-0.8)<eps,"Attractor centre strength");
        check(std::abs(gaussianInfluence(a,{10,0})-0.4)<eps,"Radius must mean half strength");
        check(std::abs(gaussianInfluence(a,{20,0})-0.05)<eps,"Gaussian falloff at two radii");
        Model typeField;typeField.attractors={a,a};
        check(std::abs(typeField.attraction(0,{0,0})-0.8)<eps,"Overlapping attractors must use max rather than sum");
        check(typeField.attraction(1,{0,0})==0,"Attractors must affect only their own type");
        check(std::abs(typeField.distancePenalty(0,{10,0})-0.8)<eps,"Distance penalty at one radius");
        check(std::abs(typeField.distancePenalty(0,{20,0})-3.2)<eps,"Distance penalty must grow quadratically beyond the radius");
        check(std::abs(typeField.distancePenalty(0,{40,0})-12.8)<eps,"Far placements must not saturate like Gaussian influence");
        check(typeField.distancePenalty(1,{40,0})==0,"Types without attractors must not be penalised");
        typeField.attractors.push_back({0,{40,0},10,0});
        check(std::abs(typeField.distancePenalty(0,{40,0})-12.8)<eps,"Zero-strength point must not bypass another active attractor");
        typeField.attractors.push_back({0,{40,0},10,0.8});
        check(typeField.distancePenalty(0,{40,0})==0,"A second active attractor must allow another cluster");
        auto cluster=nlohmann::json::parse(R"({
          "polygon":[[0,0],[40,0],[40,20],[0,20]],
          "rectangles":[{"id":"A","w":4,"h":4,"targetShare":0.5},{"id":"B","w":4,"h":4,"targetShare":0.5}],
          "settings":{"mode":"type","gridSpacing":2,"attempts":30,"shareTolerance":0.05}
        })");
        Model clustered;clustered.load(cluster);clustered.generate();clustered.solve();
        clustered.attractors={{0,{5,10},8,1},{1,{35,10},8,1}};
        double before=clustered.layoutAttraction(clustered.best);
        double distanceBefore=clustered.layoutDistancePenalty(clustered.best);
        auto farFraction=[&]() {double far=0,total=0;for(const auto& r:clustered.best) {double a=r.area();total+=a;if(clustered.distancePenalty(r.type,{r.x+r.w/2,r.y+r.h/2})>4) far+=a;}return far/total;};
        double farBefore=farFraction();
        clustered.solve();
        std::cout<<"Type attraction: "<<before<<" -> "<<clustered.attractionScore<<", coverage "<<clustered.coverage*100<<"%\n";
        check(clustered.attractionScore>before+0.05,"Type attractors must improve clustering even for equal-area types");
        std::cout<<"Type distance penalty: "<<distanceBefore<<" -> "<<clustered.attractorDistancePenalty<<", area beyond two radii: "<<farBefore<<" -> "<<farFraction()<<'\n';
        check(clustered.attractorDistancePenalty<distanceBefore*0.7 && farFraction()<farBefore,"Distance penalty must reduce distant placements");
        check(clustered.coverage+eps>=clustered.target && clustered.error<=clustered.tolerance,"Strong clustering must retain target coverage and mix");
        for(size_t i=0;i<clustered.best.size();++i) {
            check(valid(clustered.polygon,clustered.mask,clustered.best[i]),"Attracted footprint invalid");
            for(size_t k=0;k<i;++k) check(!overlaps(clustered.best[i],clustered.best[k]),"Attracted overlap");
        }
        auto attractedResult=clustered.result();clustered.solve();check(attractedResult==clustered.result(),"Attractor solve must be deterministic");
        cluster["attractors"]=clustered.attractorJson();cluster["settings"]["gridAngleDegrees"]=23;
        Model rotated;rotated.load(cluster);
        auto saved=rotated.attractorJson();
        check(std::abs(saved[0]["point"][0].get<double>()-5)<eps && std::abs(saved[0]["point"][1].get<double>()-10)<eps,"Attractor coordinates must survive grid rotation and export");
        cluster["attractors"][0]["type"]="unknown";rejected=false;
        try {rotated.load(cluster);}catch(const std::exception&) {rejected=true;}check(rejected,"Unknown attractor type accepted");
        cluster["attractors"][0]["type"]="A";cluster["attractors"][0]["radius"]=0;rejected=false;
        try {rotated.load(cluster);}catch(const std::exception&) {rejected=true;}check(rejected,"Zero attractor radius accepted");
        cluster["attractors"][0]["radius"]=8;cluster["attractors"][0]["strength"]=1.1;rejected=false;
        try {rotated.load(cluster);}catch(const std::exception&) {rejected=true;}check(rejected,"Out-of-range attractor strength accepted");
        // A mode switch must isolate solver inputs, rather than just change the preview.
        j["settings"]["mode"]="area";
        Model isolated;isolated.load(j);isolated.generate();isolated.solve();auto areaPlacements=isolated.result()["placements"];
        for(auto& point:isolated.attractors) {point.radius=1;point.strength=1;}
        isolated.solve();check(areaPlacements==isolated.result()["placements"],"Area mode must ignore attractor edits");
        check(isolated.attractionScore==0 && isolated.attractorDistancePenalty==0,"Area mode must not apply type scores");
        j["settings"]["mode"]="type";isolated.load(j);isolated.generate();isolated.solve();auto typePlacements=isolated.result()["placements"];
        isolated.boundarySource=0;isolated.influenceWidth=0.1;isolated.fieldStrength=0;
        isolated.solve();check(typePlacements==isolated.result()["placements"],"Type mode must ignore boundary edits");
        isolated.boundarySource=1;isolated.influenceWidth=500;isolated.fieldStrength=1;
        isolated.solve();check(typePlacements==isolated.result()["placements"] && isolated.fieldMismatch==0,"Type mode must never apply area penalties");
        isolated.mode=Mode::Area;isolated.solve();isolated.mode=Mode::Type;isolated.solve();
        check(typePlacements==isolated.result()["placements"],"Switching modes must preserve stored type settings");
        auto blocked=nlohmann::json::parse(R"({
          "polygon":[[0,0],[8,0],[8,4],[0,4]],
          "rectangles":[{"id":"A","w":4,"h":4,"targetShare":0.5},{"id":"B","w":2,"h":2,"targetShare":0.5}],
          "attractors":[{"type":"A","point":[6,2],"radius":2,"strength":1},{"type":"B","point":[2,2],"radius":2,"strength":1}],
          "settings":{"mode":"type","gridSpacing":2}
        })");
        Model blockedModel;blockedModel.load(blocked);blockedModel.generate();
        blockedModel.best={{0,0,4,4,0},{4,0,2,2,1},{6,0,2,2,1},{4,2,2,2,1},{6,2,2,2,1}};
        double blockedBefore=blockedModel.layoutDistancePenalty(blockedModel.best);
        blockedModel.refineTypeLocations();
        check(std::abs(blockedBefore-blockedModel.layoutDistancePenalty(blockedModel.best))<eps,"Regression must require moving several blockers together");
        blockedModel.compactTypeGroups();
        check(blockedModel.attractorDistancePenalty<blockedBefore*0.5,"Group rearrangement must pull the blocked large rectangle towards its attractor");
        check(blockedModel.best.size()==5 && blockedModel.deviation(blockedModel.best)==0,"Group rearrangement must preserve exact type inventory and mix");
        double packedArea=0;for(size_t i=0;i<blockedModel.best.size();++i) {
            const auto& r=blockedModel.best[i];packedArea+=r.area();check(valid(blockedModel.polygon,blockedModel.mask,r),"Group rearrangement footprint invalid");
            for(size_t k=0;k<i;++k) check(!overlaps(r,blockedModel.best[k]),"Group rearrangement overlap");
        }
        check(packedArea==32,"Group rearrangement must preserve full coverage");
        std::cout<<"Blocked large-rectangle distance cost: "<<blockedBefore<<" -> "<<blockedModel.attractorDistancePenalty<<" (full coverage retained)\n";
        j["settings"]["mode"]="invalid";rejected=false;
        try {isolated.load(j);}catch(const std::exception&) {rejected=true;}check(rejected,"Invalid mode accepted");
        std::cout<<"Rectangle packing checks passed\n";return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
