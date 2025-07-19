#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include "../utils/Math.h"
#include "../utils/OpenGL.h"

// Forward declarations
struct stbtt_fontinfo;

namespace alice2 {

    // Font glyph information for baked fonts
    struct FontGlyph {
        float x0, y0, x1, y1;  // Texture coordinates
        float xoff, yoff;      // Offset from baseline
        float xadvance;        // Advance width
    };

    // Font atlas containing baked glyphs
    struct FontAtlas {
        GLuint textureId;
        int width, height;
        std::unordered_map<int, FontGlyph> glyphs;
        float fontSize;
        int ascent, descent, lineGap;
    };

    class FontRenderer {
    public:
        FontRenderer();
        ~FontRenderer();

        // Initialization and cleanup
        bool initialize();
        void shutdown();

        // Font loading
        bool loadFont(const std::string& fontPath, float fontSize = 16.0f);
        bool loadDefaultFont(float fontSize = 16.0f);

        // 2D screen overlay text rendering
        void drawString(const std::string& text, float x, float y, 
                       const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f));

        // 3D world space text rendering (billboard with screen-space sizing)
        void drawText(const std::string& text, const Vec3& position,
                     float size = 1.0f,
                     const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f));

        // Utility functions
        float getTextWidth(const std::string& text) const;
        float getTextHeight() const;
        bool isInitialized() const { return m_initialized; }

    private:

        // Font management
        bool createFontAtlas(const unsigned char* fontData, size_t dataSize, float fontSize);
        void setupOpenGLState();
        void restoreOpenGLState();

        // Member variables
        bool m_initialized;
        std::unique_ptr<FontAtlas> m_fontAtlas;
        std::unique_ptr<unsigned char[]> m_fontData;
        size_t m_fontDataSize;

        // OpenGL state backup
        GLint m_prevTexture;
        GLboolean m_prevBlend;
        GLint m_prevBlendSrc, m_prevBlendDst;
        GLboolean m_prevDepthTest;
        GLint m_prevViewport[4];
        GLfloat m_prevProjectionMatrix[16];
        GLfloat m_prevModelViewMatrix[16];
    };

} // namespace alice2
