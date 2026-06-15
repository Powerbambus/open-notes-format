#include "onf_writer.h"
#include "onf_types.h"

#include <zip.h>
#include <nlohmann/json.hpp>

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

using json = nlohmann::json;

template<typename T>
void write_le(std::vector<uint8_t>& buf, T value) {
    for (size_t i = 0; i < sizeof(T); i++) {
        buf.push_back(static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
    }
}

void write_varint(std::vector<uint8_t>& buf, uint32_t value) {
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        buf.push_back(byte);
    } while (value != 0);
}

uint32_t zigzag_encode(int32_t n) {
    return (static_cast<uint32_t>(n) << 1) ^ static_cast<uint32_t>(n >> 31);
}

void write_zigzag_varint(std::vector<uint8_t>& buf, int32_t value) {
    write_varint(buf, zigzag_encode(value));
}

std::vector<uint8_t> build_strokes_bin(const Page& page) {
    std::vector<uint8_t> buf;

    buf.push_back(0x4F);
    buf.push_back(0x4E);
    buf.push_back(0x46);
    buf.push_back(0x53);
    write_le<uint16_t>(buf, 1);
    write_le<uint16_t>(buf, 0);

    for (const Stroke& stroke : page.strokes) {
        std::vector<uint8_t> stroke_body;

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

        int32_t  prev_x        = 0;
        int32_t  prev_y        = 0;
        int32_t  prev_pressure = 0;
        uint32_t prev_timestamp = 0;
        int32_t  prev_tilt_x   = 0;
        int32_t  prev_tilt_y   = 0;
        int32_t  prev_azimuth  = 0;

        for (size_t i = 0; i < stroke.samples.size(); i++) {
            const Sample& s = stroke.samples[i];
            bool first = (i == 0);

            if (first) {
                write_varint(stroke_body, static_cast<uint32_t>(s.x));
                write_varint(stroke_body, static_cast<uint32_t>(s.y));
                write_varint(stroke_body, s.pressure);
                write_varint(stroke_body, s.timestamp);
                if (has_tilt) {
                    write_varint(stroke_body, static_cast<uint32_t>(s.tilt_x));
                    write_varint(stroke_body, static_cast<uint32_t>(s.tilt_y));
                }
                if (has_azimuth) {
                    write_varint(stroke_body, s.azimuth);
                }
            } else {
                write_zigzag_varint(stroke_body, s.x - prev_x);
                write_zigzag_varint(stroke_body, s.y - prev_y);
                write_zigzag_varint(stroke_body, static_cast<int32_t>(s.pressure) - prev_pressure);
                write_varint(stroke_body,        s.timestamp - prev_timestamp);
                if (has_tilt) {
                    write_zigzag_varint(stroke_body, static_cast<int32_t>(s.tilt_x) - prev_tilt_x);
                    write_zigzag_varint(stroke_body, static_cast<int32_t>(s.tilt_y) - prev_tilt_y);
                }
                if (has_azimuth) {
                    write_zigzag_varint(stroke_body, static_cast<int32_t>(s.azimuth) - prev_azimuth);
                }
            }

            prev_x         = s.x;
            prev_y         = s.y;
            prev_pressure  = s.pressure;
            prev_timestamp = s.timestamp;
            prev_tilt_x    = s.tilt_x;
            prev_tilt_y    = s.tilt_y;
            prev_azimuth   = s.azimuth;
        }

        write_le<uint16_t>(buf, 0x0001);
        write_le<uint32_t>(buf, static_cast<uint32_t>(stroke_body.size()));
        buf.insert(buf.end(), stroke_body.begin(), stroke_body.end());
    }

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
    return {
        {"width",  page.width},
        {"height", page.height},
        {"background", {
            {"type",  page.bg_type},
            {"color", page.bg_color}
        }},
        {"text_objects", text_objects_array},
        {"assets",       json::array()}
    };
}

bool write_onf(const Document& doc, const std::string& output_path) {
    int zip_error = 0;
    zip_t* archive = zip_open(output_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    if (!archive) return false;

    std::vector<std::string>          string_buffers;
    std::vector<std::vector<uint8_t>> binary_buffers;

    // --- manifest.json (DEFLATE compressed — Section 4.5) ---
    string_buffers.push_back(build_manifest(doc).dump(4));
    const std::string& manifest_str = string_buffers.back();
    zip_source_t* manifest_src = zip_source_buffer(
        archive, manifest_str.data(), manifest_str.size(), 0);
    zip_int64_t manifest_idx = zip_file_add(
        archive, "manifest.json", manifest_src, ZIP_FL_OVERWRITE);
    zip_set_file_compression(archive, manifest_idx, ZIP_CM_DEFLATE, 6);

    // --- pages ---
    for (const Page& page : doc.pages) {

        // meta.json (DEFLATE compressed — Section 4.5)
        string_buffers.push_back(build_page_meta(page).dump(4));
        const std::string& meta_str = string_buffers.back();
        std::string meta_path = "pages/" + page.id + "/meta.json";
        zip_source_t* meta_src = zip_source_buffer(
            archive, meta_str.data(), meta_str.size(), 0);
        zip_int64_t meta_idx = zip_file_add(
            archive, meta_path.c_str(), meta_src, ZIP_FL_OVERWRITE);
        zip_set_file_compression(archive, meta_idx, ZIP_CM_DEFLATE, 6);

        // strokes.bin (no compression — Section 4.5)
        binary_buffers.push_back(build_strokes_bin(page));
        const std::vector<uint8_t>& strokes_data = binary_buffers.back();
        std::string strokes_path = "pages/" + page.id + "/strokes.bin";
        zip_source_t* strokes_src = zip_source_buffer(
            archive, strokes_data.data(), strokes_data.size(), 0);
        zip_int64_t strokes_idx = zip_file_add(
            archive, strokes_path.c_str(), strokes_src, ZIP_FL_OVERWRITE);
        zip_set_file_compression(archive, strokes_idx, ZIP_CM_STORE, 0);
    }
    zip_dir_add(archive, "assets", ZIP_FL_OVERWRITE);

    zip_close(archive);
    return true;
}