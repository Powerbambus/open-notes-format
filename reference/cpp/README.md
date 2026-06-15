# ONF Reference Implementation — C++
A reference implementation of the [OpenNotes Format](../../OpenNotes_Format.md) (ONF) v0.1.4, written in C++20.
This code exists to demonstrate how the format works in practice. It is not a production library. Anyone implementing ONF in any language can read this code alongside the specification to understand how the binary encoding, ZIP structure, and JSON metadata fit together.

## What it does
The program writes a `.onf` file containing:
- One A4 page with a dotted background
- Two ballpoint pen strokes forming a cross in the centre of the page
- One text object reading "C++" placed above the cross

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
./onf_reference
```
Output:
```
Writing output.onf...
Write OK.

Reading output.onf...
Read OK.

=== Document ===
  version:     0.1.4
  title:       C++ Reference
  ...
```

The file `output.onf` is written to the working directory. It is a standard ZIP archive and can be inspected with any ZIP tool:

```bash
unzip -v output.onf
```

## Project structure

```
reference/cpp/
├── CMakeLists.txt
├── main.cpp                  — builds document, writes, reads, prints
├── src/
│   ├── onf_types.h           — data structures (Document, Page, Stroke, Sample, TextObject)
│   ├── onf_writer.h/.cpp     — writes a Document to a .onf file
│   └── onf_reader.h/.cpp     — reads a .onf file into a Document
└── third_party/
    └── nlohmann/
        └── json.hpp
```

## How it maps to the spec

| Code | Spec section |
|---|---|
| `onf_types.h` — `Sample` | Section 7.5.1 — Sample fields |
| `onf_types.h` — `Stroke` | Section 7.4.1 — Stroke header |
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
| `onf_reader.cpp` — `read_varint` | Appendix A — LEB128 decoding |
| `onf_reader.cpp` — `zigzag_decode` | Appendix A — Zigzag decoding |
| `onf_reader.cpp` — `parse_strokes_bin` | Section 7.2–7.5 — strokes.bin parsing |
| `onf_reader.cpp` — `parse_page_meta` | Section 6.2 — meta.json parsing |
| `onf_reader.cpp` — `read_onf` | Section 6.1 — manifest parsing and page ordering |

## Conformance

This implementation covers:

- ZIP container structure with correct compression per Section 4.5
- Manifest and page metadata as JSON per Sections 6.1 and 6.2
- Binary stroke encoding with LEB128 varints and zigzag delta encoding per Section 7
- Magic byte verification on read per Section 7.2
- Unknown chunk skipping on read per Section 10.5.2
- Page ordering by `order` field on read per Section 6.1.2
- Optional layer field defaulting to `middle` per Section 8.3

It does not cover: embedded assets (Section 9), tilt and azimuth sample fields (Section 7.5.1), or thumbnail generation (Section 4.3). These are left as extensions for anyone building on this reference.