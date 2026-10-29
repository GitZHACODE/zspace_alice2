#pragma once

#include <zspace/zSlicerInspectorData.h>
#include <cstdint>
#include <map>

namespace alice2::slicing {

struct FieldPreviewTexture {
    unsigned int Id = 0;
    int Width = 0, Height = 0;
    bool Categorical = false;
};

struct FieldPreviewLayer {
    FieldPreviewTexture Path, Width, Tags;
    std::size_t Bytes = 0;
    std::uint64_t LastUsed = 0;
};

// One grid snapshot per solve; bounded GPU presentation data per visited layer.
// Clear while the GL context is current when the input/result changes or unloads.
class SlicerFieldPreview {
public:
    ~SlicerFieldPreview() { clear(); }
    SlicerFieldPreview() = default;
    SlicerFieldPreview(const SlicerFieldPreview&) = delete;
    SlicerFieldPreview& operator=(const SlicerFieldPreview&) = delete;
    const FieldPreviewLayer& layer(int index, const zSpace::zObjMeshScalarField& field,
        const zSpace::zScalarArray& pathValues, const zSpace::zScalarArray& widthValues,
        const zSpace::zScalarArray& palette, const zSpace::zTsField::FaceTagSource* source);
    const FieldGridSnapshot& grid() const { return m_grid; }
    void clear();
    std::size_t size() const { return m_layers.size(); }
    std::size_t bytes() const { return m_bytes; }
    std::uint64_t buildCount() const { return m_builds; }
private:
    static void release(FieldPreviewLayer& layer);
    FieldGridSnapshot m_grid;
    bool m_gridReady = false;
    std::map<int, FieldPreviewLayer> m_layers;
    std::size_t m_bytes = 0;
    std::uint64_t m_clock = 0;
    std::uint64_t m_builds = 0;
};

// This colour mapping is shared by field textures and the actual path overlays.
zSpace::zColor slicerFaceTagColor(int tag);

} // namespace alice2::slicing
