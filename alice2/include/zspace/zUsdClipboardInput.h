#pragma once
#include <filesystem>
#include <string>
#include <cstddef>
namespace alice2 {
struct UsdClipboardSummary {
    std::size_t vertices = 0, faces = 0;
    std::string sourceUpAxis, sourcePrim, sourceColorPrimvar;
    double sourceMetersPerUnit = 1;
    bool mirrored = false, windingReversed = false;
};
// Static, single visible mesh only. Does not repair or split topology.
UsdClipboardSummary normalizeUsdInputText(const std::string& text,
    const std::filesystem::path& outputPath, const std::string& role);
UsdClipboardSummary normalizeUsdInputFile(const std::filesystem::path& inputPath,
    const std::filesystem::path& outputPath, const std::string& role);
}
