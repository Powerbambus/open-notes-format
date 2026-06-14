# OpenNotes Format 
Version 0.1.4
## 1. Introduction
The OpenNotes Format (ONF) is an open, vendor-neutral file format for handwritten digital notes. It is designed to store pen strokes, text, and images in a self-contained file that any conforming application can read and write, without dependency on proprietary software or services. ONF prioritises compact file size, efficient parsing, and long-term accessibility.
### 1.1 Purpose
The OpenNotes Format is a platform-independent file format for storing, reading, and exchanging handwritten notes across multiple devices and operating systems.
### 1.2 Scope
This specification defines the file structure, data types, and parsing rules for ONF files. It does not define network transport, cloud storage, or the user interface of applications that use the format.
### 1.3 Design goals
The important goals for this format are:
- Efficient parsing at read and write time
- Compact file size at scale
- Cross-platform interoperability
- Long-term stability of the format
- Forward compatibility across versions
- An open, publicly documented specification
### 1.4 Non-Goals
The following are not goals of this format:
- Require proprietary software to read or write
- Define or endorse vendor-specific extensions as part of this specification
- Define real-time collaboration or synchronization protocols
- Handle encryption or access control in this version
### 1.5 Format Principles
The OpenNotes Format is based on the following principles:
- Data belongs to the user. A file MUST be fully readable without network access or vendor authentication.
- The specification is open. Anyone may implement a conforming reader or writer.
- Cross-platform by design. No aspect of the format depends on a specific operating system or device.
- Built for the long term. Files written today MUST remain readable by future conforming readers.
- Forward compatible. Readers MUST handle files written by newer versions of this specification gracefully.
## 2. Terminology and Conventions
### 2.1 Keywords
The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED",  "MAY", and "OPTIONAL" in this document are to be interpreted as described in [RFC 2119](https://www.rfc-editor.org/info/rfc2119/).
### 2.2 Definitions
#### File-level terms
- **ONF File:** a ZIP archive with the `.onf` extension containing all data for one note document
- **Archive:** the ZIP container that holds all sections of an ONF file
- **Manifest:** the JSON file at the root of the archive that describes the document
#### Document-level terms
- **Document:** the complete note, consisting of one or more pages
- **Page:** a single canvas with defined physical dimensions, containing strokes, text objects, and images
- **Painter's order:** the rendering rule that content is drawn in the order it was created, newer content appearing on top of older content
#### Content terms
- **Stroke:** a single continuous pen gesture from pen-down to pen-up, consisting of one or more samples
- **Sample:** a single timestamped data point captured from an input device during a stroke, containing position and optionally pressure, tilt, and azimuth
- **Text object:** a typed text element placed at a defined position on a page
- **Asset:** an embedded binary resource, such as an image, stored within the archive
#### Technical terms
- **Reader:** an application that opens and renders ONF files
- **Writer:** an application that creates or modifies ONF files
- **Conforming:** meeting all MUST and MUST NOT requirements for the declared conformance level
- **Chunk:** a length-prefixed binary record in the stroke data file
### 2.3 Notation
#### 2.3.1 Integer Types
The following notation is used throughout this specification to describe fixed-width integer types.
| Notation | C equivalent | Size | Range |
|---|---|---|---|
| `u8` | `uint8_t` | 1 byte | 0 to 255 |
| `u16` | `uint16_t` | 2 bytes | 0 to 65535 |
| `u32` | `uint32_t` | 4 bytes | 0 to 4294967295 |
| `i8` | `int8_t` | 1 byte | -128 to 127 |
| `i16` | `int16_t` | 2 bytes | -32768 to 32767 |
| `i32` | `int32_t` | 4 bytes | -2147483648 to 2147483647 |
#### 2.3.2 Variable-Length Integers
The notation `varint` denotes an unsigned variable-length integer. Small values encode in fewer bytes than large values, reducing file size for data where most values are small, such as delta-encoded stroke coordinates.
ONF uses the unsigned LEB128 encoding, as defined in the WebAssembly Core Specification, Section 5.2.2, and as used by Google Protocol Buffers. A full algorithm and worked examples are provided in Appendix A.
| Notation | Size | Range |
|---|---|---|
| `varint` | 1–5 bytes | 0 to 4294967295 |
#### 2.3.3 Byte Order
All multi-byte integer values in ONF binary sections MUST be stored in little-endian byte order unless explicitly stated otherwise.
Little-endian means the least significant byte is stored first. For example, the `u32` value `305419896` (`0x12345678`) is stored as: `78 56 34 12`
#### 2.3.4 Byte Sequences
Fixed-length sequences of bytes are written as `u8[N]`, where `N` is the number of bytes. For example, a four-byte magic number is written as `u8[4]`.
#### 2.3.5 JSON Values
Sections of the ONF format that use JSON follow the JSON data model as 
defined in [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259). String 
values MUST be encoded as UTF-8. Integer values in JSON MUST NOT use 
floating-point notation.
JSON integer values that correspond to typed fields defined in Section 5 
MUST NOT exceed the range of their target type. A reader encountering a 
JSON integer that exceeds the range of its target type MUST treat the 
file as malformed.
## 3. Format Overview
### 3.1 Container
An ONF file is a ZIP archive with the `.onf` file extension. It can be opened with any standard ZIP tool. This means any developer can inspect the contents of an ONF file without writing any code.
The archive contains three kinds of entries:
- A single manifest file at the archive root
- One directory per page, each containing a metadata file and a binary stroke file
- A shared asset directory containing embedded images
### 3.2 Internal Structure
A minimal ONF archive with two pages and one embedded image has the following structure:
```
note.onf
├── manifest.json
├── pages/
│   ├── p00001/
│   │   ├── meta.json
│   │   └── strokes.bin
│   └── p00002/
│       ├── meta.json
│       └── strokes.bin
└── assets/
	└── a3f1c2d4[...].webp
```
The manifest is the entry point of every ONF file. It identifies the document and defines the order of pages. A reader MUST parse the manifest before reading any other file in the archive.
Each page directory contains two files. The `meta.json` file describes the page; its physical dimensions, background, and any text objects or image placements. The `strokes.bin` file contains the binary stroke data for that page.
The `assets` directory contains embedded images referenced by one or more pages. Each asset is named by the SHA-256 hash of its contents. This ensures that identical images are stored only once regardless of how many pages reference them.
### 3.3 Two-Format Design
ONF uses two data formats for different purposes.
**JSON** is used for metadata, the manifest and page descriptions. JSON is human-readable, easy to parse in any programming language, and tolerant of unknown fields, which supports forward compatibility. Metadata is small and read once per document load, so parsing speed is not a concern.
**Binary** is used for stroke data. A note with thousands of strokes contains millions of individual data points. At that scale, a text-based format would be too large and too slow to parse in real time. The binary stroke format uses delta encoding and variable-length integers to keep files compact without losing any data. This is defined in full in Section 7.
### 3.4 Rendering Model

Content on a page is rendered in three fixed layers, in this order:

1. **Back layer:** rendered first, underneath all other content.
2. **Middle layer:** rendered second. This is the default layer for all content.
3. **Front layer:** rendered last, on top of all other content.

Within each layer, content is rendered in painter's order, sorted by creation time, oldest first. When a stroke and a text object share an identical creation time within the same layer, the stroke is rendered first.
Strokes are always placed in the middle layer. Text objects and assets default to the middle layer but MAY be explicitly placed in the back or front layer by the writer.
## 4. Container Format
### 4.1 ZIP Archive
An ONF file is a ZIP archive as defined by the PKWARE Application Note version 6.3.10, with mandatory support for the ZIP64 extension. The file MUST use the `.onf` file extension.
Readers and writers MUST support ZIP64. Writers MUST use ZIP64 when the uncompressed size of any entry exceeds 4294967295 bytes or when the archive contains more than 65535 entries.
### 4.2 Required Structure
Every valid ONF archive MUST contain the following files:
```
note.onf
├── manifest.json
├── pages/
│   └── p00001/
│       ├── meta.json
│       └── strokes.bin
└── assets/
```
The `assets/` directory MUST be present even if the document contains no embedded assets.
A document with more than one page MUST contain one subdirectory per page under `pages/`. Page directories MUST be named `p` followed by a zero-padded five-digit index starting at `00001`. The page at index `00001` is not necessarily the first page displayed. The page order is defined by the manifest, not by directory naming.
### 4.3 Optional Files
The following optional file MAY be present in the archive:
| Path | Description |
|---|---|
| `thumbnail.png` | A preview image of the page with the lowest `order` value in the manifest. |

