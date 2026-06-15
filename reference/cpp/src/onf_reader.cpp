#include "onf_reader.h"
#include "onf_types.h"

#include <zip.h>
#include <nlohmann/json.hpp>

#include <vector>
#include <string>
#include <cstdint>
#include <iostream>

using json = nlohmann::json;

// Reads a single file from an open ZIP archive into a byte buffer.
// Returns an empty vector if the file doesn't exist or can't be read.
std::vector<uint8_t> read_zip_entry(zip_t* archive, const std::string& path) {
    // Look up the file by name inside the archive
    zip_file_t* zf = zip_fopen(archive, path.c_str(), 0);
    if (!zf) {
        std::cerr << "Cannot open entry: " << path << std::endl;
        return {};
    }

    // Get the uncompressed size so we can allocate exactly the right buffer
    zip_stat_t stat;
    zip_stat_init(&stat);
    zip_stat(archive, path.c_str(), 0, &stat);

    // Allocate the buffer and read all bytes in one call
    std::vector<uint8_t> buf(stat.size);
    zip_fread(zf, buf.data(), stat.size);
    zip_fclose(zf);

    return buf;
}

// Reads a little-endian value from buf at the given offset.
// Advances offset by the size of T.
template<typename T>
T read_le(const std::vector<uint8_t>& buf, size_t& offset) {
    T value = 0;
    for (size_t i = 0; i < sizeof(T); i++) {
        value |= static_cast<T>(buf[offset++]) << (i * 8);
    }
    return value;
}

