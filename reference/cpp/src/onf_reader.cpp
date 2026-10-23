#include "onf_reader.h"
#include "onf_types.h"

#include <zip.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <optional>
#include <regex>
#include <variant>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

// ---------------------------------------------------------------------------
// Errors. Section 12.3.1 distinguishes two scopes:
//   ContainerError: file-level, the whole file is rejected.
//   MalformedError: page-level when thrown while reading a page; the page is
//                   marked unreadable and all other pages stay readable.
// ---------------------------------------------------------------------------
struct ContainerError : std::runtime_error { using std::runtime_error::runtime_error; };
struct MalformedError : std::runtime_error { using std::runtime_error::runtime_error; };

// Limits - Sections 4.7, 6.1.2, 2.3.5, 12.6
constexpr uint64_t MAX_ENTRY_SIZE     = 2ULL  << 30;   // 2 GiB
constexpr uint64_t MAX_TOTAL_SIZE     = 10ULL << 30;   // 10 GiB
constexpr uint64_t MAX_RATIO          = 100;           // 100:1
constexpr size_t   MAX_PAGES          = 99999;
constexpr int64_t  MAX_JSON_INT       = 9007199254740991;  // 2^53 - 1
constexpr int      MAX_JSON_DEPTH     = 32;
constexpr size_t   MAX_STRING_CHARS   = 65535;
constexpr size_t   MAX_CONTENT_CHARS  = 1048576;

// The reader implements ONF MAJOR version 0 (Section 10.2).
constexpr int IMPLEMENTED_MAJOR = 0;
// Binary format revision of strokes.bin that this reader implements (Section 7.2).
constexpr uint16_t IMPLEMENTED_STROKES_VERSION = 2;

// Size of the fixed header inside a STROKE chunk body (Section 7.4.1) and
// inside a SHAPE chunk body (Section 7.9.2), and of the optional created_at.
constexpr size_t STROKE_HEADER_SIZE = 20;
constexpr size_t SHAPE_HEADER_SIZE  = 16;
constexpr size_t CREATED_AT_SIZE    = 8;
// A sample takes at least 3 bytes (x, y, pressure), or 4 bytes with the
// timestamp field (Section 7.3.2).
constexpr size_t MIN_SAMPLE_SIZE_NO_TIMESTAMP = 3;
constexpr size_t MIN_SAMPLE_SIZE              = 4;
constexpr size_t MAX_SPANS = 65535;  // Section 8.3.1

// ---------------------------------------------------------------------------
// Cursor over a byte buffer. All reads are bounds-checked; reading beyond the
// end throws MalformedError instead of touching memory (Section 12.3).
// ---------------------------------------------------------------------------
class Cursor {
public:
    Cursor(const std::vector<uint8_t>& buf, size_t pos, size_t end)
        : buf_(buf), pos_(pos), end_(end) {}

    size_t pos() const       { return pos_; }
    size_t remaining() const { return end_ - pos_; }

    uint8_t u8() {
        if (pos_ >= end_) throw MalformedError("unexpected end of data");
        return buf_[pos_++];
    }

    // Little-endian value. Section 2.3.3
    template<typename T>
    T le() {
        if (remaining() < sizeof(T)) throw MalformedError("unexpected end of data");
        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            value |= static_cast<T>(static_cast<T>(buf_[pos_++]) << (i * 8));
        }
        return value;
    }

    void skip(size_t n) {
        if (remaining() < n) throw MalformedError("unexpected end of data");
        pos_ += n;
    }

    // Decodes a canonical unsigned LEB128 varint. Appendix A.2 and Section 2.3.2.
    // The varint is malformed if it is longer than 5 bytes, not canonical, or
    // larger than max_value.
    uint64_t varint(uint64_t max_value) {
        uint64_t result = 0;
        for (int i = 0; i < 5; i++) {
            uint8_t byte = u8();
            result |= static_cast<uint64_t>(byte & 0x7F) << (7 * i);
            if ((byte & 0x80) == 0) {
                if (i > 0 && byte == 0x00) throw MalformedError("non-canonical varint");
                if (result > max_value)    throw MalformedError("varint out of range");
                return result;
            }
        }
        throw MalformedError("varint longer than 5 bytes");
    }