If present, `thumbnail.png` MUST be a valid PNG image. Its longest dimension MUST NOT exceed 256 pixels. The aspect ratio of the page with the lowest `order` value in the manifest MUST be preserved. Writers SHOULD include a thumbnail to support file manager previews.
### 4.4 Unknown Archive Content
An ONF archive MUST NOT contain any file or directory whose path conflicts 
with the required structure defined in Section 4.2 or the optional files 
defined in Section 4.3.
A reader encountering files or directories not defined in this specification 
MUST silently ignore them. A reader MUST NOT treat unknown archive entries 
as errors. This rule mirrors the forward compatibility behaviour defined for 
unknown JSON keys in Section 10.5.1 and unknown binary chunks in Section 
10.5.2, and ensures that future minor versions of this specification may 
introduce new file types without breaking existing readers.
### 4.5 Compression
Writers MUST apply DEFLATE compression to the following file types:
- `manifest.json`
- All `meta.json` files

Writers MUST store the following files without compression:
- All `strokes.bin` files
The `thumbnail.png` file and all files in the `assets/` directory SHOULD be stored without compression, as their contents are already in a compressed format.
### 4.6 Path Safety
Readers MUST validate every file path in the archive before accessing it. A reader MUST reject any archive containing entries whose resolved path would fall outside the archive extraction directory. This includes paths containing `..`, absolute path prefixes such as `/` or drive letters such as `C:\`, and null bytes.
A reader that encounters an invalid path MUST NOT extract the archive and MUST report an error to the application.
### 4.7 Size Limits
Readers MUST enforce the following limits to prevent resource exhaustion:
| Limit | Value |
|---|---|
| Maximum uncompressed size of any single entry | 2 GB |
| Maximum total uncompressed size of the archive | 10 GB |
| Maximum compression ratio of any single entry | 100:1 |

A reader MUST abort extraction and report an error if any of these limits are exceeded.
## 5. Data Types

This section defines the composite data types used throughout the ONF specification. All primitive types (`u8`, `u16`, `u32`, `i8`, `i16`, `i32`, `varint`) are defined in Section 2.3.

### 5.1 Coordinate — `coord`
A `coord` value represents a physical position on a page canvas.
| Property | Value |
|---|---|
| Underlying type | `i32` |
| Unit | 1/100 millimetre |
| Range | -2147483648 to 2147483647 |

A value of `10500` represents 105.00 mm. Negative values are valid and represent positions outside the page boundary, for example a stroke that begins off the left edge of the page.
All X and Y position values in this specification are of type `coord` unless explicitly stated otherwise.
### 5.2 Color — `color`
A `color` value represents an RGBA colour.
| Byte offset | Field | Type | Description |
|---|---|---|---|
| 0 | red | `u8` | Red channel, 0–255 |
| 1 | green | `u8` | Green channel, 0–255 |
| 2 | blue | `u8` | Blue channel, 0–255 |
| 3 | alpha | `u8` | Opacity, 0 = fully transparent, 255 = fully opaque |

A `color` value is always exactly 4 bytes. The byte order is fixed as red, green, blue, alpha regardless of the platform byte order defined in Section 2.3.3.
### 5.3 Timestamp — `timestamp`
A `timestamp` value represents elapsed time in milliseconds from the beginning of a stroke.
| Property | Value |
|---|---|
| Underlying type | `u32` |
| Unit | 1 millisecond |
| Maximum duration | 4294967295 ms: approximately 49 days per stroke |

Timestamps within a stroke MUST be non-decreasing. A reader encountering a timestamp that is less than the previous sample's timestamp MUST treat the file as malformed.

### 5.4 Flags — `flags`
A `flags` value is a `u16` where individual bits carry boolean meaning. Each bit is either set (1) or unset (0). Bits not defined by this specification MUST be set to 0 by writers. Readers MUST ignore undefined bits.
Flag definitions are local to the structure that contains them and are defined where that structure is specified. The `flags` type name indicates only that a `u16` is being used as a bitfield.
### 5.5 Millisecond Timestamp — `mstime`
A `mstime` value represents an absolute point in time with millisecond 
precision.
| Property | Value |
|---|---|
| Underlying type | `u64` |
| Unit | Milliseconds elapsed since 1970-01-01 00:00:00 UTC |
| Range | 0 to 18446744073709551615 |
| Maximum date | Year 292,277,026 UTC |

A `mstime` value is used to record when a stroke, text object, or asset 
was created. Millisecond precision ensures that objects created within the 
same second can be correctly ordered in painter's order without ambiguity.
Writers MUST record the actual creation time at millisecond precision where 
the platform supports it. On platforms that only provide second-level 
precision, writers MUST multiply the second value by 1000 and MUST append 
a monotonically increasing sub-second offset to ensure uniqueness within 
the same document editing session.
## 6. Page Model
### 6.1 Manifest
The manifest is the entry point of every ONF file. It is located at `manifest.json` in the archive root. A reader MUST parse the manifest before reading any other file in the archive.
#### 6.1.1 Manifest Structure
```json
{
    "onf_version": "0.1.4",
    "title": "Lecture Notes",
    "created_at": "2026-06-14T10:32:00.000Z",
    "modified_at": "2026-06-14T12:15:00.000Z",
    "pages": [
        { "id": "p00001", "order": 1 },
        { "id": "p00002", "order": 2 },
        { "id": "p00003", "order": 3 }
    ]
}
```
#### 6.1.2 Manifest Fields
| Field | Type | Required | Description |
|---|---|---|---|
| `onf_version` | string | REQUIRED | The version of this specification the file was written against. Format: `MAJOR.MINOR.PATCH`. |
| `title` | string | REQUIRED | The document title. MAY be an empty string. |
| `created_at` | string | REQUIRED |The date and time the document was first created. MUST be a valid ISO 8601 UTC datetime string with millisecond precision. Format: `2026-06-14T10:32:00.000Z`. |
| `modified_at` | string | REQUIRED | The date and time the document was last modified. MUST be a valid ISO 8601 UTC datetime string with millisecond precision. Format: `2026-06-14T10:32:00.000Z`. |
| `pages` | array | REQUIRED | Ordered list of page descriptors. MUST contain at least one entry. |
| `pages[].id` | string | REQUIRED | The directory name of the page under `pages/`. MUST match an existing page directory in the archive. |
| `pages[].order` | integer | REQUIRED | The display position of this page. MUST be a positive integer. MUST be unique within the pages array. |

A reader MUST sort pages by their `order` value before rendering. A reader encountering duplicate `order` values MUST treat the file as malformed.
Readers MUST silently ignore any JSON key not defined in this specification.
### 6.2 Page Metadata
Each page directory contains a `meta.json` file that describes the physical properties of the page and its non-stroke content.
#### 6.2.1 Page Metadata Structure
```json
{
    "width": 21000,
    "height": 29700,
    "background": {
	    "type": "dotted",
	    "color": "#FFFFFFFF",
	    "foreground_color": "#000000FF",
	    "spacing": 500
	},
    "text_objects": [],
    "assets": []
}
```

#### 6.2.2 Page Metadata Fields
| Field | Type | Required | Description |
|---|---|---|---|
| `width` | integer | REQUIRED | Page width in `coord` units (1/100 mm). MUST be a positive integer. |
| `height` | integer | REQUIRED | Page height in `coord` units (1/100 mm). MUST be a positive integer. |
| `background` | object | REQUIRED | Background descriptor. See Section 6.3. |
| `text_objects` | array | REQUIRED | List of text objects on this page. See Section 8. MAY be empty. |
| `assets` | array | REQUIRED | List of image placements on this page. See Section 9. MAY be empty. |

When a page is resized, the writer MUST update only the `width` and 
`height` fields in `meta.json`. Content coordinates MUST NOT be 
recalculated. All content retains its absolute physical position 
on the canvas. Content that falls outside the new page boundary 
remains valid and MUST be preserved in the file.
### 6.3 Background
#### 6.3.1 Background Fields
| Field | Type | Required | Description |
|---|---|---|---|
| `type` | string | REQUIRED | One of: `blank`, `ruled`, `grid`, `dotted`. |
| `color` | string | REQUIRED | The page surface color in the format `#RRGGBBAA`. The alpha channel defines the opacity of the background surface. For a fully opaque background use `FF` as the alpha value. |
| `foreground_color` | string | REQUIRED for `ruled`, `grid`, `dotted` | The color of lines or dots in the format `#RRGGBBAA`. |
| `spacing` | integer | REQUIRED for `ruled`, `grid`, `dotted` | The distance between lines or dots in `coord` units. MUST be a positive integer. |

