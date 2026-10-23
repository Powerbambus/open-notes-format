#include "onf_writer.h"
#include "onf_types.h"
#include "asset_check.h"
#include "sha256.h"

#include <zip.h>
#include <nlohmann/json.hpp>

#include <vector>
#include <string>
#include <map>
#include <set>
#include <regex>
#include <iostream>
#include <cstdint>
#include <variant>

using json = nlohmann::json;

namespace {

constexpr int64_t  MAX_JSON_INT        = 9007199254740991;  // 2^53 - 1, Section 2.3.5
constexpr size_t   MAX_PAGES           = 99999;             // Section 4.2
constexpr size_t   MAX_CONTENT_CHARS   = 1048576;           // Section 12.6
constexpr size_t   MAX_STRING_CHARS    = 65535;             // Section 12.6
constexpr size_t   MAX_SPANS           = 65535;             // Section 8.3.1

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
    if (doc.generator && utf8_length(*doc.generator) > MAX_STRING_CHARS)
        return fail("generator too long");

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

    // Sections 9.2.1 to 9.2.3, 9.5: the files that will be embedded, by archive name. Each one must be a
    // supported format and must really be one (magic bytes, size limit).
    std::map<std::string, AssetInfo> embedded;
    for (const AssetFile& file : doc.asset_files) {
        const std::string name = sha256_hex(file.bytes) + "." + file.extension;
        if (!is_supported_asset_extension(file.extension))
            return fail("asset " + name + ": the format must be one of webp, png, jpg, pdf");
        AssetInfo info = check_asset_content(file.extension, file.bytes);
        if (!info.ok) return fail("asset " + name + ": " + info.problem);
        embedded[name] = info;
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
            if (page.bg_top_margin < 0) return fail(where + "background top_margin must not be negative");
            if (page.bg_line_width < 0) return fail(where + "background line_width must not be negative");
            if (page.bg_line_width > 0 && page.bg_type != "ruled" && page.bg_type != "grid")
                return fail(where + "background line_width is only for ruled and grid");
            if (page.bg_spacing <= 0)
                return fail(where + "background spacing must be positive");
        }