private:
    const std::vector<uint8_t>& buf_;
    size_t pos_;
    size_t end_;
};

constexpr uint64_t MAX_ZIGZAG_VARINT = (1ULL << 33) - 1;  // Section 2.3.2
constexpr uint64_t MAX_U32           = 0xFFFFFFFFULL;

// Zigzag decoding on 64-bit values. Section 7.5.3 and Appendix A.1
int64_t zigzag_decode(uint64_t n) {
    return static_cast<int64_t>((n >> 1) ^ (0 - (n & 1)));
}

int64_t read_zigzag_varint(Cursor& c) {
    return zigzag_decode(c.varint(MAX_ZIGZAG_VARINT));
}

// Throws if value is outside [lo, hi].
void check_range(const char* field, int64_t value, int64_t lo, int64_t hi) {
    if (value < lo || value > hi)
        throw MalformedError(std::string("sample field out of range: ") + field);
}

// ---------------------------------------------------------------------------
// strokes.bin - Section 7
// ---------------------------------------------------------------------------
void parse_strokes_bin(const std::vector<uint8_t>& buf, Page& page) {
    // --- File header (Section 7.2) ---
    if (buf.size() < 8) throw MalformedError("strokes.bin is too small");

    if (buf[0] != 0x4F || buf[1] != 0x4E || buf[2] != 0x46 || buf[3] != 0x53)
        throw MalformedError("strokes.bin: invalid magic bytes");

    Cursor header(buf, 4, buf.size());
    uint16_t version  = header.le<uint16_t>();
    uint16_t reserved = header.le<uint16_t>();
    (void)reserved;  // non-zero reserved bytes are ignored

    if (version == 0)
        throw MalformedError("strokes.bin: version 0 is not valid");
    if (version != IMPLEMENTED_STROKES_VERSION) {
        // Version mismatch event. The page is not parsed, even if the user has
        // confirmed opening a file with a higher MAJOR version (Section 7.2).
        throw MalformedError("strokes.bin: unsupported version " + std::to_string(version));
    }

    // --- Chunks (Section 7.3, parsing rules in Section 7.3.2) ---
    std::set<uint32_t> stroke_ids;
    size_t offset = 8;

    while (offset < buf.size()) {
        size_t left = buf.size() - offset;
        if (left < 6) throw MalformedError("strokes.bin: incomplete chunk header");

        Cursor frame(buf, offset, buf.size());
        uint16_t chunk_type   = frame.le<uint16_t>();
        uint32_t chunk_length = frame.le<uint32_t>();
        offset += 6;

        if (chunk_length > buf.size() - offset)
            throw MalformedError("strokes.bin: chunk_length exceeds the remaining data");
        size_t body_end = offset + chunk_length;

        if (chunk_type == 0x0002) {
            // END: a non-empty body is ignored, and so is everything after it.
            break;
        }

        if (chunk_type != 0x0001 && chunk_type != 0x0003) {
            // Unknown or reserved chunk type: skip it (Section 10.5.2).
            offset = body_end;
            continue;
        }

        if (chunk_type == 0x0003) {
            // --- SHAPE chunk body (Section 7.9) ---
            if (chunk_length < SHAPE_HEADER_SIZE)
                throw MalformedError("strokes.bin: SHAPE chunk shorter than the shape header");

            Cursor c(buf, offset, body_end);
            Shape shape;
            shape.stroke_id  = c.le<uint32_t>();
            shape.color_r    = c.u8();
            shape.color_g    = c.u8();
            shape.color_b    = c.u8();
            shape.color_a    = c.u8();
            shape.tool_type  = c.le<uint16_t>();
            shape.base_width = c.le<uint16_t>();
            uint16_t flags   = c.le<uint16_t>();
            shape.layer      = c.le<uint16_t>();

            if (flags & StrokeFlags::HAS_CREATED_AT) {
                if (c.remaining() < CREATED_AT_SIZE)
                    throw MalformedError("strokes.bin: SHAPE chunk shorter than the shape header");
                shape.created_at = c.le<uint64_t>();
            }
            if (c.remaining() < 2)
                throw MalformedError("strokes.bin: SHAPE chunk has no shape_type");
            shape.shape_type = c.le<uint16_t>();

            size_t param_size = 0;
            switch (shape.shape_type) {
                case ShapeType::ELLIPSE:
                case ShapeType::RECTANGLE: param_size = 18; break;
                case ShapeType::LINE:      param_size = 16; break;
                case ShapeType::TRIANGLE:  param_size = 24; break;
                default:
                    // Unknown shape type: skip the chunk (Section 7.9.3).
                    offset = body_end;
                    continue;
            }
            if (c.remaining() < param_size)
                throw MalformedError("strokes.bin: SHAPE chunk shorter than its parameters");

            if (shape.layer == 0)
                throw MalformedError("strokes.bin: layer is 0");
            if (shape.base_width == 0)
                throw MalformedError("strokes.bin: base_width is 0");
            if (shape.tool_type == ToolType::LINE_ERASER)
                throw MalformedError("strokes.bin: SHAPE with tool_type 4");
            if (!stroke_ids.insert(shape.stroke_id).second)
                throw MalformedError("strokes.bin: duplicate stroke_id");

            size_t coords = (shape.shape_type == ShapeType::TRIANGLE) ? 6 : 4;
            for (size_t i = 0; i < coords; i++) shape.p[i] = c.le<int32_t>();
            if (shape.shape_type == ShapeType::ELLIPSE || shape.shape_type == ShapeType::RECTANGLE) {
                shape.rotation = c.le<uint16_t>();
                if (shape.p[2] <= 0 || shape.p[3] <= 0)
                    throw MalformedError("strokes.bin: shape extent must be greater than 0");
                if (shape.rotation > 35999)
                    throw MalformedError("strokes.bin: shape rotation out of range");
            }

            // Bytes left in the chunk body after the parameters are ignored.
            page.ink.push_back(std::move(shape));
            offset = body_end;
            continue;
        }

        // --- STROKE chunk body (Section 7.4) ---
        if (chunk_length < STROKE_HEADER_SIZE)
            throw MalformedError("strokes.bin: STROKE chunk shorter than the stroke header");

        Cursor c(buf, offset, body_end);
        Stroke stroke;
        stroke.stroke_id  = c.le<uint32_t>();
        stroke.color_r    = c.u8();
        stroke.color_g    = c.u8();
        stroke.color_b    = c.u8();
        stroke.color_a    = c.u8();
        stroke.tool_type  = c.le<uint16_t>();
        stroke.base_width = c.le<uint16_t>();
        stroke.flags      = c.le<uint16_t>();
        stroke.layer      = c.le<uint16_t>();
        uint32_t sample_count = c.le<uint32_t>();

        if (stroke.flags & StrokeFlags::HAS_CREATED_AT) {
            if (c.remaining() < CREATED_AT_SIZE)
                throw MalformedError("strokes.bin: STROKE chunk shorter than the stroke header");
            stroke.created_at = c.le<uint64_t>();
        }

        if (stroke.layer == 0)
            throw MalformedError("strokes.bin: layer is 0");
        if (!stroke_ids.insert(stroke.stroke_id).second)
            throw MalformedError("strokes.bin: duplicate stroke_id");
        if (sample_count == 0)
            throw MalformedError("strokes.bin: sample_count is 0");
        if (stroke.base_width == 0)
            throw MalformedError("strokes.bin: base_width is 0");

        bool has_tilt     = stroke.flags & StrokeFlags::HAS_TILT;
        bool has_azimuth  = stroke.flags & StrokeFlags::HAS_AZIMUTH;
        bool no_timestamp = stroke.flags & StrokeFlags::NO_TIMESTAMP;

        // Reject impossible sample counts before allocating memory.
        const size_t min_sample = no_timestamp ? MIN_SAMPLE_SIZE_NO_TIMESTAMP : MIN_SAMPLE_SIZE;
        if (sample_count > c.remaining() / min_sample)
            throw MalformedError("strokes.bin: sample_count exceeds the chunk body");

        // --- Sample block (Section 7.5) ---
        // All samples, including the first one, are stored as deltas. For the
        // first sample the previous values are zero (Section 7.5.2). Values
        // are accumulated in 64-bit arithmetic and range-checked after every
        // sample. Fields that are not present are not checked.
        int64_t x = 0, y = 0, pressure = 0, timestamp = 0;
        int64_t tilt_x = 0, tilt_y = 0, azimuth = 0;

        stroke.samples.reserve(sample_count);
        for (uint32_t i = 0; i < sample_count; i++) {
            x        += read_zigzag_varint(c);
            y        += read_zigzag_varint(c);
            pressure += read_zigzag_varint(c);
            if (!no_timestamp)
                timestamp += static_cast<int64_t>(c.varint(MAX_U32));  // plain varint
            if (has_tilt) {
                tilt_x += read_zigzag_varint(c);
                tilt_y += read_zigzag_varint(c);
            }
            if (has_azimuth) {
                azimuth += read_zigzag_varint(c);
            }

            check_range("x",         x,         INT32_MIN, INT32_MAX);
            check_range("y",         y,         INT32_MIN, INT32_MAX);
            check_range("pressure",  pressure,  0, 255);
            if (!no_timestamp)
                check_range("timestamp", timestamp, 0, static_cast<int64_t>(MAX_U32));
            check_range("tilt_x",    tilt_x,    -90, 90);
            check_range("tilt_y",    tilt_y,    -90, 90);
            check_range("azimuth",   azimuth,   0, 35999);

            Sample s;
            s.x         = static_cast<int32_t>(x);
            s.y         = static_cast<int32_t>(y);
            s.pressure  = static_cast<uint8_t>(pressure);
            s.timestamp = static_cast<uint32_t>(timestamp);
            s.tilt_x    = static_cast<int8_t>(tilt_x);
            s.tilt_y    = static_cast<int8_t>(tilt_y);
            s.azimuth   = static_cast<uint16_t>(azimuth);
            stroke.samples.push_back(s);
        }

        // Bytes left in the chunk body after the last sample are ignored.
        page.ink.push_back(std::move(stroke));
        offset = body_end;
    }
}

