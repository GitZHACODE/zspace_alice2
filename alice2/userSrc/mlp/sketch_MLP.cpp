// alice2 MLP Educational Sketch
// Demonstrates Multi-Layer Perceptron learning for polygon SDF approximation

//#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <ML/polygonSDF_MLP.h>
#include <objects/GraphObject.h>
#include <fstream>
#include <charconv>
#include <set>

using namespace alice2;

class MLPSketch : public ISketch
{
private:
    // MLP and training data
    PolygonSDF_MLP m_mlp;
    std::vector<float> m_mlp_input_data;
    std::vector<float> m_gradients;
    std::vector<Vec3> m_polygon;
    std::vector<Vec3> m_training_samples;
    std::vector<float> m_sdf_ground_truth;
    GraphObject m_contours;
    float building_width = 30.0f;
    float building_height = 40.0f;
    int NUM_CENTERS;
    std::string path_polygon = "C://Users//taizhong_chen//source//repos//GitZHCODE//zspace_alice2//alice2//data/PARCEL.txt";

    // Training parameters
    double m_learning_rate;
    float m_time;

    // Boolean flags for computation controls (prefix with "b_")
    bool b_run_training;

    // Boolean flags for visualization controls (prefix with "d_")
    bool d_draw_field;
    bool d_draw_gradients;
    bool d_draw_loss_graph;
    bool d_draw_centers;
    bool d_draw_training_samples;
    bool d_draw_polygon;
    bool d_draw_mlp_viz;
    bool d_draw_contours;

public:
    MLPSketch()
        : m_learning_rate(0.1)
        , m_time(0.0f)
        , b_run_training(false)
        , d_draw_field(true)
        , d_draw_gradients(false)
        , d_draw_loss_graph(true)
        , d_draw_centers(true)
        , d_draw_training_samples(true)
        , d_draw_polygon(true)
        , d_draw_mlp_viz(true)
        , d_draw_contours(true)
        , NUM_CENTERS(8)
    {}

    ~MLPSketch() = default;

    // Sketch information
    std::string getName() const override
    {
        return "MLP Polygon SDF Learning";
    }

    std::string getDescription() const override
    {
        return "Educational sketch demonstrating MLP learning for polygon SDF approximation";
    }

    std::string getAuthor() const override
    {
        return "alice2 Educational Series";
    }

    // Sketch lifecycle
    void setup() override
    {
        // Set scene configuration following established patterns
        scene().setBackgroundColor(Color(1.0f, 1.0f, 1.0f));
        scene().setShowGrid(false);  // Disabled for cleaner visualization
        scene().setGridSize(25.0f);
        scene().setGridDivisions(4);
        scene().setShowAxes(true);
        scene().setAxesLength(10.0f);

        std::cout << "MLP Polygon SDF Learning loaded" << std::endl;
        std::cout << "Educational sketch demonstrating MLP learning for polygon SDF approximation" << std::endl;

        // Initialize MLP and load data
        Vec3 minBB(-50, -50, 0), maxBB(50, 50, 0);

        // Initialize MLP building size
        m_mlp.building_width = building_width;
        m_mlp.building_height = building_height;

        initialize_mlp();
        load_polygon_data(minBB, maxBB);
        initialize_ground_truth_field(minBB, maxBB, 100);
        generate_training_data();

        std::cout << "Setup complete - Ready for MLP training" << std::endl;
    }

    void update(float deltaTime) override
    {
        // Update animation time
        m_time += deltaTime;

        // Run MLP training if enabled
        if (b_run_training)
        {
            perform_training_step();
        }
    }

    void draw(Renderer& renderer, Camera& camera) override
    {
        // Draw contour lines
        if (d_draw_contours) {
            draw_contours(renderer);
        }

        // Draw the generated scalar field
        if (d_draw_field) {
            draw_scalar_field(renderer);
        }

        // Draw polygon boundary
        if (d_draw_polygon) {
            draw_polygon(renderer);
        }

        // Draw training samples as blue points
        if (d_draw_training_samples) {
            draw_training_samples(renderer);
        }

        // Draw fitted centers as red circles
        if (d_draw_centers) {
            draw_fitted_centers(renderer);
        }

        // Draw gradients if we have MLP input data
        if (d_draw_gradients) {
            draw_gradients(renderer);
        }

        // Draw MLP network visualization
        if (d_draw_mlp_viz) {
            draw_mlp_visualization(renderer, camera);
        }

        // Draw loss information
        if (d_draw_loss_graph) {
            draw_loss_information(renderer);
        }

        // Draw UI and controls
        draw_ui(renderer);
    }