        // Sections 5.6, 7.4.1, 7.4.2, 7.4.3, 7.5.1, 7.9
        // stroke_id is unique across strokes and shapes (Section 7.4.1).
        std::set<uint32_t> stroke_ids;
        for (const Ink& item : page.ink) {
            if (const Stroke* st = std::get_if<Stroke>(&item)) {
                const std::string sw = where + "stroke " + std::to_string(st->stroke_id) + ": ";
                if (!stroke_ids.insert(st->stroke_id).second) return fail(sw + "duplicate stroke_id");
                if (st->layer < MIN_LAYER) return fail(sw + "layer must be between 1 and 65535");
                if (st->created_at && *st->created_at > static_cast<uint64_t>(MAX_JSON_INT))
                    return fail(sw + "created_at too large");
                if (st->tool_type > ToolType::HIGHLIGHTER) return fail(sw + "tool type must be between 0 and 3");
                if (st->base_width == 0) return fail(sw + "base_width must be greater than 0");
                if (st->flags & ~(StrokeFlags::HAS_TILT | StrokeFlags::HAS_AZIMUTH |
                                  StrokeFlags::NO_PRESSURE | StrokeFlags::NO_TIMESTAMP |
                                  StrokeFlags::HAS_CREATED_AT | StrokeFlags::NO_TAPER_START | StrokeFlags::NO_TAPER_END))
                    return fail(sw + "undefined flag bits must be 0");
                if (st->samples.empty()) return fail(sw + "sample_count must be greater than 0");
                const bool tilt = st->flags & StrokeFlags::HAS_TILT;
                const bool az   = st->flags & StrokeFlags::HAS_AZIMUTH;
                const bool nop  = st->flags & StrokeFlags::NO_PRESSURE;
                const bool nots = st->flags & StrokeFlags::NO_TIMESTAMP;
                uint32_t prev_t = 0;
                for (const Sample& s : st->samples) {
                    if (!nots) {
                        if (s.timestamp < prev_t) return fail(sw + "timestamps must not decrease");
                        prev_t = s.timestamp;
                    }
                    if (nop && s.pressure != 255) return fail(sw + "NO_PRESSURE requires pressure 255");
                    if (tilt && (s.tilt_x < -90 || s.tilt_x > 90 || s.tilt_y < -90 || s.tilt_y > 90))
                        return fail(sw + "tilt must be between -90 and 90");
                    if (az && s.azimuth > 35999) return fail(sw + "azimuth must be between 0 and 35999");
                }
            } else {
                const Shape& sh = std::get<Shape>(item);
                const std::string sw = where + "shape " + std::to_string(sh.stroke_id) + ": ";
                if (!stroke_ids.insert(sh.stroke_id).second) return fail(sw + "duplicate stroke_id");
                if (sh.layer < MIN_LAYER) return fail(sw + "layer must be between 1 and 65535");
                if (sh.created_at && *sh.created_at > static_cast<uint64_t>(MAX_JSON_INT))
                    return fail(sw + "created_at too large");
                if (sh.tool_type > ToolType::HIGHLIGHTER) return fail(sw + "tool type must be between 0 and 3");
                if (sh.base_width == 0) return fail(sw + "base_width must be greater than 0");
                switch (sh.shape_type) {
                    case ShapeType::ELLIPSE:
                        if (sh.p[2] <= 0 || sh.p[3] <= 0) return fail(sw + "rx and ry must be greater than 0");
                        if (sh.rotation > 35999) return fail(sw + "rotation must be between 0 and 35999");
                        break;
                    case ShapeType::RECTANGLE:
                        if (sh.p[2] <= 0 || sh.p[3] <= 0) return fail(sw + "width and height must be greater than 0");
                        if (sh.rotation > 35999) return fail(sw + "rotation must be between 0 and 35999");
                        break;
                    case ShapeType::LINE:
                    case ShapeType::TRIANGLE:
                        break;
                    default:
                        return fail(sw + "shape type is reserved");
                }
            }
        }

        // Sections 5.6, 8.3, 8.3.1, 8.4.1, 8.7
        std::set<std::string> text_ids;
        for (const TextObject& t : page.text_objects) {
            const std::string tw = where + "text object " + t.id + ": ";
            if (!text_ids.insert(t.id).second) return fail(tw + "duplicate id");
            if (t.layer < MIN_LAYER) return fail(tw + "layer must be between 1 and 65535");
            if (t.created_at && *t.created_at > static_cast<uint64_t>(MAX_JSON_INT))
                return fail(tw + "created_at too large");
            if (t.width <= 0 || t.height <= 0 || t.font_size <= 0)
                return fail(tw + "width, height and font_size must be positive");
            if (utf8_length(t.content) > MAX_CONTENT_CHARS) return fail(tw + "content too long");
            if (utf8_length(t.id) > MAX_STRING_CHARS || utf8_length(t.font_family) > MAX_STRING_CHARS)
                return fail(tw + "string too long");
            if (t.generic_family != "sans-serif" && t.generic_family != "serif" && t.generic_family != "monospace")
                return fail(tw + "generic_family must be sans-serif, serif or monospace");
            if (!is_color_string(t.color)) return fail(tw + "invalid color");

            if (t.spans.size() > MAX_SPANS) return fail(tw + "too many spans");
            const size_t content_len = utf8_length(t.content);
            int64_t prev_end = 0;
            for (const Span& sp : t.spans) {
                if (sp.start < 0 || sp.end <= sp.start) return fail(tw + "span end must be greater than start");
                if (sp.start < prev_end) return fail(tw + "spans must be sorted and must not overlap");
                if (static_cast<size_t>(sp.end) > content_len) return fail(tw + "span end exceeds the content");
                prev_end = sp.end;
                if (sp.font_size && *sp.font_size <= 0) return fail(tw + "span font_size must be positive");
                if (sp.color && !is_color_string(*sp.color)) return fail(tw + "invalid span color");
                if (sp.font_family && !sp.generic_family)
                    return fail(tw + "span font_family requires generic_family");
                if (sp.font_family && utf8_length(*sp.font_family) > MAX_STRING_CHARS)
                    return fail(tw + "string too long");
                if (sp.generic_family && *sp.generic_family != "sans-serif" &&
                    *sp.generic_family != "serif" && *sp.generic_family != "monospace")
                    return fail(tw + "span generic_family must be sans-serif, serif or monospace");
            }
        }

