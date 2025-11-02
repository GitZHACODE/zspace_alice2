# Alice2 User Sketches

This directory contains user sketches for the Alice2 3D viewer. User sketches allow you to create custom 3D content and interactions without modifying the core viewer code.

## Getting Started

### 1. Understanding the Sketch System

The Alice2 viewer uses a sketch-based architecture where:
- The core viewer handles window management, rendering, and input
- User sketches define custom 3D content, animations, and interactions
- Sketches are automatically loaded and integrated into the viewer

### 2. Example Sketches

This directory includes several example sketches:

- **`sketch_base.cpp`** - Basic sketch template showing the minimal structure
- **`sketch_geometry.cpp`** - Demonstrates drawing basic 3D geometry (points, lines, meshes)
- **`sketch_particles.cpp`** - Shows particle system implementation

### 3. Creating Your Own Sketch

To create a new sketch:

1. Copy `sketch_base.cpp` to a new file (e.g., `my_sketch.cpp`)
2. Modify the sketch class name and implement your custom logic
3. Rebuild the project to include your new sketch

### 4. Sketch Structure

Each sketch should inherit from the base sketch class and implement:

```cpp
class MySketch : public SketchBase {
public:
    void setup() override {
        // Initialize your sketch (called once)
    }
    
    void update() override {
        // Update logic (called every frame)
    }
    
    void draw() override {
        // Rendering logic (called every frame)
    }
    
    void onKeyPress(int key) override {
        // Handle keyboard input
    }
    
    void onMouseMove(double x, double y) override {
        // Handle mouse movement
    }
};
```

### 5. Available APIs

The Alice2 viewer provides several APIs for sketches:

- **Renderer**: Draw points, lines, meshes with `renderer.drawPoint()`, `renderer.drawLine()`, etc.
- **Math**: Vector, Matrix, and Quaternion classes for 3D math
- **Input**: Access to keyboard and mouse input
- **Camera**: Control camera position and orientation
- **Scene**: Manage 3D objects and transformations

### 6. Building Your Sketches

#### Option A: Visual Studio
1. Open `alice2.sln` in Visual Studio
2. Build the solution (Ctrl+Shift+B)
3. Run the executable from `bin/Release/alice2.exe`

#### Option B: CMake
1. Open a command prompt in this directory
2. Run: `cmake --build . --config Release`
3. Run the executable from `bin/Release/alice2.exe`

### 7. Tips and Best Practices

- Keep sketches focused on a single concept or demo
- Use meaningful names for your sketch files and classes
- Comment your code to explain complex 3D math or algorithms
- Test with both keyboard and mouse interactions
- Consider performance when drawing many objects

### 8. Coordinate System

Alice2 uses a **Z-up coordinate system** for zSpace compatibility:
- X-axis: Right
- Y-axis: Forward  
- Z-axis: Up

### 9. Need Help?

- Check the example sketches for common patterns
- Refer to the API documentation in `docs/SketchAPI.md`
- Look at the core alice2 headers in `include/alice2/`

Happy sketching!
