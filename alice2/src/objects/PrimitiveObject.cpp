#include "PrimitiveObject.h"
#include "../core/Renderer.h"
#include "../core/Camera.h"

namespace alice2 {

    PrimitiveObject::PrimitiveObject(PrimitiveType type, const std::string& name)
        : SceneObject(name)
        , m_primitiveType(type)
        , m_size(1.0f, 1.0f, 1.0f)
        , m_radius(0.5f)
        , m_height(1.0f)
    {
        calculateBounds();
    }

    void PrimitiveObject::renderImpl(Renderer& renderer, Camera& /*camera*/) {
        renderer.setColor(m_color, m_opacity);
        renderer.setWireframe(m_wireframe);

        switch (m_primitiveType) {
            case PrimitiveType::Cube:
                renderCube(renderer);
                break;
            case PrimitiveType::Sphere:
                renderSphere(renderer);
                break;
            case PrimitiveType::Cylinder:
                renderCylinder(renderer);
                break;
            case PrimitiveType::Plane:
                renderPlane(renderer);
                break;
            case PrimitiveType::Line:
                renderLine(renderer);
                break;
            case PrimitiveType::Point:
                renderPoint(renderer);
                break;
        }
    }

    void PrimitiveObject::calculateBounds() {
        switch (m_primitiveType) {
            case PrimitiveType::Cube:
                setBounds(-m_size * 0.5f, m_size * 0.5f);
                break;
            case PrimitiveType::Sphere:
                setBounds(Vec3(-m_radius, -m_radius, -m_radius), Vec3(m_radius, m_radius, m_radius));
                break;
            case PrimitiveType::Cylinder:
                setBounds(Vec3(-m_radius, -m_height * 0.5f, -m_radius), Vec3(m_radius, m_height * 0.5f, m_radius));
                break;
            case PrimitiveType::Plane:
                setBounds(-m_size * 0.5f, m_size * 0.5f);
                break;
            case PrimitiveType::Line:
                setBounds(Vec3(-m_size.x * 0.5f, 0, 0), Vec3(m_size.x * 0.5f, 0, 0));
                break;
            case PrimitiveType::Point:
                setBounds(Vec3(-0.01f, -0.01f, -0.01f), Vec3(0.01f, 0.01f, 0.01f));
                break;
        }
    }

    void PrimitiveObject::renderCube(Renderer& renderer) {
        renderer.pushMatrix();
        
        // Scale by size
        Mat4 scale = Mat4::scale(m_size);
        renderer.multMatrix(scale);
        
        renderer.drawCube(1.0f);
        
        renderer.popMatrix();
    }

    void PrimitiveObject::renderSphere(Renderer& renderer) {
        renderer.pushMatrix();
        
        // Scale by radius
        Mat4 scale = Mat4::scale(Vec3(m_radius, m_radius, m_radius));
        renderer.multMatrix(scale);
        
        renderer.drawSphere(1.0f);
        
        renderer.popMatrix();
    }

    void PrimitiveObject::renderCylinder(Renderer& renderer) {
        renderer.pushMatrix();
        
        // Scale by radius and height
        Mat4 scale = Mat4::scale(Vec3(m_radius, m_height, m_radius));
        renderer.multMatrix(scale);
        
        renderer.drawCylinder(1.0f, 1.0f);
        
        renderer.popMatrix();
    }

    void PrimitiveObject::renderPlane(Renderer& renderer) {
        // Render as a quad
        Vec3 halfSize = m_size * 0.5f;
        renderer.drawQuad(
            Vec3(-halfSize.x, 0, -halfSize.z),
            Vec3(halfSize.x, 0, -halfSize.z),
            Vec3(halfSize.x, 0, halfSize.z),
            Vec3(-halfSize.x, 0, halfSize.z)
        );
    }

    void PrimitiveObject::renderLine(Renderer& renderer) {
        Vec3 start(-m_size.x * 0.5f, 0, 0);
        Vec3 end(m_size.x * 0.5f, 0, 0);
        renderer.drawLine(start, end);
    }

    void PrimitiveObject::renderPoint(Renderer& renderer) {
        renderer.drawPoint(Vec3(0, 0, 0));
    }

} // namespace alice2
