#pragma once

#ifndef ALICE2_H
#define ALICE2_H

// alice2 - A robust 3D scene viewer for zSpace objects
// Namespace: alice2

// Core includes
#include "../src/core/Application.h"
#include "../src/core/Scene.h"
#include "../src/core/Renderer.h"
#include "../src/core/Camera.h"
#include "../src/core/Transform.h"

// Object includes
#include "../src/objects/SceneObject.h"
#include "../src/objects/ZSpaceObject.h"
#include "../src/objects/PrimitiveObject.h"

// Input includes
#include "../src/input/InputManager.h"
#include "../src/input/CameraController.h"

// Sketch includes
#include "../src/sketches/SketchManager.h"
#include "../src/sketches/ISketch.h"

// UI includes
#include "../src/ui/SimpleUI.h"

// Utility includes
#include "../src/utils/Math.h"
#include "../src/utils/OpenGL.h"

// Convenience namespace
namespace alice2 {
    // Main application entry point
    int run(int argc, char** argv);
}

#endif // ALICE2_H