        // Sections 5.6, 9.2.2, 9.3.2, 9.4.5, 9.5
        std::set<std::string> asset_ids;
        for (const AssetRef& a : page.assets) {
            const std::string aw = where + "asset " + a.id + ": ";
            if (utf8_length(a.id) > MAX_STRING_CHARS) return fail(aw + "id too long");
            if (!asset_ids.insert(a.id).second) return fail(aw + "duplicate id");
            if (a.layer < MIN_LAYER) return fail(aw + "layer must be between 1 and 65535");
            if (a.created_at && *a.created_at > static_cast<uint64_t>(MAX_JSON_INT))
                return fail(aw + "created_at too large");
            if (a.width <= 0 || a.height <= 0) return fail(aw + "width and height must be greater than 0");
            if (!is_supported_asset_extension(a.extension))
                return fail(aw + "extension must be one of webp, png, jpg, pdf");

            if (a.extension == "pdf") {
                if (!a.pdf_page || *a.pdf_page < 1) return fail(aw + "pdf_page is required for pdf and must be 1 or more");
                if (a.crop) return fail(aw + "crop must not be written for pdf");
            } else if (a.pdf_page) {
                return fail(aw + "pdf_page must not be written for an image");
            }

            auto file = embedded.find(a.hash + "." + a.extension);
            if (file == embedded.end())
                return fail(aw + "the file " + a.hash + "." + a.extension + " is not among the asset files");

            if (a.crop) {
                if (a.crop->x < 0 || a.crop->y < 0) return fail(aw + "crop x and y must be at least 0");
                if (a.crop->width < 1 || a.crop->height < 1) return fail(aw + "crop width and height must be at least 1");
                if (int64_t(a.crop->x) + a.crop->width > file->second.width ||
                    int64_t(a.crop->y) + a.crop->height > file->second.height)
                    return fail(aw + "the crop region must lie inside the image");  // Section 9.4.5
            }
        }
    }
    return true;
}

void write_color(std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    buf.push_back(r);
    buf.push_back(g);
    buf.push_back(b);
    buf.push_back(a);
}

// Chunk frame - Section 7.3
void write_chunk(std::vector<uint8_t>& buf, uint16_t type, const std::vector<uint8_t>& body) {
    write_le<uint16_t>(buf, type);
    write_le<uint32_t>(buf, static_cast<uint32_t>(body.size()));
    buf.insert(buf.end(), body.begin(), body.end());
}

