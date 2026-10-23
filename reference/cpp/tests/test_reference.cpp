// Tests of the reference implementation. They need no framework: CHECK counts failures and
// main() returns 1 if there was one. Run with `ctest` or `./onf_ref_tests` in the build directory.
//
// The vectors V1 to V6 are copied from Appendix C of the specification. The SHA-256 digests were
// computed with Python's hashlib, independently of the code under test.

#include <zip.h>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "../src/asset_check.h"
#include "../src/onf_reader.h"
#include "../src/onf_types.h"
#include "../src/onf_writer.h"
#include "../src/sha256.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                   \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #condition "\n";   \
            failures++;                                                                    \
        }                                                                                  \
    } while (0)

using Bytes = std::vector<uint8_t>;

// read_onf and write_onf report problems on std::cerr. The tests provoke many, so it is muted.
struct QuietCerr {
    std::streambuf* old;
    std::ostringstream sink;
    QuietCerr() : old(std::cerr.rdbuf(sink.rdbuf())) {}
    ~QuietCerr() { std::cerr.rdbuf(old); }
};

Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }

Bytes from_hex(const std::string& hex) {
    Bytes out;
    std::istringstream in(hex);
    std::string token;
    while (in >> token) out.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
    return out;
}

// ---------------------------------------------------------------------------
// ZIP helpers (libzip), so that files can be built and damaged independently of write_onf
// ---------------------------------------------------------------------------
struct ZipEntry {
    std::string name;
    Bytes       data;
    bool        deflate = true;
};

void write_zip(const std::string& path, const std::vector<ZipEntry>& entries) {
    int err = 0;
    zip_t* archive = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!archive) { std::cout << "cannot create " << path << "\n"; failures++; return; }
    for (const ZipEntry& e : entries) {
        zip_source_t* src = zip_source_buffer(archive, e.data.data(), e.data.size(), 0);
        zip_int64_t idx = zip_file_add(archive, e.name.c_str(), src, ZIP_FL_OVERWRITE);
        zip_set_file_compression(archive, idx, e.deflate ? ZIP_CM_DEFLATE : ZIP_CM_STORE, 0);
    }
    zip_dir_add(archive, "assets", ZIP_FL_OVERWRITE);
    zip_close(archive);
}

std::vector<ZipEntry> read_zip(const std::string& path) {
    std::vector<ZipEntry> out;
    int err = 0;
    zip_t* archive = zip_open(path.c_str(), ZIP_RDONLY, &err);
    if (!archive) return out;
    zip_int64_t n = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < n; i++) {
        std::string name = zip_get_name(archive, static_cast<zip_uint64_t>(i), 0);
        if (name.back() == '/') continue;  // directories are added again by write_zip
        zip_stat_t st;
        zip_stat_index(archive, static_cast<zip_uint64_t>(i), 0, &st);
        ZipEntry e;
        e.name = name;
        e.data.resize(st.size);
        zip_file_t* f = zip_fopen_index(archive, static_cast<zip_uint64_t>(i), 0);
        if (st.size > 0) zip_fread(f, e.data.data(), st.size);
        zip_fclose(f);
        e.deflate = st.comp_method == ZIP_CM_DEFLATE;
        out.push_back(std::move(e));
    }
    zip_close(archive);
    return out;
}

Bytes entry_of(const std::string& path, const std::string& name) {
    for (const ZipEntry& e : read_zip(path)) if (e.name == name) return e.data;
    return {};
}

// ---------------------------------------------------------------------------
// Test data
// ---------------------------------------------------------------------------

// A PNG that is only a header: signature and IHDR. check_asset_content reads nothing more.
Bytes make_png(uint32_t width, uint32_t height) {
    Bytes out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (uint32_t v : {width, height})
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(v >> shift));
    for (int b : {8, 2, 0, 0, 0, 0, 0, 0, 0}) out.push_back(static_cast<uint8_t>(b));
    return out;
}

const Bytes PDF_BYTES = bytes_of("%PDF-1.7\n% a stand-in for a PDF document\n");

const std::string MANIFEST_TEMPLATE =
    R"({"onf_version":"0.5.0","title":"T","created_at":"2026-06-14T10:32:00.000Z",)"
    R"("modified_at":"2026-06-14T12:15:00.000Z","pages":[{"id":"p00001","order":1},{"id":"p00002","order":2}]})";

std::string meta_of(const std::string& text_objects, const std::string& assets) {
    return R"({"width":21000,"height":29700,"background":{"type":"blank","color":"#FFFFFFFF"},"text_objects":[)" +
           text_objects + R"(],"assets":[)" + assets + "]}";
}

std::string text_json(const std::string& id = "t1") {
    return R"({"id":")" + id + R"(","layer":1,"x":0,"y":0,"width":100,"height":100,"content":"x","font_family":"F",)"
           R"("generic_family":"serif","font_size":423,"color":"#000000FF","bold":false,"italic":false})";
}

std::string asset_json(const std::string& hash, const std::string& extension, const std::string& extra = "",
                       const std::string& id = "a1") {
    return R"({"id":")" + id + R"(","layer":1,"hash":")" + hash + R"(","extension":")" + extension +
           R"(","x":0,"y":0,"width":1000,"height":500)" + extra + "}";
}

const Bytes EMPTY_STROKES = from_hex("4F 4E 46 53 02 00 00 00 02 00 00 00 00 00");

