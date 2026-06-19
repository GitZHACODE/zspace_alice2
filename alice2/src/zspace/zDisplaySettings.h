#pragma once

#ifndef ALICE2_ZDISPLAY_H
#define ALICE2_ZDISPLAY_H

#include "../utils/Math.h"

namespace alice2 {

    struct zDisplaySetting {
        bool showVertices = true;
        bool showEdges = true;
        bool showFaces = true;
        Color vertexColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
        Color edgeColor = Color(0.05f, 0.05f, 0.05f, 1.0f);
        Color faceColor = Color(0.75f, 0.78f, 0.82f, 1.0f);
        float vertexSize = 4.0f;
        float edgeWidth = 1.0f;
    };

    struct zDisplayMeshSetting : public zDisplaySetting {};

    struct zDisplayGraphSetting : public zDisplaySetting {
        zDisplayGraphSetting()
        {
            showFaces = false;
            edgeColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
            vertexColor = Color(1.0f, 0.8f, 0.2f, 1.0f);
            edgeWidth = 2.0f;
        }
    };

    struct zDisplayPointCloudSetting : public zDisplaySetting {
        zDisplayPointCloudSetting()
        {
            showFaces = false;
            showEdges = false;
            vertexColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
            vertexSize = 4.0f;
        }
    };

    namespace Display {
        inline zDisplayMeshSetting wireframe(const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f), float width = 1.5f)
        {
            zDisplayMeshSetting display;
            display.showFaces = false;
            display.showEdges = true;
            display.showVertices = false;
            display.edgeColor = color;
            display.edgeWidth = width;
            return display;
        }

        inline zDisplayGraphSetting lines(const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f), float width = 2.0f)
        {
            zDisplayGraphSetting display;
            display.edgeColor = color;
            display.edgeWidth = width;
            return display;
        }

        inline zDisplayPointCloudSetting points(const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f), float size = 4.0f)
        {
            zDisplayPointCloudSetting display;
            display.vertexColor = color;
            display.vertexSize = size;
            return display;
        }
    }

} // namespace alice2

#endif // ALICE2_ZDISPLAY_H