// STROKE chunk body - Sections 7.4 and 7.5
std::vector<uint8_t> build_stroke_body(const Stroke& stroke) {
    std::vector<uint8_t> body;

    // HAS_CREATED_AT is set if and only if created_at is written (Section 7.4.3).
    uint16_t flags = stroke.flags & ~StrokeFlags::HAS_CREATED_AT;
    if (stroke.created_at) flags |= StrokeFlags::HAS_CREATED_AT;

    // Stroke header - Section 7.4.1
    write_le<uint32_t>(body, stroke.stroke_id);
    write_color(body, stroke.color_r, stroke.color_g, stroke.color_b, stroke.color_a);
    write_le<uint16_t>(body, stroke.tool_type);
    write_le<uint16_t>(body, stroke.base_width);
    write_le<uint16_t>(body, flags);
    write_le<uint16_t>(body, stroke.layer);
    write_le<uint32_t>(body, static_cast<uint32_t>(stroke.samples.size()));
    if (stroke.created_at) write_le<uint64_t>(body, *stroke.created_at);

    bool has_tilt    = flags & StrokeFlags::HAS_TILT;
    bool has_azimuth = flags & StrokeFlags::HAS_AZIMUTH;
    bool no_timestamp = flags & StrokeFlags::NO_TIMESTAMP;

    // Sample block - Section 7.5. Every sample, including the first one,
    // stores the difference to the previous sample. For the first sample
    // the previous values are zero (Section 7.5.2).
    int64_t prev_x = 0, prev_y = 0, prev_pressure = 0;
    int64_t prev_timestamp = 0;
    int64_t prev_tilt_x = 0, prev_tilt_y = 0, prev_azimuth = 0;

    for (const Sample& s : stroke.samples) {
        write_zigzag_varint(body, s.x - prev_x);
        write_zigzag_varint(body, s.y - prev_y);
        write_zigzag_varint(body, s.pressure - prev_pressure);
        if (!no_timestamp) {
            write_varint(body, static_cast<uint64_t>(s.timestamp - prev_timestamp));  // plain varint
            prev_timestamp = s.timestamp;
        }
        if (has_tilt) {
            write_zigzag_varint(body, s.tilt_x - prev_tilt_x);
            write_zigzag_varint(body, s.tilt_y - prev_tilt_y);
        }
        if (has_azimuth) {
            write_zigzag_varint(body, s.azimuth - prev_azimuth);
        }

        prev_x         = s.x;
        prev_y         = s.y;
        prev_pressure  = s.pressure;
        prev_tilt_x    = s.tilt_x;
        prev_tilt_y    = s.tilt_y;
        prev_azimuth   = s.azimuth;
    }
    return body;
}

// SHAPE chunk body - Section 7.9.2
std::vector<uint8_t> build_shape_body(const Shape& shape) {
    std::vector<uint8_t> body;

    // Only HAS_CREATED_AT is defined for shapes.
    uint16_t flags = shape.created_at ? StrokeFlags::HAS_CREATED_AT : 0;

    write_le<uint32_t>(body, shape.stroke_id);
    write_color(body, shape.color_r, shape.color_g, shape.color_b, shape.color_a);
    write_le<uint16_t>(body, shape.tool_type);
    write_le<uint16_t>(body, shape.base_width);
    write_le<uint16_t>(body, flags);
    write_le<uint16_t>(body, shape.layer);
    if (shape.created_at) write_le<uint64_t>(body, *shape.created_at);
    write_le<uint16_t>(body, shape.shape_type);

    // Parameters - Section 7.9.4
    size_t coords = 0;
    bool has_rotation = false;
    switch (shape.shape_type) {
        case ShapeType::ELLIPSE:
        case ShapeType::RECTANGLE: coords = 4; has_rotation = true; break;
        case ShapeType::LINE:      coords = 4; break;
        case ShapeType::TRIANGLE:  coords = 6; break;
    }
    for (size_t i = 0; i < coords; i++) write_le<int32_t>(body, shape.p[i]);
    if (has_rotation) write_le<uint16_t>(body, shape.rotation);
    return body;
}

std::vector<uint8_t> build_strokes_bin(const Page& page) {
    std::vector<uint8_t> buf;

    // File header - Section 7.2
    buf.push_back(0x4F);  // 'O'
    buf.push_back(0x4E);  // 'N'
    buf.push_back(0x46);  // 'F'
    buf.push_back(0x53);  // 'S'
    write_le<uint16_t>(buf, 2);  // version
    write_le<uint16_t>(buf, 0);  // reserved

    // Ink is written in the order of page.ink (Section 7.6).
    for (const Ink& item : page.ink) {
        if (const Stroke* stroke = std::get_if<Stroke>(&item))
            write_chunk(buf, 0x0001, build_stroke_body(*stroke));  // STROKE
        else
            write_chunk(buf, 0x0003, build_shape_body(std::get<Shape>(item)));  // SHAPE
    }

    // END chunk, empty body - Section 7.3.1
    write_chunk(buf, 0x0002, {});

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
    json manifest = {
        {"onf_version", doc.onf_version},
        {"title",       doc.title},
        {"created_at",  doc.created_at},
        {"modified_at", doc.modified_at},
        {"pages",       pages_array}
    };
    if (doc.generator) manifest["generator"] = *doc.generator;
    return manifest;
}