// ---------------------------------------------------------------------------
// JSON helpers - Sections 2.3.5 and 12.6
// ---------------------------------------------------------------------------
// SAX handler that only tracks the nesting depth. It builds no tree, so
// checking the depth takes linear time and constant memory (Section 12.6).
struct DepthChecker : nlohmann::json_sax<json> {
    int  depth    = 0;
    bool too_deep = false;

    bool open() {
        if (++depth > MAX_JSON_DEPTH) { too_deep = true; return false; }
        return true;
    }
    bool start_object(std::size_t) override { return open(); }
    bool start_array(std::size_t)  override { return open(); }
    bool end_object() override { depth--; return true; }
    bool end_array()  override { depth--; return true; }

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(json::number_integer_t) override { return true; }
    bool number_unsigned(json::number_unsigned_t) override { return true; }
    bool number_float(json::number_float_t, const std::string&) override { return true; }
    bool string(std::string&) override { return true; }
    bool binary(json::binary_t&) override { return true; }
    bool key(std::string&) override { return true; }
    // Syntax errors are reported by the parse below, with a precise message.
    bool parse_error(std::size_t, const std::string&, const json::exception&) override { return false; }
};

json parse_json(const std::vector<uint8_t>& buf) {
    // The nesting depth is checked first, so the parser below never builds a
    // structure that is deeper than MAX_JSON_DEPTH. A parse callback would
    // enforce the same limit, but makes nlohmann/json quadratic in the number
    // of array elements.
    DepthChecker checker;
    json::sax_parse(buf.begin(), buf.end(), &checker);
    if (checker.too_deep) throw MalformedError("JSON nesting is too deep");
    try {
        return json::parse(buf.begin(), buf.end());
    } catch (const json::exception& e) {
        throw MalformedError(std::string("invalid JSON: ") + e.what());
    }
}

