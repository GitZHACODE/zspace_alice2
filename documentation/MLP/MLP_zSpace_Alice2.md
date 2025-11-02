# Alice2 MLP Sketch

This directory contains the **MLP Polygon SDF Learning** sketch for the Alice2 3D viewer. This educational sketch demonstrates Multi-Layer Perceptron (MLP) learning for polygon Signed Distance Function (SDF) approximation.

## Getting Started

### 1. Understanding the MLP Sketch System

The MLP sketch showcases machine learning concepts in a visual 3D environment where:
- A Multi-Layer Perceptron learns to approximate the SDF of polygon shapes
- Real-time training visualization shows the learning process
- Interactive controls allow you to modify training parameters and visualization
- The sketch demonstrates neural network concepts in an intuitive visual way

### 2. MLP Sketch Features

The **`MLP/sketch_MLP.cpp`** educational sketch includes:

- **Multi-Layer Perceptron Implementation** - Neural network with configurable hidden layers
- **Polygon SDF Learning** - Learns to approximate signed distance functions for complex polygon shapes
- **Real-time Training Visualization** - Watch the network learn in real-time
- **Interactive Controls** - Modify training parameters and visualization options
- **Loss Function Visualization** - See training progress through loss graphs
- **Gradient Visualization** - Visualize network gradients during training
- **Contour Generation** - Generate and display SDF contour lines

![MLP Sketch Visualization](Assets/Screenshot%202025-08-26%20222504.png)
*Figure 1: MLP sketch interface showing polygon SDF learning with real-time visualization of the neural network, training samples, scalar field, and interactive controls*

### 3. MLP Sketch Structure

The MLP sketch inherits from the ISketch interface and implements:

```cpp
class MLPSketch : public ISketch {
private:
    // MLP and training data
    PolygonSDF_MLP m_mlp;
    std::vector<Vec3> m_polygon;
    std::vector<Vec3> m_training_samples;
    std::vector<float> m_sdf_ground_truth;
    
    // Training parameters
    double m_learning_rate;
    bool b_run_training;

public:
    void setup() override {
        // Initialize MLP, load polygon data, generate training samples
    }
    
    void update(float deltaTime) override {
        // Perform training steps if enabled
    }
    
    void draw(Renderer& renderer, Camera& camera) override {
        // Visualize MLP network, training data, and results
    }
    
    bool onKeyPress(unsigned char key, int x, int y) override {
        // Handle interactive controls for training and visualization
    }
};
```

### 4. Available MLP APIs

The MLP sketch provides several specialized APIs:

- **PolygonSDF_MLP**: Multi-layer perceptron implementation for SDF learning
- **ScalarField2D**: 2D scalar field generation and visualization
- **Training Methods**: `computeLoss()`, `computeGradient()`, `backward()`
- **Visualization**: Network architecture display, gradient visualization, loss graphs
- **Data Generation**: Automatic training sample generation and ground truth computation

### 5. Creating Your Own MLP Sketch

To create a new MLP-based sketch:

1. Copy `MLP/sketch_MLP.cpp` to a new file (e.g., `my_mlp_sketch.cpp`)
2. Modify the `MLPSketch` class name and implement your custom ML logic
3. Adjust the neural network architecture in `initialize_mlp()`
4. Rebuild the project to include your new sketch

![MLP Training Process](Assets/Screenshot%202025-08-26%20222434.png)
*Figure 2: MLP training in progress - observe the network learning to approximate the polygon SDF with gradient visualization and loss function monitoring*

![MLP Training Progress](Assets/Screenshot%202025-08-26%20223643.png)
*Figure 3: Advanced training stage showing improved SDF approximation with refined contour lines and converged loss values*

### 6. Building the MLP Sketch

#### Option A: Visual Studio
1. Open `alice2.sln` in Visual Studio
2. Build the solution (Ctrl+Shift+B)
3. Run the executable from `bin/Release/alice2.exe`

#### Option B: CMake
1. Open a command prompt in this directory
2. Run: `cmake --build . --config Release`
3. Run the executable from `bin/Release/alice2.exe`

### 7. Interactive Controls

The MLP sketch supports real-time interaction:

- **'R'** - Toggle training (start/stop neural network training)
- **'U'** - Update field (regenerate visualization)
- **'F'** - Toggle field visualization (show/hide scalar field)
- **'P'** - Toggle polygon (show/hide input polygon)
- **'T'** - Toggle training samples (show/hide training data points)
- **'C'** - Toggle centers (show/hide fitted centers)
- **'G'** - Toggle gradients (show/hide gradient visualization)
- **'M'** - Toggle MLP visualization (show/hide network architecture)
- **'L'** - Toggle loss graph (show/hide training loss)
- **'N'** - Toggle contours (show/hide SDF contour lines)
- **'K'/'k'** - Increase/decrease number of neural network centers

### 8. MLP Training Process

The sketch demonstrates a complete machine learning pipeline:

1. **Data Preparation**: Load polygon data and generate training samples
2. **Network Initialization**: Create MLP with configurable architecture
3. **Ground Truth Generation**: Compute true SDF values for training
4. **Training Loop**: Iterative gradient descent with backpropagation
5. **Visualization**: Real-time display of learning progress
6. **Evaluation**: Visual comparison of learned vs. ground truth SDF

### 9. Educational Concepts

This sketch teaches several key ML concepts:

- **Neural Networks**: Multi-layer perceptron architecture and forward propagation
- **Backpropagation**: Gradient computation and weight updates
- **Loss Functions**: Training objective and optimization
- **Overfitting/Underfitting**: Network capacity vs. data complexity
- **Hyperparameters**: Learning rate, network size, training iterations
- **Visualization**: Making ML concepts visible and intuitive

### 10. Customization Options

You can modify the sketch to explore different scenarios:

- **Network Architecture**: Change hidden layer sizes in `initialize_mlp()`
- **Learning Rate**: Adjust `m_learning_rate` for different convergence behaviors
- **Polygon Shapes**: Load different polygon data or create procedural shapes
- **Training Data**: Modify sampling strategies in `generate_training_data()`
- **Visualization**: Add custom rendering for specific ML concepts

### 11. Coordinate System

Alice2 uses a **Z-up coordinate system** for zSpace compatibility:
- X-axis: Right
- Y-axis: Forward  
- Z-axis: Up

The MLP operates in 2D (X-Y plane) for polygon SDF learning.