json build_page_meta(const Page& page) {
    json text_objects_array = json::array();
    for (const TextObject& t : page.text_objects) {
        json obj = {
            {"id",             t.id},
            {"layer",          t.layer},
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
            {"italic",         t.italic}
        };
        if (t.created_at) obj["created_at"] = *t.created_at;

        // Spans - Section 8.3.1. Only the keys that are set are written.
        if (!t.spans.empty()) {
            json spans_array = json::array();
            for (const Span& sp : t.spans) {
                json span = {{"start", sp.start}, {"end", sp.end}};
                if (sp.bold)           span["bold"]           = *sp.bold;
                if (sp.italic)         span["italic"]         = *sp.italic;
                if (sp.underline)      span["underline"]      = *sp.underline;
                if (sp.strikethrough)  span["strikethrough"]  = *sp.strikethrough;
                if (sp.color)          span["color"]          = *sp.color;
                if (sp.font_size)      span["font_size"]      = *sp.font_size;
                if (sp.font_family)    span["font_family"]    = *sp.font_family;
                if (sp.generic_family) span["generic_family"] = *sp.generic_family;
                spans_array.push_back(span);
            }
            obj["spans"] = spans_array;
        }
        text_objects_array.push_back(obj);
    }

    // Asset placements - Section 9.3.2
    json assets_array = json::array();
    for (const AssetRef& a : page.assets) {
        json obj = {
            {"id",        a.id},
            {"layer",     a.layer},
            {"hash",      a.hash},
            {"extension", a.extension},
            {"x",         a.x},
            {"y",         a.y},
            {"width",     a.width},
            {"height",    a.height}
        };
        if (a.pdf_page)   obj["pdf_page"]   = *a.pdf_page;
        if (a.crop)       obj["crop"]       = {{"x", a.crop->x}, {"y", a.crop->y}, {"width", a.crop->width}, {"height", a.crop->height}};
        if (a.created_at) obj["created_at"] = *a.created_at;
        assets_array.push_back(obj);
    }

    // All required background fields are written explicitly - Section 6.3.1
    json background = {
        {"type",  page.bg_type},
        {"color", page.bg_color}
    };
    if (page.bg_type != "blank") {
        background["foreground_color"] = page.bg_foreground_color;
        background["spacing"]          = page.bg_spacing;
        if (page.bg_top_margin > 0) background["top_margin"] = page.bg_top_margin;
        if (page.bg_line_width > 0) background["line_width"] = page.bg_line_width;
    }

    return {
        {"width",        page.width},
        {"height",       page.height},
        {"background",   background},
        {"text_objects", text_objects_array},
        {"assets",       assets_array}
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
    binary_buffers.reserve(doc.pages.size() + doc.asset_files.size());

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

    // --- asset files, named by the SHA-256 of their content (Section 9.2.1), stored without
    // compression because they are already compressed (Section 4.5). The same file is stored once.
    std::set<std::string> written;
    for (const AssetFile& file : doc.asset_files) {
        const std::string path = "assets/" + sha256_hex(file.bytes) + "." + file.extension;
        if (!written.insert(path).second) continue;
        binary_buffers.push_back(file.bytes);
        const std::vector<uint8_t>& data = binary_buffers.back();
        zip_source_t* src = zip_source_buffer(archive, data.data(), data.size(), 0);
        zip_int64_t idx = zip_file_add(archive, path.c_str(), src, ZIP_FL_OVERWRITE);
        zip_set_file_compression(archive, idx, ZIP_CM_STORE, 0);
    }

    zip_close(archive);
    return true;
}