size_t utf8_length(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}

// Reads an integer field within [lo, hi]. JSON numbers written in floating
// point notation are rejected (Section 2.3.5).
int64_t get_int(const json& obj, const char* key, int64_t lo, int64_t hi) {
    const json& v = obj.at(key);
    if (!v.is_number_integer())
        throw MalformedError(std::string("field must be an integer: ") + key);
    if (v.is_number_unsigned() && v.get<uint64_t>() > static_cast<uint64_t>(MAX_JSON_INT))
        throw MalformedError(std::string("integer out of range: ") + key);
    int64_t value = v.get<int64_t>();
    if (value < -MAX_JSON_INT || value < lo || value > hi)
        throw MalformedError(std::string("integer out of range: ") + key);
    return value;
}

std::string get_string(const json& obj, const char* key, size_t max_chars = MAX_STRING_CHARS) {
    const json& v = obj.at(key);
    if (!v.is_string()) throw MalformedError(std::string("field must be a string: ") + key);
    std::string s = v.get<std::string>();
    if (utf8_length(s) > max_chars) throw MalformedError(std::string("string too long: ") + key);
    return s;
}

bool get_bool(const json& obj, const char* key) {
    const json& v = obj.at(key);
    if (!v.is_boolean()) throw MalformedError(std::string("field must be a boolean: ") + key);
    return v.get<bool>();
}

