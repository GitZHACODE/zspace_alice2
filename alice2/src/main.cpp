// alice2 - Main Entry Point
// A robust 3D scene viewer for zSpace objects

#include "../include/alice2.h"
#include <iostream>
#include <string>
#include "tna/BestFitTnaChecks.h"
#include "tna/TnaOffsetSolver.h"
#include "fdm/ProjectedMixedFDM.h"

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--check-projected-mixed-fdm")
        return alice2::fdm::runProjectedMixedFDMChecks();
    if (argc > 1 && std::string(argv[1]) == "--check-tna-offset")
        return alice2::runTnaOffsetChecks(argc > 2 ? argv[2] : nullptr, argc > 3 ? std::stoi(argv[3]) : 50,
            argc > 4 ? std::stof(argv[4]) : 1, argc > 5 ? std::stoi(argv[5]) : 10000);
    if (argc > 2 && std::string(argv[1]) == "--check-offset-cable")
        return alice2::runOffsetCableCheck(argv[2]);
    if (argc > 1 && std::string(argv[1]) == "--check-bestfit-tna")
        return alice2::runBestFitTnaChecks(argc > 2 ? argv[2] : nullptr);
    if (argc > 2 && std::string(argv[1]) == "--benchmark-bestfit-tna")
        return alice2::runBestFitTnaBenchmark(argv[2], argc > 3 && std::string(argv[3]) == "--dense");
    std::cout << "alice2 - 3D Scene Viewer" << std::endl;
    std::cout << "=========================" << std::endl;
    
    try {
        return alice2::run(argc, argv);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return -1;
    }
}
