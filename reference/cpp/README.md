# ONF Reference Implementation — C++
A reference implementation of the [OpenNotes Format](../../SPECIFICATION.md) (ONF) v0.4.0, written in C++20.
This code exists to demonstrate how the format works in practice. It is not a production library. Anyone implementing ONF in any language can read this code alongside the specification to understand how the binary encoding, ZIP structure, and JSON metadata fit together.

## What it does
The program writes a `.onf` file containing:
- One A4 page with a dotted background (including the required `foreground_color` and `spacing`)
- Two ballpoint pen strokes forming a cross in the centre of the page, one with timestamps and a `created_at` value, one with the `NO_TIMESTAMP` flag and without `created_at`
- One ellipse shape (a circle) around the cross, on the same layer as the strokes
- One text object reading "C++" on a higher layer, with a span that makes the first character bold
- One placement of page 2 of a PDF document (a stand-in file that starts with `%PDF-`), stored under its SHA-256 hash

It then reads the file back and prints the parsed content to the console, demonstrating a complete write → read round trip.

## Prerequisites
- C++20 compiler (GCC or Clang)
- CMake 3.14 or later
- libzip

## Third-party libraries
| Library | Version | License | Purpose |
|---|---|---|---|
| [libzip](https://libzip.org) | system | BSD 3-Clause | ZIP archive read/write |
| [nlohmann/json](https://github.com/nlohmann/json) | bundled | MIT | JSON read/write |

`nlohmann/json` is bundled as a single header in `third_party/nlohmann/json.hpp`. libzip is linked from the system.

## Building
```bash
cd reference/cpp
mkdir build && cd build
cmake ..
cmake --build .
```
Or open the `reference/cpp` folder directly in CLion — it will detect the `CMakeLists.txt` automatically.

## Running
```bash
./cpp
```
Output:
```
Writing output.onf...
Write OK.

Reading output.onf...
Read OK.

=== Document ===
  version:     0.4.0
  title:       C++ Reference
  ...
```

The file `output.onf` is written to the working directory. It is a standard ZIP archive and can be inspected with any ZIP tool:

```bash
unzip -v output.onf
```

## Tests

```bash
ctest
```

Run it in the build directory. The tests need no framework (`tests/test_reference.cpp`). They check:

- SHA-256 against digests computed with Python's `hashlib`
- The vectors V1 to V7 of Appendix C: the writer produces exactly the bytes of the specification, and the reader decodes them into the expected values
- Assets: the writer stores files by hash, the reader finds them again, and every outcome of Sections 9.2 and 9.4.5 (missing, unsupported, hash mismatch, wrong content, too many pixels, crop outside the image) gives the right status
- Malformed placements (invalid `crop`, `pdf_page`, hash, duplicate ids, layer 0) make only one page unreadable
- Tool type 4 (the removed line eraser): such strokes and shapes are skipped, the writer never writes it, and `top_margin` of backgrounds
- Calendar dates in the manifest, byte order marks, missing page files, structural limits in `meta.json`, and the documents the writer refuses

## Project structure

```
reference/cpp/
├── CMakeLists.txt
├── main.cpp                  — builds document, writes, reads, prints
├── src/
│   ├── onf_types.h           — data structures (Document, Page, Stroke, Shape, Sample, TextObject, Span, AssetRef)
│   ├── onf_writer.h/.cpp     — writes a Document to a .onf file
│   ├── onf_reader.h/.cpp     — reads a .onf file into a Document
│   ├── sha256.h/.cpp         — SHA-256, for the names of asset files
│   └── asset_check.h/.cpp    — magic bytes, image size and limits of an asset file
├── tests/
│   └── test_reference.cpp    — tests (run with ctest)
└── third_party/
    └── nlohmann/
        └── json.hpp
```

## How it maps to the spec

| Code | Spec section |
|---|---|
| `onf_types.h` — `Sample` | Section 7.5.1 — Sample fields |
| `onf_types.h` — `Stroke` | Section 7.4.1 — Stroke header |
| `onf_types.h` — `Shape` | Section 7.9 — Shapes |
| `onf_types.h` — `Span` | Section 8.3.1 — Spans |
| `onf_types.h` — `AssetRef`, `Crop`, `AssetFile` | Sections 9.3.2 and 9.4.5 — Asset placements and cropping |
| `onf_types.h` — `StrokeFlags` | Section 7.4.3 — Stroke flags |
| `onf_types.h` — `ToolType` | Section 7.4.2 — Tool type registry |
| `onf_types.h` — `TextObject` | Section 8.3 — Text object fields |
| `onf_types.h` — `Page` | Section 6.2.2 — Page metadata fields |
| `onf_types.h` — `Document` | Section 6.1.2 — Manifest fields |
| `onf_writer.cpp` — `write_le` | Section 2.3.3 — Little-endian byte order |
| `onf_writer.cpp` — `write_varint` | Section 2.3.2 and Appendix A — LEB128 encoding |
| `onf_writer.cpp` — `zigzag_encode` | Section 7.5.3 and Appendix A — Zigzag encoding |
| `onf_writer.cpp` — `build_strokes_bin` | Section 7.2–7.5 — strokes.bin format |
| `onf_writer.cpp` — `build_manifest` | Section 6.1 — manifest.json |
| `onf_writer.cpp` — `build_page_meta` | Section 6.2 — meta.json |
| `onf_writer.cpp` — `write_onf` | Section 4 — ZIP container and compression rules |
| `onf_writer.cpp` — `validate_document` | Sections 5.6, 6.1.2, 7.4, 7.5.1, 7.9, 8.3 — checks before writing |
| `onf_reader.cpp` — `Cursor::varint` | Section 2.3.2 and Appendix A.2 — canonical LEB128 decoding |
| `onf_reader.cpp` — `zigzag_decode` | Section 7.5.3 and Appendix A.1 — Zigzag decoding |
| `onf_reader.cpp` — `parse_strokes_bin` | Section 7.2–7.5, 7.3.2 and 7.9 — strokes.bin parsing |
| `onf_reader.cpp` — `parse_page_meta` | Section 6.2 — meta.json parsing |
| `onf_reader.cpp` — `parse_asset` | Section 9.3.2 — Asset placement fields, `pdf_page`, `crop` |
| `onf_reader.cpp` — `load_assets` | Sections 9.2.1 to 9.2.3 and 9.4.5 — hash, content and crop checks |
| `asset_check.cpp` — `check_asset_content` | Section 9.2.3 — Magic bytes and the 100000000 pixel limit |
| `sha256.cpp` — `sha256_hex` | Section 9.2.1 — File names of assets |
| `onf_reader.cpp` — `parse_manifest` | Section 6.1.2 — manifest validation and page ordering |
| `onf_reader.cpp` — `check_archive` | Sections 4.1, 4.6, 4.7 — ZIP checks |
| `onf_reader.cpp` — `read_onf` | Section 12.3.1 — file-level and page-level errors |

## Conformance

This implementation covers:

- ZIP container structure with correct compression per Section 4.5, and the required `assets/` directory entry
- ZIP checks on read: path safety (Section 4.6), duplicate entries, encrypted entries, symbolic links, and the size limits of Section 4.7 (also while reading)
- Manifest and page metadata as JSON per Sections 6.1 and 6.2, including the strict `id` and `order` rules of Section 6.1.2 and the required background fields of Section 6.3.1
- Binary stroke encoding per Section 7, with the `strokes.bin` version `2`, the `layer` field, the optional `created_at` field (`HAS_CREATED_AT`), and strokes without timestamps (`NO_TIMESTAMP`): every sample, including the first one, is stored as a zigzag or plain varint delta (64-bit arithmetic), and varints are canonical and range-checked (Section 2.3.2)
- Tilt and azimuth sample fields (Section 7.5.1), and the flags `NO_TAPER_START` and `NO_TAPER_END` (Section 7.4.3)
- Strokes and shapes with tool type `4`, the line eraser of earlier versions, are parsed and skipped (Section 7.4.2). Erasing is the job of an application (Section 7.6.1); this implementation has no erase function
- The optional `top_margin` of `ruled`, `grid` and `dotted` backgrounds (Section 6.3.1)
- `SHAPE` chunks with ellipse, rectangle, line, and triangle (Section 7.9). Unknown shape types are skipped. Strokes and shapes are kept in one sequence (`Page::ink`) in file order.
- Text object `spans` (Section 8.3.1), validated on read and write
- Accumulated sample values are range-checked per Section 7.5.2
- The chunk parsing rules of Section 7.3.2, magic byte and `version` verification per Section 7.2 (a `strokes.bin` version other than `2`, such as the `1` of ONF v0.2.0 files, makes the page unreadable), and skipping of unknown and reserved chunk types per Section 10.5.2
- Error scopes per Section 12.3.1: a malformed file is rejected as a whole, a malformed page is marked as unreadable (`Page::readable`, `Page::error`) while the other pages remain readable. A page directory that lacks `meta.json` or `strokes.bin`, a byte order mark in a JSON file, and a violated structural limit of Section 12.6 in `meta.json` are page-level errors (in `manifest.json` file-level); a missing page directory is file-level
- Refusal of files with a higher MAJOR version per Section 10.3 (the reference implementation has no user interface to ask for confirmation)
- Required integer `layer` (1 to 65535, Section 5.6) on strokes, shapes, text objects, and asset placements; `0` or a value out of range is malformed
- Optional `created_at` on strokes, shapes, text objects, and asset placements, and the optional `generator` field of the manifest (Section 6.1.2). The dates of the manifest have to be real dates, not only match the pattern
- Asset placements with `hash`, `extension`, `pdf_page` (required for PDF) and `crop` (images only, validated per Section 9.3.2). Duplicate ids of text objects and of placements make the page malformed
- Asset files: stored by the SHA-256 of their content and only once, checked on read for the hash, the magic bytes and the pixel limit, and for a crop region inside the image (Section 9.4.5). A missing, unsupported or corrupt file is not malformed: it is recorded in `AssetRef::status`
- Rejection of documents that would violate the specification before writing (`validate_document`)

It does not cover: rendering of any kind (so the painter's order of Section 3.4, tool rendering, shape rendering, text layout, and fonts are not implemented), rendering of images and PDF pages, so also the annotations of Section 9.4.4, the PDF page count and encryption of a PDF (these need a PDF parser), a `thumbnail.png` (Section 4.3), ZIP64 handling beyond what libzip does automatically, and font substitution or other host application events. These are left as extensions for anyone building on this reference.

The reference implementation reads and writes ONF v0.4.0 only. Files written against earlier versions are not readable: v0.2.0 files carry a `strokes.bin` version `1` with a different stroke header, and v0.1.4 files additionally use a different sample encoding for the first sample of a stroke.