Writers MUST explicitly state all required fields for the chosen background type. Readers MUST NOT apply default values for missing required fields. A reader encountering a missing required background field MUST treat the file as malformed.
#### 6.3.2 Background Rendering Rules
Readers that support background rendering MUST follow these rules exactly. These rules are normative. Deviation from these rules produces a non-conforming reader.
**Blank**
The page surface is filled with `color`. No additional elements are drawn.
**Ruled**
The page surface is filled with `color`. Horizontal lines are drawn across the full page width at the following positions:
- The first line is drawn at Y coordinate equal to the `spacing` value.
- Subsequent lines are drawn at intervals of `spacing` in `coord` units.
- No line is drawn at Y coordinate `0`.

Line appearance:
- Width: 1 physical display pixel, not scaled with document zoom.
- Color: `foreground_color` at 20% opacity.

**Grid**
The page surface is filled with `color`. Vertical and horizontal lines are drawn across the full page at the following positions:
- The first vertical line is drawn at X coordinate equal to the `spacing` value.
- The first horizontal line is drawn at Y coordinate equal to the `spacing` value.
- Subsequent lines are drawn at intervals of `spacing` in `coord` units in both directions.
- No line is drawn at X coordinate `0` or Y coordinate `0`.

Line appearance:
- Width: 1 physical display pixel, not scaled with document zoom.
- Color: `foreground_color` at 20% opacity.

**Dotted**
The page surface is filled with `color`. Filled circles are drawn at the intersection of an invisible grid with spacing equal to the `spacing` value in `coord` units.
- The first dot centre is at coordinate `(spacing, spacing)`.
- Subsequent dot centres are placed at intervals of `spacing` in both directions.
- No dot is drawn at X coordinate `0` or Y coordinate `0`.

Dot appearance:
- Diameter: 20% of the `spacing` value in `coord` units, converted to physical display pixels.
- Color: `foreground_color` at 25% opacity.

#### 6.3.3 Unsupported Backgrounds
A reader that does not support a specific background type MUST still open the file. It MUST fill the page surface with the `color` value and render no background pattern. It MUST NOT treat an unsupported background type as an error.
### 6.4 Page Dimensions
Writers SHOULD use the standard DIN dimensions defined in Appendix B. Custom dimensions are permitted. The `width` and `height` fields in `meta.json` are always authoritative.
A page MAY be oriented in portrait or landscape. There is no explicit orientation field; orientation is determined by whether `width` is less than or greater than `height`.
A writer MUST NOT change the orientation of a page after it has been 
created. A page created in portrait MUST remain portrait. A page 
created in landscape MUST remain landscape. Orientation is determined 
by whether `width` is less than or greater than `height` at the time 
of page creation.
### 6.5 Coordinate Origin
The coordinate origin `(0, 0)` is the top-left corner of the page. The X axis increases to the right. The Y axis increases downward. This is consistent across all pages and all devices.
Content positioned outside the page boundary, where X is less than `0`, X is greater than `width`, Y is less than `0`, or Y is greater than `height`, is valid. Readers SHOULD clip such content to the page boundary when rendering.
## 7. Stroke Format
### 7.1 Overview
Stroke data for each page is stored in the `strokes.bin` binary file located in the page directory. This file contains all strokes for that page in painter's order.
`strokes.bin` uses a chunk-based structure. Every piece of data is wrapped in a chunk with a type identifier and a length field. A reader that encounters an unknown chunk type MUST skip it using the length field and continue reading. A reader MUST NOT treat an unknown chunk type as an error.
### 7.2 File Header
Every `strokes.bin` file MUST begin with the following 8-byte header:
| Offset | Size | Type | Field | Value |
|---|---|---|---|---|
| 0 | 4 | `u8[4]` | magic | `0x4F 0x4E 0x46 0x53` |
| 4 | 2 | `u16` | version | `1` |
| 6 | 2 | `u8[2]` | reserved | MUST be `0x00 0x00` |

The magic bytes spell `ONFS` in ASCII. A reader MUST verify the magic 
bytes before reading any further data. A reader encountering incorrect 
magic bytes MUST treat the file as invalid and MUST NOT attempt to parse 
further.
The `version` field is an independent binary format revision counter. It 
is not related to the `onf_version` string in `manifest.json`. In ONF 
v0.x and v1.0.0 this field MUST be `1`. If the binary stroke format 
changes in a future major version of the specification this field will be 
incremented. A reader encountering a `version` value higher than it 
implements SHOULD signal a version mismatch event to the host application 
but MUST attempt to parse the file using the chunk skipping mechanism 
defined in Section 7.3.
Readers MUST ignore non-zero reserved bytes and MUST NOT treat them as 
an error.
### 7.3 Chunk Structure
All content after the file header is structured as a sequence of chunks. Each chunk begins with a 6-byte frame header followed immediately by the chunk body:
| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 2 | `u16` | chunk_type | Identifies the kind of data in this chunk |
| 2 | 4 | `u32` | chunk_length | Byte length of the chunk body, not including this header |
| 6 | N | bytes | chunk_body | Exactly `chunk_length` bytes of chunk data |

The next chunk begins immediately after the last byte of the current chunk body.
Writers MUST set `chunk_length` accurately. A reader that cannot trust `chunk_length` cannot safely skip unknown chunks.
#### 7.3.1 Chunk Type Registry
| Value | Name | Description |
|---|---|---|
| `0x0001` | `STROKE` | A single stroke record. See Section 7.4. |
| `0x0002` | `END` | Marks the end of stroke data. Chunk body is empty. |
| `0x8000`–`0xFFFF` | Vendor extension | Reserved for vendor-defined chunk types. MUST be skipped by readers that do not recognise them. |

The `END` chunk is RECOMMENDED. Writers SHOULD write an `END` chunk as the final chunk in every `strokes.bin` file. Readers MUST NOT require an `END` chunk to be present.
### 7.4 Stroke Record
A `STROKE` chunk body contains one complete stroke. It consists of a fixed-size stroke header followed by a variable-length sample block.
#### 7.4.1 Stroke Header
| Offset | Size | Type | Field | Description |
|---|---|---|---|---|
| 0 | 4 | `u32` | stroke_id | Unique identifier for this stroke within the page. MUST be unique. Assigned by the writer. |
| 4 | 8 | `mstime` | created_at | The time this stroke was created in milliseconds since Unix epoch. |
| 12 | 4 | `color` | color | The ink color of this stroke. See Section 5.2. |
| 16 | 2 | `u16` | tool_type | The tool used to draw this stroke. See Section 7.4.2. |
| 18 | 2 | `u16` | base_width | The base width of this stroke in `coord` units. MUST be greater than 0. |
| 20 | 2 | `flags` | flags | Indicates which optional fields are present in the sample block. See Section 7.4.3. |
| 22 | 4 | `u32` | sample_count | The number of samples in the sample block. MUST be greater than 0. |
#### 7.4.2 Tool Type Registry

| Value | Tool | Description |
|---|---|---|
| `0` | Ballpoint pen | Consistent width with slight pressure response. |
| `1` | Fountain pen | Strong pressure response, variable width. |
| `2` | Pencil | Textured appearance, pressure affects opacity. |
| `3` | Highlighter | Semi-transparent, rendered with multiply blend mode. |
| `4` | Line eraser | Masks underlying content. See Section 7.4.4. |
| `0x8000`–`0xFFFF` | Vendor-defined | Reserved for vendor-defined tool types. |

