#include "BestFitTnaChecks.h"
#include "BestFitTnaSolver.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace alice2 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
TnaFormDiagram pyramid(bool tension = false) {
    MeshData mesh;
    for (Vec3 p : {Vec3(-1,-1,0), Vec3(1,-1,0), Vec3(1,1,0), Vec3(-1,1,0), Vec3(0,0,1)})
        mesh.vertices.emplace_back(p);
    for (int i = 0; i < 4; ++i) mesh.faces.emplace_back(std::vector<int>{i,(i+1)%4,4});
    auto form = TnaSolver{}.makeFormDiagram(mesh, {0,1,2,3});
    if (tension) for (auto& e : form.activeFormEdges)
        e.isTension = e.vertexA == 0 || e.vertexB == 0 || e.vertexA == 2 || e.vertexB == 2;
    return form;
}
void fit(BestFitTnaSolver& solver) {
    require(solver.startFit(), "Could not start fitting");
    for (int i = 0; i < 200 && solver.running(); ++i) solver.step();
    std::cout << solver.diagnostic() << '\n';
    require(!solver.running(), "Fit exceeded step budget");
    require(solver.fitResult().success, "No valid fitted shape");
    require(solver.report().finite && solver.report().residual <= 1e-3, "Fitted residual too large");
}
void refine(BestFitTnaSolver& solver, int iterations = 500) {
    const MeshData before = solver.fitResult().mesh;
    require(solver.startTna(iterations), "Could not start TNA");
    for (int i = 0; i < iterations + 200 && solver.running(); ++i) solver.step();
    for (std::size_t i = 0; i < before.vertices.size(); ++i) {
        require(before.vertices[i].position.x == solver.mesh().vertices[i].position.x &&
                before.vertices[i].position.y == solver.mesh().vertices[i].position.y, "TNA changed fitted XY");
    }
    std::cout << solver.diagnostic() << '\n';
}
} // namespace

