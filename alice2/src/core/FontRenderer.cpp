#include "FontRenderer.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

// STB TrueType implementation
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../depends/stb/stb_truetype.h"

namespace alice2 {

    FontRenderer::FontRenderer()
        : m_initialized(false)
        , m_fontAtlas(nullptr)
        , m_fontData(nullptr)
        , m_fontDataSize(0)
        , m_prevTexture(0)
        , m_prevBlend(GL_FALSE)
        , m_prevBlendSrc(0)
        , m_prevBlendDst(0)
        , m_prevDepthTest(GL_FALSE)
    {
        memset(m_prevViewport, 0, sizeof(m_prevViewport));
        memset(m_prevProjectionMatrix, 0, sizeof(m_prevProjectionMatrix));
        memset(m_prevModelViewMatrix, 0, sizeof(m_prevModelViewMatrix));
    }

    FontRenderer::~FontRenderer() {
        shutdown();
    }

    bool FontRenderer::initialize() {
        if (m_initialized) return true;

        // Check OpenGL extensions
        if (!GLEW_VERSION_1_1) {
            std::cerr << "FontRenderer: OpenGL 1.1 or higher required" << std::endl;
            return false;
        }

        m_initialized = true;
        return true;
    }

    void FontRenderer::shutdown() {
        if (m_fontAtlas && m_fontAtlas->textureId != 0) {
            glDeleteTextures(1, &m_fontAtlas->textureId);
        }
        m_fontAtlas.reset();
        m_fontData.reset();
        m_fontDataSize = 0;
        m_initialized = false;
    }

    bool FontRenderer::loadDefaultFont(float fontSize) {
        // Embedded minimal font data (we'll use a simple approach for now)
        // In a real implementation, you'd embed JetBrains Mono or load from system fonts
        
        // For now, let's try to load a system font
        std::vector<std::string> systemFontPaths = {
            "C:/Windows/Fonts/consola.ttf",  // Consolas (monospace)
            "C:/Windows/Fonts/arial.ttf",    // Arial (fallback)
            "C:/Windows/Fonts/calibri.ttf"   // Calibri (fallback)
        };

        for (const auto& path : systemFontPaths) {
            if (loadFont(path, fontSize)) {
                std::cout << "FontRenderer: Loaded system font: " << path << std::endl;
                return true;
            }
        }

        std::cerr << "FontRenderer: Failed to load any system font" << std::endl;
        return false;
    }

    bool FontRenderer::loadFont(const std::string& fontPath, float fontSize) {
        if (!m_initialized) {
            std::cerr << "FontRenderer: Not initialized" << std::endl;
            return false;
        }

        // Load font file
        std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "FontRenderer: Failed to open font file: " << fontPath << std::endl;
            return false;
        }

        m_fontDataSize = file.tellg();
        file.seekg(0, std::ios::beg);

        m_fontData = std::make_unique<unsigned char[]>(m_fontDataSize);
        if (!file.read(reinterpret_cast<char*>(m_fontData.get()), m_fontDataSize)) {
            std::cerr << "FontRenderer: Failed to read font file: " << fontPath << std::endl;
            return false;
        }
        file.close();

        // Create font atlas
        if (!createFontAtlas(m_fontData.get(), m_fontDataSize, fontSize)) {
            std::cerr << "FontRenderer: Failed to create font atlas" << std::endl;
            return false;
        }