A reader encountering an unrecognised tool type MUST render the stroke as a ballpoint pen using the stored color and base width. A reader MUST NOT treat an unrecognised tool type as an error.
#### 7.4.3 Stroke Flags
| Bit | Name | Meaning when set |
|---|---|---|
| 0 | `HAS_TILT` | Sample block includes tilt_x and tilt_y fields. |
| 1 | `HAS_AZIMUTH` | Sample block includes azimuth field. |
| 2 | `NO_PRESSURE` | Device reported no pressure. All pressure values are 255. |
| 3–15 | Reserved | MUST be 0. Readers MUST ignore. |
#### 7.4.4 Line Eraser Rendering
A stroke with tool type `4` MUST be rendered as a mask over ink strokes 
that appear earlier in the total painter's order on the same page. The 
eraser removes ink along its path. The eraser stroke itself MUST NOT be 
visible as ink.
The line eraser affects only ink strokes. It MUST NOT mask text objects 
or embedded assets regardless of their layer or position in painter's 
order. Text objects and assets can only be removed by deleting them from 
the file entirely — there is no non-destructive erase for these content 
types.
The line eraser affects content on the middle layer. It 
MUST NOT mask content on the front and back layer.
The masked area is determined by the eraser stroke path and its 
`base_width`. Pressure data in the sample block MAY be used to vary the 
eraser width along the path.
A reader that does not support line eraser rendering MUST skip eraser 
strokes entirely. It MUST NOT render them as ink strokes.
### 7.5 Sample Block
The sample block immediately follows the stroke header. It contains exactly `sample_count` samples encoded sequentially using delta encoding and varint compression.
#### 7.5.1 Sample Fields
Each sample contains the following fields in the order listed. The encoding column specifies whether the delta value is encoded as a plain unsigned LEB128 varint or as a zigzag-encoded LEB128 varint as defined in Sections 7.5.2 and 7.5.3.
| Field | Always present | Delta encoding | Varint encoding | Description |
|---|---|---|---|---|
| x | YES | Yes | Zigzag varint | X position delta in `coord` units. |
| y | YES | Yes | Zigzag varint | Y position delta in `coord` units. |
| pressure | YES | Yes | Zigzag varint | Pressure delta. Range 0–255. |
| timestamp | YES | Yes | Plain varint | Timestamp delta in milliseconds since stroke start. Never negative — delta is always ≥ 0. |
| tilt_x | Only if `HAS_TILT` is set | Yes | Zigzag varint | Tilt along X axis delta. Range -90 to +90 degrees. |
| tilt_y | Only if `HAS_TILT` is set | Yes | Zigzag varint | Tilt along Y axis delta. Range -90 to +90 degrees. |
| azimuth | Only if `HAS_AZIMUTH` is set | Yes | Zigzag varint | Azimuth delta. Range 0–35999 hundredths of a degree. |

**Tilt reference frame:** `tilt_x` and `tilt_y` are stored in whole degrees. A value of `0` for both fields means the pen is perfectly vertical. Positive `tilt_x` means the pen tilts toward the right of the page. Negative `tilt_x` means the pen tilts toward the left. Positive `tilt_y` means the pen tilts toward the bottom of the page. Negative `tilt_y` means the pen tilts toward the top.
**Azimuth reference frame:** `azimuth` is stored in hundredths of a degree. A value of `0` means the pen tip points toward the top of the page. Values increase clockwise.
**First sample encoding exception:** The first sample in every stroke stores absolute values, not deltas. All fields in the first sample are encoded as plain unsigned LEB128 varints regardless of whether they would normally use zigzag encoding. Absolute values are always non-negative and therefore do not require zigzag mapping.
Optional fields are either present in every sample of a stroke or absent from every sample. They MUST NOT vary per sample within a stroke.
#### 7.5.2 Delta Encoding
All sample fields use delta encoding. Each value stored is the difference from the previous sample's value, not the absolute value.
For the first sample in a stroke, the delta is relative to zero, meaning the absolute value is stored directly as the delta.
Example with three samples:
```
Sample 0:  x=10000  y=5000  pressure=200  timestamp=0
Sample 1:  x=10004  y=5003  pressure=202  timestamp=8
Sample 2:  x=10009  y=5007  pressure=200  timestamp=16

Stored as:
Sample 0:  dx=10000  dy=5000  dp=200  dt=0
Sample 1:  dx=4      dy=3     dp=2    dt=8
Sample 2:  dx=5      dy=4     dp=-2   dt=8
```
A reader MUST track the running absolute value of each field by accumulating deltas from the first sample. If the accumulated value of any `coord` field exceeds the range of `i32` 
(-2147483648 to 2147483647), the reader MUST treat the file as malformed and MUST NOT attempt to continue parsing the stroke.
#### 7.5.3 Zigzag Encoding
Delta values for fields marked "Zigzag varint" in Section 7.5.1 can be negative. Varint encoding as defined in Section 2.3.2 only handles unsigned values. Negative deltas are therefore mapped to unsigned values using zigzag encoding before varint encoding is applied.
The `timestamp` field is marked "Plain varint" because its delta is always non-negative. Timestamps within a stroke are non-decreasing as defined in Section 5.3. Zigzag encoding MUST NOT be applied to `timestamp` deltas.
Zigzag encoding maps signed integers to unsigned integers so that small negative numbers produce small unsigned values:
```
0  →  0
-1 →  1
1  →  2
-2 →  3
2  →  4
```
The encoding and decoding functions are:
```
encode: (n << 1) ^ (n >> 31)
decode: (n >> 1) ^ -(n & 1)
```
A full worked example is provided in Appendix A.
#### 7.5.4 Devices Without Pressure Support
If a device does not report pressure, the writer MUST set the `NO_PRESSURE` flag in the stroke header and MUST store the value `255` for the pressure field of every sample. The delta for pressure in all samples after the first MUST be `0`.
A reader encountering the `NO_PRESSURE` flag SHOULD render the stroke at a fixed width determined by `base_width`, ignoring pressure values entirely.
### 7.6 Stroke Ordering
Strokes in `strokes.bin` MUST be written in the order they were created, oldest first. This defines the painter's order for rendering. A reader MUST render strokes in the order they appear in the file.
When a writer deletes a stroke, for example as the result of a stroke eraser operation, it MUST remove the corresponding `STROKE` chunk from the file entirely. Deleted strokes MUST NOT be retained in the file in any form.
### 7.7 Tool Rendering
This section defines the rendering behaviour for each tool type. These rules are normative. A conforming reader MUST follow them when rendering strokes.
Exact pixel-perfect rendering consistency across all implementations is a goal for future versions of this specification. For v1, conforming readers that follow these rules will produce visually similar results.
All width values in this section are derived from the stroke `base_width` field. All opacity values are derived from the alpha channel of the stroke `color` field unless stated otherwise.
#### 7.7.1 Pressure to Width Mapping
For tools where width varies with pressure, the following linear mapping MUST be applied:
```
width = min_width + (max_width - min_width) * (pressure / 255)
```
Where `min_width` and `max_width` are defined per tool type in the sections below.
#### 7.7.2 Ballpoint Pen: tool type `0`
| Property | Rule |
|---|---|
| Width | Varies linearly with pressure. Minimum 40% of `base_width` at pressure 0. Maximum 100% of `base_width` at pressure 255. |
| Opacity | Fixed at the alpha value of `color`. |
| Taper | None. |
| Blend mode | Normal. |
#### 7.7.3 Fountain Pen: tool type `1`
| Property | Rule |
|---|---|
| Width | Varies with pressure. Minimum 10% of `base_width` at pressure 0. Maximum 100% of `base_width` at pressure 255. |
| Opacity | Fixed at the alpha value of `color`. |
| Taper | The stroke tapers at its start and end. Taper is applied over the first and last 5% of samples. Width at the first and last sample is 0. Width increases and decreases linearly between the taper boundary and the stroke body. |
| Blend mode | Normal. |
#### 7.7.4 Pencil: tool type `2`
| Property | Rule |
|---|---|
| Width | Fixed at `base_width`. Does not vary with pressure. |
| Opacity | Varies linearly with pressure. Minimum 20% of the alpha value of `color` at pressure 0. Maximum 100% of the alpha value of `color` at pressure 255. |
| Taper | None. |
| Blend mode | Normal. |
#### 7.7.5 Highlighter: tool type `3`
| Property | Rule |
|---|---|
| Width | Fixed at `base_width`. Does not vary with pressure. |
| Opacity | Fixed at 40% regardless of the alpha value of `color`. |
| Taper | None. |
| Blend mode | Multiply, as defined in the W3C Compositing and Blending specification Level 1, Section 8.1. |
#### 7.7.6 Line Eraser: tool type `4`
| Property | Rule |
|---|---|
| Width | Varies linearly with pressure. Minimum 60% of `base_width` at pressure 0. Maximum 100% of `base_width` at pressure 255. |
| Opacity | Not applicable. The eraser masks content rather than drawing ink. |
| Taper | None. |
| Blend mode | Not applicable. |
#### 7.7.7 Unsupported Tool Types
A reader encountering an unrecognised tool type MUST render the stroke using the ballpoint pen rules defined in Section 7.7.2, using the stored `color` and `base_width`. A reader MUST NOT treat an unrecognised tool type as an error.
### 7.8 Degenerate Strokes
A stroke with a `sample_count` of `1` is valid. A single-sample stroke 
MUST be rendered as a filled circle at the sample position. The diameter 
of the circle MUST equal the `base_width` of the stroke. The color and 
opacity are determined by the tool type rendering rules in Section 7.7.
For fountain pen strokes (tool type `1`) where the taper regions defined 
in Section 7.7.3 would overlap, specifically where the stroke contains 
fewer than 20 samples, meaning 5% of samples rounds to zero, no taper 
MUST be applied. The stroke MUST be rendered at full `base_width` for its 
entire length.
## 8. Text Objects
### 8.1 Overview
Text objects represent keyboard-entered text placed at a defined position on a page canvas. They are stored in the `text_objects` array of the page `meta.json` file.
Text objects participate in painter's order rendering alongside strokes. A reader MUST merge text objects and strokes by their `created_at` value before rendering. When a text object and a stroke share an identical `created_at` value, the stroke MUST be rendered first.
A text object MAY be assigned to the back or front layer. A text object with no `layer` field MUST be treated as belonging to the middle layer.
### 8.2 Text Object Structure
```json
{
    "id": "t00001",
    "created_at": 1718358720000,
    "x": 5000,
    "y": 3000,
    "width": 8000,
    "height": 4000,
    "content": "This is a text object.\nThis is a second line.",
    "font_family": "Helvetica",
    "generic_family": "sans-serif",
    "font_size": 423,
    "color": "#000000FF",
    "bold": false,
    "italic": false,
    "layer": "middle"
}
```
### 8.3 Text Object Fields
| Field | Type | Required | Description |
|---|---|---|---|
| `id` | string | REQUIRED | Unique identifier for this text object within the page. MUST be unique across all text objects on the page. |
| `created_at` | integer | REQUIRED | The time this text object was created in milliseconds since Unix epoch, as a `mstime` value. Used for painter's order sorting. |
| `x` | integer | REQUIRED | X coordinate of the top-left corner of the text box in `coord` units. |
| `y` | integer | REQUIRED | Y coordinate of the top-left corner of the text box in `coord` units. |
| `width` | integer | REQUIRED | Width of the text bounding box in `coord` units. MUST be greater than 0. |
| `height` | integer | REQUIRED | Height of the text bounding box in `coord` units. MUST be greater than 0. |
| `layer` | string | OPTIONAL | The rendering layer for this text object. One of: `back`, `middle`, `front`. Default: `middle`. |
| `content` | string | REQUIRED | The text content. MUST be a valid UTF-8 string. Newline characters (`\n`) represent line breaks. MAY be an empty string. |
| `font_family` | string | REQUIRED | The font family requested by the writer. MAY be any font family name. |
| `generic_family` | string | REQUIRED | The generic font family to use when `font_family` is not available. See Section 8.4. |
| `font_size` | integer | REQUIRED | Font size in `coord` units. MUST be greater than 0. |
| `color` | string | REQUIRED | Text color in the format `#RRGGBBAA`. |
| `bold` | boolean | REQUIRED | Whether the text is bold. |
| `italic` | boolean | REQUIRED | Whether the text is italic. |
### 8.4 Font Handling
#### 8.4.1 Generic Font Families
The following generic font family values are defined by this specification:
| Value | Description |
|---|---|
| `sans-serif` | A font without serifs. Examples: Helvetica, Arial, Roboto. |
| `serif` | A font with serifs. Examples: Times New Roman, Georgia. |
| `monospace` | A font where every character has equal width. Examples: Courier, Consolas. |