std::string get_color(const json& obj, const char* key) {
    static const std::regex re("^#[0-9A-Fa-f]{8}$");
    std::string s = get_string(obj, key);
    if (!std::regex_match(s, re)) throw MalformedError(std::string("invalid color: ") + key);
    return s;
}

// Reads the required layer field of a text object or asset placement (Section 5.6).
uint16_t get_layer(const json& obj) {
    return static_cast<uint16_t>(get_int(obj, "layer", 1, 65535));
}

// Optional typed fields of a span (Section 8.3.1). A missing key leaves the
// optional empty; a key of the wrong type is malformed.
std::optional<bool> opt_bool(const json& obj, const char* key) {
    if (!obj.contains(key)) return std::nullopt;
    return get_bool(obj, key);
}

// Parses and validates the spans array of a text object - Section 8.3.1
void parse_spans(const json& spans, size_t content_length, std::vector<Span>& out) {
    if (!spans.is_array()) throw MalformedError("spans must be an array");
    if (spans.size() > MAX_SPANS) throw MalformedError("too many spans");

    int64_t prev_end = 0;
    for (const auto& j : spans) {
        if (!j.is_object()) throw MalformedError("span must be an object");
        Span sp;
        sp.start = static_cast<int32_t>(get_int(j, "start", 0, INT32_MAX));
        sp.end   = static_cast<int32_t>(get_int(j, "end",   0, INT32_MAX));
        if (sp.end <= sp.start)             throw MalformedError("span end must be greater than start");
        if (sp.start < prev_end)            throw MalformedError("spans must be sorted and must not overlap");
        if (static_cast<size_t>(sp.end) > content_length)
            throw MalformedError("span end exceeds the content");
        prev_end = sp.end;

        sp.bold          = opt_bool(j, "bold");
        sp.italic        = opt_bool(j, "italic");
        sp.underline     = opt_bool(j, "underline");
        sp.strikethrough = opt_bool(j, "strikethrough");
        if (j.contains("color"))     sp.color     = get_color(j, "color");
        if (j.contains("font_size")) sp.font_size = static_cast<int32_t>(get_int(j, "font_size", 1, INT32_MAX));
        if (j.contains("font_family"))    sp.font_family    = get_string(j, "font_family");
        if (j.contains("generic_family")) sp.generic_family = get_string(j, "generic_family");
        if (sp.font_family && !sp.generic_family)
            throw MalformedError("span font_family requires generic_family");
        out.push_back(std::move(sp));
    }
}

