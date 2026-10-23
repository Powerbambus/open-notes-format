#include <iostream>
#include "src/onf_writer.h"
#include "src/onf_reader.h"
#include "src/onf_types.h"

// Prints a Document to stdout so we can verify round-trip correctness
void print_document(const Document& doc) {
    std::cout << "=== Document ===" << std::endl;
    std::cout << "  version:     " << doc.onf_version << std::endl;
    if (doc.generator) std::cout << "  generator:   " << *doc.generator << std::endl;
    std::cout << "  title:       " << doc.title       << std::endl;
    std::cout << "  created_at:  " << doc.created_at  << std::endl;
    std::cout << "  modified_at: " << doc.modified_at << std::endl;
    std::cout << "  pages:       " << doc.pages.size() << std::endl;

    for (const Page& page : doc.pages) {
        std::cout << "\n  === Page " << page.id << " (order=" << page.order << ") ===" << std::endl;
        std::cout << "    size:       " << page.width << " x " << page.height << " coord units" << std::endl;
        if (!page.readable) {
            std::cout << "    UNREADABLE: " << page.error << std::endl;
            continue;
        }
        std::cout << "    background: " << page.bg_type << " " << page.bg_color;
        if (page.bg_type != "blank") {
            std::cout << " foreground " << page.bg_foreground_color
                      << " spacing " << page.bg_spacing;
        }
        std::cout << std::endl;
        std::cout << "    ink:        " << page.ink.size() << std::endl;
        std::cout << "    texts:      " << page.text_objects.size() << std::endl;

        for (const Ink& item : page.ink) {
            if (const Stroke* stroke = std::get_if<Stroke>(&item)) {
                std::cout << "\n    --- Stroke id=" << stroke->stroke_id << " ---" << std::endl;
                std::cout << "      layer:       " << stroke->layer << std::endl;
                if (stroke->created_at)
                    std::cout << "      created_at:  " << *stroke->created_at << std::endl;
                std::cout << "      color:       rgba("
                          << (int)stroke->color_r << ", "
                          << (int)stroke->color_g << ", "
                          << (int)stroke->color_b << ", "
                          << (int)stroke->color_a << ")" << std::endl;
                std::cout << "      tool_type:   " << stroke->tool_type  << std::endl;
                std::cout << "      base_width:  " << stroke->base_width << std::endl;
                std::cout << "      flags:       0x" << std::hex << stroke->flags
                          << std::dec << std::endl;
                std::cout << "      samples:     " << stroke->samples.size() << std::endl;

                // Print first and last sample so we can verify coordinates
                if (!stroke->samples.empty()) {
                    const bool no_ts = stroke->flags & StrokeFlags::NO_TIMESTAMP;
                    const Sample& first = stroke->samples.front();
                    const Sample& last  = stroke->samples.back();
                    std::cout << "      first sample: x=" << first.x
                              << " y=" << first.y
                              << " pressure=" << (int)first.pressure;
                    if (!no_ts) std::cout << " t=" << first.timestamp;
                    std::cout << std::endl;
                    std::cout << "      last sample:  x=" << last.x
                              << " y=" << last.y
                              << " pressure=" << (int)last.pressure;
                    if (!no_ts) std::cout << " t=" << last.timestamp;
                    std::cout << std::endl;
                }
            } else {
                const Shape& shape = std::get<Shape>(item);
                std::cout << "\n    --- Shape id=" << shape.stroke_id << " ---" << std::endl;
                std::cout << "      layer:       " << shape.layer << std::endl;
                if (shape.created_at)
                    std::cout << "      created_at:  " << *shape.created_at << std::endl;
                std::cout << "      shape_type:  " << shape.shape_type << std::endl;
                std::cout << "      tool_type:   " << shape.tool_type  << std::endl;
                std::cout << "      base_width:  " << shape.base_width << std::endl;
                std::cout << "      params:      ";
                for (int32_t v : shape.p) std::cout << v << " ";
                std::cout << "rotation=" << shape.rotation << std::endl;
            }
        }

        for (const TextObject& text : page.text_objects) {
            std::cout << "\n    --- TextObject id=" << text.id << " ---" << std::endl;
            std::cout << "      layer:          " << text.layer << std::endl;
            if (text.created_at)
                std::cout << "      created_at:     " << *text.created_at << std::endl;
            std::cout << "      position:       x=" << text.x << " y=" << text.y << std::endl;
            std::cout << "      size:           " << text.width << " x " << text.height << std::endl;
            std::cout << "      content:        \"" << text.content << "\"" << std::endl;
            std::cout << "      font_family:    " << text.font_family    << std::endl;
            std::cout << "      generic_family: " << text.generic_family << std::endl;
            std::cout << "      font_size:      " << text.font_size      << std::endl;
            std::cout << "      color:          " << text.color          << std::endl;
            std::cout << "      bold:           " << (text.bold   ? "true" : "false") << std::endl;
            std::cout << "      italic:         " << (text.italic ? "true" : "false") << std::endl;
            std::cout << "      spans:          " << text.spans.size() << std::endl;
        }
    }
}