The `generic_family` field MUST contain one of these three values. A reader MUST NOT treat an unrecognised `generic_family` value as an error, it MUST fall back to `sans-serif`.
#### 8.4.2 Font Resolution
The reader MUST signal a font substitution event to the host application, 
identifying the requested font and the font used as a substitute. The host 
application MUST present this information to the user. In contexts where 
no user interface is available the host application MUST log or otherwise 
record the substitution in a way accessible to the operator.
A reader MUST NOT substitute a font without signalling this event.
#### 8.4.3 Font Size
Font size is stored in `coord` units (1/100 mm) rather than typographic points. This ensures text renders at a consistent physical size across all devices and screen resolutions.
To convert to typographic points for rendering:
```
points = font_size_in_coord_units * (72 / 2540)
```
Common font sizes expressed in `coord` units:
| coord units | Millimetres | Typographic points | Typical use |
|---|---|---|---|
| 353 | 3.53 mm | ~10 pt | Small annotation |
| 423 | 4.23 mm | ~12 pt | Standard body text |
| 529 | 5.29 mm | ~15 pt | Subheading |
| 706 | 7.06 mm | ~20 pt | Heading |

Writers SHOULD use values from this table for standard text sizes. Writers MAY use any positive integer value.
### 8.5 Multiline Text
A text object MAY contain newline characters (`\n`) in its `content` field. Each newline represents a hard line break. A reader MUST render each segment separated by a newline on a new line within the text bounding box.
Text that exceeds the width of the bounding box MUST be wrapped by the reader at word boundaries. Text that exceeds the height of the bounding box SHOULD be clipped to the bounding box boundary.
### 8.6 Text Layout Model
Readers MUST follow these layout rules when rendering text objects. These 
rules define a minimal but precise layout model that ensures consistent 
rendering across conforming implementations.
#### 8.6.1 Line Height
The line height MUST be 120% of the rendered font size. For a font size 
of 423 coord units (approximately 12pt), the line height is 508 coord 
units.
#### 8.6.2 Text Alignment
The default text alignment is left-aligned. This specification does not 
define additional alignment options in v1. A future version MAY introduce 
an alignment field.
#### 8.6.3 Word Breaking
Text MUST be wrapped at word boundaries — spaces and other Unicode 
word-break opportunities as defined in Unicode Standard Annex #14. For 
scripts that do not use spaces to delimit words — including but not 
limited to Chinese, Japanese, and Korean — text MUST be wrapped at 
character boundaries.
#### 8.6.4 Bidirectional Text
Bidirectional text rendering is not defined in v1. Readers SHOULD apply 
the Unicode Bidirectional Algorithm (Unicode Standard Annex #9) where 
platform support allows. Exact bidirectional rendering consistency across 
implementations is a goal for v2.
#### 8.6.5 Clipping
Text that exceeds the height of the bounding box MUST be clipped at the 
bounding box boundary. Clipped content remains in the file and MUST NOT 
be deleted by a reader.
### 8.7 Text Object Ordering
Text objects MUST be listed in the `text_objects` array in ascending `created_at` order. A reader encountering text objects out of order MUST sort them by `created_at` before rendering.
## 9. Embedded Assets
### 9.1 Overview
Assets are binary resources embedded directly within the ONF archive. In v0.1.4 of this specification, assets are limited to images. Assets are stored in the `assets/` directory at the archive root and referenced by pages via the `assets` array in `meta.json`.
An asset is stored once in the archive regardless of how many pages reference it. If the same image appears on three pages, it is stored as a single file in `assets/` and referenced three times.
### 9.2 Asset Storage
#### 9.2.1 File Naming
Asset files MUST be named using the lowercase hexadecimal SHA-256 hash of their uncompressed content, followed by the appropriate file extension:
```
assets/
    a3f1c2d4e5b6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2.webp
```
A writer MUST compute the SHA-256 hash of the raw asset file bytes prior to ZIP compression and use that hash as the filename. This ensures that identical assets always produce identical filenames, enabling automatic deduplication across pages.
A reader SHOULD verify that the content of each asset file matches its hash-derived filename before rendering it. A reader that performs verification and finds a mismatch MUST treat the asset as corrupt, render a placeholder in its place, and signal a corrupt asset event to the host application. A reader MAY skip verification for performance reasons on large assets.
#### 9.2.2 Supported Formats
| Extension | Format | Notes |
|---|---|---|
| `.webp` | `image/webp` | RECOMMENDED for photographs and mixed content. |
| `.png` | `image/png` | RECOMMENDED for diagrams, screenshots, and images requiring transparency. |
| `.jpg` | `image/jpeg` | Accepted. Writers SHOULD prefer `.jpg` for new assets. |
| `.jpeg` | `image/jpeg` | Accepted. Writers SHOULD prefer `.jpg` over`.jpeg` for new assets. |

A writer MUST NOT embed assets in formats not listed in this table. A reader encountering an asset with an unrecognized extension MUST NOT attempt to decode it. The reader MUST render a placeholder in its place and MUST signal an unsupported asset format event to the host application.
### 9.3 Asset References
Each page references its assets through the `assets` array in `meta.json`. An asset reference describes where and how an asset is placed on the page.
#### 9.3.1 Asset Reference Structure
```json
{
    "id": "a00001",
    "created_at": 1718358720000,
    "hash": "a3f1c2d4e5b6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2",
    "extension": "webp",
    "x": 2000,
    "y": 4000,
    "width": 6000,
    "height": 4500,
    "layer": "back"
}
```
#### 9.3.2 Asset Reference Fields
| Field | Type | Required | Description |
|---|---|---|---|
| `id` | string | REQUIRED | Unique identifier for this asset placement within the page. MUST be unique across all asset references on the page. |
| `created_at` | integer | REQUIRED | The time this asset was placed on the page in milliseconds since Unix epoch. Used for painter's order sorting within the middle layer. |
| `hash` | string | REQUIRED | The lowercase hexadecimal SHA-256 hash of the asset content. Used to locate the asset file in the `assets/` directory. |
| `extension` | string | REQUIRED | The file extension of the asset. MUST be one of the values defined in Section 9.2.2. |
| `x` | integer | REQUIRED | X coordinate of the top-left corner of the asset in `coord` units. |
| `y` | integer | REQUIRED | Y coordinate of the top-left corner of the asset in `coord` units. |
| `width` | integer | REQUIRED | Rendered width of the asset in `coord` units. MUST be greater than 0. |
| `height` | integer | REQUIRED | Rendered height of the asset in `coord` units. MUST be greater than 0. |
| `layer` | string | OPTIONAL | The rendering layer for this asset. One of: `back`, `middle`, `front`. Default: `middle`. |

A reader encountering a reference whose asset file is missing from the archive MUST render a placeholder in its place and MUST signal a missing asset event to the host application, identifying the asset by its hash. The host application MUST present this information to the user. In contexts where no user interface is available the host application MUST log the missing asset.
### 9.4 Rendering
#### 9.4.1 Layer Assignment
Assets are rendered according to their `layer` value as defined in the three-layer rendering model described in Section 3.4:
| Layer value | Rendering position |
|---|---|
| `back` | Rendered first, beneath all middle and front layer content. |
| `middle` | Rendered with strokes and text objects, sorted by `created_at`. |
| `front` | Rendered last, above all back and middle layer content. |

An asset with no `layer` field MUST be treated as belonging to the middle layer.
#### 9.4.2 Painter's Order Within Layers
Within the back and front layers, assets are rendered in ascending `created_at` order. Within the middle layer, assets are merged with strokes and text objects and rendered together in ascending `created_at` order.
When a stroke, text object, and asset share an identical `created_at` value within the middle layer, the rendering order MUST be: stroke first, then text object, then asset.
#### 9.4.3 Scaling
A reader MUST scale the asset to fill the `width` and `height` defined in the asset reference. The aspect ratio of the original asset content is not preserved unless `width` and `height` are set by the writer to match the original proportions. Writers SHOULD set `width` and `height` to preserve the original aspect ratio unless the user has explicitly requested distortion.
### 9.5 Self-Containment
All assets MUST be embedded in the archive. A writer MUST NOT store external URI references as a substitute for embedding. An ONF file MUST render its complete intended content without network access. This is a requirement of the data sovereignty principle defined in Section 1.5.
## 10. Versioning and Compatibility
### 10.1 Version Number Format
ONF uses semantic versioning in the format `MAJOR.MINOR.PATCH`, optionally followed by `-draft` for pre-release versions. The version is stored as a string in the `onf_version` field of `manifest.json`.
Examples of valid version strings:
| String | Meaning |
|---|---|
| `0.1.4-draft` | Pre-release draft, not yet stable |
| `1.0.0` | First stable release |
| `1.1.0` | Stable release with additive changes |
| `2.0.0` | Second stable release with breaking changes |

The meaning of each component is defined as follows:
| Component | Meaning |
|---|---|
| `MAJOR` | A breaking change to the format. Files written against a new MAJOR version may not be correctly readable by readers implementing a previous MAJOR version. |
| `MINOR` | An additive change. New features are added but nothing is removed or modified. Files written against a new MINOR version MUST be readable by any conforming reader implementing the same MAJOR version. |
| `PATCH` | A clarification or correction to the specification text only. No change to the format structure or data model. All readers implementing the same MAJOR and MINOR version are fully compatible. |
| `-draft` | A pre-release version. No stability guarantees apply. Readers MAY refuse to open files with a `-draft` version string. Writers MUST NOT use a `-draft` version string in production software. |

When applying version comparison rules in Sections 10.2 through 10.4, readers MUST extract the MAJOR, MINOR, and PATCH components as integers from the version string. The `-draft` suffix MUST be ignored for the purposes of version comparison.
### 10.2 Minimum Supported Version
All conforming readers MUST support all valid ONF files with MAJOR version `1`. A reader MUST NOT refuse to open a file solely because its MINOR or PATCH version is higher than the version the reader was written against.
Files with MAJOR version `0` are pre-release drafts. Conforming readers MAY support MAJOR version `0` files but are not required to. Readers that do not support MAJOR version `0` MUST signal an unsupported version event to the host application when encountering such files.
When a new MAJOR version of this specification is published, implementers SHOULD update their readers to support the new version as quickly as possible to maintain interoperability across the ecosystem.
### 10.3 Handling Files With a Higher MAJOR Version
When a reader encounters a file whose MAJOR version integer is higher than the highest MAJOR version it implements, the reader MUST NOT open the file silently. The reader MUST signal a major version mismatch event to the host application. The host application MUST inform the user that the file was created with a newer version of the ONF specification and that opening it may result in incorrect rendering or data loss. The host application MUST request explicit confirmation from the user before the
reader proceeds. In contexts where no user interface is available the host application MUST NOT open the file without operator configuration explicitly permitting it.
If the user or operator confirms, the reader MUST attempt to open the file using the forward compatibility mechanisms defined in Section 10.5. If the user or operator declines, the reader MUST NOT open the file.
### 10.4 Handling Files With a Lower MAJOR Version
A reader encountering a file with a lower MAJOR version than it implements MUST open the file without warnings related to version compatibility. The reader MUST correctly interpret the older format structures it was written to support.
### 10.5 Forward Compatibility Rules
The following rules apply globally across all sections of this specification. They ensure that files written by future versions of ONF remain openable by older conforming readers.
#### 10.5.1 Unknown JSON Keys
A reader MUST silently ignore any JSON key not defined in the version of this specification it implements. A reader MUST NOT treat an unknown JSON key as an error.
#### 10.5.2 Unknown Binary Chunk Types
A reader MUST skip any chunk in `strokes.bin` whose `chunk_type` value is not defined in the version of this specification it implements. The reader MUST use the `chunk_length` field to skip the correct number of bytes and continue reading the next chunk. A reader MUST NOT treat an unknown chunk type as an error.
#### 10.5.3 Unknown Enumerated Values
When a reader encounters an unknown value for a defined enumerated field, such as an unknown tool type, an unknown layer value, or an unknown background type, it MUST apply the fallback behaviour defined for that field in the relevant section of this specification. A reader MUST NOT treat an unknown enumerated value as an error.
#### 10.5.4 Unknown Flag Bits
A reader encountering a flags field with bits set that it does not recognise MUST ignore those bits. A reader MUST NOT treat an unknown flag bit as an error.
### 10.6 Backward Compatibility Rules
Fields and chunk types defined in a MAJOR version of this specification MUST NOT be removed or have their meaning changed within that MAJOR version. Removal or redefinition of existing fields requires a new MAJOR version.
Deprecated fields will be marked as deprecated in the specification for at least one full MINOR version before being considered for removal in a future MAJOR version.
### 10.7 Version Identification in strokes.bin
The `version` field in the `strokes.bin` file header defined in Section 7.2 is an independent binary format revision counter, stored as a `u16`. It is not related to the semantic version string in `manifest.json`.
In all ONF versions up to and including v1.0.0 this field MUST be `1`. If the binary stroke format changes in a future major version of this specification this field will be incremented independently of the `onf_version` string.
A reader encountering a `strokes.bin` version field higher than it implements MUST apply the same forward compatibility rules defined in Section 10.5, specifically the chunk skipping mechanism in Section 10.5.2.
## 11. Conformance
### 11.1 Overview
This section defines the conformance levels for ONF. A conformance level is a checklist of requirements. An application meets a conformance level if and only if it satisfies every requirement on that level's checklist.
Conformance levels are version-specific. Each major version of this specification defines its own conformance requirements. An application conforming to ONF v1 does not automatically conform to ONF v2. When a new major version is published, implementers MUST evaluate their applications against the new conformance requirements independently.
Applications SHOULD clearly state which version of the ONF specification and which conformance level they implement.
### 11.2 Conformance Levels
ONF v1 defines four conformance levels:
| Level | Description |
|---|---|
| Minimal Reader | Can open and display any valid ONF v1 file at a basic level. |
| Full Reader | Correctly renders all features defined in ONF v1. |
| Conforming Writer | Produces valid ONF v1 files that any conforming reader can open. |
| Conforming Application | Meets both Full Reader and Conforming Writer requirements. |
### 11.3 Minimal Reader
A Minimal Reader MUST satisfy all of the following requirements.
#### Container
- MUST open a valid ZIP archive with `.onf` extension.
- MUST parse `manifest.json` and read the `pages` array.
- MUST sort pages by their `order` field before displaying.
- MUST validate all file paths and reject archives containing path traversal attempts as defined in Section 4.6.
- MUST enforce the size limits defined in Section 4.7.
#### Page
- MUST read `width` and `height` from `meta.json`.
- MUST render the page surface in the `background.color` value.
- MUST NOT treat an unsupported background type as an error. MUST fill the page with `background.color` and continue.
#### Strokes
- MUST parse the `strokes.bin` file header and verify magic bytes.
- MUST reject a `strokes.bin` file with incorrect magic bytes.
- MUST read STROKE chunks using the chunk framing defined in Section 7.3.
- MUST decode sample fields using plain or zigzag varint encoding per 
  the encoding column in Section 7.5.1.
- MUST render at minimum the X position, Y position, pressure, and color 
  of each sample.
- MUST skip unknown chunk types using `chunk_length`. MUST NOT treat 
  unknown chunk types as errors.
- MUST fall back to ballpoint pen rendering for unknown tool types as 
  defined in Section 7.4.2.
- MUST either render line eraser strokes as masks or skip them entirely. 
  MUST NOT render line eraser strokes as ink.
#### Forward Compatibility
- MUST silently ignore unknown JSON keys.
- MUST skip unknown binary chunks using `chunk_length`.
- MUST ignore unknown flag bits.
- MUST apply the defined fallback behavior for unknown enumerated values.
- MUST signal a major version mismatch event to the host application and 
  MUST NOT open the file without host application confirmation when the 
  file MAJOR version is higher than the reader implements.
### 11.4 Full Reader
A Full Reader MUST satisfy all Minimal Reader requirements and additionally:
#### Backgrounds
- MUST correctly render all four background types — `blank`, `ruled`, `grid`, and `dotted` — exactly as defined in Section 6.3.2.
#### Strokes
- MUST render all five tool types according to the rules defined in Section 7.7.
- MUST decode and use tilt and azimuth sample fields when the `HAS_TILT` and `HAS_AZIMUTH` flags are set.
- MUST apply the multiply blend mode for the highlighter tool as defined in Section 7.7.5.
- MUST correctly mask underlying content for line eraser strokes as defined in Section 7.4.4.
#### Text Objects
- MUST render all text objects from the `text_objects` array in 
  `meta.json`.
- MUST attempt to load the font named in `font_family`.
- MUST fall back to the platform default font for the `generic_family` 
  value when `font_family` is unavailable.
- MUST signal a font substitution event to the host application when a 
  font substitution occurs. MUST NOT substitute silently.
- MUST render multiline text with correct line breaks at `\n` characters.
- MUST clip text to the bounding box defined by `width` and `height`.
#### Assets
- MUST render embedded WebP, PNG, and JPEG assets.
- MUST resolve asset files by SHA-256 hash from the `assets/` directory as defined in Section 9.2.1.
- MUST scale assets to the rendered `width` and `height` defined in the asset reference.
- MUST render a placeholder and signal a missing asset event to the host application when a referenced asset file is missing from the archive.
#### Layers
- MUST render back, middle, and front layers in the correct order as defined in Section 3.4.
- MUST merge strokes, text objects, and assets within the middle layer by `created_at` before rendering.
- MUST apply the tiebreaker order when `created_at` values are equal: stroke first, then text object, then asset.
#### Compatibility
- MUST support all valid ONF files with MAJOR version `1`.
- MUST open files with a higher MINOR or PATCH version without version-related warnings.
### 11.5 Conforming Writer
A Conforming Writer MUST satisfy all of the following requirements.
#### Container
- MUST produce a valid ZIP archive with the `.onf` file extension.
- MUST write `manifest.json`, a `pages/` directory, and an `assets/` directory.
- MUST name page directories `p` followed by a zero-padded five-digit index starting at `00001`.
- MUST apply DEFLATE compression to `manifest.json` and all `meta.json` files.
- MUST store all `strokes.bin` files without compression.
- SHOULD write a `thumbnail.png` if the application supports thumbnail generation.
#### Manifest
- MUST write all required fields in `manifest.json` as defined in Section 6.1.2.
- MUST set `onf_version` to the version of this specification it targets.
- MUST assign unique positive integer `order` values to all pages.
- MUST write `created_at` and `modified_at` as valid ISO 8601 UTC datetime strings.
#### Pages
- MUST write all required fields in `meta.json` as defined in Section 6.2.2.
- MUST write `width` and `height` in `coord` units.
- MUST write all required background fields explicitly. MUST NOT omit required background fields.
- MUST NOT change the orientation of a page after it has been created.
- MUST update only `width` and `height` when resizing a page. MUST NOT recalculate content coordinates.
#### Strokes
- MUST write a valid `strokes.bin` file with correct magic bytes and 
  version field set to `1`.
- MUST write all strokes in ascending `created_at` order.
- MUST set `chunk_length` accurately for every chunk.
- MUST encode all sample fields using plain or zigzag varint encoding 
  per the encoding column in Section 7.5.1.
- MUST set the `NO_PRESSURE` flag and store `255` for all pressure 
  values on devices without pressure support.
- MUST remove deleted strokes completely from the file. MUST NOT retain 
  deleted strokes in any form.
#### Text Objects and Assets
- MUST write all required fields for text objects as defined in Section 8.3.
- MUST write all required fields for asset references as defined in Section 9.3.2.
- MUST name asset files by the SHA-256 hash of their uncompressed content.
- MUST embed all assets within the archive. MUST NOT write external URI references.
- MUST write `created_at` on all text objects and asset references.
#### Compatibility
- MUST set all undefined flag bits to `0`.
- MUST NOT use chunk type values in the vendor extension range 
  `0x8000`–`0xFFFF`. These ranges are reserved for future use and MUST 
  NOT be used by conforming writers in v1.
### 11.6 Conforming Application
A Conforming Application MUST satisfy all Full Reader requirements defined in Section 11.4 and all Conforming Writer requirements defined in Section 11.5.
### 11.7 Conformance and Future Versions
Conformance levels are defined per major version of this specification. This section defines conformance for ONF v1 only.
When ONF v2 is published it will define new conformance requirements independently of this section. An application conforming to ONF v1 does not automatically conform to ONF v2. Implementers MUST evaluate their applications against the conformance requirements of each major version independently.
When a new major version is published, implementers SHOULD update their applications to meet the new conformance requirements as quickly as possible to maintain interoperability across the ONF ecosystem.
## 12. Security Considerations
### 12.1 Overview
This section collects all security requirements for ONF readers and writers. Many of these requirements are defined in earlier sections and referenced here for completeness. All requirements in this section are normative.
### 12.2 Archive Safety
#### 12.2.1 Path Traversal
Readers MUST validate every file path in the archive before accessing it as defined in Section 4.6. A reader MUST reject any archive containing entries whose resolved path would fall outside the archive extraction directory. This includes paths containing `..`, absolute path prefixes, drive letters, and null bytes.
A reader that encounters an invalid path MUST NOT extract any files from the archive and MUST report an error to the application.
#### 12.2.2 ZIP Bomb
Readers MUST enforce the size and compression ratio limits defined in Section 4.7 before fully extracting any archive entry. A reader MUST abort extraction and report an error if any limit is exceeded.
Readers SHOULD check the uncompressed size declared in the ZIP entry header before beginning decompression. If the declared size exceeds the limits defined in Section 4.7, the reader MUST abort without decompressing.
#### 12.2.3 Unexpected Archive Content
A reader MUST silently ignore unexpected archive entries as defined in Section 4.4. No event signalling is required for unknown files.
### 12.3 Malformed Input
A reader MUST handle malformed input gracefully at all times. A malformed ONF file MUST NOT cause a reader to crash, enter an infinite loop, allocate unbounded memory, or produce undefined behaviour.
Specific malformed input cases a reader MUST handle safely:
- A `chunk_length` value that exceeds the remaining bytes in the file
- A varint that does not terminate within 5 bytes
- A `sample_count` value that exceeds the remaining bytes in the chunk
- A `manifest.json` or `meta.json` file that is not valid JSON
- A `strokes.bin` file whose magic bytes are incorrect
- An asset reference whose hash does not match any file in the `assets/` directory
- A pages array in `manifest.json` containing duplicate `order` values
- A `strokes.bin` file header where reserved bytes are non-zero. Readers 
MUST ignore non-zero reserved bytes and MUST NOT treat them as an error.
- A `pages/` directory not referenced by the manifest. Readers MUST 
silently ignore orphan page directories.

Non-zero reserved bytes in the `strokes.bin` header and orphan page directories are explicitly not malformed conditions. They are listed here for clarity. All other conditions in this list are malformed conditions that MUST cause the reader to report an error.
In all of these cases the reader MUST report an error to the application. The reader MUST NOT attempt to recover or guess the intended content.
### 12.4 Asset Safety
#### 12.4.1 No Script Execution
Readers MUST treat all embedded assets as passive data. A reader MUST NOT execute any script, code, or markup found within an embedded asset regardless of the asset format. This applies to all supported asset formats including PNG, JPEG, and WebP.
A reader MUST NOT pass embedded assets to any system component that may execute scripts or follow external references without first ensuring that component operates in a fully sandboxed mode with scripting disabled.
#### 12.4.2 Untrusted Input
A reader MUST treat all embedded assets as untrusted input regardless of the source of the ONF file. Asset decoders MUST be prepared to handle malformed asset data without crashing or producing undefined behaviour.
This applies equally to the `thumbnail.png` file. A reader MUST treat the thumbnail as untrusted input and MUST handle malformed PNG data safely.
#### 12.4.3 No External References
Readers MUST NOT follow any external URI found within an embedded asset. If an asset contains an external reference, the reader MUST ignore it entirely.
Writers MUST NOT embed assets that contain external references. Writers MUST NOT store external URI references as a substitute for embedding as defined in Section 9.5.
### 12.5 Privacy
An ONF file may contain sensitive personal data including handwritten notes, typed text, and images. Readers and writers MUST NOT transmit the contents of an ONF file to any external server or service without explicit informed consent from the user.
This requirement is a direct consequence of the data sovereignty principle defined in Section 1.5. A user's notes belong to the user. No part of the ONF ecosystem — readers, writers, or libraries — may treat note content as data to be collected, analysed, or transmitted without the user's knowledge and consent.
### 12.6 Malformed JSON
Readers MUST NOT use an unsafe JSON parser that is vulnerable to denial of service through deeply nested structures or extremely long strings. A reader encountering a `manifest.json` or `meta.json` file that exceeds reasonable structural limits MUST reject the file and report an error rather than attempting to parse it fully.
Reasonable structural limits are left to the implementation but SHOULD include a maximum nesting depth of 32 levels and a maximum string length of 65535 characters for any single JSON string value.
## Appendix
### Appendix A — Varint Encoding Algorithm
ONF uses unsigned LEB128 variable-length integer encoding for all sample fields in the stroke sample block. Fields marked "Zigzag varint" in Section 7.5.1 are first converted using zigzag encoding before LEB128 encoding is applied. Fields marked "Plain varint" are encoded directly as unsigned LEB128 without zigzag mapping. The first sample of every stroke always uses plain varint for all fields.
#### A.1 Zigzag Encoding
Zigzag encoding converts a signed integer to an unsigned integer so that small negative numbers produce small unsigned values.
**Encoding — signed to unsigned:**
```
zigzag_encode(n) = (n << 1) ^ (n >> 31)
```
**Decoding — unsigned to signed:**
```
zigzag_decode(n) = (n >> 1) ^ -(n & 1)
```
**Mapping table:**
| Signed value | Unsigned zigzag value |
|---|---|
| 0 | 0 |
| -1 | 1 |
| 1 | 2 |
| -2 | 3 |
| 2 | 4 |
| -3 | 5 |
| 3 | 6 |
#### A.2 LEB128 Encoding
After zigzag encoding (where applicable), the resulting unsigned integer is encoded as an unsigned LEB128 varint.
**Encoding algorithm:**
```
encode_varint(value, buffer):
    while value > 0x7F:
        buffer.append((value & 0x7F) | 0x80)
        value >>= 7
    buffer.append(value & 0x7F)
```
**Decoding algorithm:**
```
decode_varint(buffer, offset):
    result = 0
    shift = 0
    while true:
        byte = buffer[offset]
        offset += 1
        result |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            break
        shift += 7
    return result, offset
```
#### A.3 Worked Examples
**Example 1 — small positive delta (zigzag varint)**
Delta value: `dx = 4`
Step 1 — Zigzag encode:
```
zigzag_encode(4) = (4 << 1) ^ (4 >> 31) = 8 ^ 0 = 8
```
Step 2 — LEB128 encode:
```
8 fits in 7 bits → single byte
encoded: 0x08
```
Total: **1 byte**

---

**Example 2 — small negative delta (zigzag varint)**
Delta value: `dx = -3`
Step 1 — Zigzag encode:
```
zigzag_encode(-3) = (-3 << 1) ^ (-3 >> 31) = -6 ^ -1 = 5
```
Step 2 — LEB128 encode:
```
5 fits in 7 bits → single byte
encoded: 0x05
```
Total: **1 byte**

---

**Example 3 — first sample absolute X value (plain varint)**
First sample X position: `x = 10000`
The first sample uses plain varint — no zigzag encoding is applied.
LEB128 encode `10000`:
```
Byte 0: 10000 & 0x7F = 16  → with continuation: 0x90
        10000 >> 7   = 78

Byte 1: 78 & 0x7F    = 78  → no continuation:   0x4E
        78 >> 7      = 0   → done

encoded: 0x90 0x4E
```
Verify decode:
```
byte 0: 0x90 → continuation set,  value bits = 16
byte 1: 0x4E → continuation clear, value bits = 78

result = 16 | (78 << 7) = 16 | 9984 = 10000 ✓
```
Total: **2 bytes** — compared to 4 bytes as a raw `i32`.

---

**Example 4 — first sample absolute X value, larger number (plain varint)**
First sample X position: `x = 20000`
LEB128 encode `20000`:
```
Byte 0: 20000 & 0x7F = 20000 & 127 = 32  → with continuation: 0xA0
20000 >> 7 = 156

Byte 1: 156 & 0x7F = 28               → with continuation: 0x9C
156 >> 7 = 1

Byte 2: 1 & 0x7F = 1                  → no continuation: 0x01
1 >> 7 = 0 → done

encoded: 0xA0 0x9C 0x01
```
Verify decode:
```
byte 0: 0xA0 = 1|0100000  continuation set, value bits = 0100000 = 32
byte 1: 0x9C = 1|0011100  continuation set, value bits = 0011100 = 28
byte 2: 0x01 = 0|0000001  continuation clear, value bits = 0000001 = 1

result = 32 | (28 << 7) | (1 << 14)
       = 32 | 3584 | 16384
       = 20000 ✓
```
Total: **3 bytes** — compared to 4 bytes as a raw `i32`.

---

**Example 5 — canonical zigzag varint test vectors**
These vectors MUST be used by implementers to verify correctness of their encoder and decoder:
| Signed delta | Zigzag value | LEB128 bytes |
|---|---|---|
| 0 | 0 | `0x00` |
| -1 | 1 | `0x01` |
| 1 | 2 | `0x02` |
| -2 | 3 | `0x03` |
| 2 | 4 | `0x04` |
| -64 | 127 | `0x7F` |
| 64 | 128 | `0x80 0x01` |
| -128 | 255 | `0xFF 0x01` |
| 128 | 256 | `0x80 0x02` |

**Plain varint test vectors** (for `timestamp` deltas and first-sample values):

| Unsigned value | LEB128 bytes |
|---|---|
| 0 | `0x00` |
| 1 | `0x01` |
| 127 | `0x7F` |
| 128 | `0x80 0x01` |
| 255 | `0xFF 0x01` |
| 10000 | `0x90 0x4E` |
| 20000 | `0xA0 0x9C 0x01` |

---
### Appendix B: DIN Paper Size Reference
All dimensions are given in `coord` units (1/100 mm). Portrait orientation is assumed. For landscape, swap width and height.
| Size | Width (mm) | Height (mm) | Width (coord) | Height (coord) |
|---|---|---|---|---|
| DIN A3 | 297 | 420 | 29700 | 42000 |
| DIN A4 | 210 | 297 | 21000 | 29700 |
| DIN A5 | 148 | 210 | 14800 | 21000 |
| DIN A6 | 105 | 148 | 10500 | 14800 |
| DIN A7 | 74 | 105 | 7400 | 10500 |
| Letter (US) | 215.9 | 279.4 | 21590 | 27940 |
| Legal (US) | 215.9 | 355.6 | 21590 | 35560 |

Writers using custom dimensions MUST still define `width` and `height` explicitly in `meta.json`. The values in this table are provided for reference only and carry no special meaning within the format.