    void cleanup() override
    {
        std::cout << "MLP Polygon SDF Learning cleanup" << std::endl;
    }

    // Input handling (optional)
    bool onKeyPress(unsigned char key, int x, int y) override
    {
        switch (key)
        {
        case 27: // ESC key
            return false; // Not handled - allow default exit
        case 'u':
        case 'U':
            update_field();
            return true;
        case 'r':
        case 'R':
            b_run_training = !b_run_training;
            std::cout << "Training " << (b_run_training ? "started" : "stopped") << std::endl;
            return true;
        case 'g':
        case 'G':
            d_draw_gradients = !d_draw_gradients;
            return true;
        case 'l':
        case 'L':
            d_draw_loss_graph = !d_draw_loss_graph;
            return true;
        case 'f':
        case 'F':
            d_draw_field = !d_draw_field;
            return true;
        case 'c':
        case 'C':
            d_draw_centers = !d_draw_centers;
            return true;
        case 't':
        case 'T':
            d_draw_training_samples = !d_draw_training_samples;
            return true;
        case 'p':
        case 'P':
            d_draw_polygon = !d_draw_polygon;
            return true;
        case 'm':
        case 'M':
            d_draw_mlp_viz = !d_draw_mlp_viz;
            return true;
        // case 'n':
        // case 'N':
        //     d_draw_contours = !d_draw_contours;
            return true;
        case 'k': NUM_CENTERS++;
            setup();
            return true;
        case 'K': NUM_CENTERS--;
            setup();
            return true;
        }
        return false; // Not handled
    }

    bool onMousePress(int button, int state, int x, int y) override
    {
        // Handle mouse button input
        // button: 0=left, 1=middle, 2=right
        // state: 0=down, 1=up
        return false; // Not handled - allow default camera controls
    }

    bool onMouseMove(int x, int y) override
    {
        // Handle mouse movement
        return false; // Not handled - allow default camera controls
    }

private:
    // Helper methods for initialization
    void initialize_mlp()
    {
        int input_dim = NUM_CENTERS * 4;
        int output_dim = NUM_CENTERS * 4;
        std::vector<int> hidden_dims = {16};

        m_mlp = PolygonSDF_MLP(input_dim, hidden_dims, output_dim);
        m_mlp_input_data.assign(input_dim, 1.0f);
        m_mlp.number_sdf = NUM_CENTERS;

        std::cout << "MLP initialized with " << NUM_CENTERS << " centers" << std::endl;
    }

    void load_polygon_data(Vec3& minBB, Vec3& maxBB)
    {
        // Use relative path from alice2 directory
        std::string filename = path_polygon;
        load_polygon_from_csv(filename, m_polygon);

        m_mlp.polygon = m_polygon;

        computeAABB(m_polygon, minBB, maxBB);

        std::cout << "Polygon loaded with " << m_polygon.size() << " vertices" << std::endl;
    }

    void computeAABB(const std::vector<Vec3>& pts, Vec3& outMin, Vec3& outMax) {
    if (pts.empty()) throw std::runtime_error("Empty polygon!");
    // start both at the first point
    outMin = outMax = pts[0];

    for (const auto& p : pts) {
        outMin.x = std::min(outMin.x, p.x);
        outMin.y = std::min(outMin.y, p.y);
        outMin.z = std::min(outMin.z, p.z);
        
        outMax.x = std::max(outMax.x, p.x);
        outMax.y = std::max(outMax.y, p.y);
        outMax.z = std::max(outMax.z, p.z);
    }
}

    void generate_training_data()
    {
        m_mlp.samplePoints(m_training_samples, m_sdf_ground_truth, m_polygon);

        m_mlp.trainingSamples = m_training_samples;
        m_mlp.sdfGT = m_sdf_ground_truth;
        m_mlp.losses.resize(m_sdf_ground_truth.size());
        m_mlp.losses_ang.resize(m_sdf_ground_truth.size());

        std::cout << "Generated " << m_training_samples.size() << " training samples" << std::endl;
    }

