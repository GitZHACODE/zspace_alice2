#include <GL/glew.h>
#include <zspace/zSlicerFieldPreview.h>
#include <zspace/zSlicerTiming.h>
#include <algorithm>
#include <stdexcept>

namespace alice2::slicing {
namespace {
constexpr std::size_t maxLayers = 128;
constexpr std::size_t maxBytes = 64 * 1024 * 1024;

FieldPreviewTexture upload(const TexturePixels& pixels, bool categorical)
{
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (pixels.Width <= 0 || pixels.Height <= 0 || pixels.Width > maxSize || pixels.Height > maxSize)
        throw std::runtime_error("Field preview texture exceeds the available GL dimensions.");
    FieldPreviewTexture texture;
    texture.Width = pixels.Width; texture.Height = pixels.Height; texture.Categorical = categorical;
    glGenTextures(1, &texture.Id);
    if (!texture.Id) throw std::runtime_error("Cannot allocate field preview texture.");
    glBindTexture(GL_TEXTURE_2D, texture.Id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, categorical ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, categorical ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.Width, pixels.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.Rgba.data());
    GLint width = 0, height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    if (width != pixels.Width || height != pixels.Height) {
        glDeleteTextures(1, &texture.Id);
        throw std::runtime_error("Cannot upload field preview texture.");
    }
    return texture;
}
}

zSpace::zColor slicerFaceTagColor(int tag)
{
    switch (tag) {
    case 3: return {.13f,.77f,.37f,1};
    case 4: return {.64f,.29f,.90f,1};
    case 5: return {.04f,.72f,.88f,1};
    case 6: return {.90f,.12f,.22f,1};
    default: return {.62f,.65f,.69f,1};
    }
}

void SlicerFieldPreview::release(FieldPreviewLayer& layer)
{
    for (auto* texture : {&layer.Path, &layer.Width, &layer.Tags}) {
        if (texture->Id) glDeleteTextures(1, &texture->Id);
        texture->Id = 0;
    }
}

void SlicerFieldPreview::clear()
{
    for (auto& entry : m_layers) release(entry.second);
    m_layers.clear(); m_grid = {}; m_gridReady = false; m_bytes = 0; m_clock = 0; m_builds = 0;
}

const FieldPreviewLayer& SlicerFieldPreview::layer(int index, const zSpace::zObjMeshScalarField& field,
    const zSpace::zScalarArray& pathValues, const zSpace::zScalarArray& widthValues,
    const zSpace::zScalarArray& palette, const zSpace::zTsField::FaceTagSource* source)
{
    auto found = m_layers.find(index);
    if (found != m_layers.end()) { found->second.LastUsed = ++m_clock; return found->second; }
    const SlicerMethodTimer timer("cacheFieldPreview");
    if (!m_gridReady) { m_grid = fieldGridSnapshot(field); m_gridReady = true; }
    if (pathValues.size() != m_grid.Positions.size() || widthValues.size() != m_grid.Positions.size())
        throw std::invalid_argument("Field preview values do not match the grid.");
    zSpace::zColorArray pathColors, widthColors, tagColors;
    zSpace::zTsUtils::colorPathField(pathValues, pathColors);
    zSpace::zTsUtils::colorWidthField(widthValues, widthColors, palette.empty() ? nullptr : &palette);
    if (source && !source->edgeTags.empty()) {
        zSpace::zIntArray tags;
        if (!zSpace::zTsField::queryFaceTags(*source, m_grid.Positions, tags))
            throw std::runtime_error("Cannot build the FaceTag field preview.");
        tagColors.reserve(tags.size());
        for (int tag : tags) tagColors.push_back(slicerFaceTagColor(tag));
    }
    const auto path = fieldPixels(m_grid, pathColors);
    const auto width = fieldPixels(m_grid, widthColors);
    const auto tags = tagColors.empty() ? TexturePixels{} : fieldPixels(m_grid, tagColors, true);
    const std::size_t needed = path.Rgba.size() + width.Rgba.size() + tags.Rgba.size();
    if (needed > maxBytes) throw std::runtime_error("Field preview exceeds the 64 MiB cache limit.");
    while (!m_layers.empty() && (m_layers.size() >= maxLayers || m_bytes + needed > maxBytes)) {
        const auto oldest = std::min_element(m_layers.begin(), m_layers.end(), [](const auto& a, const auto& b) {
            return a.second.LastUsed < b.second.LastUsed;
        });
        m_bytes -= oldest->second.Bytes; release(oldest->second); m_layers.erase(oldest);
    }
    FieldPreviewLayer result;
    GLint unpackBuffer = 0, activeTexture = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glPushAttrib(GL_TEXTURE_BIT | GL_ENABLE_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    try {
        result.Path = upload(path, false); result.Width = upload(width, false);
        if (!tags.Rgba.empty()) result.Tags = upload(tags, true);
    } catch (...) {
        release(result); glPopClientAttrib(); glPopAttrib();
        glActiveTexture(activeTexture); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer); throw;
    }
    glPopClientAttrib(); glPopAttrib();
    glActiveTexture(activeTexture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
    result.Bytes = needed; result.LastUsed = ++m_clock;
    ++m_builds;
    m_bytes += needed;
    std::cout << "[FieldPreview] cached layer=" << index << " grid=" << m_grid.Nx << 'x' << m_grid.Ny
              << " resident=" << (m_layers.size()+1) << " bytes=" << m_bytes << '\n';
    return m_layers.emplace(index, result).first->second;
}

} // namespace alice2::slicing
