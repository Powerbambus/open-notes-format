#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Flag bit positions for Stroke.flags - Spec Section 7.4.3
namespace StrokeFlags {
    constexpr uint16_t HAS_TILT        = 1 << 0;  // bit 0
    constexpr uint16_t HAS_AZIMUTH     = 1 << 1;  // bit 1
    constexpr uint16_t NO_PRESSURE     = 1 << 2;  // bit 2
    constexpr uint16_t NO_TIMESTAMP    = 1 << 3;  // bit 3
    constexpr uint16_t HAS_CREATED_AT  = 1 << 4;  // bit 4, derived from Stroke::created_at by the writer
}

// Tool types for Stroke.tool_type - Spec Section 7.4.2
namespace ToolType {
    constexpr uint16_t BALLPOINT  = 0;
    constexpr uint16_t FOUNTAIN   = 1;
    constexpr uint16_t PENCIL     = 2;
    constexpr uint16_t HIGHLIGHTER = 3;
    constexpr uint16_t LINE_ERASER = 4;
}

// Shape types for Shape.shape_type - Spec Section 7.9.3
namespace ShapeType {
    constexpr uint16_t ELLIPSE   = 1;
    constexpr uint16_t RECTANGLE = 2;
    constexpr uint16_t LINE      = 3;
    constexpr uint16_t TRIANGLE  = 4;
}

// Layer range - Spec Section 5.6
constexpr uint32_t MIN_LAYER = 1;
constexpr uint32_t MAX_LAYER = 65535;

// Represents one data point captured during a stroke
// Spec Section 7.5
struct Sample {
    int32_t x;          // position in coord units (1/100 mm), may be negative
    int32_t y;
    uint8_t pressure;   // 0-255
    uint32_t timestamp; // milliseconds since stroke start, ignored if NO_TIMESTAMP is set

    // Optional fields - only meaningful if the parent stroke's flags say so
    int8_t   tilt_x;    // -90 to +90 degrees, pen tilt along X axis
    int8_t   tilt_y;    // -90 to +90 degrees, pen tilt along Y axis
    uint16_t azimuth;   // 0-35999 hundredths of a degree, clockwise from top
};

// Represents one continuous pen gesture (pen-down to pen-up)
// Spec Section 7.4
struct Stroke {
    uint32_t stroke_id;
    uint16_t layer;                       // 1 to 65535 (Section 5.6)
    std::optional<uint64_t> created_at;   // milliseconds since Unix epoch, written only if known

    uint8_t  color_r;
    uint8_t  color_g;
    uint8_t  color_b;
    uint8_t  color_a;

    uint16_t tool_type;     // 0=ballpoint, 1=fountain, 2=pencil, 3=highlighter, 4=eraser
                            // Values 5 and above are reserved (Section 7.4.2)
    uint16_t base_width;    // stroke width in coord units
    uint16_t flags;         // which optional sample fields are present

    std::vector<Sample> samples;
};

// Represents a geometric outline stored by its parameters
// Spec Section 7.9
struct Shape {
    uint32_t stroke_id;                   // shared with strokes (Section 7.4.1)
    uint16_t layer;
    std::optional<uint64_t> created_at;

    uint8_t  color_r;
    uint8_t  color_g;
    uint8_t  color_b;
    uint8_t  color_a;

    uint16_t tool_type;     // 0 to 3 (Section 7.9.2)
    uint16_t base_width;
    uint16_t shape_type;    // ShapeType::*

    // Parameters (Section 7.9.4):
    //   ELLIPSE:   p = { cx, cy, rx, ry },        rotation
    //   RECTANGLE: p = { cx, cy, width, height }, rotation
    //   LINE:      p = { x1, y1, x2, y2 }
    //   TRIANGLE:  p = { x1, y1, x2, y2, x3, y3 }
    std::array<int32_t, 6> p{};
    uint16_t rotation = 0;  // hundredths of a degree, 0 to 35999 (ellipse and rectangle only)
};

// Ink is a single sequence of strokes and shapes in file order (Section 3.4)
using Ink = std::variant<Stroke, Shape>;

// Character style override for a range of a text object
// Spec Section 8.3.1
struct Span {
    int32_t start;
    int32_t end;
    std::optional<bool>        bold;
    std::optional<bool>        italic;
    std::optional<bool>        underline;
    std::optional<bool>        strikethrough;
    std::optional<std::string> color;           // "#RRGGBBAA"
    std::optional<int32_t>     font_size;
    std::optional<std::string> font_family;     // requires generic_family
    std::optional<std::string> generic_family;
};

// Represents a typed text element on a page
// Spec Section 8
struct TextObject {
    std::string id;
    uint16_t    layer;                    // 1 to 65535 (Section 5.6)
    std::optional<uint64_t> created_at;

    int32_t     x;
    int32_t     y;
    int32_t     width;
    int32_t     height;

    std::string content;
    std::string font_family;
    std::string generic_family;
    int32_t     font_size;
    std::string color;       // stored as "#RRGGBBAA" string for JSON
    bool        bold;
    bool        italic;

    std::vector<Span> spans;
};

// Represents one canvas in the document
// Spec Section 6.2
struct Page {
    std::string id;          // e.g. "p00001"
    int         order = 0;   // display position

    int32_t     width  = 0;  // in coord units
    int32_t     height = 0;

    // Background - Section 6.3.1
    std::string bg_type;             // "blank", "ruled", "grid", "dotted"
    std::string bg_color;            // "#RRGGBBAA"
    std::string bg_foreground_color; // "#RRGGBBAA", required for ruled, grid, dotted
    int32_t     bg_spacing = 0;      // coord units, required for ruled, grid, dotted

    std::vector<Ink>        ink;     // strokes and shapes in file order
    std::vector<TextObject> text_objects;

    // Page-level error (Section 12.3.1). When a page is malformed the reader
    // keeps the other pages readable and marks this page instead. A page with
    // readable == false carries no ink and no text objects.
    bool        readable = true;
    std::string error;
};

// The complete note document
// Spec Section 6.1
struct Document {
    std::string onf_version;  // MAJOR.MINOR.PATCH, e.g. "0.3.0"
    std::optional<std::string> generator;  // Section 6.1.2, informational
    std::string title;
    std::string created_at;   // ISO 8601 UTC string
    std::string modified_at;

    std::vector<Page> pages;
};
