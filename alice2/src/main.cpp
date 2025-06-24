// alice2 - Main Entry Point
// A robust 3D scene viewer for zSpace objects

#include "../include/alice2.h"
#include <iostream>

int main(int argc, char** argv) {
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