    void initialize_ground_truth_field(const Vec3& minBB, const Vec3 maxBB, int dim)
    {
        m_mlp.generatedField.clear_field();
        m_mlp.generatedField = ScalarField2D(minBB, maxBB, dim, dim);

        std::pair<int, int> resolution = m_mlp.generatedField.get_resolution();
        std::vector<float> field_values = m_mlp.generatedField.get_values();
        std::vector<Vec3> field_points = m_mlp.generatedField.get_points();

        for (size_t i = 0; i < field_values.size(); ++i) {
            field_values[i] = m_mlp.polygonSDF(field_points[i], m_polygon);
        }

        m_mlp.rescaleToRange(field_values);
        m_mlp.generatedField.set_values(field_values);

        std::cout << "Ground truth field initialized" << std::endl;
    }

    void load_polygon_from_csv(const std::string& filename, std::vector<Vec3>& polygon)
    {
        polygon.clear();
        std::ifstream file(filename);

        if (!file.is_open()) {
            std::cout << "Warning: Could not open polygon file: " << filename << std::endl;
            create_default_polygon(polygon);
            return;
        }

        std::string line;
        std::set<std::pair<float, float>> unique_points; // To avoid duplicates

        while (std::getline(file, line)) {
            if (line.empty()) continue; // Skip empty lines

            std::string_view sv{line};
            auto first_comma = sv.find(',');
            if (first_comma == std::string_view::npos) continue;

            auto second_comma = sv.find(',', first_comma + 1);
            if (second_comma == std::string_view::npos) continue;

            float x{}, y{}, z{};
            auto result1 = std::from_chars(sv.data(), sv.data() + first_comma, x);
            auto result2 = std::from_chars(sv.data() + first_comma + 1, sv.data() + second_comma, y);
            auto result3 = std::from_chars(sv.data() + second_comma + 1, sv.data() + sv.size(), z);

            if (result1.ec == std::errc{} && result2.ec == std::errc{} && result3.ec == std::errc{}) {
                // Only add unique points to avoid duplicates
                if (unique_points.find({x, y}) == unique_points.end()) {
                    unique_points.insert({x, y});
                    polygon.emplace_back(x, y, 0.0f); // Use z=0 for 2D polygon
                }
            }
        }

        if (polygon.empty()) {
            std::cout << "No valid polygon data found, using default polygon" << std::endl;
            create_default_polygon(polygon);
        } else {
            std::cout << "Loaded polygon with " << polygon.size() << " unique vertices" << std::endl;
        }
    }

    void create_default_polygon(std::vector<Vec3>& polygon)
    {
        // Create a simple rectangular polygon as fallback
        polygon = {
            Vec3(-20, -15, 0),
            Vec3(20, -15, 0),
            Vec3(20, 15, 0),
            Vec3(-20, 15, 0)
        };
        std::cout << "Using default rectangular polygon" << std::endl;
    }

    // Helper methods for training
    void perform_training_step()
    {
        m_gradients.clear();
        std::vector<float> dummy;
        float loss = m_mlp.computeLoss(m_mlp_input_data, dummy);

        m_mlp.computeGradient(m_mlp_input_data, dummy, m_gradients);

        m_mlp.backward(m_gradients, m_learning_rate);

        // Decay learning rate
        m_learning_rate *= 0.99;

        // Update field visualization
        m_mlp.GenerateField(m_mlp_input_data);
        m_contours = m_mlp.generatedField.get_contours(0.02f);
        m_contours.setShowVertices(false);
        m_contours.setEdgeWidth(2.0f);
        m_contours.setEdgeColor(Color(0.0f, 0.0f, 0.0f));

        // Print loss occasionally
        static int step_count = 0;
        if (++step_count % 10 == 0) {
            std::cout << "Training step " << step_count << ", loss: " << loss
                      << ", lr: " << m_learning_rate << std::endl;
        }
    }

    void update_field()
    {
        m_mlp.GenerateField(m_mlp_input_data);
        m_contours = m_mlp.generatedField.get_contours(0.02f);
        m_contours.setShowVertices(false);
        m_contours.setEdgeWidth(2.0f);
        m_contours.setEdgeColor(Color(0.0f, 0.0f, 0.0f));
        std::cout << "Field updated" << std::endl;
    }