// A document with two pages. Page p00001 has the given meta.json, p00002 is always valid.
void build_onf(const std::string& path, const std::string& meta_page1, const std::vector<ZipEntry>& assets = {},
               const std::string& manifest = MANIFEST_TEMPLATE) {
    std::vector<ZipEntry> entries = {
        {"manifest.json", bytes_of(manifest), true},
        {"pages/p00001/meta.json", bytes_of(meta_page1), true},
        {"pages/p00001/strokes.bin", EMPTY_STROKES, false},
        {"pages/p00002/meta.json", bytes_of(meta_of("", "")), true},
        {"pages/p00002/strokes.bin", EMPTY_STROKES, false},
    };
    for (const ZipEntry& a : assets) entries.push_back(a);
    write_zip(path, entries);
}

ZipEntry asset_entry(const Bytes& data, const std::string& extension) {
    return {"assets/" + sha256_hex(data) + "." + extension, data, false};
}

Document read_ok(const std::string& path) {
    QuietCerr quiet;
    Document doc;
    bool ok = read_onf(path, doc);
    CHECK(ok);
    return doc;
}

bool rejected(const std::string& path) {
    QuietCerr quiet;
    Document doc;
    return !read_onf(path, doc);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_sha256() {
    CHECK(sha256_hex(bytes_of("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex(bytes_of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex(bytes_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // Lengths around the block size (55, 56, 63, 64, 65 bytes) need different padding.
    CHECK(sha256_hex(Bytes(55, 'a')) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(sha256_hex(Bytes(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(sha256_hex(Bytes(63, 'a')) == "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    CHECK(sha256_hex(Bytes(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    CHECK(sha256_hex(Bytes(65, 'a')) == "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
    CHECK(sha256_hex(Bytes(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// The vectors of Appendix C: complete chunks including the 6-byte frame.
const char* V1 = "01 00 1E 00 00 00 01 00 00 00 00 00 00 FF 00 00 64 00 0C 00 02 00 02 00 00 00 A0 9C 01 90 4E FE 03 08 06 00";
const char* V2 = "01 00 28 00 00 00 02 00 00 00 00 00 00 FF 01 00 64 00 10 00 01 00 02 00 00 00 00 88 B7 C5 9E 01 00 00 A0 9C 01 90 4E 90 03 00 08 06 04 08";
const char* V3 = "03 00 24 00 00 00 03 00 00 00 FF 00 00 FF 00 00 64 00 00 00 02 00 01 00 04 29 00 00 02 3A 00 00 B8 0B 00 00 DC 05 00 00 94 11";
const char* V4 = "03 00 2C 00 00 00 04 00 00 00 00 00 00 66 03 00 90 01 10 00 03 00 01 88 B7 C5 9E 01 00 00 02 00 04 29 00 00 02 3A 00 00 70 17 00 00 B8 0B 00 00 28 23";
const char* V5 = "03 00 22 00 00 00 05 00 00 00 00 00 00 FF 00 00 64 00 00 00 01 00 03 00 0C FE FF FF D0 07 00 00 E8 03 00 00 18 FC FF FF";
const char* V7 = "01 00 1D 00 00 00 07 00 00 00 00 00 00 FF 01 00 64 00 6C 00 01 00 02 00 00 00 D0 0F A0 1F FE 03 14 0A 00";
const char* V6 = "03 00 2A 00 00 00 06 00 00 00 00 00 00 FF 02 00 96 00 00 00 01 00 04 00 88 13 00 00 88 13 00 00 40 1F 00 00 88 13 00 00 64 19 00 00 4C 1D 00 00";

Page vector_page() {
    Page page;
    page.id = "p00001";
    page.order = 1;
    page.width = 21000;
    page.height = 29700;
    page.bg_type = "blank";
    page.bg_color = "#FFFFFFFF";

    Stroke v1{};
    v1.stroke_id = 1; v1.layer = 2; v1.color_a = 255; v1.base_width = 100;
    v1.flags = StrokeFlags::NO_PRESSURE | StrokeFlags::NO_TIMESTAMP;
    v1.samples = {Sample{10000, 5000, 255, 0, 0, 0, 0}, Sample{10004, 5003, 255, 0, 0, 0, 0}};
    page.ink.push_back(v1);

    Stroke v2{};
    v2.stroke_id = 2; v2.layer = 1; v2.created_at = 1781433600000ull; v2.color_a = 255;
    v2.tool_type = ToolType::FOUNTAIN; v2.base_width = 100;
    v2.samples = {Sample{10000, 5000, 200, 0, 0, 0, 0}, Sample{10004, 5003, 202, 8, 0, 0, 0}};
    page.ink.push_back(v2);

    Shape v3{};
    v3.stroke_id = 3; v3.layer = 2; v3.color_r = 255; v3.color_a = 255; v3.base_width = 100;
    v3.shape_type = ShapeType::ELLIPSE; v3.p = {10500, 14850, 3000, 1500}; v3.rotation = 4500;
    page.ink.push_back(v3);

    Shape v4{};
    v4.stroke_id = 4; v4.layer = 3; v4.created_at = 1781433600001ull; v4.color_a = 0x66;
    v4.tool_type = ToolType::HIGHLIGHTER; v4.base_width = 400;
    v4.shape_type = ShapeType::RECTANGLE; v4.p = {10500, 14850, 6000, 3000}; v4.rotation = 9000;
    page.ink.push_back(v4);

    Shape v5{};
    v5.stroke_id = 5; v5.layer = 1; v5.color_a = 255; v5.base_width = 100;
    v5.shape_type = ShapeType::LINE; v5.p = {-500, 2000, 1000, -1000};
    page.ink.push_back(v5);

    Shape v6{};
    v6.stroke_id = 6; v6.layer = 1; v6.color_a = 255; v6.tool_type = ToolType::PENCIL; v6.base_width = 150;
    v6.shape_type = ShapeType::TRIANGLE; v6.p = {5000, 5000, 8000, 5000, 6500, 7500};
    page.ink.push_back(v6);

    Stroke v7{};
    v7.stroke_id = 7; v7.layer = 1; v7.color_a = 255; v7.tool_type = ToolType::FOUNTAIN; v7.base_width = 100;
    v7.flags = StrokeFlags::NO_PRESSURE | StrokeFlags::NO_TIMESTAMP | StrokeFlags::NO_TAPER_START | StrokeFlags::NO_TAPER_END;
    v7.samples = {Sample{1000, 2000, 255, 0, 0, 0, 0}, Sample{1010, 2005, 255, 0, 0, 0, 0}};
    page.ink.push_back(v7);
    return page;
}

Bytes strokes_file_of(std::initializer_list<const char*> chunks) {
    Bytes out = from_hex("4F 4E 46 53 02 00 00 00");
    for (const char* hex : chunks) {
        Bytes c = from_hex(hex);
        out.insert(out.end(), c.begin(), c.end());
    }
    Bytes end = from_hex("02 00 00 00 00 00");
    out.insert(out.end(), end.begin(), end.end());
    return out;
}

void test_appendix_c_vectors() {
    const std::string path = "t_vectors.onf";

    // The writer produces exactly the bytes of the specification.
    Document doc;
    doc.onf_version = "0.5.0";
    doc.title = "Vectors";
    doc.created_at = doc.modified_at = "2026-06-14T10:32:00.000Z";
    doc.pages.push_back(vector_page());
    {
        QuietCerr quiet;
        CHECK(write_onf(doc, path));
    }
    CHECK(entry_of(path, "pages/p00001/strokes.bin") == strokes_file_of({V1, V2, V3, V4, V5, V6, V7}));

    // The reader decodes them into the same values.
    std::vector<ZipEntry> entries = {
        {"manifest.json", bytes_of(R"({"onf_version":"0.5.0","title":"T","created_at":"2026-06-14T10:32:00.000Z","modified_at":"2026-06-14T10:32:00.000Z","pages":[{"id":"p00001","order":1}]})"), true},
        {"pages/p00001/meta.json", bytes_of(meta_of("", "")), true},
        {"pages/p00001/strokes.bin", strokes_file_of({V1, V2, V3, V4, V5, V6, V7}), false},
    };
    write_zip("t_vectors2.onf", entries);
    Document read = read_ok("t_vectors2.onf");
    CHECK(read.pages.size() == 1 && read.pages[0].readable);
    const auto& ink = read.pages[0].ink;
    CHECK(ink.size() == 7);
    if (ink.size() == 7) {
        const Stroke& a = std::get<Stroke>(ink[0]);
        CHECK(a.layer == 2 && !a.created_at && (a.flags & StrokeFlags::NO_TIMESTAMP));
        CHECK(a.samples.size() == 2 && a.samples[1].x == 10004 && a.samples[1].y == 5003 && a.samples[1].pressure == 255);
        const Stroke& b = std::get<Stroke>(ink[1]);
        CHECK(b.created_at == 1781433600000ull && b.samples[1].timestamp == 8 && b.samples[1].pressure == 202);
        const Shape& c = std::get<Shape>(ink[2]);
        CHECK(c.shape_type == ShapeType::ELLIPSE && c.p[2] == 3000 && c.p[3] == 1500 && c.rotation == 4500);
        const Shape& d = std::get<Shape>(ink[3]);
        CHECK(d.color_a == 0x66 && d.tool_type == ToolType::HIGHLIGHTER && d.created_at == 1781433600001ull && d.rotation == 9000);
        const Shape& e = std::get<Shape>(ink[4]);
        CHECK(e.p[0] == -500 && e.p[3] == -1000);
        const Shape& f = std::get<Shape>(ink[5]);
        CHECK(f.shape_type == ShapeType::TRIANGLE && f.p[4] == 6500 && f.p[5] == 7500);
        const Stroke& g = std::get<Stroke>(ink[6]);
        CHECK(g.tool_type == ToolType::FOUNTAIN && (g.flags & StrokeFlags::NO_TAPER_START) && (g.flags & StrokeFlags::NO_TAPER_END));
    }
}

void test_asset_checks() {
    CHECK(is_supported_asset_extension("png") && is_supported_asset_extension("pdf"));
    CHECK(!is_supported_asset_extension("jpeg") && !is_supported_asset_extension("gif") && !is_supported_asset_extension("PNG"));

    AssetInfo png = check_asset_content("png", make_png(640, 480));
    CHECK(png.ok && png.width == 640 && png.height == 480);
    CHECK(check_asset_content("png", make_png(10000, 10000)).ok);             // exactly 100000000 pixels
    CHECK(!check_asset_content("png", make_png(10001, 10000)).ok);            // one more row of pixels
    CHECK(!check_asset_content("png", make_png(0xFFFFFFFF, 0xFFFFFFFF)).ok);  // no overflow
    CHECK(!check_asset_content("png", make_png(0, 5)).ok);
    CHECK(!check_asset_content("png", bytes_of("not a png")).ok);
    CHECK(!check_asset_content("jpg", make_png(10, 10)).ok);                  // content must match the extension
    CHECK(check_asset_content("pdf", PDF_BYTES).ok && !check_asset_content("pdf", make_png(1, 1)).ok);

    // Every truncated header is rejected without reading past the end.
    Bytes full = make_png(10, 10);
    for (size_t size = 0; size < 24; size++)
        CHECK(!check_asset_content("png", Bytes(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size))).ok);
}

void test_assets_round_trip() {
    const Bytes png = make_png(200, 100);

    Document doc;
    doc.onf_version = "0.5.0";
    doc.title = "Assets";
    doc.created_at = doc.modified_at = "2026-06-14T10:32:00.000Z";
    doc.asset_files.push_back({"png", png});
    doc.asset_files.push_back({"pdf", PDF_BYTES});
    doc.asset_files.push_back({"png", png});  // the same file twice is stored once

    Page page;
    page.id = "p00001";
    page.order = 1;
    page.width = 21000;
    page.height = 29700;
    page.bg_type = "blank";
    page.bg_color = "#FFFFFFFF";

    AssetRef image{};
    image.id = "a1"; image.layer = 1; image.hash = sha256_hex(png); image.extension = "png";
    image.crop = Crop{10, 20, 150, 60};
    image.x = 0; image.y = 0; image.width = 5000; image.height = 2000;
    AssetRef whole = image;
    whole.id = "a2"; whole.crop.reset(); whole.created_at = 5;
    AssetRef pdf{};
    pdf.id = "a3"; pdf.layer = 2; pdf.hash = sha256_hex(PDF_BYTES); pdf.extension = "pdf"; pdf.pdf_page = 7;
    pdf.x = 0; pdf.y = 3000; pdf.width = 5000; pdf.height = 7000;
    page.assets = {image, whole, pdf};
    doc.pages.push_back(page);

    const std::string path = "t_assets.onf";
    {
        QuietCerr quiet;
        CHECK(write_onf(doc, path));
    }

    // Archive: both files, each once, named by their hash.
    int files = 0;
    for (const ZipEntry& e : read_zip(path))
        if (e.name.rfind("assets/", 0) == 0) {
            files++;
            CHECK(e.deflate == false);  // stored (Section 4.5)
        }
    CHECK(files == 2);
    CHECK(entry_of(path, "assets/" + sha256_hex(png) + ".png") == png);
    CHECK(entry_of(path, "assets/" + sha256_hex(PDF_BYTES) + ".pdf") == PDF_BYTES);

    Document read = read_ok(path);
    CHECK(read.pages.size() == 1 && read.pages[0].readable);
    if (read.pages.empty()) return;
    const auto& a = read.pages[0].assets;
    CHECK(a.size() == 3);
    if (a.size() == 3) {
        CHECK(a[0].status == AssetStatus::Ok && a[1].status == AssetStatus::Ok && a[2].status == AssetStatus::Ok);
        CHECK(a[0].crop && a[0].crop->x == 10 && a[0].crop->y == 20 && a[0].crop->width == 150 && a[0].crop->height == 60);
        CHECK(!a[1].crop && a[1].created_at == 5);
        CHECK(a[2].pdf_page == 7 && !a[2].crop);
    }
    CHECK(read.asset_files.size() == 2);
}

// Read a document in which the placement was changed or the file damaged, and return its first asset's status.
AssetStatus status_of(const std::string& meta_assets, const std::vector<ZipEntry>& files) {
    build_onf("t_status.onf", meta_of("", meta_assets), files);
    Document doc = read_ok("t_status.onf");
    if (doc.pages.empty() || !doc.pages[0].readable || doc.pages[0].assets.empty()) {
        std::cout << "FAIL the page with the asset is not readable\n";
        failures++;
        return AssetStatus::Corrupt;
    }
    return doc.pages[0].assets[0].status;
}

void test_asset_statuses() {
    const Bytes png = make_png(200, 100);
    const std::string hash = sha256_hex(png);
    const ZipEntry file = asset_entry(png, "png");

    CHECK(status_of(asset_json(hash, "png"), {file}) == AssetStatus::Ok);
    CHECK(status_of(asset_json(hash, "png"), {}) == AssetStatus::Missing);
    CHECK(status_of(asset_json(hash, "gif"), {file}) == AssetStatus::UnsupportedFormat);
    CHECK(status_of(asset_json(hash, "jpeg"), {file}) == AssetStatus::UnsupportedFormat);

    // The file is not what its name says.
    CHECK(status_of(asset_json(hash, "png"), {{"assets/" + hash + ".png", make_png(201, 100), false}}) == AssetStatus::Corrupt);
    // The name is right but the content is no PNG.
    const Bytes fake = bytes_of("this is not a png");
    CHECK(status_of(asset_json(sha256_hex(fake), "png"), {asset_entry(fake, "png")}) == AssetStatus::Corrupt);
    // More pixels than allowed.
    const Bytes huge = make_png(20000, 20000);
    CHECK(status_of(asset_json(sha256_hex(huge), "png"), {asset_entry(huge, "png")}) == AssetStatus::Corrupt);

    // crop: the region has to lie completely inside the image (200 x 100), otherwise the asset is corrupt.
    CHECK(status_of(asset_json(hash, "png", R"(,"crop":{"x":0,"y":0,"width":200,"height":100})"), {file}) == AssetStatus::Ok);
    CHECK(status_of(asset_json(hash, "png", R"(,"crop":{"x":199,"y":99,"width":1,"height":1})"), {file}) == AssetStatus::Ok);
    CHECK(status_of(asset_json(hash, "png", R"(,"crop":{"x":1,"y":0,"width":200,"height":100})"), {file}) == AssetStatus::Corrupt);
    CHECK(status_of(asset_json(hash, "png", R"(,"crop":{"x":0,"y":1,"width":200,"height":100})"), {file}) == AssetStatus::Corrupt);
    CHECK(status_of(asset_json(hash, "png", R"(,"crop":{"x":0,"y":0,"width":2147483647,"height":2147483647})"), {file}) == AssetStatus::Corrupt);

    // For pdf a crop is ignored, and a pdf_page behind the end of the document is not checked here.
    CHECK(status_of(asset_json(sha256_hex(PDF_BYTES), "pdf", R"(,"pdf_page":3,"crop":{"x":-1})"), {asset_entry(PDF_BYTES, "pdf")}) == AssetStatus::Ok);
    // For an image pdf_page is ignored.
    CHECK(status_of(asset_json(hash, "png", R"(,"pdf_page":0)"), {file}) == AssetStatus::Ok);
}

// A page is malformed: it is unreadable, page p00002 stays readable, the file is still opened (Section 12.3.1).
void expect_page_malformed(const std::string& meta, const std::vector<ZipEntry>& files = {}) {
    build_onf("t_bad.onf", meta, files);
    Document doc = read_ok("t_bad.onf");
    CHECK(doc.pages.size() == 2);
    if (doc.pages.size() == 2) {
        CHECK(!doc.pages[0].readable);
        CHECK(doc.pages[0].assets.empty());
        CHECK(doc.pages[1].readable);
    }
}

void test_malformed_placements() {
    const Bytes png = make_png(200, 100);
    const std::string h = sha256_hex(png);
    const std::vector<ZipEntry> files = {asset_entry(png, "png")};

    // crop (Section 9.3.2)
    for (const char* crop : {R"(,"crop":5)", R"(,"crop":[])", R"(,"crop":null)", R"(,"crop":{})",
                             R"(,"crop":{"x":0,"y":0,"width":1})", R"(,"crop":{"x":0,"y":0,"height":1})",
                             R"(,"crop":{"y":0,"width":1,"height":1})", R"(,"crop":{"x":-1,"y":0,"width":1,"height":1})",
                             R"(,"crop":{"x":0,"y":-1,"width":1,"height":1})", R"(,"crop":{"x":0,"y":0,"width":0,"height":1})",
                             R"(,"crop":{"x":0,"y":0,"width":1,"height":0})", R"(,"crop":{"x":2147483648,"y":0,"width":1,"height":1})",
                             R"(,"crop":{"x":0,"y":0,"width":2147483648,"height":1})", R"(,"crop":{"x":"0","y":0,"width":1,"height":1})",
                             R"(,"crop":{"x":0.5,"y":0,"width":1,"height":1})"}) {
        build_onf("t_bad.onf", meta_of("", asset_json(h, "png", crop)), files);
        Document doc = read_ok("t_bad.onf");
        const bool unreadable = doc.pages.size() == 2 && !doc.pages[0].readable && doc.pages[1].readable;
        if (!unreadable) std::cout << "  crop was accepted: " << crop << "\n";
        CHECK(unreadable);
    }
    // Unknown members of crop are ignored (Section 10.5.1).
    build_onf("t_ok.onf", meta_of("", asset_json(h, "png", R"(,"crop":{"x":0,"y":0,"width":1,"height":1,"unit":"px"})")), files);
    CHECK(read_ok("t_ok.onf").pages[0].readable);

    // The other fields of a placement
    expect_page_malformed(meta_of("", asset_json(h, "pdf", "")), files);                       // pdf_page missing
    expect_page_malformed(meta_of("", asset_json(h, "pdf", R"(,"pdf_page":0)")), files);
    expect_page_malformed(meta_of("", asset_json(h, "pdf", R"(,"pdf_page":2147483648)")), files);
    expect_page_malformed(meta_of("", asset_json(std::string(64, 'A'), "png")), files);        // hash in upper case
    expect_page_malformed(meta_of("", asset_json(std::string(63, 'a'), "png")), files);
    expect_page_malformed(meta_of("", asset_json("../" + std::string(61, 'a'), "png")), files);
}

void test_duplicate_ids_and_layers() {
    const std::string h(64, 'a');
    expect_page_malformed(meta_of(text_json("t1") + "," + text_json("t1"), ""));
    expect_page_malformed(meta_of("", asset_json(h, "png") + "," + asset_json(h, "png")));
    // A text object and an asset may use the same id: ids are unique per array.
    build_onf("t_ok.onf", meta_of(text_json("x"), asset_json(h, "png", "", "x")));
    CHECK(read_ok("t_ok.onf").pages[0].readable);

    // layer of an asset (Section 5.6)
    std::string zero = asset_json(h, "png");
    zero.replace(zero.find("\"layer\":1"), 9, "\"layer\":0");
    expect_page_malformed(meta_of("", zero));
    std::string big = asset_json(h, "png");
    big.replace(big.find("\"layer\":1"), 9, "\"layer\":65536");
    expect_page_malformed(meta_of("", big));
}

// Section 2.3.5: a byte order mark makes the file malformed, in the manifest the whole file and in
// meta.json only the page.
void test_byte_order_mark() {
    const Bytes bom = {0xEF, 0xBB, 0xBF};
    auto with_bom = [&](const std::string& text) {
        Bytes out = bom;
        Bytes rest = bytes_of(text);
        out.insert(out.end(), rest.begin(), rest.end());
        return out;
    };

    write_zip("t_bom1.onf", {{"manifest.json", with_bom(MANIFEST_TEMPLATE), true},
                              {"pages/p00001/meta.json", bytes_of(meta_of("", "")), true},
                              {"pages/p00001/strokes.bin", EMPTY_STROKES, false},
                              {"pages/p00002/meta.json", bytes_of(meta_of("", "")), true},
                              {"pages/p00002/strokes.bin", EMPTY_STROKES, false}});
    CHECK(rejected("t_bom1.onf"));

    write_zip("t_bom2.onf", {{"manifest.json", bytes_of(MANIFEST_TEMPLATE), true},
                              {"pages/p00001/meta.json", with_bom(meta_of("", "")), true},
                              {"pages/p00001/strokes.bin", EMPTY_STROKES, false},
                              {"pages/p00002/meta.json", bytes_of(meta_of("", "")), true},
                              {"pages/p00002/strokes.bin", EMPTY_STROKES, false}});
    Document doc = read_ok("t_bom2.onf");
    CHECK(doc.pages.size() == 2 && !doc.pages[0].readable && doc.pages[1].readable);
}

// Section 12.3.1: a page directory that exists but misses a file is a page-level error, a missing
// page directory is a file-level error.
void test_missing_page_files() {
    auto base = [](const std::string& missing) {
        std::vector<ZipEntry> entries = {
            {"manifest.json", bytes_of(MANIFEST_TEMPLATE), true},
            {"pages/p00001/meta.json", bytes_of(meta_of("", "")), true},
            {"pages/p00001/strokes.bin", EMPTY_STROKES, false},
            {"pages/p00002/meta.json", bytes_of(meta_of("", "")), true},
            {"pages/p00002/strokes.bin", EMPTY_STROKES, false},
        };
        std::vector<ZipEntry> kept;
        for (const ZipEntry& e : entries) if (e.name.rfind(missing, 0) != 0 || missing.empty()) kept.push_back(e);
        return kept;
    };

    for (const char* name : {"pages/p00001/meta.json", "pages/p00001/strokes.bin"}) {
        std::vector<ZipEntry> kept;
        for (const ZipEntry& e : base("")) if (e.name != name) kept.push_back(e);
        write_zip("t_missing.onf", kept);
        Document doc = read_ok("t_missing.onf");
        CHECK(doc.pages.size() == 2 && !doc.pages[0].readable && doc.pages[1].readable);
    }

    std::vector<ZipEntry> no_directory = base("pages/p00002/");
    write_zip("t_missing.onf", no_directory);
    CHECK(rejected("t_missing.onf"));
}

// Section 12.6: a string that is too long makes only the page malformed. The string is random, so
// that the compression ratio rule of Section 4.7 does not apply.
void test_structural_limits_in_meta() {
    std::string text(70000, 'a');
    unsigned seed = 12345;
    for (char& c : text) {
        seed = seed * 1103515245u + 12345u;
        c = static_cast<char>('a' + (seed >> 16) % 26);
    }
    expect_page_malformed(meta_of("", asset_json(std::string(64, 'a'), text)));

    std::string deep = R"({"width":1,"height":1,"x":)" + std::string(40, '[') + std::string(40, ']') + "}";
    expect_page_malformed(deep);
}

Document small_document() {
    Document doc;
    doc.onf_version = "0.5.0";
    doc.title = "T";
    doc.created_at = doc.modified_at = "2026-06-14T10:32:00.000Z";
    Page page;
    page.id = "p00001";
    page.order = 1;
    page.width = 21000;
    page.height = 29700;
    page.bg_type = "blank";
    page.bg_color = "#FFFFFFFF";
    doc.pages.push_back(page);
    return doc;
}

// Section 7.4.2: tool type 4 was the line eraser of ONF 0.3.0 and 0.3.1. A reader skips such a stroke or
// shape (it is neither an error nor rendered) and still checks the chunk like every other one.
void test_removed_tool_type() {
    // V1 and V3 with the tool type changed to 4: bytes 8 and 9 of a stroke body, 14 and 15 of a shape body.
    Bytes eraser_stroke = from_hex(V1);
    eraser_stroke[6 + 8] = 4;
    Bytes eraser_shape = from_hex(V3);
    eraser_shape[6 + 8] = 4;
    Bytes normal = from_hex(V2);

    auto file_of = [&](const std::vector<Bytes>& chunks) {
        Bytes out = from_hex("4F 4E 46 53 02 00 00 00");
        for (const Bytes& c : chunks) out.insert(out.end(), c.begin(), c.end());
        Bytes end = from_hex("02 00 00 00 00 00");
        out.insert(out.end(), end.begin(), end.end());
        return out;
    };
    auto read_ink = [&](const Bytes& strokes) {
        write_zip("t_tool4.onf", {{"manifest.json", bytes_of(R"({"onf_version":"0.5.0","title":"T","created_at":"2026-06-14T10:32:00.000Z","modified_at":"2026-06-14T10:32:00.000Z","pages":[{"id":"p00001","order":1}]})"), true},
                                  {"pages/p00001/meta.json", bytes_of(meta_of("", "")), true},
                                  {"pages/p00001/strokes.bin", strokes, false}});
        return read_ok("t_tool4.onf");
    };

    Document doc = read_ink(file_of({eraser_stroke, normal, eraser_shape}));
    CHECK(doc.pages.size() == 1 && doc.pages[0].readable);
    if (!doc.pages.empty()) {
        CHECK(doc.pages[0].ink.size() == 1);  // only the normal stroke
        if (doc.pages[0].ink.size() == 1) CHECK(std::get<Stroke>(doc.pages[0].ink[0]).stroke_id == 2);
    }

    // The chunk is still checked: a stroke with tool type 4 and layer 0 makes the page malformed ...
    Bytes bad = eraser_stroke;
    bad[6 + 14] = 0;
    bad[6 + 15] = 0;
    Document broken = read_ink(file_of({bad}));
    CHECK(!broken.pages[0].readable);
    // ... and its stroke_id counts for the uniqueness rule.
    Bytes same_id = from_hex(V1);
    Document duplicate = read_ink(file_of({eraser_stroke, same_id}));
    CHECK(!duplicate.pages[0].readable);
}

// The writer never writes tool type 4 and only knows the flags of Section 7.4.3.
void test_writer_rejects_removed_tool_type() {
    QuietCerr quiet;
    Document doc = small_document();
    doc.pages[0].ink.push_back(vector_page().ink[0]);
    CHECK(write_onf(doc, "t_w.onf"));

    Stroke& stroke = std::get<Stroke>(doc.pages[0].ink[0]);
    stroke.tool_type = ToolType::REMOVED;
    CHECK(!write_onf(doc, "t_w.onf"));
    stroke.tool_type = ToolType::BALLPOINT;
    stroke.flags |= 1 << 7;  // reserved bit
    CHECK(!write_onf(doc, "t_w.onf"));
    stroke.flags &= ~(1 << 7);
    stroke.flags |= StrokeFlags::NO_TAPER_START | StrokeFlags::NO_TAPER_END;
    CHECK(write_onf(doc, "t_w.onf"));

    Shape shape{};
    shape.stroke_id = 9; shape.layer = 1; shape.color_a = 255; shape.base_width = 100;
    shape.tool_type = ToolType::REMOVED; shape.shape_type = ShapeType::LINE; shape.p = {0, 0, 10, 10};
    doc.pages[0].ink.push_back(shape);
    CHECK(!write_onf(doc, "t_w.onf"));
}

// Section 6.3: top_margin of ruled, grid and dotted backgrounds.
void test_top_margin() {
    auto meta_with_background = [](const std::string& background) {
        return R"({"width":21000,"height":29700,"background":)" + background + R"(,"text_objects":[],"assets":[]})";
    };
    auto margin_of = [&](const std::string& background, bool* readable) {
        build_onf("t_margin.onf", meta_with_background(background));
        Document doc = read_ok("t_margin.onf");
        *readable = doc.pages.size() == 2 && doc.pages[0].readable;
        return *readable ? doc.pages[0].bg_top_margin : -1;
    };
    bool ok = false;
    CHECK(margin_of(R"({"type":"dotted","color":"#FFFFFFFF","foreground_color":"#000000FF","spacing":500})", &ok) == 0 && ok);
    CHECK(margin_of(R"({"type":"dotted","color":"#FFFFFFFF","foreground_color":"#000000FF","spacing":500,"top_margin":1000})", &ok) == 1000 && ok);
    CHECK(margin_of(R"({"type":"ruled","color":"#FFFFFFFF","foreground_color":"#000000FF","spacing":500,"top_margin":0})", &ok) == 0 && ok);
    CHECK(margin_of(R"({"type":"grid","color":"#FFFFFFFF","foreground_color":"#000000FF","spacing":500,"top_margin":2147483647})", &ok) == 2147483647 && ok);
    // blank and unknown types ignore the field, even an invalid one
    margin_of(R"({"type":"blank","color":"#FFFFFFFF","top_margin":-5})", &ok);
    CHECK(ok);
    // invalid values make the page malformed
    for (const char* bad : {"-1", "2147483648", "\"10\"", "null", "1.5", "[]"}) {
        margin_of(std::string(R"({"type":"grid","color":"#FFFFFFFF","foreground_color":"#000000FF","spacing":500,"top_margin":)") + bad + "}", &ok);
        CHECK(!ok);
    }

    // written and read back
    Document doc = small_document();
    doc.pages[0].bg_type = "dotted";
    doc.pages[0].bg_foreground_color = "#000000FF";
    doc.pages[0].bg_spacing = 500;
    doc.pages[0].bg_top_margin = 1000;
    {
        QuietCerr quiet;
        CHECK(write_onf(doc, "t_margin2.onf"));
        doc.pages[0].bg_top_margin = -1;
        CHECK(!write_onf(doc, "t_margin3.onf"));
    }
    CHECK(read_ok("t_margin2.onf").pages[0].bg_top_margin == 1000);
}

// Section 6.3: line_width of ruled and grid backgrounds.
void test_line_width() {
    auto width_of = [](const std::string& background, bool* readable) {
        build_onf("t_width.onf", R"({"width":21000,"height":29700,"background":)" + background + R"(,"text_objects":[],"assets":[]})");
        Document doc = read_ok("t_width.onf");
        *readable = doc.pages.size() == 2 && doc.pages[0].readable;
        return *readable ? doc.pages[0].bg_line_width : -1;
    };
    const std::string common = R"("color":"#FFFFFFFF","foreground_color":"#000000FF","spacing":500)";
    bool ok = false;
    CHECK(width_of(R"({"type":"ruled",)" + common + "}", &ok) == 0 && ok);
    CHECK(width_of(R"({"type":"ruled",)" + common + R"(,"line_width":8})", &ok) == 8 && ok);
    CHECK(width_of(R"({"type":"grid",)" + common + R"(,"line_width":2147483647})", &ok) == 2147483647 && ok);
    // dotted and blank ignore the field, even an invalid one
    CHECK(width_of(R"({"type":"dotted",)" + common + R"(,"line_width":0})", &ok) == 0 && ok);
    width_of(R"({"type":"blank","color":"#FFFFFFFF","line_width":-5})", &ok);
    CHECK(ok);
    for (const char* bad : {"0", "-1", "2147483648", "\"2\"", "null", "1.5", "[]"}) {
        width_of(R"({"type":"grid",)" + common + R"(,"line_width":)" + bad + "}", &ok);
        CHECK(!ok);
    }

    Document doc = small_document();
    doc.pages[0].bg_type = "ruled";
    doc.pages[0].bg_foreground_color = "#000000FF";
    doc.pages[0].bg_spacing = 500;
    doc.pages[0].bg_line_width = 6;
    {
        QuietCerr quiet;
        CHECK(write_onf(doc, "t_width2.onf"));
        doc.pages[0].bg_line_width = -1;
        CHECK(!write_onf(doc, "t_width3.onf"));
        doc.pages[0].bg_line_width = 6;
        doc.pages[0].bg_type = "dotted";
        CHECK(!write_onf(doc, "t_width4.onf"));
    }
    CHECK(read_ok("t_width2.onf").pages[0].bg_line_width == 6);
}

void test_manifest_dates() {
    auto manifest_with = [](const std::string& created) {
        std::string m = MANIFEST_TEMPLATE;
        m.replace(m.find("2026-06-14T10:32:00.000Z"), 24, created);
        return m;
    };
    auto rejected_date = [&](const std::string& created) {
        build_onf("t_date.onf", meta_of("", ""), {}, manifest_with(created));
        return rejected("t_date.onf");
    };
    CHECK(!rejected_date("2026-06-14T10:32:00.000Z"));
    CHECK(!rejected_date("2028-02-29T00:00:00.000Z"));   // a leap year
    CHECK(!rejected_date("2000-02-29T23:59:59.999Z"));   // divisible by 400
    CHECK(rejected_date("2027-02-29T10:32:00.000Z"));
    CHECK(rejected_date("1900-02-29T10:32:00.000Z"));    // divisible by 100 but not by 400
    CHECK(rejected_date("2026-02-30T10:32:00.000Z"));
    CHECK(rejected_date("2026-04-31T10:32:00.000Z"));
    CHECK(rejected_date("2026-13-01T10:32:00.000Z"));
    CHECK(rejected_date("2026-00-10T10:32:00.000Z"));
    CHECK(rejected_date("2026-06-00T10:32:00.000Z"));
    CHECK(rejected_date("2026-06-14T24:00:00.000Z"));
    CHECK(rejected_date("2026-06-14T10:60:00.000Z"));
    CHECK(rejected_date("2026-06-14T10:32:60.000Z"));
}

void test_writer_refuses() {
    QuietCerr quiet;
    const Bytes png = make_png(200, 100);

    auto with_asset = [&](AssetRef ref, bool embed = true) {
        Document doc = small_document();
        if (embed) doc.asset_files.push_back({"png", png});
        doc.pages[0].assets.push_back(ref);
        return doc;
    };
    AssetRef good{};
    good.id = "a1"; good.layer = 1; good.hash = sha256_hex(png); good.extension = "png";
    good.x = 0; good.y = 0; good.width = 100; good.height = 100;

    CHECK(write_onf(with_asset(good), "t_w.onf"));
    CHECK(!write_onf(with_asset(good, false), "t_w.onf"));  // the file is not embedded

    AssetRef r = good; r.layer = 0;                               CHECK(!write_onf(with_asset(r), "t_w.onf"));
    r = good; r.width = 0;                                         CHECK(!write_onf(with_asset(r), "t_w.onf"));
    r = good; r.extension = "gif";                                 CHECK(!write_onf(with_asset(r), "t_w.onf"));
    r = good; r.pdf_page = 1;                                      CHECK(!write_onf(with_asset(r), "t_w.onf"));  // pdf_page for an image
    r = good; r.crop = Crop{0, 0, 201, 100};                       CHECK(!write_onf(with_asset(r), "t_w.onf"));  // outside the image
    r = good; r.crop = Crop{150, 0, 51, 10};                       CHECK(!write_onf(with_asset(r), "t_w.onf"));
    r = good; r.crop = Crop{-1, 0, 10, 10};                        CHECK(!write_onf(with_asset(r), "t_w.onf"));
    r = good; r.crop = Crop{0, 0, 0, 10};                          CHECK(!write_onf(with_asset(r), "t_w.onf"));
    r = good; r.crop = Crop{0, 0, 200, 100};                       CHECK(write_onf(with_asset(r), "t_w.onf"));
    r = good; r.crop = Crop{199, 99, 1, 1};                        CHECK(write_onf(with_asset(r), "t_w.onf"));

    // pdf: pdf_page is required, crop is not allowed
    Document pdf_doc = small_document();
    pdf_doc.asset_files.push_back({"pdf", PDF_BYTES});
    AssetRef p{};
    p.id = "a1"; p.layer = 1; p.hash = sha256_hex(PDF_BYTES); p.extension = "pdf"; p.x = 0; p.y = 0; p.width = 10; p.height = 10;
    pdf_doc.pages[0].assets = {p};
    CHECK(!write_onf(pdf_doc, "t_w.onf"));                         // pdf_page missing
    pdf_doc.pages[0].assets[0].pdf_page = 1;
    CHECK(write_onf(pdf_doc, "t_w.onf"));
    pdf_doc.pages[0].assets[0].crop = Crop{0, 0, 1, 1};
    CHECK(!write_onf(pdf_doc, "t_w.onf"));

    // a file that is not what its extension says
    Document fake = small_document();
    fake.asset_files.push_back({"png", bytes_of("nope")});
    CHECK(!write_onf(fake, "t_w.onf"));
    Document unsupported = small_document();
    unsupported.asset_files.push_back({"gif", bytes_of("GIF89a")});
    CHECK(!write_onf(unsupported, "t_w.onf"));

    // duplicate ids
    Document dup = with_asset(good);
    AssetRef again = good;
    dup.pages[0].assets.push_back(again);
    CHECK(!write_onf(dup, "t_w.onf"));
}

} // namespace

int main() {
    test_sha256();
    test_appendix_c_vectors();
    test_asset_checks();
    test_assets_round_trip();
    test_asset_statuses();
    test_malformed_placements();
    test_duplicate_ids_and_layers();
    test_byte_order_mark();
    test_missing_page_files();
    test_structural_limits_in_meta();
    test_removed_tool_type();
    test_writer_rejects_removed_tool_type();
    test_top_margin();
    test_line_width();
    test_manifest_dates();
    test_writer_refuses();

    if (failures == 0) std::cout << "all tests passed\n";
    else std::cout << failures << " check(s) failed\n";
    return failures == 0 ? 0 : 1;
}