        std::cout << "FontRenderer: Successfully loaded font: " << fontPath 
                  << " (size: " << fontSize << ")" << std::endl;
        return true;
    }

    bool FontRenderer::createFontAtlas(const unsigned char* fontData, size_t dataSize, float fontSize) {
        // Initialize STB font info
        stbtt_fontinfo fontInfo;
        if (!stbtt_InitFont(&fontInfo, fontData, stbtt_GetFontOffsetForIndex(fontData, 0))) {
            std::cerr << "FontRenderer: Failed to initialize font" << std::endl;
            return false;
        }

        // Create new font atlas
        m_fontAtlas = std::make_unique<FontAtlas>();
        m_fontAtlas->fontSize = fontSize;
        m_fontAtlas->width = 512;
        m_fontAtlas->height = 512;

        // Get font metrics
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
        float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontSize);
        
        m_fontAtlas->ascent = static_cast<int>(ascent * scale);
        m_fontAtlas->descent = static_cast<int>(descent * scale);
        m_fontAtlas->lineGap = static_cast<int>(lineGap * scale);

        // Create bitmap for baking
        std::vector<unsigned char> bitmap(m_fontAtlas->width * m_fontAtlas->height);
        
        // Bake ASCII characters (32-126)
        std::vector<stbtt_bakedchar> bakedChars(95);
        int result = stbtt_BakeFontBitmap(fontData, 0, fontSize, 
                                         bitmap.data(), m_fontAtlas->width, m_fontAtlas->height,
                                         32, 95, bakedChars.data());
        
        if (result <= 0) {
            std::cerr << "FontRenderer: Failed to bake font bitmap" << std::endl;
            return false;
        }

        // Store glyph information
        for (int i = 0; i < 95; i++) {
            FontGlyph glyph;
            glyph.x0 = bakedChars[i].x0 / float(m_fontAtlas->width);
            glyph.y0 = bakedChars[i].y0 / float(m_fontAtlas->height);
            glyph.x1 = bakedChars[i].x1 / float(m_fontAtlas->width);
            glyph.y1 = bakedChars[i].y1 / float(m_fontAtlas->height);
            glyph.xoff = bakedChars[i].xoff;
            glyph.yoff = bakedChars[i].yoff;
            glyph.xadvance = bakedChars[i].xadvance;
            
            m_fontAtlas->glyphs[32 + i] = glyph;
        }

        // Create OpenGL texture
        glGenTextures(1, &m_fontAtlas->textureId);
        glBindTexture(GL_TEXTURE_2D, m_fontAtlas->textureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, m_fontAtlas->width, m_fontAtlas->height, 
                     0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        return true;
    }

    void FontRenderer::drawString(const std::string& text, float x, float y,
                                 const Vec3& color, float alpha) {
        if (!m_initialized || !m_fontAtlas || text.empty()) return;

        renderText2D(text, x, y, color, alpha);
    }

    void FontRenderer::drawText(const std::string& text, const Vec3& position,
                               float size, const Vec3& color, float alpha) {
        if (!m_initialized || !m_fontAtlas || text.empty()) return;

        renderText3D(text, position, size, color, alpha);
    }

    void FontRenderer::renderText2D(const std::string& text, float x, float y,
                                   const Vec3& color, float alpha) {
        setupOpenGLState();

        glColor4f(color.x, color.y, color.z, alpha);
        glBindTexture(GL_TEXTURE_2D, m_fontAtlas->textureId);

        // Set up 2D orthographic projection
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, m_prevViewport[2], m_prevViewport[3], 0, -1, 1);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glBegin(GL_QUADS);

        float currentX = x;
        float currentY = y;

        for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];

            if (c == '\n') {
                currentX = x;
                currentY += m_fontAtlas->ascent - m_fontAtlas->descent + m_fontAtlas->lineGap;
                continue;
            }

            if (c < 32 || c > 126) continue; // Skip non-printable characters

            auto it = m_fontAtlas->glyphs.find(c);
            if (it == m_fontAtlas->glyphs.end()) continue;

            const FontGlyph& glyph = it->second;

            float x0 = currentX + glyph.xoff;
            float y0 = currentY + glyph.yoff;
            float x1 = x0 + (glyph.x1 - glyph.x0) * m_fontAtlas->width;
            float y1 = y0 + (glyph.y1 - glyph.y0) * m_fontAtlas->height;

            glTexCoord2f(glyph.x0, glyph.y0); glVertex2f(x0, y0);
            glTexCoord2f(glyph.x1, glyph.y0); glVertex2f(x1, y0);
            glTexCoord2f(glyph.x1, glyph.y1); glVertex2f(x1, y1);
            glTexCoord2f(glyph.x0, glyph.y1); glVertex2f(x0, y1);

            currentX += glyph.xadvance;
        }

        glEnd();

        // Restore matrices
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        restoreOpenGLState();
    }

    void FontRenderer::renderText3D(const std::string& text, const Vec3& position,
                                   float size, const Vec3& color, float alpha) {
        setupOpenGLState();

        glColor4f(color.x, color.y, color.z, alpha);
        glBindTexture(GL_TEXTURE_2D, m_fontAtlas->textureId);

        // Get current matrices for billboard calculation
        GLfloat modelView[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, modelView);

        // Calculate billboard vectors (camera-facing)
        Vec3 right(modelView[0], modelView[4], modelView[8]);
        Vec3 up(modelView[1], modelView[5], modelView[9]);

        // Normalize and scale
        right = right.normalized() * size;
        up = up.normalized() * size;

        glBegin(GL_QUADS);

        float currentX = 0.0f;
        float textWidth = getTextWidth(text) * size / m_fontAtlas->fontSize;
        float startX = -textWidth * 0.5f; // Center the text

        for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];

            if (c == '\n') {
                currentX = startX;
                // Handle newlines in 3D (move down)
                continue;
            }

            if (c < 32 || c > 126) continue;

            auto it = m_fontAtlas->glyphs.find(c);
            if (it == m_fontAtlas->glyphs.end()) continue;

            const FontGlyph& glyph = it->second;

            float charWidth = (glyph.x1 - glyph.x0) * m_fontAtlas->width * size / m_fontAtlas->fontSize;
            float charHeight = (glyph.y1 - glyph.y0) * m_fontAtlas->height * size / m_fontAtlas->fontSize;

            float x0 = currentX + glyph.xoff * size / m_fontAtlas->fontSize;
            float y0 = glyph.yoff * size / m_fontAtlas->fontSize;
            float x1 = x0 + charWidth;
            float y1 = y0 + charHeight;

            // Calculate billboard quad vertices
            Vec3 v0 = position + right * x0 + up * y0;
            Vec3 v1 = position + right * x1 + up * y0;
            Vec3 v2 = position + right * x1 + up * y1;
            Vec3 v3 = position + right * x0 + up * y1;

            glTexCoord2f(glyph.x0, glyph.y0); glVertex3f(v0.x, v0.y, v0.z);
            glTexCoord2f(glyph.x1, glyph.y0); glVertex3f(v1.x, v1.y, v1.z);
            glTexCoord2f(glyph.x1, glyph.y1); glVertex3f(v2.x, v2.y, v2.z);
            glTexCoord2f(glyph.x0, glyph.y1); glVertex3f(v3.x, v3.y, v3.z);

            currentX += glyph.xadvance * size / m_fontAtlas->fontSize;
        }

        glEnd();

        restoreOpenGLState();
    }

    void FontRenderer::setupOpenGLState() {
        // Save current OpenGL state
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_prevTexture);
        glGetBooleanv(GL_BLEND, &m_prevBlend);
        glGetIntegerv(GL_BLEND_SRC, &m_prevBlendSrc);
        glGetIntegerv(GL_BLEND_DST, &m_prevBlendDst);
        glGetBooleanv(GL_DEPTH_TEST, &m_prevDepthTest);
        glGetIntegerv(GL_VIEWPORT, m_prevViewport);
        glGetFloatv(GL_PROJECTION_MATRIX, m_prevProjectionMatrix);
        glGetFloatv(GL_MODELVIEW_MATRIX, m_prevModelViewMatrix);

        // Set up state for text rendering
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
    }

    void FontRenderer::restoreOpenGLState() {
        // Restore previous OpenGL state
        glBindTexture(GL_TEXTURE_2D, m_prevTexture);

        if (m_prevBlend) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        glBlendFunc(m_prevBlendSrc, m_prevBlendDst);

        if (m_prevDepthTest) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
    }

    float FontRenderer::getTextWidth(const std::string& text) const {
        if (!m_fontAtlas || text.empty()) return 0.0f;

        float width = 0.0f;
        for (char c : text) {
            if (c == '\n') continue;
            if (c < 32 || c > 126) continue;

            auto it = m_fontAtlas->glyphs.find(c);
            if (it != m_fontAtlas->glyphs.end()) {
                width += it->second.xadvance;
            }
        }
        return width;
    }

    float FontRenderer::getTextHeight() const {
        if (!m_fontAtlas) return 0.0f;
        return m_fontAtlas->ascent - m_fontAtlas->descent;
    }

    int FontRenderer::getNextCodepoint(const char*& text) const {
        // Simple UTF-8 decoding (basic implementation)
        unsigned char c = *text++;
        if (c < 0x80) return c;

        // For now, just return the first byte for non-ASCII
        // A full implementation would handle multi-byte UTF-8 sequences
        return c;
    }

    bool FontRenderer::isValidUTF8(const std::string& text) const {
        // Basic UTF-8 validation
        for (size_t i = 0; i < text.length(); i++) {
            unsigned char c = text[i];
            if (c > 0x7F) {
                // For now, accept any high-bit characters
                // A full implementation would validate UTF-8 sequences
                continue;
            }
        }
        return true;
    }

} // namespace alice2