// Decodes an unsigned LEB128 varint from buf at offset.
// Advances offset past the varint bytes.
uint32_t read_varint(const std::vector<uint8_t>& buf, size_t& offset) {
    uint32_t result = 0;
    uint32_t shift  = 0;
    while (true) {
        uint8_t byte = buf[offset++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;  // no continuation bit = last byte
        shift += 7;
    }
    return result;
}

// Zigzag-decodes an unsigned value back to a signed integer.
int32_t zigzag_decode(uint32_t n) {
    return static_cast<int32_t>((n >> 1) ^ -(n & 1));
}

// Reads a zigzag varint and returns the signed result.
int32_t read_zigzag_varint(const std::vector<uint8_t>& buf, size_t& offset) {
    return zigzag_decode(read_varint(buf, offset));
}

bool parse_strokes_bin(const std::vector<uint8_t>& buf, Page& page) {
    size_t offset = 0;

    // --- Verify file header (Section 7.2) ---
    if (buf.size() < 8) {
        std::cerr << "strokes.bin too small" << std::endl;
        return false;
    }

    // Check magic bytes: must be "ONFS"
    if (buf[0] != 0x4F || buf[1] != 0x4E ||
        buf[2] != 0x46 || buf[3] != 0x53) {
        std::cerr << "strokes.bin: invalid magic bytes" << std::endl;
        return false;
    }
    offset += 4;

    uint16_t version  = read_le<uint16_t>(buf, offset);
    uint16_t reserved = read_le<uint16_t>(buf, offset);
    (void)reserved;  // spec says ignore non-zero reserved bytes

    if (version != 1) {
        // Per Section 7.2: signal mismatch but still attempt to parse
        std::cerr << "strokes.bin: unexpected version " << version << std::endl;
    }

    // --- Read chunks (Section 7.3) ---
    while (offset + 6 <= buf.size()) {
        uint16_t chunk_type   = read_le<uint16_t>(buf, offset);
        uint32_t chunk_length = read_le<uint32_t>(buf, offset);

        if (chunk_type == 0x0002) {
            // END chunk — stop reading
            break;
        }

        if (chunk_type != 0x0001) {
            // Unknown chunk type — skip it per Section 10.5.2
            offset += chunk_length;
            continue;
        }

        // --- Parse STROKE chunk body (Section 7.4) ---
        size_t chunk_start = offset;

        Stroke stroke;
        stroke.stroke_id  = read_le<uint32_t>(buf, offset);
        stroke.created_at = read_le<uint64_t>(buf, offset);
        stroke.color_r    = buf[offset++];
        stroke.color_g    = buf[offset++];
        stroke.color_b    = buf[offset++];
        stroke.color_a    = buf[offset++];
        stroke.tool_type  = read_le<uint16_t>(buf, offset);
        stroke.base_width = read_le<uint16_t>(buf, offset);
        stroke.flags      = read_le<uint16_t>(buf, offset);
        uint32_t sample_count = read_le<uint32_t>(buf, offset);

        bool has_tilt    = stroke.flags & StrokeFlags::HAS_TILT;
        bool has_azimuth = stroke.flags & StrokeFlags::HAS_AZIMUTH;

        // --- Parse sample block (Section 7.5) ---
        int32_t  prev_x         = 0;
        int32_t  prev_y         = 0;
        int32_t  prev_pressure  = 0;
        uint32_t prev_timestamp = 0;
        int32_t  prev_tilt_x   = 0;
        int32_t  prev_tilt_y   = 0;
        int32_t  prev_azimuth  = 0;

        for (uint32_t i = 0; i < sample_count; i++) {
            Sample s;
            bool first = (i == 0);

            if (first) {
                // First sample: absolute values, plain varint
                s.x         = static_cast<int32_t>(read_varint(buf, offset));
                s.y         = static_cast<int32_t>(read_varint(buf, offset));
                s.pressure  = static_cast<uint8_t>(read_varint(buf, offset));
                s.timestamp = read_varint(buf, offset);
                if (has_tilt) {
                    s.tilt_x = static_cast<int8_t>(read_varint(buf, offset));
                    s.tilt_y = static_cast<int8_t>(read_varint(buf, offset));
                }
                if (has_azimuth) {
                    s.azimuth = static_cast<uint16_t>(read_varint(buf, offset));
                }
            } else {
                // Subsequent samples: deltas
                s.x         = prev_x        + read_zigzag_varint(buf, offset);
                s.y         = prev_y        + read_zigzag_varint(buf, offset);
                s.pressure  = static_cast<uint8_t>(
                                prev_pressure + read_zigzag_varint(buf, offset));
                s.timestamp = prev_timestamp + read_varint(buf, offset);
                if (has_tilt) {
                    s.tilt_x = static_cast<int8_t>(
                                prev_tilt_x + read_zigzag_varint(buf, offset));
                    s.tilt_y = static_cast<int8_t>(
                                prev_tilt_y + read_zigzag_varint(buf, offset));
                }
                if (has_azimuth) {
                    s.azimuth = static_cast<uint16_t>(
                                prev_azimuth + read_zigzag_varint(buf, offset));
                }
            }

            // For optional fields not present, zero-initialise
            if (!has_tilt)    { s.tilt_x = 0; s.tilt_y = 0; }
            if (!has_azimuth) { s.azimuth = 0; }

            prev_x         = s.x;
            prev_y         = s.y;
            prev_pressure  = s.pressure;
            prev_timestamp = s.timestamp;
            prev_tilt_x    = s.tilt_x;
            prev_tilt_y    = s.tilt_y;
            prev_azimuth   = s.azimuth;

            stroke.samples.push_back(s);
        }

        page.strokes.push_back(stroke);

        // Safety check: did we consume exactly chunk_length bytes?
        size_t bytes_consumed = offset - chunk_start;
        if (bytes_consumed != chunk_length) {
            std::cerr << "strokes.bin: chunk length mismatch" << std::endl;
            return false;
        }
    }

    return true;
}

bool parse_page_meta(const std::string& json_str, Page& page) {
    json meta = json::parse(json_str);

    page.width  = meta["width"];
    page.height = meta["height"];

    page.bg_type  = meta["background"]["type"];
    page.bg_color = meta["background"]["color"];

    // Parse text objects
    for (const auto& t : meta["text_objects"]) {
        TextObject text;
        text.id             = t["id"];
        text.created_at     = t["created_at"];
        text.x              = t["x"];
        text.y              = t["y"];
        text.width          = t["width"];
        text.height         = t["height"];
        text.content        = t["content"];
        text.font_family    = t["font_family"];
        text.generic_family = t["generic_family"];
        text.font_size      = t["font_size"];
        text.color          = t["color"];
        text.bold           = t["bold"];
        text.italic         = t["italic"];
        text.layer          = t.value("layer", "middle"); // optional field
        page.text_objects.push_back(text);
    }

    return true;
}

bool read_onf(const std::string& input_path, Document& doc) {
    int zip_error = 0;
    zip_t* archive = zip_open(input_path.c_str(), ZIP_RDONLY, &zip_error);
    if (!archive) {
        std::cerr << "Cannot open file: " << input_path << std::endl;
        return false;
    }

    // --- Read and parse manifest.json ---
    std::vector<uint8_t> manifest_buf = read_zip_entry(archive, "manifest.json");
    if (manifest_buf.empty()) {
        zip_close(archive);
        return false;
    }

    // Convert byte buffer to string for JSON parsing
    std::string manifest_str(manifest_buf.begin(), manifest_buf.end());
    json manifest = json::parse(manifest_str);

    doc.onf_version = manifest["onf_version"];
    doc.title       = manifest["title"];
    doc.created_at  = manifest["created_at"];
    doc.modified_at = manifest["modified_at"];

    // --- Read each page in order ---
    // Sort pages by their order field before reading (Section 6.1.2)
    std::vector<json> page_entries(
        manifest["pages"].begin(),
        manifest["pages"].end()
    );
    std::sort(page_entries.begin(), page_entries.end(),
        [](const json& a, const json& b) {
            return a["order"] < b["order"];
        }
    );

    for (const json& page_entry : page_entries) {
        Page page;
        page.id    = page_entry["id"];
        page.order = page_entry["order"];

        // Read meta.json
        std::string meta_path = "pages/" + page.id + "/meta.json";
        std::vector<uint8_t> meta_buf = read_zip_entry(archive, meta_path);
        if (meta_buf.empty()) {
            zip_close(archive);
            return false;
        }
        std::string meta_str(meta_buf.begin(), meta_buf.end());
        if (!parse_page_meta(meta_str, page)) {
            zip_close(archive);
            return false;
        }

        // Read strokes.bin
        std::string strokes_path = "pages/" + page.id + "/strokes.bin";
        std::vector<uint8_t> strokes_buf = read_zip_entry(archive, strokes_path);
        if (strokes_buf.empty()) {
            zip_close(archive);
            return false;
        }
        if (!parse_strokes_bin(strokes_buf, page)) {
            zip_close(archive);
            return false;
        }

        doc.pages.push_back(page);
    }

    zip_close(archive);
    return true;
}