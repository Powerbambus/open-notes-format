#include "onf_writer.h"
#include "onf_types.h"

#include <zip.h>
#include <nlohmann/json.hpp>

#include <vector>
#include <string>
#include <set>
#include <regex>
#include <iostream>
#include <cstdint>

using json = nlohmann::json;

namespace {

constexpr int64_t  MAX_JSON_INT        = 9007199254740991;  // 2^53 - 1, Section 2.3.5
constexpr size_t   MAX_PAGES           = 99999;             // Section 4.2
constexpr size_t   MAX_CONTENT_CHARS   = 1048576;           // Section 12.6
constexpr size_t   MAX_STRING_CHARS    = 65535;             // Section 12.6

// Writes a little-endian value. Section 2.3.3
template<typename T>
void write_le(std::vector<uint8_t>& buf, T value) {
    for (size_t i = 0; i < sizeof(T); i++) {
        buf.push_back(static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
    }
}

// Writes the canonical unsigned LEB128 encoding of value. Appendix A.2
// The value must not exceed 2^33 - 1 (Section 2.3.2).
void write_varint(std::vector<uint8_t>& buf, uint64_t value) {
    while (value > 0x7F) {
        buf.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(value & 0x7F));
}

// Zigzag encoding on 64-bit values. Section 7.5.3 and Appendix A.1
uint64_t zigzag_encode(int64_t n) {
    return (static_cast<uint64_t>(n) << 1) ^ static_cast<uint64_t>(n >> 63);
}

void write_zigzag_varint(std::vector<uint8_t>& buf, int64_t value) {
    write_varint(buf, zigzag_encode(value));
}

// Number of Unicode code points in a UTF-8 string.
size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}

bool is_color_string(const std::string& s) {
    static const std::regex re("^#[0-9A-Fa-f]{8}$");
    return std::regex_match(s, re);
}

// Checks that a Document can be written as a conforming ONF file.
// Returns false and prints the reason if a rule of the specification would be
// violated. Section references are given next to each check.
bool validate_document(const Document& doc) {
    auto fail = [](const std::string& msg) {
        std::cerr << "Cannot write document: " << msg << std::endl;
        return false;
    };

    // Section 6.1.2: version format
    static const std::regex version_re("^[0-9]+\\.[0-9]+\\.[0-9]+$");
    if (!std::regex_match(doc.onf_version, version_re))
        return fail("onf_version must have the format MAJOR.MINOR.PATCH");

    // Section 6.1.2: 1 to 99999 pages, ids p00001..pN, orders 1..N, each once
    const size_t n = doc.pages.size();
    if (n < 1 || n > MAX_PAGES) return fail("page count must be between 1 and 99999");
    std::vector<bool> seen_id(n + 1, false), seen_order(n + 1, false);
    for (const Page& page : doc.pages) {
        static const std::regex id_re("^p[0-9]{5}$");
        if (!std::regex_match(page.id, id_re)) return fail("invalid page id " + page.id);
        size_t id_num = std::stoul(page.id.substr(1));
        if (id_num < 1 || id_num > n || seen_id[id_num])
            return fail("page ids must be p00001 to pNNNNN without gaps or duplicates");
        seen_id[id_num] = true;
        if (page.order < 1 || static_cast<size_t>(page.order) > n || seen_order[page.order])
            return fail("page order values must be 1 to N without gaps or duplicates");
        seen_order[page.order] = true;
    }

    for (const Page& page : doc.pages) {
        const std::string where = "page " + page.id + ": ";
        if (page.width <= 0 || page.height <= 0) return fail(where + "width and height must be positive");

        // Section 6.3.1: required background fields
        if (!is_color_string(page.bg_color)) return fail(where + "invalid background color");
        if (page.bg_type != "blank" && page.bg_type != "ruled" &&
            page.bg_type != "grid"  && page.bg_type != "dotted")
            return fail(where + "unknown background type");
        if (page.bg_type != "blank") {
            if (!is_color_string(page.bg_foreground_color))
                return fail(where + "background foreground_color is required for this type");
            if (page.bg_spacing <= 0)
                return fail(where + "background spacing must be positive");
        }

        // Sections 7.4.1, 7.4.2, 7.4.3, 7.5.1, 7.6
        std::set<uint32_t> stroke_ids;
        uint64_t last_created = 0;
        for (const Stroke& st : page.strokes) {
            const std::string sw = where + "stroke " + std::to_string(st.stroke_id) + ": ";
            if (!stroke_ids.insert(st.stroke_id).second) return fail(sw + "duplicate stroke_id");
            if (st.created_at < last_created) return fail(sw + "strokes must be in ascending created_at order");
            last_created = st.created_at;
            if (st.created_at > static_cast<uint64_t>(MAX_JSON_INT)) return fail(sw + "created_at too large");
            if (st.tool_type > ToolType::LINE_ERASER) return fail(sw + "tool type is reserved");
            if (st.base_width == 0) return fail(sw + "base_width must be greater than 0");
            if (st.flags & ~(StrokeFlags::HAS_TILT | StrokeFlags::HAS_AZIMUTH | StrokeFlags::NO_PRESSURE))
                return fail(sw + "undefined flag bits must be 0");
            if (st.samples.empty()) return fail(sw + "sample_count must be greater than 0");
            const bool tilt = st.flags & StrokeFlags::HAS_TILT;
            const bool az   = st.flags & StrokeFlags::HAS_AZIMUTH;
            const bool nop  = st.flags & StrokeFlags::NO_PRESSURE;
            uint32_t prev_t = 0;
            for (const Sample& s : st.samples) {
                if (s.timestamp < prev_t) return fail(sw + "timestamps must not decrease");
                prev_t = s.timestamp;
                if (nop && s.pressure != 255) return fail(sw + "NO_PRESSURE requires pressure 255");
                if (tilt && (s.tilt_x < -90 || s.tilt_x > 90 || s.tilt_y < -90 || s.tilt_y > 90))
                    return fail(sw + "tilt must be between -90 and 90");
                if (az && s.azimuth > 35999) return fail(sw + "azimuth must be between 0 and 35999");
            }
        }

        // Sections 8.3, 8.4.1, 8.7
        std::set<std::string> text_ids;
        last_created = 0;
        for (const TextObject& t : page.text_objects) {
            const std::string tw = where + "text object " + t.id + ": ";
            if (!text_ids.insert(t.id).second) return fail(tw + "duplicate id");
            if (t.created_at < last_created) return fail(tw + "text objects must be in ascending created_at order");
            last_created = t.created_at;
            if (t.created_at > static_cast<uint64_t>(MAX_JSON_INT)) return fail(tw + "created_at too large");
            if (t.width <= 0 || t.height <= 0 || t.font_size <= 0)
                return fail(tw + "width, height and font_size must be positive");
            if (utf8_length(t.content) > MAX_CONTENT_CHARS) return fail(tw + "content too long");
            if (utf8_length(t.id) > MAX_STRING_CHARS || utf8_length(t.font_family) > MAX_STRING_CHARS)
                return fail(tw + "string too long");
            if (t.generic_family != "sans-serif" && t.generic_family != "serif" && t.generic_family != "monospace")
                return fail(tw + "generic_family must be sans-serif, serif or monospace");
            if (!is_color_string(t.color)) return fail(tw + "invalid color");
            if (t.layer != "back" && t.layer != "middle" && t.layer != "front")
                return fail(tw + "layer must be back, middle or front");
        }
    }
    return true;
}

std::vector<uint8_t> build_strokes_bin(const Page& page) {
    std::vector<uint8_t> buf;

    // File header - Section 7.2
    buf.push_back(0x4F);  // 'O'
    buf.push_back(0x4E);  // 'N'
    buf.push_back(0x46);  // 'F'
    buf.push_back(0x53);  // 'S'
    write_le<uint16_t>(buf, 1);  // version
    write_le<uint16_t>(buf, 0);  // reserved

    for (const Stroke& stroke : page.strokes) {
        std::vector<uint8_t> stroke_body;

        // Stroke header - Section 7.4.1
        write_le<uint32_t>(stroke_body, stroke.stroke_id);
        write_le<uint64_t>(stroke_body, stroke.created_at);
        stroke_body.push_back(stroke.color_r);
        stroke_body.push_back(stroke.color_g);
        stroke_body.push_back(stroke.color_b);
        stroke_body.push_back(stroke.color_a);
        write_le<uint16_t>(stroke_body, stroke.tool_type);
        write_le<uint16_t>(stroke_body, stroke.base_width);
        write_le<uint16_t>(stroke_body, stroke.flags);
        write_le<uint32_t>(stroke_body, static_cast<uint32_t>(stroke.samples.size()));

        bool has_tilt    = stroke.flags & StrokeFlags::HAS_TILT;
        bool has_azimuth = stroke.flags & StrokeFlags::HAS_AZIMUTH;

        // Sample block - Section 7.5. Every sample, including the first one,
        // stores the difference to the previous sample. For the first sample
        // the previous values are zero (Section 7.5.2).
        int64_t prev_x = 0, prev_y = 0, prev_pressure = 0;
        int64_t prev_timestamp = 0;
        int64_t prev_tilt_x = 0, prev_tilt_y = 0, prev_azimuth = 0;

        for (const Sample& s : stroke.samples) {
            write_zigzag_varint(stroke_body, s.x - prev_x);
            write_zigzag_varint(stroke_body, s.y - prev_y);
            write_zigzag_varint(stroke_body, s.pressure - prev_pressure);
            write_varint(stroke_body, static_cast<uint64_t>(s.timestamp - prev_timestamp));  // plain varint
            if (has_tilt) {
                write_zigzag_varint(stroke_body, s.tilt_x - prev_tilt_x);
                write_zigzag_varint(stroke_body, s.tilt_y - prev_tilt_y);
            }
            if (has_azimuth) {
                write_zigzag_varint(stroke_body, s.azimuth - prev_azimuth);
            }

            prev_x         = s.x;
            prev_y         = s.y;
            prev_pressure  = s.pressure;
            prev_timestamp = s.timestamp;
            prev_tilt_x    = s.tilt_x;
            prev_tilt_y    = s.tilt_y;
            prev_azimuth   = s.azimuth;
        }

        // Chunk frame - Section 7.3
        write_le<uint16_t>(buf, 0x0001);  // STROKE
        write_le<uint32_t>(buf, static_cast<uint32_t>(stroke_body.size()));
        buf.insert(buf.end(), stroke_body.begin(), stroke_body.end());
    }

    // END chunk, empty body - Section 7.3.1
    write_le<uint16_t>(buf, 0x0002);
    write_le<uint32_t>(buf, 0);

    return buf;
}

json build_manifest(const Document& doc) {
    json pages_array = json::array();
    for (const Page& page : doc.pages) {
        pages_array.push_back({
            {"id",    page.id},
            {"order", page.order}
        });
    }
    return {
        {"onf_version", doc.onf_version},
        {"title",       doc.title},
        {"created_at",  doc.created_at},
        {"modified_at", doc.modified_at},
        {"pages",       pages_array}
    };
}

json build_page_meta(const Page& page) {
    json text_objects_array = json::array();
    for (const TextObject& t : page.text_objects) {
        text_objects_array.push_back({
            {"id",             t.id},
            {"created_at",     t.created_at},
            {"x",              t.x},
            {"y",              t.y},
            {"width",          t.width},
            {"height",         t.height},
            {"content",        t.content},
            {"font_family",    t.font_family},
            {"generic_family", t.generic_family},
            {"font_size",      t.font_size},
            {"color",          t.color},
            {"bold",           t.bold},
            {"italic",         t.italic},
            {"layer",          t.layer}
        });
    }

    // All required background fields are written explicitly - Section 6.3.1
    json background = {
        {"type",  page.bg_type},
        {"color", page.bg_color}
    };
    if (page.bg_type != "blank") {
        background["foreground_color"] = page.bg_foreground_color;
        background["spacing"]          = page.bg_spacing;
    }

    return {
        {"width",        page.width},
        {"height",       page.height},
        {"background",   background},
        {"text_objects", text_objects_array},
        {"assets",       json::array()}
    };
}

} // namespace