// ---------------------------------------------------------------------------
// meta.json - Section 6.2
// ---------------------------------------------------------------------------
void parse_page_meta(const json& meta, Page& page) {
    try {
        page.width  = static_cast<int32_t>(get_int(meta, "width",  1, INT32_MAX));
        page.height = static_cast<int32_t>(get_int(meta, "height", 1, INT32_MAX));

        // Background - Section 6.3. Required fields are never defaulted.
        const json& bg = meta.at("background");
        if (!bg.is_object()) throw MalformedError("background must be an object");
        page.bg_type  = get_string(bg, "type");
        page.bg_color = get_color(bg, "color");
        if (page.bg_type == "ruled" || page.bg_type == "grid" || page.bg_type == "dotted") {
            page.bg_foreground_color = get_color(bg, "foreground_color");
            page.bg_spacing = static_cast<int32_t>(get_int(bg, "spacing", 1, INT32_MAX));
        }
        // "blank" and background types unknown to this reader need only "color"
        // (Section 6.3.3).

        // Text objects - Section 8
        const json& texts = meta.at("text_objects");
        if (!texts.is_array()) throw MalformedError("text_objects must be an array");
        for (const auto& t : texts) {
            TextObject text;
            text.id             = get_string(t, "id");
            text.layer          = get_layer(t);
            if (t.contains("created_at"))
                text.created_at = static_cast<uint64_t>(get_int(t, "created_at", 0, MAX_JSON_INT));
            text.x              = static_cast<int32_t>(get_int(t, "x", INT32_MIN, INT32_MAX));
            text.y              = static_cast<int32_t>(get_int(t, "y", INT32_MIN, INT32_MAX));
            text.width          = static_cast<int32_t>(get_int(t, "width",  1, INT32_MAX));
            text.height         = static_cast<int32_t>(get_int(t, "height", 1, INT32_MAX));
            text.content        = get_string(t, "content", MAX_CONTENT_CHARS);
            text.font_family    = get_string(t, "font_family");
            text.generic_family = get_string(t, "generic_family");
            text.font_size      = static_cast<int32_t>(get_int(t, "font_size", 1, INT32_MAX));
            text.color          = get_color(t, "color");
            text.bold           = get_bool(t, "bold");
            text.italic         = get_bool(t, "italic");
            if (t.contains("spans"))
                parse_spans(t.at("spans"), utf8_length(text.content), text.spans);
            page.text_objects.push_back(std::move(text));
        }

        // Asset placements (Section 9) are not implemented by this reference
        // implementation, but the field is required and must be an array.
        // The layer of every placement is validated (Section 5.6).
        const json& assets = meta.at("assets");
        if (!assets.is_array()) throw MalformedError("assets must be an array");
        for (const auto& a : assets) {
            if (!a.is_object()) throw MalformedError("asset placement must be an object");
            get_layer(a);
        }
    } catch (const json::exception& e) {
        // Missing key or wrong type
        throw MalformedError(std::string("invalid meta.json: ") + e.what());
    }
}

// ---------------------------------------------------------------------------
// ZIP container - Section 4
// ---------------------------------------------------------------------------

// Section 4.6: path safety
void validate_entry_path(const std::string& name) {
    if (name.empty())                         throw ContainerError("empty archive path");
    if (name.find('\0') != std::string::npos) throw ContainerError("archive path contains a null byte");
    if (name.find('\\') != std::string::npos) throw ContainerError("archive path contains a backslash: " + name);
    if (name[0] == '/')                       throw ContainerError("absolute archive path: " + name);
    if (name.size() >= 2 && std::isalpha(static_cast<unsigned char>(name[0])) && name[1] == ':')
        throw ContainerError("archive path with drive letter: " + name);

    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        if (name.compare(start, end - start, "..") == 0)
            throw ContainerError("archive path contains '..': " + name);
        start = end + 1;
    }
}

