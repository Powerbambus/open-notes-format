#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Flag bit positions for Stroke.flags - Spec Section 7.4.3
namespace StrokeFlags {
    constexpr uint16_t HAS_TILT     = 1 << 0;  // bit 0
    constexpr uint16_t HAS_AZIMUTH  = 1 << 1;  // bit 1
    constexpr uint16_t NO_PRESSURE  = 1 << 2;  // bit 2
}

// Tool types for Stroke.tool_type - Spec Section 7.4.2
namespace ToolType {
    constexpr uint16_t BALLPOINT  = 0;
    constexpr uint16_t FOUNTAIN   = 1;
    constexpr uint16_t PENCIL     = 2;
    constexpr uint16_t HIGHLIGHTER = 3;
    constexpr uint16_t LINE_ERASER = 4;
}

// Represents one data point captured during a stroke
// Spec Section 7.5
struct Sample {
    int32_t x;          // position in coord units (1/100 mm)
    int32_t y;
    uint8_t pressure;   // 0-255
    uint32_t timestamp; // milliseconds since stroke start

    // Optional fields - only meaningful if the parent stroke's flags say so
    int8_t   tilt_x;    // -90 to +90 degrees, pen tilt along X axis
    int8_t   tilt_y;    // -90 to +90 degrees, pen tilt along Y axis
    uint16_t azimuth;   // 0-35999 hundredths of a degree, clockwise from top
};

// Represents one continuous pen gesture (pen-down to pen-up)
// Spec Section 7.4
struct Stroke {
    uint32_t stroke_id;
    uint64_t created_at;    // milliseconds since Unix epoch

    uint8_t  color_r;
    uint8_t  color_g;
    uint8_t  color_b;
    uint8_t  color_a;

    uint16_t tool_type;     // 0=ballpoint, 1=fountain, 2=pencil, 3=highlighter, 4=eraser
    uint16_t base_width;    // stroke width in coord units
    uint16_t flags;         // which optional sample fields are present

    std::vector<Sample> samples;
};

// Represents a typed text element on a page
// Spec Section 8
struct TextObject {
    std::string id;
    uint64_t    created_at;

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
    std::string layer;       // "back", "middle", "front"
};

// Represents one canvas in the document
// Spec Section 6.2
struct Page {
    std::string id;          // e.g. "p00001"
    int         order;       // display position

    int32_t     width;       // in coord units
    int32_t     height;

    // Background
    std::string bg_type;     // "blank", "ruled", "grid", "dotted"
    std::string bg_color;    // "#RRGGBBAA"

    std::vector<Stroke>     strokes;
    std::vector<TextObject> text_objects;
};

// The complete note document
// Spec Section 6.1
struct Document {
    std::string onf_version;  // e.g. "0.1.4"
    std::string title;
    std::string created_at;   // ISO 8601 UTC string
    std::string modified_at;

    std::vector<Page> pages;
};