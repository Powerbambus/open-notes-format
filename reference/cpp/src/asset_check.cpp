#include "asset_check.h"

#include <algorithm>
#include <cstring>

namespace {

AssetInfo bad(const char* why) {
    AssetInfo info;
    info.problem = why;
    return info;
}

AssetInfo sized(uint64_t width, uint64_t height) {
    if (width == 0 || height == 0) return bad("the image has a width or height of 0");
    if (width * height > MAX_DECODED_PIXELS) return bad("the image has more than 100000000 pixels");
    AssetInfo info;
    info.ok     = true;
    info.width  = static_cast<int64_t>(width);
    info.height = static_cast<int64_t>(height);
    return info;
}

uint32_t be16(const std::vector<uint8_t>& b, size_t at) { return (uint32_t(b[at]) << 8) | b[at + 1]; }
uint32_t be32(const std::vector<uint8_t>& b, size_t at) { return (be16(b, at) << 16) | be16(b, at + 2); }
uint32_t le16(const std::vector<uint8_t>& b, size_t at) { return b[at] | (uint32_t(b[at + 1]) << 8); }
uint32_t le24(const std::vector<uint8_t>& b, size_t at) { return le16(b, at) | (uint32_t(b[at + 2]) << 16); }

bool starts_with(const std::vector<uint8_t>& b, const char* text, size_t at = 0) {
    size_t n = std::strlen(text);
    return b.size() >= at + n && std::memcmp(b.data() + at, text, n) == 0;
}

// PNG: signature, then the IHDR chunk with width and height as big-endian u32.
AssetInfo check_png(const std::vector<uint8_t>& b) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() < 8 || std::memcmp(b.data(), signature, 8) != 0) return bad("the file does not start with the PNG signature");
    if (b.size() < 24 || be32(b, 8) != 13 || !starts_with(b, "IHDR", 12)) return bad("the PNG file has no IHDR chunk at the start");
    return sized(be32(b, 16), be32(b, 20));
}

// JPEG: FF D8 FF, then segments. The first start-of-frame segment holds height and width (big-endian u16).
AssetInfo check_jpeg(const std::vector<uint8_t>& b) {
    if (b.size() < 3 || b[0] != 0xFF || b[1] != 0xD8 || b[2] != 0xFF) return bad("the file does not start with the JPEG signature");
    size_t at = 2;
    while (at + 4 <= b.size()) {
        if (b[at] != 0xFF) return bad("the JPEG segments are malformed");
        uint8_t marker = b[at + 1];
        if (marker == 0xFF) { at++; continue; }                                            // fill byte
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) { at += 2; continue; }   // no length
        size_t length = be16(b, at + 2);
        if (length < 2) return bad("a JPEG segment has an invalid length");
        bool frame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (frame) {
            if (length < 8 || at + 9 > b.size()) return bad("the JPEG frame header is truncated");
            return sized(be16(b, at + 7), be16(b, at + 5));
        }
        if (marker == 0xD9 || marker == 0xDA) break;  // end of image or scan before any frame header
        at += 2 + length;
    }
    return bad("the JPEG file has no frame header");
}

// WebP: a RIFF container whose first chunk is VP8 (lossy), VP8L (lossless) or VP8X (extended).
AssetInfo check_webp(const std::vector<uint8_t>& b) {
    if (b.size() < 20 || !starts_with(b, "RIFF") || !starts_with(b, "WEBP", 8)) return bad("the file does not start with a RIFF WebP header");
    const size_t data = 20;  // first byte of the chunk data
    if (starts_with(b, "VP8 ", 12)) {
        if (b.size() < data + 10 || b[data + 3] != 0x9D || b[data + 4] != 0x01 || b[data + 5] != 0x2A) return bad("the VP8 frame header is invalid");
        return sized(le16(b, data + 6) & 0x3FFF, le16(b, data + 8) & 0x3FFF);
    }
    if (starts_with(b, "VP8L", 12)) {
        if (b.size() < data + 5 || b[data] != 0x2F) return bad("the VP8L header is invalid");
        uint32_t bits = b[data + 1] | (uint32_t(b[data + 2]) << 8) | (uint32_t(b[data + 3]) << 16) | (uint32_t(b[data + 4]) << 24);
        return sized((bits & 0x3FFFu) + 1, ((bits >> 14) & 0x3FFFu) + 1);
    }
    if (starts_with(b, "VP8X", 12)) {
        if (b.size() < data + 10) return bad("the VP8X header is truncated");
        return sized(uint64_t(le24(b, data + 4)) + 1, uint64_t(le24(b, data + 7)) + 1);
    }
    return bad("the WebP file has no VP8, VP8L or VP8X chunk at the start");
}

} // namespace

bool is_supported_asset_extension(const std::string& extension) {
    return extension == "webp" || extension == "png" || extension == "jpg" || extension == "pdf";
}

AssetInfo check_asset_content(const std::string& extension, const std::vector<uint8_t>& bytes) {
    if (extension == "png")  return check_png(bytes);
    if (extension == "jpg")  return check_jpeg(bytes);
    if (extension == "webp") return check_webp(bytes);
    if (extension == "pdf") {
        if (!starts_with(bytes, "%PDF-")) return bad("the file does not start with %PDF-");
        AssetInfo info;
        info.ok = true;
        return info;
    }
    return bad("the extension is not supported");
}
