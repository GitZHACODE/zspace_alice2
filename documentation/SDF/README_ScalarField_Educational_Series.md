# Scalar Field Educational Series

This directory contains 4 educational sketches demonstrating progressive concepts in scalar field usage with alice2. Each sketch is a complete, standalone C++ file that can be built and run independently.

## Common Features

All sketches follow consistent design patterns:

### Field Specifications
- **Dimensions**: 100x100 grid with world bounds (-50, -50) to (50, 50)
- **Coordinate System**: Rectangle centers at (0, 0, 0), Circle centers at (15, 10, 0)
- **Grid Display**: Disabled for cleaner visualization
- **Naming Conventions**:
  - Computation controls: prefix with "b_" (e.g., `b_computeCircle`)
  - Visualization controls: prefix with "d_" (e.g., `d_drawField`)

### Standard Controls
- **d_drawField**: Toggle scalar field visualization
- **d_drawValues**: Toggle value display as 3D text
- **Single Contour Lines**: Clean, focused contour visualization
- **FPS Display**: Real-time performance monitoring
- **Clear UI**: On-screen instructions and status indicators

## Sketch Descriptions

### Sketch 1: Basic Field Construction
**File**: `sketch_scalarField_01_basic.cpp`

**Purpose**: Introduction to basic scalar field generation and visualization

**Features**:
- Circle and rectangle scalar field generation
- Smooth animations using sin/cos functions for radius and dimensions
- Single animated contour line with sine-wave offset values
- 3D text labels at geometric centers ("CIRCLE" or "RECT")
- Real-time switching between field types

**Controls**:
- `G`: Toggle between circle and rectangle modes
- `C`: Toggle contour visualization
- `F`: Toggle field point visualization
- `V`: Toggle scalar value display

**Learning Objectives**:
- Understanding basic SDF (Signed Distance Field) concepts
- Visualizing scalar field data as colored points
- Observing contour extraction at different threshold levels
- Seeing smooth animation techniques for field parameters

---

### Sketch 2: Boolean Operations
**File**: `sketch_scalarField_02_boolean.cpp`

**Purpose**: Demonstrate boolean operations between geometric shapes

**Features**:
- Base rectangle field (40x30 units) at center (0, 0)
- Four corner circles with animated radii
- Dynamic boolean operations based on radius:
  - Small radius circles: `boolean_union` (green indicators)
  - Large radius circles: `boolean_subtract` (red indicators)
- Single contour line visualization
- Visual indicators showing operation types (U for union, S for subtract)
- Preview modes for individual operation types

**Controls**:
- `B`: Toggle boolean computation
- `U`: Preview union operations only
- `S`: Preview subtract operations only
- `F`: Toggle field visualization
- `C`: Toggle contours

**Learning Objectives**:
- Understanding boolean operations on scalar fields
- Visualizing union vs. subtract operations
- Seeing how multiple operations combine
- Learning strategic placement of geometric elements

---

### Sketch 3: SDF Blending and Tower Visualization
**File**: `sketch_scalarField_03_blending.cpp`

**Purpose**: Advanced blending techniques and 3D visualization concepts

**Features**:
- Two overlapping fields: rectangle (lower) and circle (upper)
- Smooth minimum (smin) blending with adjustable blend factor
- Multi-level contour extraction at 20 Z-levels: 0, 3, 6, 9, ..., 57
- Tower visualization: stacked contours with magenta-to-purple gradient
- Side-by-side display with improved separation to prevent overlap
- Interactive blend factor adjustment
- Single contour line visualization for main field

**Controls**:
- `B`: Toggle blend computation
- `T`: Toggle tower visualization mode
- `+`/`-`: Adjust blend factor (0.2 to 5.0)
- `F`: Toggle field visualization
- `C`: Toggle contours

**Learning Objectives**:
- Understanding smooth blending vs. hard boolean operations
- Visualizing 3D concepts through contour stacking
- Interactive parameter adjustment effects
- Advanced visualization techniques

---

### Sketch 4: Directional Boolean Operations with Sun Vector
**File**: `sketch_scalarField_04_directional.cpp`

**Purpose**: Dynamic operations based on environmental factors

**Features**:
- Base setup similar to Sketch 3 with rotated upper rectangle (30°)
- Animated 2D sun direction vector with visual arrow indicator
- Dynamic boolean operations based on sun exposure:
  - High exposure faces (>0.7): Subtract circles on boundary edges
  - Medium exposure faces (0.3-0.7): Union circles on boundary edges
  - Low exposure faces (<0.3): Multiple union circles on boundary edges
- Circle positioning directly on rectangle boundary edges (no offset)
- Fixed radius circles (no animation) for cleaner visualization
- Face normal calculation and dot product exposure determination
- Tower contour visualization with 20 levels and magenta-to-purple gradient
- Manual sun direction control with arrow keys

**Controls**:
- `S`: Toggle sun animation
- `Arrow Keys`: Manual sun direction control (when animation off)
- `D`: Toggle directional computation
- `T`: Toggle tower visualization
- `F`: Toggle field visualization
- `C`: Toggle contours

**Learning Objectives**:
- Environmental influence on procedural generation
- Vector mathematics in practical applications
- Dynamic system behavior based on external parameters
- Advanced procedural techniques

## Building and Running

### Build All Sketches
```bash
cd alice2
.\build.bat
```

### Run the Viewer
```bash
.\run.bat
```

### Sketch Selection
Once the alice2 viewer is running, you can switch between sketches using the built-in sketch selection system. Each sketch will be automatically registered and available in the sketch menu.

## Educational Progression

The sketches are designed to be studied in order:

1. **Basic Construction** → Understanding fundamental concepts
2. **Boolean Operations** → Combining multiple shapes
3. **SDF Blending** → Advanced blending and 3D visualization
4. **Directional Operations** → Dynamic, environment-driven systems

Each sketch builds upon concepts from previous ones while introducing new techniques and visualization methods.

## Technical Notes

- All sketches use the modern ScalarField2D API with RAII principles
- Consistent error handling and performance considerations
- Proper OpenGL state management through alice2's renderer
- Educational comments and clear variable naming throughout
- Frame rate monitoring for performance awareness

## Extending the Series

These sketches provide a foundation for further exploration:
- Add noise-based field generation
- Implement field-based particle systems
- Create interactive field editing tools
- Develop real-time field deformation systems