bool write_onf(const Document& doc, const std::string& output_path) {
    if (!validate_document(doc)) return false;

    int zip_error = 0;
    zip_t* archive = zip_open(output_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    if (!archive) return false;

    // libzip keeps pointers into these buffers until zip_close(), so they
    // must stay alive and must not move until then. Reserving the final
    // capacity up front guarantees that push_back never reallocates.
    std::vector<std::string>          string_buffers;
    std::vector<std::vector<uint8_t>> binary_buffers;
    string_buffers.reserve(1 + doc.pages.size());
    binary_buffers.reserve(doc.pages.size());

    // --- manifest.json (DEFLATE compressed, Section 4.5) ---
    string_buffers.push_back(build_manifest(doc).dump(4));
    const std::string& manifest_str = string_buffers.back();
    zip_source_t* manifest_src = zip_source_buffer(
        archive, manifest_str.data(), manifest_str.size(), 0);
    zip_int64_t manifest_idx = zip_file_add(
        archive, "manifest.json", manifest_src, ZIP_FL_OVERWRITE);
    zip_set_file_compression(archive, manifest_idx, ZIP_CM_DEFLATE, 6);

    // --- pages ---
    for (const Page& page : doc.pages) {

        // meta.json (DEFLATE compressed, Section 4.5)
        string_buffers.push_back(build_page_meta(page).dump(4));
        const std::string& meta_str = string_buffers.back();
        std::string meta_path = "pages/" + page.id + "/meta.json";
        zip_source_t* meta_src = zip_source_buffer(
            archive, meta_str.data(), meta_str.size(), 0);
        zip_int64_t meta_idx = zip_file_add(
            archive, meta_path.c_str(), meta_src, ZIP_FL_OVERWRITE);
        zip_set_file_compression(archive, meta_idx, ZIP_CM_DEFLATE, 6);

        // strokes.bin (no compression, Section 4.5)
        binary_buffers.push_back(build_strokes_bin(page));
        const std::vector<uint8_t>& strokes_data = binary_buffers.back();
        std::string strokes_path = "pages/" + page.id + "/strokes.bin";
        zip_source_t* strokes_src = zip_source_buffer(
            archive, strokes_data.data(), strokes_data.size(), 0);
        zip_int64_t strokes_idx = zip_file_add(
            archive, strokes_path.c_str(), strokes_src, ZIP_FL_OVERWRITE);
        zip_set_file_compression(archive, strokes_idx, ZIP_CM_STORE, 0);
    }

    // The assets/ directory is required even without assets (Section 4.2).
    // libzip stores it as an explicit directory entry "assets/".
    zip_dir_add(archive, "assets", ZIP_FL_OVERWRITE);

    zip_close(archive);
    return true;
}