int main() {
    std::string output_path = "output.onf";

    // --- Write ---
    std::cout << "Writing " << output_path << "..." << std::endl;

    Stroke horizontal;
    horizontal.stroke_id  = 1;
    horizontal.layer      = 2;
    horizontal.created_at = 1781433600000;
    horizontal.color_r    = 0;
    horizontal.color_g    = 0;
    horizontal.color_b    = 0;
    horizontal.color_a    = 255;
    horizontal.tool_type  = ToolType::BALLPOINT;
    horizontal.base_width = 100;
    horizontal.flags      = StrokeFlags::NO_PRESSURE;

    for (int i = 0; i <= 10; i++) {
        Sample s;
        s.x         = 7500 + (i * 600);
        s.y         = 14850;
        s.pressure  = 255;
        s.timestamp = static_cast<uint32_t>(i * 10);
        s.tilt_x    = 0;
        s.tilt_y    = 0;
        s.azimuth   = 0;
        horizontal.samples.push_back(s);
    }

    Stroke vertical;
    vertical.stroke_id  = 2;
    vertical.layer      = 2;
    vertical.created_at = std::nullopt;  // creation time unknown: the field is omitted
    vertical.color_r    = 0;
    vertical.color_g    = 0;
    vertical.color_b    = 0;
    vertical.color_a    = 255;
    vertical.tool_type  = ToolType::BALLPOINT;
    vertical.base_width = 100;
    vertical.flags      = StrokeFlags::NO_PRESSURE | StrokeFlags::NO_TIMESTAMP;

    for (int i = 0; i <= 10; i++) {
        Sample s;
        s.x         = 10500;
        s.y         = 11850 + (i * 600);
        s.pressure  = 255;
        s.timestamp = 0;  // not stored because NO_TIMESTAMP is set
        s.tilt_x    = 0;
        s.tilt_y    = 0;
        s.azimuth   = 0;
        vertical.samples.push_back(s);
    }

    // A circle around the cross: an ellipse with rx equal to ry (Section 7.9.4)
    Shape circle;
    circle.stroke_id  = 3;
    circle.layer      = 2;
    circle.color_r    = 255;
    circle.color_g    = 0;
    circle.color_b    = 0;
    circle.color_a    = 255;
    circle.tool_type  = ToolType::BALLPOINT;
    circle.base_width = 100;
    circle.shape_type = ShapeType::ELLIPSE;
    circle.p          = {10500, 14850, 4000, 4000};
    circle.rotation   = 0;

    TextObject text;
    text.id             = "t00001";
    text.layer          = 3;
    text.x              = 9500;
    text.y              = 10500;
    text.width          = 2000;
    text.height         = 1000;
    text.content        = "C++";
    text.font_family    = "Helvetica";
    text.generic_family = "sans-serif";
    text.font_size      = 706;
    text.color          = "#000000FF";
    text.bold           = false;
    text.italic         = false;
    text.spans.push_back(Span{.start = 0, .end = 1, .bold = true});  // the "C" is bold

    Page page;
    page.id       = "p00001";
    page.order    = 1;
    page.width    = 21000;
    page.height   = 29700;
    page.bg_type  = "dotted";
    page.bg_color = "#FFFFFFFF";
    page.bg_foreground_color = "#000000FF";
    page.bg_spacing          = 500;
    page.ink.push_back(horizontal);
    page.ink.push_back(vertical);
    page.ink.push_back(circle);
    page.text_objects.push_back(text);

    Document doc;
    doc.onf_version = "0.3.0";
    doc.generator   = "onf-reference-cpp 0.3.0";
    doc.title       = "C++ Reference";
    doc.created_at  = "2026-06-14T10:32:00.000Z";
    doc.modified_at = "2026-06-14T10:32:00.000Z";
    doc.pages.push_back(page);

    if (!write_onf(doc, output_path)) {
        std::cerr << "Write failed." << std::endl;
        return 1;
    }
    std::cout << "Write OK." << std::endl;

    // --- Read back ---
    std::cout << "\nReading " << output_path << "..." << std::endl;
    Document read_doc;
    if (!read_onf(output_path, read_doc)) {
        std::cerr << "Read failed." << std::endl;
        return 1;
    }
    std::cout << "Read OK." << std::endl;

    // --- Print what we read ---
    std::cout << std::endl;
    print_document(read_doc);

    return 0;
}