// Sections 4.1, 4.6 and 4.7: checks every entry before anything is read.
void check_archive(zip_t* archive) {
    zip_int64_t count = zip_get_num_entries(archive, 0);
    std::set<std::string> names;
    uint64_t total = 0;

    for (zip_int64_t i = 0; i < count; i++) {
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &st) != 0)
            throw ContainerError("cannot read archive entry information");
        const zip_uint64_t need = ZIP_STAT_NAME | ZIP_STAT_SIZE | ZIP_STAT_COMP_SIZE |
                                  ZIP_STAT_ENCRYPTION_METHOD;
        if ((st.valid & need) != need)
            throw ContainerError("incomplete archive entry information");

        std::string name = st.name;
        validate_entry_path(name);
        if (!names.insert(name).second)
            throw ContainerError("duplicate archive entry: " + name);
        if (st.encryption_method != ZIP_EM_NONE)
            throw ContainerError("encrypted archive entry: " + name);

        // Symbolic links are not permitted (Section 4.1).
        zip_uint8_t  opsys = 0;
        zip_uint32_t attributes = 0;
        if (zip_file_get_external_attributes(archive, static_cast<zip_uint64_t>(i), 0,
                                             &opsys, &attributes) == 0 &&
            opsys == ZIP_OPSYS_UNIX && ((attributes >> 16) & 0170000) == 0120000)
            throw ContainerError("symbolic link in archive: " + name);

        // Size limits on the declared sizes (Section 4.7). The bytes that are
        // actually produced are checked again while reading.
        if (st.size > MAX_ENTRY_SIZE)
            throw ContainerError("archive entry too large: " + name);
        if (st.size > st.comp_size * MAX_RATIO)
            throw ContainerError("compression ratio too high: " + name);
        total += st.size;
        if (total > MAX_TOTAL_SIZE)
            throw ContainerError("archive too large");
    }
}

// Reads one entry completely. The size is checked against the bytes that are
// really produced, because the declared size may be falsified (Section 12.2.2).
std::vector<uint8_t> read_zip_entry(zip_t* archive, const std::string& path) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(archive, path.c_str(), 0, &st) != 0 || !(st.valid & ZIP_STAT_SIZE))
        throw ContainerError("cannot find archive entry: " + path);

    zip_file_t* zf = zip_fopen(archive, path.c_str(), 0);
    if (!zf) throw ContainerError("cannot open archive entry: " + path);

    std::vector<uint8_t> buf(st.size);
    zip_int64_t got = st.size == 0 ? 0 : zip_fread(zf, buf.data(), st.size);
    uint8_t extra;
    zip_int64_t more = zip_fread(zf, &extra, 1);
    zip_fclose(zf);

    if (got < 0 || static_cast<zip_uint64_t>(got) != st.size || more != 0)
        throw ContainerError("archive entry size does not match its content: " + path);
    return buf;
}

bool has_entry(zip_t* archive, const std::string& path) {
    return zip_name_locate(archive, path.c_str(), 0) >= 0;
}

// ---------------------------------------------------------------------------
// manifest.json - Section 6.1
// ---------------------------------------------------------------------------
struct PageEntry {
    std::string id;
    int         order;
};

