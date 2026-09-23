#include "BestFitTnaChecks.h"
#include "BestFitTnaSolver.h"
#include "BestFitTnaWorker.h"
#include <cmath>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace alice2 {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void checkFit(const TnaFormDiagram& design, const BestFitForceDensitySettings& settings,
              const BestFitForceDensityResult& result) {
    require(result.success && result.report.finite, "No finite fitted result");
    const auto measured = measureTnaEquilibrium(result.mesh, design.mesh, design.activeFormEdges,
        result.forceDensities, design.supportVertices, settings.loads,
        settings.allowSlidingTensionSupports, settings.slidingSupportPenalty);
    require(measured.finite && measured.residual <= settings.loads.residualTolerance, "Fitted residual too large");
    const auto sliding = slidingSupports(design.supportVertices, design.activeFormEdges,
        static_cast<int>(design.mesh.vertices.size()), settings.allowSlidingTensionSupports);
    for (int v : design.supportVertices) {
        const auto& p = result.mesh.vertices[v].position;
        const auto& original = design.mesh.vertices[v].position;
        require(p.z == original.z, "Support height moved");
        if (std::find(sliding.begin(), sliding.end(), v) == sliding.end())
            require(p.x == original.x && p.y == original.y, "Fixed support moved");
    }
    for (int v : settings.loads.additionalFixedVertices)
        require(result.mesh.vertices[v].position.z == design.mesh.vertices[v].position.z, "Red height moved");
    for (std::size_t i = 0; i < result.forceDensities.size(); ++i) {
        const double magnitude = (design.activeFormEdges[i].isTension ? -1.0 : 1.0) * result.forceDensities[i];
        require(magnitude >= settings.minimumDensityMagnitude * (1.0 - 1e-6), "Density bound/sign violated");
    }
    for (int i : settings.nearZeroEdges)
        require(std::abs(std::abs(result.forceDensities[i]) - settings.minimumDensityMagnitude) <
                settings.minimumDensityMagnitude * 1e-6, "Near-zero density changed");
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

int runBestFitTnaBenchmark(const char* objPath, bool dense) {
    try {
        MeshObject object("bestfit_benchmark");
        object.readFromObj(objPath); object.weld(1e-3f);
        const auto& mesh = *object.getMeshData();
        std::vector<int> black, red;
        for (int i = 0; i < static_cast<int>(mesh.vertices.size()); ++i) {
            const auto& c = mesh.vertices[i].color;
            if (c.r <= .02f && c.g <= .02f && c.b <= .02f) black.push_back(i);
            else if (c.r >= .8f && c.g <= .2f && c.b <= .2f) red.push_back(i);
        }
        TnaSolver topology;
        auto form = topology.makeFormDiagram(mesh, black);
        require(form.success && !form.supportVertices.empty() &&
                topology.setOuterBoundaryEdgesTension(form, form.supportVertices.front()), "Benchmark topology failed");
        BestFitForceDensitySettings settings;
        settings.loads.nodalLoad = .001f;
        settings.loads.additionalFixedVertices = red;
        settings.loads.unloadedFaces = form.exteriorFormFaces;
        settings.slidingSupportPenalty = 50.0; // Current sketch default.
        settings.sparseDensityFit = !dense;
        BestFitForceDensitySolver solver;
        require(solver.reset(form, settings), "Benchmark reset failed");
        std::cout << "Benchmark vertices=" << mesh.vertices.size()
                  << " edges=" << form.activeFormEdges.size() << " backend=" << (dense ? "dense" : "sparse") << std::endl;
        const auto begin = std::chrono::steady_clock::now();
        double previousRms = std::numeric_limits<double>::infinity();
        while (solver.result().status == BestFitStatus::Running) {
            const auto stepBegin = std::chrono::steady_clock::now();
            solver.step();
            const auto& r = solver.result();
            if (r.success) {
                checkFit(form, settings, r);
                require(r.report.rmsDesignDistance <= previousRms, "Benchmark fit regressed");
                previousRms = r.report.rmsDesignDistance;
            }
            std::cout << std::setprecision(10) << "iteration=" << r.iteration
                      << " seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - stepBegin).count()
                      << " residual=" << r.report.residual << " rms=" << r.report.rmsDesignDistance << std::endl;
        }
        std::cout << "total_seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count()
                  << " " << solver.result().diagnostic << std::endl;
        std::cout << "density_seconds=" << solver.result().densityFitSeconds
                  << " equilibrium_seconds=" << solver.result().equilibriumSeconds
                  << " trials=" << solver.result().equilibriumTrials
                  << " density_fallbacks=" << solver.result().densityFallbacks << std::endl;
        require(solver.result().success && solver.result().report.residual <= settings.loads.residualTolerance,
                "Benchmark failed to produce equilibrium");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL benchmark: " << error.what() << std::endl; return 1;
    }
}

int runBestFitTnaChecks(const char* objPath) {
    try {
        // Start faces at the hole and vary their order: the supported ring,
        // not the first boundary encountered, must receive outer tension.
        for (bool reverseOrder : {false, true}) {
            MeshData annulus;
            for (Vec3 p : {Vec3(-1,-1,0), Vec3(1,-1,0), Vec3(1,1,0), Vec3(-1,1,0),
                           Vec3(-3,-3,0), Vec3(3,-3,0), Vec3(3,3,0), Vec3(-3,3,0)})
                annulus.vertices.emplace_back(p);
            for (int i = 0; i < 4; ++i)
                annulus.faces.emplace_back(std::vector<int>{i, i + 4, (i + 1) % 4 + 4, (i + 1) % 4});
            if (reverseOrder) std::reverse(annulus.faces.begin(), annulus.faces.end());
            TnaSolver topology;
            auto form = topology.makeFormDiagram(annulus, {4, 6});
            require(form.success && topology.setOuterBoundaryEdgesTension(form, form.supportVertices.front()),
                    "Supported outer ring selection failed");
            int tensionCount = 0;
            for (const auto& edge : form.activeFormEdges) {
                const bool outer = edge.vertexA >= 4 && edge.vertexB >= 4;
                require(edge.isTension == outer, "Outer selection marked an inner ring or missed the supported ring");
                if (edge.isTension) ++tensionCount;
            }
            require(tensionCount == 4, "Supported outer ring is incomplete");
        }
        std::cout << "PASS black-supported outer ring, inner ring exclusion and face-order independence\n";
        BestFitForceDensitySettings settings;
        settings.loads.density = 0; settings.loads.nodalLoad = 4;
        for (bool tension : {false, true}) {
            auto form = pyramid(tension);
            require(form.success, "Pyramid topology failed");
            BestFitTnaSolver solver;
            require(solver.reset(form, settings), "Pyramid reset failed");
            require(!solver.startTna(), "TNA ran before fit");
            fit(solver);
            if (!tension) require(solver.fitResult().densityFallbacks > 0,
                                  "Rank-deficient fixture did not exercise COD fallback");
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
        // A nonfunicular grid requires repeated fits, exercising warm starts,
        // unequal XY/Z free sets, signed members, and density bounds.
        MeshData grid;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            grid.vertices.emplace_back(Vec3(float(x), float(y),
                float(.3 * (x * (3 - x) + y * (3 - y)) + .1 * std::sin(x + 2 * y))));
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) {
            const int v = 4 * y + x;
            grid.faces.emplace_back(std::vector<int>{v, v + 1, v + 5, v + 4});
        }
        bool exercisedWarmStart = false;
        for (bool sliding : {false, true}) for (bool nearZero : {false, true}) {
            auto form = TnaSolver{}.makeFormDiagram(grid, {0, 3, 12, 15});
            require(form.success, "Grid topology failed");
            if (sliding) require(TnaSolver{}.setOuterBoundaryEdgesTension(form), "Grid tension marks failed");
            BestFitForceDensitySettings gridSettings;
            gridSettings.loads.nodalLoad = .1f;
            gridSettings.loads.additionalFixedVertices = {1};
            gridSettings.loads.unloadedFaces = form.exteriorFormFaces;
            gridSettings.allowSlidingTensionSupports = sliding;
            gridSettings.slidingSupportPenalty = 50;
            if (nearZero) gridSettings.nearZeroEdges = {0};
            BestFitForceDensitySolver gridSolver;
            require(gridSolver.reset(form, gridSettings), "Grid reset failed");
            double previous = std::numeric_limits<double>::infinity();
            while (gridSolver.result().status == BestFitStatus::Running) {
                gridSolver.step();
                checkFit(form, gridSettings, gridSolver.result());
                require(gridSolver.result().report.rmsDesignDistance <= previous, "Grid design fit regressed");
                previous = gridSolver.result().report.rmsDesignDistance;
            }
            exercisedWarmStart = exercisedWarmStart || gridSolver.result().iteration > 1;
            auto denseSettings = gridSettings;
            denseSettings.sparseDensityFit = false;
            BestFitForceDensitySolver reference;
            require(reference.reset(form, denseSettings), "Dense grid reset failed");
            while (reference.result().status == BestFitStatus::Running) reference.step();
            checkFit(form, denseSettings, reference.result());
            require(gridSolver.result().report.rmsDesignDistance <= reference.result().report.rmsDesignDistance + 1e-4,
                    "Sparse grid fit worse than dense reference");
            // Reset invalidates topology/reference caches and accumulated timing.
            require(gridSolver.reset(pyramid(), settings), "Cached solver reset failed");
            require(gridSolver.result().equilibriumTrials == 0, "Reset retained profiling state");
            gridSolver.step();
            checkFit(pyramid(), settings, gridSolver.result());
        }
        require(exercisedWarmStart, "Grid did not exercise repeated density updates");
        // Cancellation before work and after an accepted result retains valid
        // state and must never fall through to an expensive dense retry.
        auto cancelSettings = settings;
        cancelSettings.maximumIterations = 50;
        auto cancelForm = TnaSolver{}.makeFormDiagram(grid, {0, 3, 12, 15});
        cancelSettings.loads.unloadedFaces = cancelForm.exteriorFormFaces;
        BestFitForceDensitySolver cancelled;
        require(cancelled.reset(cancelForm, cancelSettings), "Cancellation reset failed");
        std::stop_source stop;
        stop.request_stop();
        cancelled.step(stop.get_token());
        require(cancelled.result().status == BestFitStatus::Cancelled && cancelled.result().iteration == 0,
                "Pre-cancelled fit did work");
        require(cancelled.reset(cancelForm, cancelSettings), "Cancellation restart failed");
        cancelled.step();
        require(cancelled.result().status == BestFitStatus::Running, "Cancellation fixture needs another update");
        const auto accepted = cancelled.result().forceDensities;
        cancelled.step(stop.get_token());
        require(cancelled.result().status == BestFitStatus::Cancelled && cancelled.result().forceDensities == accepted,
                "Cancellation lost accepted densities");
        checkFit(cancelForm, cancelSettings, cancelled.result());
        // Exercise the same snapshot/cancel/restart worker used by the sketch.
        BestFitTnaWorker worker;
        BestFitTnaSolver background;
        require(background.reset(cancelForm, cancelSettings) && background.startFit(), "Worker setup failed");
        worker.start(background);
        worker.requestStop();
        const auto waitForWorker = [&]() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (std::chrono::steady_clock::now() < deadline) {
                if (auto snapshot = worker.poll()) {
                    if (snapshot->stage() != BestFitTnaStage::Fitting) return *snapshot;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            throw std::runtime_error("Background fit did not finish/cancel");
        };
        const auto stopped = waitForWorker();
        require(stopped.fitResult().status == BestFitStatus::Cancelled, "Worker ignored cancellation");
        require(background.reset(pyramid(), settings) && background.startFit(), "Worker restart failed");
        worker.start(background);
        const auto finished = waitForWorker();
        require(finished.stage() == BestFitTnaStage::Fitted && finished.mesh().vertices.size() == 5,
                "Worker published stale result after restart");
        checkFit(pyramid(), settings, finished.fitResult());
        worker.discard();
        require(!worker.poll(), "Discard retained a stale snapshot");
        std::cout << "PASS sparse/dense comparison, cancellation and worker restart\n";
        std::cout << "PASS repeated fits, sliding/fixed/red supports, near-zero bounds and cache reset\n";
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
            require(form.success && !form.supportVertices.empty() &&
                    topology.setOuterBoundaryEdgesTension(form, form.supportVertices.front()), "OBJ form/tension topology failed");
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
            checkFit(form, settings, solver.fitResult());
            for (int v : red) require(solver.mesh().vertices[v].position.z == original.vertices[v].position.z, "Red height moved");
            refine(solver, 5000);
            require(solver.stage() == BestFitTnaStage::Complete, "OBJ TNA did not equilibrate");
            for (int v : black) require((solver.mesh().vertices[v].position - solver.fitResult().mesh.vertices[v].position).length() == 0,
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
