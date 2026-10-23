# Changelog

All changes to the ONF specification (`SPECIFICATION.md`) are documented here, including patch releases. The newest version is first.

ONF is in a pre-release phase (MAJOR version `0`). Any `0.x.y` release may break the format, so every entry states whether it changes the binary layout, whether files of the previous version stay readable, and which value `strokes.bin` carries in its `version` field (Section 7.2).

Versions follow Section 10.1. From 1.0.0 on, MINOR means additive changes and PATCH means clarifications only. Until then every release may break the format. Since 0.5.0 the working convention is the one of 1.0.0: a new field or feature raises MINOR, a text-only clarification raises PATCH. Earlier releases (0.3.1) used PATCH for additions as well.

Entries use these headings: **Added**, **Changed**, **Removed**, **Clarified** (text only, no change to what a reader or writer does).

## 0.5.0

Compatibility: no change to any binary layout. `strokes.bin` version stays `2`. A 0.4.0 reader opens 0.5.0 files and ignores the new field (Section 10.5), so it draws the lines of a `ruled` or `grid` background with a width of 1 pixel.

### Added
- `line_width` in the background (Section 6.3.1): optional, only for `ruled` and `grid`. The width of the lines in `coord` units, from `1` to `2147483647`. If it is absent, the lines are 1 physical display pixel wide, as before. `dotted`, `blank` and unknown background types ignore the field (Section 6.3.2).

## 0.4.0

Compatibility: breaking for the line eraser, otherwise additive. `strokes.bin` version stays `2`; no chunk layout changes. A 0.4.0 reader skips tool type `4` (see below), so in a 0.3.x file the ink that a line eraser had erased becomes visible again. A 0.3.x reader opens 0.4.0 files and ignores the new flag bits.

### Removed
- Tool type `4`, the line eraser, and with it the masking rules of the old Section 7.4.4 and 7.7.6. Writers never write tool type `4`. A reader parses and checks a stroke or shape with tool type `4` like any other and then skips it (Section 7.4.2). A `SHAPE` chunk with tool type `4` is no longer malformed.

### Added
- Erasing by removing samples (Section 7.6.1): the writer deletes the sample points that lie in the erased area. A stroke is erased whole or split into parts. The parts get new `stroke_id` values. A writer SHOULD insert interpolated samples at the borders of the erased area. A shape is deleted whole or replaced by strokes. An application erases only in the layer the user works on; this is application behavior, not part of the file.
- Stroke flags `NO_TAPER_START` (bit 5) and `NO_TAPER_END` (bit 6) (Section 7.4.3). They switch off the fountain pen taper at one end (Section 7.7.3.1). A writer sets them at an end that was created by cutting a stroke.
- `top_margin` in the background (Section 6.3.1): optional, for `ruled`, `grid` and `dotted`. The height in `coord` units of the area at the top that is free of pattern elements (Section 6.3.2).
- Test vector V7 (Appendix C.1): a fountain pen stroke with `NO_PRESSURE`, `NO_TIMESTAMP`, `NO_TAPER_START` and `NO_TAPER_END` (flags `0x006C`).

### Changed
- Single object rule (Section 7.7): a stroke of a tool with fixed opacity (ballpoint pen, fountain pen, highlighter) is rendered as one object. Overlapping parts of the same stroke are composited once. Separate highlighter strokes that overlap still get darker.
- Smoothing of strokes by a reader is allowed (MAY) but not defined (Section 7.7).
- The `strokes.bin` version note (Section 7.2 and 10.7) lists 0.4.0.

## 0.3.1

Compatibility: no change to any binary layout. `strokes.bin` version stays `2`.

### Added
- `crop` in asset references of images (Sections 9.3.2 and 9.4.5): the shown region in pixels of the decoded image. The region has to lie inside the image. A `crop` that violates a range or type rule is a page-level error; a region outside the image makes the asset corrupt. For `pdf` the field is not written and a reader ignores it.
- A JSON file of an ONF archive MUST NOT begin with a byte order mark. A reader treats such a file as malformed (Section 2.3.5, 12.3).
- A page directory without `meta.json` or `strokes.bin` is a page-level error (Section 12.3.1).

### Changed
- PDF annotations (Section 9.4.4): the rule is now one condition: an annotation is rendered if and only if it has a normal appearance stream and neither the Hidden nor the NoView flag is set. Link and Popup annotations are never rendered. Form fields (Widget) are rendered as static annotations. A Full Reader MUST render them (Section 11.4).
- Highlighter (Section 7.7.5): a writer without a preference SHOULD use the alpha value `0x66` (40%). The opacity is still the alpha value of `color`.
- Manifest `created_at` and `modified_at` (Section 6.1.2): an application that converts a document from another format SHOULD use the times of the source file if they are known. The wording of the `created_at` field of objects is separated from the manifest field.
- The `version` note of `strokes.bin` lists 0.3.1 (Section 10.7).

## 0.3.0