int runBestFitTnaChecks(const char* objPath) {
    try {
        BestFitForceDensitySettings settings;
        settings.loads.density = 0; settings.loads.nodalLoad = 4;
        for (bool tension : {false, true}) {
            auto form = pyramid(tension);
            require(form.success, "Pyramid topology failed");
            BestFitTnaSolver solver;
            require(solver.reset(form, settings), "Pyramid reset failed");
            require(!solver.startTna(), "TNA ran before fit");
            fit(solver);
            require(solver.report().maximumDesignDistance < 1e-4, "Known-equilibrium design moved");
            for (std::size_t i = 0; i < solver.edges().size(); ++i)
                require(solver.edges()[i].isTension ? solver.densities()[i] < 0 : solver.densities()[i] > 0,
                        "Density sign violated");
            const auto densities = solver.densities();
            refine(solver);
            require(solver.stage() == BestFitTnaStage::Complete && solver.exportable(), "Known equilibrium was not verified");
            require(solver.densities() == densities, "Verified equilibrium densities changed");
            // Force a real horizontal/vertical pass, bypassing the tolerance-based no-op.
            require(solver.startTna(200, 3.0f, 0.05f), "Could not start constrained refinement");
            for (int i = 0; i < 400 && solver.running(); ++i) solver.step();
            require(!solver.running(), "Constrained refinement exceeded budget");
            std::cout << solver.diagnostic() << '\n';
            require(solver.stage() == BestFitTnaStage::Complete, "Active horizontal/vertical solve did not converge");
            if (solver.stage() == BestFitTnaStage::Complete)
                require(solver.report().residual <= settings.loads.residualTolerance, "False convergence");
            for (std::size_t i = 0; i < form.mesh.vertices.size(); ++i) {
                const auto& p = solver.fitResult().mesh.vertices[i].position;
                require(p.x == solver.mesh().vertices[i].position.x && p.y == solver.mesh().vertices[i].position.y,
                        "Constrained refinement changed XY");
            }
        }
        auto bad = pyramid(); bad.supportVertices.clear();
        BestFitForceDensitySolver fitSolver;
        require(!fitSolver.reset(bad, settings), "Missing supports accepted");
        bad = pyramid(); bad.mesh.vertices.emplace_back(Vec3(10,10,10));
        require(!fitSolver.reset(bad, settings), "Disconnected free vertex accepted");
        bad = pyramid(); bad.mesh.vertices[4].position.z = std::numeric_limits<float>::quiet_NaN();
        require(!fitSolver.reset(bad, settings), "Nonfinite geometry accepted");
        bad = pyramid(); auto invalid = settings; invalid.loads.nodalLoads = {1};
        require(!fitSolver.reset(bad, invalid), "Invalid load array accepted");
        auto seeded = TnaSolver{}.makeForceDiagram(bad, {1});
        require(!seeded.success, "Invalid seed mapping accepted");
        // The pyramid is underdetermined in q; its success above exercises
        // rank-deficient least squares without imposing an artificial ridge.
        std::cout << "PASS synthetic equilibrium, signed/rank-deficient fit, transfer, fixed XY and invalid inputs\n";

        if (objPath) {
            MeshObject object("bestfit_check"); object.readFromObj(objPath); object.weld(1e-3f);
            const auto original = *object.getMeshData();
            std::vector<int> black, red;
            for (int i = 0; i < static_cast<int>(original.vertices.size()); ++i) {
                const auto& c = original.vertices[i].color;
                if (c.r <= .02f && c.g <= .02f && c.b <= .02f) black.push_back(i);
                else if (c.r >= .8f && c.g <= .2f && c.b <= .2f) red.push_back(i);
            }
            TnaSolver topology; auto form = topology.makeFormDiagram(original, black);
            require(form.success && topology.setOuterBoundaryEdgesTension(form), "OBJ form/tension topology failed");
            settings = {}; settings.loads.nodalLoad = .001f;
            settings.loads.additionalFixedVertices = red; settings.loads.unloadedFaces = form.exteriorFormFaces;
            BestFitTnaSolver solver;
            require(solver.reset(form, settings), "OBJ reset failed");
            require(solver.startFit(), "OBJ fitting failed to start");
            for (int i = 0; i < 200 && solver.running(); ++i) {
                solver.step();
                std::cout << solver.diagnostic() << std::endl;
            }
            require(solver.fitResult().success, "OBJ did not produce a valid fitted shape");
            require(solver.report().residual <= settings.loads.residualTolerance, "OBJ fitted residual too large");
            for (int v : black) require((solver.mesh().vertices[v].position - original.vertices[v].position).length() == 0,
                                        "Black support moved");
            for (int v : red) require(solver.mesh().vertices[v].position.z == original.vertices[v].position.z, "Red height moved");
            refine(solver, 5000);
            require(solver.stage() == BestFitTnaStage::Complete, "OBJ TNA did not equilibrate");
            for (int v : black) require((solver.mesh().vertices[v].position - original.vertices[v].position).length() == 0,
                                        "TNA black support moved");
            for (int v : red) require(solver.mesh().vertices[v].position.z == original.vertices[v].position.z, "TNA red height moved");
            solver.restoreFit();
            require(solver.stage() == BestFitTnaStage::Fitted && solver.exportable(), "Restoring fit failed");
            require(solver.startTna(1, 0.0f, 1.0f), "Could not start limited-iteration test");
            solver.step();
            require(solver.stage() == BestFitTnaStage::Failed && !solver.exportable(), "Iteration exhaustion falsely reported equilibrium");
            std::cout << solver.diagnostic() << '\n';
            solver.restoreFit();
            std::cout << "PASS actual OBJ, supports, self-weight, stage transfer and fixed XY\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL best-fit TNA: " << error.what() << std::endl; return 1;
    }
}
} // namespace alice2