    // Helper methods for visualization
    void draw_contours(Renderer& renderer)
    {
        renderer.setColor(Color(0.0f, 0.0f, 0.0f));
        auto data = m_contours.getGraphData();
        if (!data)
        {
            return;
        }

        for (const auto& edge : data->edges)
        {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(data->vertices.size()) ||
                edge.vertexB >= static_cast<int>(data->vertices.size()))
            {
                continue;
            }

            const Vec3& start = data->vertices[edge.vertexA].position;
            const Vec3& end = data->vertices[edge.vertexB].position;
            renderer.drawLine(start, end, Color(0.0f, 0.0f, 0.0f), 2.0f);
        }
    }

    void draw_scalar_field(Renderer& renderer)
    {
        m_mlp.visualiseField(renderer, 0.01f, true);
    }

    void draw_polygon(Renderer& renderer)
    {
        if (m_polygon.empty()) return;

        renderer.setColor(Color(0.0f, 0.0f, 0.0f)); // White polygon
        for (size_t i = 0; i < m_polygon.size(); i++) {
            size_t j = (i + 1) % m_polygon.size();
            renderer.drawLine(m_polygon[i], m_polygon[j]);
        }
    }

    void draw_training_samples(Renderer& renderer)
    {
        renderer.setColor(Color(1.0f, 0.0f, 0.5f)); // Magenta points
        for (const auto& point : m_training_samples) {
            renderer.drawPoint(point, Color(1.0f, 0.0f, 0.5f), 3.0f);
        }
    }

    void draw_fitted_centers(Renderer& renderer)
    {
        for (const auto& center : m_mlp.fittedCenters) {
            m_mlp.drawCircle(renderer, center, 3.0f, 32, Color(1.0f, 0.0f, 0.0f)); // Red circles
        }
    }

    void draw_gradients(Renderer& renderer)
    {
        if (!m_mlp_input_data.empty()) {
            m_mlp.visualiseGradients(renderer, m_mlp_input_data);
        }
    }

    void draw_mlp_visualization(Renderer& renderer, Camera& camera)
    {
        // Position MLP visualization to the right of UI text to avoid overlap
        // UI text occupies x=10-300, y=30-350, so place MLP at x=400
        m_mlp.visualize(renderer, camera, Vec3(20, 460, 0), 300, 200);
    }

    void draw_loss_information(Renderer& renderer)
    {
        // Draw loss text
        m_mlp.drawLossText(renderer, 370);

        // Draw loss bar graphs
        if (!m_mlp.losses.empty()) {
            m_mlp.drawLossBarGraph(renderer, m_mlp.losses, 20, 700, 300, 60);
        }
        if (!m_mlp.losses_ang.empty()) {
            m_mlp.drawLossBarGraph(renderer, m_mlp.losses_ang, 20, 780, 300, 60);
        }
    }

    void draw_ui(Renderer& renderer)
    {
        // Basic sketch information
        renderer.setColor(Color(0.0f, 0.0f, 0.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString(getDescription(), 10, 50);

        renderer.setColor(Color(1.0f, 0.0f, 0.5f));
        renderer.drawString("FPS: " + std::to_string(Application::getInstance()->getFPS()), 10, 70);

        // Control instructions
        renderer.setColor(Color(0.55f, 0.55f, 0.55f));
        renderer.drawString("Controls:", 10, 100);
        renderer.drawString("'R' - Toggle training", 10, 120);
        renderer.drawString("'U' - Update field", 10, 140);
        renderer.drawString("'F' - Toggle field visualization", 10, 160);
        renderer.drawString("'P' - Toggle polygon", 10, 180);
        renderer.drawString("'T' - Toggle training samples", 10, 200);
        renderer.drawString("'C' - Toggle centers", 10, 220);
        renderer.drawString("'G' - Toggle gradients", 10, 240);
        renderer.drawString("'M' - Toggle MLP visualization", 10, 260);
        renderer.drawString("'L' - Toggle loss graph", 10, 280);
        renderer.drawString("'N' - Toggle contours", 10, 300);

        // Training status
        renderer.setColor(Color(1.0f, 0.0f, 0.5f));
        std::string status = b_run_training ? "TRAINING" : "STOPPED";
        renderer.drawString("Status: " + status, 10, 330);
        renderer.drawString("Learning Rate: " + std::to_string(m_learning_rate), 10, 350);
    }
};

// Register the sketch with alice2 (both old and new systems)
// ALICE2_REGISTER_SKETCH_AUTO(MLPSketch)

#endif // __MAIN__





