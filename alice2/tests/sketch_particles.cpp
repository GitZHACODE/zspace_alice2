// alice2 Particle System Sketch
// Demonstrates a simple particle system with physics

// #define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <vector>
#include <random>

using namespace alice2;

class ParticleSketch : public ISketch
{
    struct Particle
    {
        Vec3 position;
        Vec3 velocity;
        Color color;
        float life;
        float maxLife;
        float size;
    };

private:
    std::vector<Particle> m_particles;
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist;
    float m_emissionTimer;
    float m_emissionRate;
    Vec3 m_gravity;
    bool m_paused;

public:
    ParticleSketch()
        : m_rng(std::random_device{}()), m_dist(-1.0f, 1.0f), m_emissionTimer(0.0f), m_emissionRate(0.05f) // Emit every 50ms
          ,
          m_gravity(0, 0, -9.8f), m_paused(false)
    {
        m_particles.reserve(1000);
    }

    ~ParticleSketch() = default;

    // Sketch information
    std::string getName() const override
    {
        return "Particle System";
    }

    std::string getDescription() const override
    {
        return "A simple particle system with gravity and color transitions";
    }

    std::string getAuthor() const override
    {
        return "alice2 Examples";
    }

    // Sketch lifecycle
    void setup() override
    {
        // Set a black background for better particle visibility
        scene().setBackgroundColor(Color(0.05f, 0.05f, 0.1f));
        std::cout << "Particle System loaded - Background set to dark" << std::endl;

        // Example: Enable grid
        scene().setShowGrid(true);
        scene().setGridSize(10.0f);
        scene().setGridDivisions(10);

        // Example: Enable axes
        scene().setShowAxes(true);
        scene().setAxesLength(2.0f);
    }

    void update(float deltaTime) override
    {
        if (m_paused)
            return;

        // Emit new particles
        m_emissionTimer += deltaTime;
        while (m_emissionTimer >= m_emissionRate && m_particles.size() < 800)
        {
            emitParticle();
            m_emissionTimer -= m_emissionRate;
        }

        // Update existing particles
        for (auto it = m_particles.begin(); it != m_particles.end();)
        {
            Particle &p = *it;

            // Update physics
            p.velocity += m_gravity * deltaTime;
            p.position += p.velocity * deltaTime;
            p.life -= deltaTime;

            // Update color based on life (fade from white to red to black)
            float lifeRatio = p.life / p.maxLife;
            if (lifeRatio > 0.5f)
            {
                // White to yellow to red
                float t = (lifeRatio - 0.5f) * 2.0f;
                p.color = Color(1.0f, t, t * 0.3f);
            }
            else
            {
                // Red to black
                float t = lifeRatio * 2.0f;
                p.color = Color(t, t * 0.2f, 0.0f);
            }

            // Remove dead particles
            if (p.life <= 0.0f || p.position.z < -5.0f)
            {
                it = m_particles.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void draw(Renderer &renderer, Camera &camera) override
    {
        // Draw all particles
        for (const auto &particle : m_particles)
        {
            renderer.drawPoint(particle.position, particle.color, particle.size);
        }

        // Draw emission point
        renderer.drawPoint(Vec3(0, 0, 0), Color(0.8f, 0.8f, 1.0f), 10.0f);

        // Draw some reference lines
        renderer.drawLine(Vec3(-2, 0, 0), Vec3(2, 0, 0), Color(0.3f, 0.3f, 0.3f), 1.0f);
        renderer.drawLine(Vec3(0, -2, 0), Vec3(0, 2, 0), Color(0.3f, 0.3f, 0.3f), 1.0f);

        // 2D text rendering (screen overlay)
        renderer.setColor(Color(1.0f, 1.0f, 1.0f));
        renderer.drawString(getName(), 10, 30);
        renderer.drawString(getDescription(), 10, 50);

        renderer.setColor(Color(0.0f, 1.0f, 1.0f));
        renderer.drawString("FPS: " + std::to_string((Application::getInstance()->getFPS())), 10, 70);

        renderer.setColor(Color(0.75f, 0.75f, 0.75f));
        renderer.drawString("'ESC' - Exit ", 10, 200);
        renderer.drawString("'F'   - Extend view ", 10, 220);
        renderer.drawString("'N'   - Switch to the next sketch ", 10, 240);
        renderer.drawString("'P'   - Switch to the previous sketch ", 10, 260);
    }

    void cleanup() override
    {
        m_particles.clear();
        std::cout << "Particle System cleanup" << std::endl;
    }

    // Input handling
    bool onKeyPress(unsigned char key, int x, int y) override
    {
        switch (key)
        {
        case ' ':
            m_paused = !m_paused;
            std::cout << "Particle system " << (m_paused ? "paused" : "resumed") << std::endl;
            return true;

        case 'c':
        case 'C':
            m_particles.clear();
            std::cout << "Particles cleared" << std::endl;
            return true;

        case '+':
        case '=':
            m_emissionRate = std::max(0.01f, m_emissionRate - 0.01f);
            std::cout << "Emission rate increased (interval: " << m_emissionRate << "s)" << std::endl;
            return true;

        case '-':
        case '_':
            m_emissionRate += 0.01f;
            std::cout << "Emission rate decreased (interval: " << m_emissionRate << "s)" << std::endl;
            return true;
        }
        return false; // Not handled
    }

private:
    void emitParticle()
    {
        Particle p;

        // Start at origin with some random offset
        p.position = Vec3(
            m_dist(m_rng) * 0.2f,
            m_dist(m_rng) * 0.2f,
            0.0f);

        // Random initial velocity (fountain-like)
        p.velocity = Vec3(
            m_dist(m_rng) * 3.0f,
            m_dist(m_rng) * 3.0f,
            5.0f + m_dist(m_rng) * 2.0f);

        // Initial color (bright white/yellow)
        p.color = Color(1.0f, 1.0f, 0.8f);

        // Random life span
        p.maxLife = p.life = 2.0f + m_dist(m_rng) * 1.0f;

        // Random size
        p.size = 4.0f + m_dist(m_rng) * 3.0f;

        m_particles.push_back(p);
    }
};

// Register the sketch with alice2
//ALICE2_REGISTER_SKETCH_AUTO(ParticleSketch)

#endif // __MAIN__
