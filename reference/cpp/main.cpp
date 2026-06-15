#include <iostream>
#include "src/onf_writer.h"
#include "src/onf_reader.h"
#include "src/onf_types.h"

// Prints a Document to stdout so we can verify round-trip correctness
void print_document(const Document& doc) {
    std::cout << "=== Document ===" << std::endl;
    std::cout << "  version:     " << doc.onf_version << std::endl;
    std::cout << "  title:       " << doc.title       << std::endl;
    std::cout << "  created_at:  " << doc.created_at  << std::endl;
    std::cout << "  modified_at: " << doc.modified_at << std::endl;
    std::cout << "  pages:       " << doc.pages.size() << std::endl;

    for (const Page& page : doc.pages) {
        std::cout << "\n  === Page " << page.id << " (order=" << page.order << ") ===" << std::endl;
        std::cout << "    size:       " << page.width << " x " << page.height << " coord units" << std::endl;
        std::cout << "    background: " << page.bg_type << " " << page.bg_color << std::endl;
        std::cout << "    strokes:    " << page.strokes.size() << std::endl;
        std::cout << "    texts:      " << page.text_objects.size() << std::endl;

        for (const Stroke& stroke : page.strokes) {
            std::cout << "\n    --- Stroke id=" << stroke.stroke_id << " ---" << std::endl;
            std::cout << "      created_at:  " << stroke.created_at << std::endl;
            std::cout << "      color:       rgba("
                      << (int)stroke.color_r << ", "
                      << (int)stroke.color_g << ", "
                      << (int)stroke.color_b << ", "
                      << (int)stroke.color_a << ")" << std::endl;
            std::cout << "      tool_type:   " << stroke.tool_type  << std::endl;
            std::cout << "      base_width:  " << stroke.base_width << std::endl;
            std::cout << "      flags:       0x" << std::hex << stroke.flags
                      << std::dec << std::endl;
            std::cout << "      samples:     " << stroke.samples.size() << std::endl;

            // Print first and last sample so we can verify coordinates
            if (!stroke.samples.empty()) {
                const Sample& first = stroke.samples.front();
                const Sample& last  = stroke.samples.back();
                std::cout << "      first sample: x=" << first.x
                          << " y=" << first.y
                          << " pressure=" << (int)first.pressure
                          << " t=" << first.timestamp << std::endl;
                std::cout << "      last sample:  x=" << last.x
                          << " y=" << last.y
                          << " pressure=" << (int)last.pressure
                          << " t=" << last.timestamp << std::endl;
            }
        }

        for (const TextObject& text : page.text_objects) {
            std::cout << "\n    --- TextObject id=" << text.id << " ---" << std::endl;
            std::cout << "      created_at:     " << text.created_at  << std::endl;
            std::cout << "      position:       x=" << text.x << " y=" << text.y << std::endl;
            std::cout << "      size:           " << text.width << " x " << text.height << std::endl;
            std::cout << "      content:        \"" << text.content << "\"" << std::endl;
            std::cout << "      font_family:    " << text.font_family    << std::endl;
            std::cout << "      generic_family: " << text.generic_family << std::endl;
            std::cout << "      font_size:      " << text.font_size      << std::endl;
            std::cout << "      color:          " << text.color          << std::endl;
            std::cout << "      bold:           " << (text.bold   ? "true" : "false") << std::endl;
            std::cout << "      italic:         " << (text.italic ? "true" : "false") << std::endl;
            std::cout << "      layer:          " << text.layer          << std::endl;
        }
    }
}

int main() {
    std::string output_path = "output.onf";

    // --- Write ---
    std::cout << "Writing " << output_path << "..." << std::endl;

    Stroke horizontal;
    horizontal.stroke_id  = 1;
    horizontal.created_at = 1718358720000;
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
    vertical.created_at = 1718358720001;
    vertical.color_r    = 0;
    vertical.color_g    = 0;
    vertical.color_b    = 0;
    vertical.color_a    = 255;
    vertical.tool_type  = ToolType::BALLPOINT;
    vertical.base_width = 100;
    vertical.flags      = StrokeFlags::NO_PRESSURE;

    for (int i = 0; i <= 10; i++) {
        Sample s;
        s.x         = 10500;
        s.y         = 11850 + (i * 600);
        s.pressure  = 255;
        s.timestamp = static_cast<uint32_t>(i * 10);
        s.tilt_x    = 0;
        s.tilt_y    = 0;
        s.azimuth   = 0;
        vertical.samples.push_back(s);
    }

    TextObject text;
    text.id             = "t00001";
    text.created_at     = 1718358720002;
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
    text.layer          = "middle";

    Page page;
    page.id       = "p00001";
    page.order    = 1;
    page.width    = 21000;
    page.height   = 29700;
    page.bg_type  = "dotted";
    page.bg_color = "#FFFFFFFF";
    page.strokes.push_back(horizontal);
    page.strokes.push_back(vertical);
    page.text_objects.push_back(text);

    Document doc;
    doc.onf_version = "0.1.4";
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