std::vector<PageEntry> parse_manifest(const json& manifest, Document& doc) {
    try {
        static const std::regex version_re("^[0-9]+\\.[0-9]+\\.[0-9]+$");
        static const std::regex time_re("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3}Z$");
        static const std::regex id_re("^p[0-9]{5}$");

        doc.onf_version = get_string(manifest, "onf_version");
        if (!std::regex_match(doc.onf_version, version_re))
            throw MalformedError("onf_version has an invalid format");
        doc.title       = get_string(manifest, "title");
        if (manifest.contains("generator")) doc.generator = get_string(manifest, "generator");
        doc.created_at  = get_string(manifest, "created_at");
        doc.modified_at = get_string(manifest, "modified_at");
        if (!std::regex_match(doc.created_at, time_re) || !std::regex_match(doc.modified_at, time_re))
            throw MalformedError("created_at and modified_at must be ISO 8601 UTC with milliseconds");

        // Section 10.3: a higher MAJOR version is not opened without
        // confirmation. This reference implementation has no user interface,
        // so it refuses the file (no operator configuration exists).
        std::string major_str = doc.onf_version.substr(0, doc.onf_version.find('.'));
        int major = major_str.size() > 9 ? INT32_MAX : std::stoi(major_str);
        if (major > IMPLEMENTED_MAJOR)
            throw ContainerError("major version mismatch: file version " + doc.onf_version);

        const json& pages = manifest.at("pages");
        if (!pages.is_array()) throw MalformedError("pages must be an array");
        const size_t n = pages.size();
        if (n < 1 || n > MAX_PAGES) throw MalformedError("pages must contain 1 to 99999 entries");

        // ids must be p00001..pN and orders 1..N, each exactly once (Section 6.1.2).
        std::vector<bool> seen_id(n + 1, false), seen_order(n + 1, false);
        std::vector<PageEntry> entries;
        for (const auto& p : pages) {
            PageEntry e;
            e.id = get_string(p, "id");
            if (!std::regex_match(e.id, id_re)) throw MalformedError("invalid page id: " + e.id);
            e.order = static_cast<int>(get_int(p, "order", 1, static_cast<int64_t>(n)));

            size_t id_num = std::stoul(e.id.substr(1));
            if (id_num < 1 || id_num > n || seen_id[id_num])
                throw MalformedError("page ids must be p00001 to pNNNNN without gaps or duplicates");
            if (seen_order[e.order])
                throw MalformedError("page order values must be 1 to N without gaps or duplicates");
            seen_id[id_num] = true;
            seen_order[e.order] = true;
            entries.push_back(e);
        }

        std::sort(entries.begin(), entries.end(),
                  [](const PageEntry& a, const PageEntry& b) { return a.order < b.order; });
        return entries;
    } catch (const json::exception& e) {
        throw MalformedError(std::string("invalid manifest.json: ") + e.what());
    }
}

} // namespace

bool read_onf(const std::string& input_path, Document& doc) {
    int zip_error = 0;
    zip_t* archive = zip_open(input_path.c_str(), ZIP_RDONLY, &zip_error);
    if (!archive) {
        std::cerr << "Cannot open file: " << input_path << std::endl;
        return false;
    }

    try {
        // --- Container checks (file-level errors) ---
        check_archive(archive);

        // --- manifest.json (file-level errors) ---
        std::vector<PageEntry> entries;
        try {
            json manifest = parse_json(read_zip_entry(archive, "manifest.json"));
            entries = parse_manifest(manifest, doc);
        } catch (const MalformedError& e) {
            throw ContainerError(e.what());
        }

        // A manifest that points to a missing page directory is a file-level error.
        for (const PageEntry& e : entries) {
            if (!has_entry(archive, "pages/" + e.id + "/meta.json") ||
                !has_entry(archive, "pages/" + e.id + "/strokes.bin"))
                throw ContainerError("page " + e.id + " is missing from the archive");
        }

        // --- Pages (page-level errors) ---
        for (const PageEntry& e : entries) {
            Page page;
            page.id    = e.id;
            page.order = e.order;
            try {
                json meta = parse_json(read_zip_entry(archive, "pages/" + e.id + "/meta.json"));
                parse_page_meta(meta, page);
                parse_strokes_bin(read_zip_entry(archive, "pages/" + e.id + "/strokes.bin"), page);
            } catch (const MalformedError& err) {
                // Report the error, show the page as unreadable and keep the
                // other pages. No part of the page is kept (Section 12.3.1).
                std::cerr << "Page " << e.id << " is malformed: " << err.what() << std::endl;
                page.readable = false;
                page.error    = err.what();
                page.ink.clear();
                page.text_objects.clear();
            }
            doc.pages.push_back(std::move(page));
        }
    } catch (const ContainerError& e) {
        std::cerr << "Cannot read " << input_path << ": " << e.what() << std::endl;
        zip_close(archive);
        doc = Document{};
        return false;
    }

    zip_close(archive);
    return true;
}
