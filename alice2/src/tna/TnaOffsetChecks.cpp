#include "TnaOffsetSolver.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace alice2 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
MeshData cablePyramid() {
    MeshData mesh;
    for (Vec3 p : {Vec3(-1,-1,0),Vec3(1,-1,0),Vec3(1,1,0),Vec3(-1,1,0),Vec3(0,0,1)}) {
        mesh.vertices.emplace_back(p); mesh.vertices.back().color = Color(1,1,0,1);
    }
    for (int i = 0; i < 4; ++i) mesh.faces.emplace_back(std::vector<int>{i,(i+1)%4,4});
    return mesh;
}
void reachFit(TnaOffsetSolver& solver, int limit = 210000) {
    if (!solver.startCompressionFit()) throw std::runtime_error(solver.diagnostic());
    for (int i = 0; i < limit && solver.running(); ++i) solver.step();
    std::cout << solver.diagnostic() << std::endl;
    require(solver.stage() == TnaOffsetStage::CompressionFitted && !solver.offsetBuiltFromTna(),
        "Compression fit did not stop before TNA");
    if (!solver.startTna()) throw std::runtime_error(solver.diagnostic());
    for (int i = 0; i < limit && solver.running(); ++i) solver.step();
    std::cout << solver.diagnostic() << std::endl;
    require(solver.stage() == TnaOffsetStage::TnaComplete && !solver.offsetBuiltFromTna(),
        "Compression TNA failed or automatically started cable fitting");
    require(solver.forceDiagram().success && !solver.forceDiagram().mesh.edges.empty(), "No compression force diagram");
    require(solver.compressionReport().finite && solver.compressionReport().residual <= 1e-3,
        "Compression stage did not physically equilibrate");
    if (!solver.startCableFit()) throw std::runtime_error(solver.diagnostic());
}
void checkNetwork(const TnaOffsetSolver& solver) {
    const auto& n = solver.network(); const auto& r = solver.result();
    require(r.report.finite, "Mixed network is nonfinite");
    require(n.form.mesh.faces.size() == solver.input().faces.size(), "Exterior faces leaked into physical surface");
    require(n.surfaceVertexCount == int(solver.input().vertices.size()), "Source IDs changed");
    for (int v : n.form.supportVertices)
        require((r.mesh.vertices[v].position - solver.input().vertices[v].position).length() == 0, "Black anchor moved");
    for (int v : solver.fitSettings().loads.additionalFixedVertices)
        require(r.mesh.vertices[v].position.z == solver.input().vertices[v].position.z, "Sliding height moved");
    for (size_t i = 0; i < r.edges.size(); ++i)
        require(std::isfinite(r.forceDensities[i]) && (r.edges[i].isTension ? -1 : 1) * r.forceDensities[i] > 0,
            "Mixed density sign changed");
}
}
int runTnaOffsetChecks(const char* objPath, int fitIterations, float formWeight, int horizontalIterations) {
    try {
        TnaOffsetSolver synthetic;
        TnaOffsetSettings settings; settings.density = 0; settings.nodalLoad = 1;
        settings.fitIterations = 120; settings.attraction = .01; settings.yellowPenalty = 50;
        settings.offset = -.2f;
        require(synthetic.reset(cablePyramid(), settings), "Synthetic reset failed");
        require(!synthetic.startTna() && !synthetic.startCableFit() && synthetic.stage() == TnaOffsetStage::Ready,
            "Out-of-order keys started a stage");
        reachFit(synthetic);
        checkNetwork(synthetic);
        require(synthetic.network().form.mesh.vertices.size() == 6, "Interior offset node count wrong");
        require(synthetic.network().yellowSupports.size() == 4, "Boundary yellow roles lost");
        const int cable = synthetic.network().cableNode[4];
        require(cable == 5, "Cable mapping changed");
        const auto& reference = synthetic.network().form.mesh.vertices[4];
        const auto offset = synthetic.network().form.mesh.vertices[cable].position - reference.position;
        require(std::abs(offset.length() - .2f) < 1e-5f && offset.dot(reference.normal) < 0,
            "Cable offset did not follow negative TNA normal");
        int valence = 0;
        for (size_t i = 0; i < synthetic.network().memberKinds.size(); ++i) {
            const auto& e = synthetic.network().form.activeFormEdges[i];
            if (synthetic.network().memberKinds[i] == "cable" && (e.vertexA == cable || e.vertexB == cable)) ++valence;
            if (synthetic.network().memberKinds[i] != "compression") continue;
            for (size_t j = 0; j < synthetic.compressionEdges().size(); ++j) {
                const auto& c = synthetic.compressionEdges()[j];
                if (std::minmax(c.vertexA,c.vertexB) == std::minmax(e.vertexA,e.vertexB))
                    require(std::abs(synthetic.result().forceDensities[i] - synthetic.compressionDensities()[j]) < 1e-6f,
                        "TNA compression density was not transferred");
            }
        }
        require(valence == 4, "Branching cable node unsupported");
        const double initial = synthetic.result().report.residual;
        while (synthetic.running()) synthetic.step();
        checkNetwork(synthetic);
        std::cout << "Synthetic " << synthetic.diagnostic() << std::endl;
        require(synthetic.result().success && synthetic.result().report.residual < initial,
            "Synthetic mixed fit did not reach vertical-reaction equilibrium");
        // Stage-1 temporary restraints must not change the stage-2 colour roles.
        auto mixedRoles = cablePyramid();
        mixedRoles.vertices[0].color = Color(0,0,0,1);
        mixedRoles.vertices[1].color = Color(1,0,0,1);
        TnaOffsetSolver roles;
        require(roles.reset(mixedRoles,settings), "Mixed-role reset failed");
        reachFit(roles); roles.step(); checkNetwork(roles);
        require(roles.network().form.supportVertices == std::vector<int>{0} &&
            roles.network().sliding == std::vector<int>{1}, "Temporary TNA restraints leaked into mixed fit");
        TnaOffsetSolver cancelled;
        require(cancelled.reset(cablePyramid(), settings) && cancelled.startCompressionFit(), "Cancellation setup failed");
        std::stop_source stop; stop.request_stop(); cancelled.step(stop.get_token());
        require(cancelled.stage() == TnaOffsetStage::Cancelled, "Cancellation did not stop stage 1");
        settings.offset = .2f;
        require(!cancelled.reset(cablePyramid(), settings), "Positive offset accepted");
        // The default v2 call is unchanged; explicit seeds reject size/sign errors.
        const auto network = makeOffsetCableNetwork(cablePyramid(), -.2f, true);
        VerticalReactionFit seeded;
        require(!seeded.reset(network.form, offsetCableSettings(network,1,0),0,network.yellowSupports,50,{1}),
            "Invalid density seed accepted");
        std::vector<float> wrongSign(network.form.activeFormEdges.size(),1);
        require(!seeded.reset(network.form,offsetCableSettings(network,1,0),0,network.yellowSupports,50,wrongSign),
            "Wrong tension seed sign accepted");
        // Changing only the offset must retain the TNA reference for c.
        auto changed = settings; changed.offset = -.3f;
        const auto before = synthetic.compressionReference().vertices[4].position;
        synthetic.configure(changed);
        require(synthetic.stage() == TnaOffsetStage::TnaComplete && synthetic.tnaVerified() &&
            (synthetic.compressionReference().vertices[4].position-before).length() == 0,
            "Cable controls discarded the solved TNA reference");
        require(synthetic.startCableFit(), "Cannot rerun c from retained TNA reference");
        changed.forceScale = 2; std::stop_source cableStop; cableStop.request_stop(); synthetic.step(cableStop.get_token());
        synthetic.configure(changed);
        require(synthetic.stage() == TnaOffsetStage::CompressionFitted && !synthetic.tnaVerified(),
            "TNA controls did not invalidate only stage 2");
        std::cout << "PASS manual q/t/c stage order, force diagram, density transfer, negative offset, branching, supports, controls and cancellation\n";
        if (!objPath) return 0;
        MeshObject object("tna_offset_check"); object.readFromObj(objPath);
        const auto input = weldOffsetCableMesh(*object.getMeshData());
        TnaOffsetSolver actual; TnaOffsetSettings actualSettings;
        actualSettings.fitIterations = std::max(1, fitIterations);
        actualSettings.formWeight = formWeight;
        actualSettings.horizontalIterations = horizontalIterations;
        const auto begin = std::chrono::steady_clock::now();
        require(actual.reset(input, actualSettings), "Input network reset failed");
        std::cout << "Input " << objPath << " welded vertices=" << input.vertices.size()
            << " faces=" << input.faces.size() << std::endl;
        reachFit(actual, actualSettings.horizontalIterations * 2 + actualSettings.verticalIterations + 2);
        std::cout << "compression_seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()
            << " horizontal_steps=" << actual.horizontalSteps() << " vertical_steps=" << actual.verticalSteps()
            << " compression_residual=" << actual.compressionReport().residual << std::endl;
        const auto& q = actual.compressionDensities();
        const auto bounds = std::minmax_element(q.begin(),q.end());
        std::cout << "compression_q_min=" << *bounds.first << " compression_q_max=" << *bounds.second
            << " compression_RMS_from_input=" << actual.compressionReport().rmsDesignDistance << std::endl;
        while (actual.running()) {
            const auto stepBegin = std::chrono::steady_clock::now(); actual.step(); checkNetwork(actual);
            std::cout << "fit_step_seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now()-stepBegin).count()
                << " " << actual.diagnostic() << std::endl;
        }
        std::cout << "total_seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()
            << " equilibrated=" << actual.result().success << std::endl;
        return actual.result().success ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "FAIL TNA-offset: " << e.what() << std::endl; return 1;
    }
}
} // namespace alice2
