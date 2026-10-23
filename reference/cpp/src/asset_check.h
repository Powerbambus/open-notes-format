#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Checks of an asset file's content - Spec Sections 9.2.2, 9.2.3 and 9.4.5.
// Nothing is decoded: only the header is read, before any decoder would see the data.

constexpr uint64_t MAX_DECODED_PIXELS = 100000000;  // Section 9.2.3

// webp, png, jpg or pdf - Section 9.2.2. Lowercase, exact.
bool is_supported_asset_extension(const std::string& extension);

struct AssetInfo {
    bool ok = false;           // the leading bytes match the extension and the size limit holds
    std::string problem;       // the reason if ok is false
    int64_t width  = 0;        // pixels, 0 for pdf
    int64_t height = 0;
};

// The leading bytes (magic number) must identify the format given by the extension, and an
// image must not declare more than 100000000 pixels. For pdf only "%PDF-" is checked.
AssetInfo check_asset_content(const std::string& extension, const std::vector<uint8_t>& bytes);