Compatibility: breaking. `strokes.bin` version `2` (before: `1`). The `STROKE` header and the `SHAPE` chunk are new, layers replace the three named layers, assets were reworked. Files of 0.2.0 are not readable.

### Added
- Layers (Section 5.6, 3.4): every object carries an integer `layer` from `1` to `65535`. `0` is reserved. Layers replace the three named layers `back`, `middle`, `front`. Order within a layer: assets, text objects, ink, each in source order.
- Shapes (Section 7.9): the chunk type `SHAPE` (`0x0003`) with ellipse, rectangle, line and triangle, stored by their parameters. Rotation in hundredths of a degree for ellipse and rectangle.
- `NO_TIMESTAMP` (bit 3) and `HAS_CREATED_AT` (bit 4) stroke flags (Section 7.4.3). `created_at` is optional everywhere and does not influence the rendering order.
- Text spans (Section 8.3.1): character styles for ranges of a text object (`bold`, `italic`, `underline`, `strikethrough`, `color`, `font_size`, `font_family`).
- PDF assets (Section 9): PDF documents as assets, `pdf_page` in asset references, PDF rendering rules (page box, scaling, annotations, no active content).
- `generator` in the manifest (Section 6.1.2): optional name and version of the writing application.

### Changed
- Stroke header (Section 7.4.1): now 20 bytes with `layer`; `created_at` is an optional 8-byte field after the header, present if `HAS_CREATED_AT` is set. Shape header: 16 bytes.
- Painter's order (Section 3.4, 7.6): defined by layer, content type and position in the source. Creation time plays no role. Ink is written in painter's order within each layer.
- Highlighter (Section 7.7.5): the opacity is the alpha value of `color` (before: fixed at 40%).
- Line eraser (old Section 7.4.4): masks ink in the same layer that precedes it in the file (before: the middle layer). Removed again in 0.4.0.
- Line height of text (Section 8.6.1): 120% of the largest font size of the line.
- `text_objects` and `assets` are drawn in array order; the `created_at` sorting of text objects is gone.

## 0.2.0

Compatibility: breaking. `strokes.bin` stays at version `1`, but the encoding of the first sample changed (see below), so files of 0.1.4 are not readable.

### Changed
- First sample of a stroke (Section 7.5): encoded like every other sample, as a zigzag delta relative to `0` (before: absolute plain varints). The zigzag encoding uses 64-bit arithmetic (`n >> 63`).
- Varint (Section 2.3.2): up to 33 bits (5 bytes). Non-canonical encodings and values outside the range of the field are malformed. Writers emit only canonical encodings. Deltas and accumulated values use at least 64-bit signed arithmetic and every accumulated value has to stay within the range of its field.
- Chunk types `0x8000` to `0xFFFF` and tool types `0x8000` to `0xFFFF` are no longer vendor extensions but reserved (Sections 7.3.1, 7.4.2). Unknown chunks are skipped.
- A reader that finds a `strokes.bin` `version` higher than it implements does not parse the file and treats the page as unreadable (Section 7.2).
- Pages (Section 6.1.2): `id` values are exactly `p00001` to `pNNNNN` and `order` values exactly `1` to `N`; at most 99999 pages.
- Page orientation: the ban on changing it became non-normative application guidance (Section 6.4).
- Pencil: any texture is implementation-defined (Section 7.4.2).

### Added
- Chunk parsing rules (Section 7.3.2): what a reader does for every short, long or repeated chunk.
- Taper calculation (Section 7.7.3.1): `K = floor(0.05 * N)`, no taper below 20 samples.
- A reader MAY ignore the pressure values (Section 7.7), and the single-sample rule is defined independent of the tool (Section 7.8).
- ZIP rules (Section 4.1, 4.2, 4.6.1, 4.7): ZIP64 conditions, no encryption, no multi-disk archives, no duplicate names, no symbolic links, an explicit `assets/` directory entry, identifier rules for `pages[].id`, `hash` and `extension`, size limits enforced against the declared and the actually produced bytes.
- JSON rules (Section 2.3.5): integers within the range of an IEEE 754 double (`-9007199254740991` to `9007199254740991`), no unpaired surrogates, case rules for hex digits (hashes lowercase).
- Color (Section 5.2): sRGB, straight alpha. Time (Section 5.5): the two time representations and the JSON range.
- `u64` and `i64` primitive types (Section 2.3).
- Font fallback event when a font family is replaced as a whole (Section 8.4), and the wrapping and clipping rules of the text layout (Section 8.6).
- Background rendering rules are clearer: pixel rounding is implementation-defined, opacities multiply with the alpha of `foreground_color`, an undefined background type needs only `color` (Section 6.3.2, 6.3.3).

## 0.1.4

First published draft of the specification. Tool types and chunk types in the range `0x8000` to `0xFFFF` were vendor extensions, painter's order used `created_at`, content was split into the three layers `back`, `middle` and `front`, and the first sample of a stroke was stored as an absolute value.
