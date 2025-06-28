#pragma once

#ifndef ALICE2_PRIMITIVE_OBJECT_H
#define ALICE2_PRIMITIVE_OBJECT_H

#include "SceneObject.h"

namespace alice2 {

    enum class PrimitiveType {
        Cube,
        Sphere,
        Cylinder,
        Plane,
        Line,
        Point
    };

    class PrimitiveObject : public SceneObject {
    public:
        PrimitiveObject(PrimitiveType type, const std::string& name = "Primitive");
        virtual ~PrimitiveObject() = default;

        // Type
        ObjectType getType() const override { return ObjectType::Primitive; }

        // Primitive type
        void setPrimitiveType(PrimitiveType type) { m_primitiveType = type; calculateBounds(); }
        PrimitiveType getPrimitiveType() const { return m_primitiveType; }

        // Size parameters
        void setSize(const Vec3& size) { m_size = size; calculateBounds(); }
        const Vec3& getSize() const { return m_size; }
        
        void setRadius(float radius) { m_radius = radius; calculateBounds(); }
        float getRadius() const { return m_radius; }
        
        void setHeight(float height) { m_height = height; calculateBounds(); }
        float getHeight() const { return m_height; }

        // Rendering
        void renderImpl(Renderer& renderer, Camera& camera) override;

        // Bounds calculation
        void calculateBounds() override;

    private:
        PrimitiveType m_primitiveType;
        Vec3 m_size;
        float m_radius;
        float m_height;

        void renderCube(Renderer& renderer);
        void renderSphere(Renderer& renderer);
        void renderCylinder(Renderer& renderer);
        void renderPlane(Renderer& renderer);
        void renderLine(Renderer& renderer);
        void renderPoint(Renderer& renderer);
    };

} // namespace alice2

#endif // ALICE2_PRIMITIVE_OBJECT_H
