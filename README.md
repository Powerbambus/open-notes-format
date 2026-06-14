# OpenNotes Format (ONF)
**An open, vendor-neutral file format for handwritten digital notes.**

---

## The Problem

Every major note-taking application today uses a proprietary file format. Your notes are locked to the app you use to write them. If the app is discontinued, the company disappears, or you simply want to switch — your data is at risk. There is no open standard for handwritten digital notes the way ODF exists for word processing or PDF exists for document exchange.

ONF exists to fix that.

---

## What ONF Is

ONF is an open specification for a file format that stores handwritten notes — including pen strokes, pressure and tilt data, typed text, and embedded images — in a way that any application can implement, read, and write.

The format is designed to be:

- **Self-contained.** An ONF file holds everything needed to render it. No cloud account, no vendor dependency, no internet connection required.
- **Application-neutral.** Any developer can write a conforming reader or writer from the public specification alone.
- **Built for real-time input.** Designed from the ground up for stylus-based note-taking at full quality, including pressure, tilt, and timing data.
- **Forward-compatible.** Files written today will open correctly in applications built years from now.
- **Physically grounded.** Coordinates map to real-world units. A note written on A4 is described in A4 dimensions, independent of screen or device.

---

## What ONF Is Not

- A note-taking application. ONF is a format specification, not software.
- A cloud service or sync protocol.
- A replacement for PDF (which is a presentation format). ONF stores editable, living notes.
- A finished standard. This project is in early development.

---

## Project Scope

The v1.0 specification will cover:

- Pen stroke storage (position, pressure, tilt, timestamps)
- Typed text objects
- Embedded image assets
- Multi-page documents
- A physical, DIN-standard-aligned coordinate system
- A binary stroke encoding optimised for performance and file size
- A forward-compatibility mechanism so older readers handle newer files gracefully

Out of scope for v1.0 (may be addressed in future versions):

- Real-time collaboration
- End-to-end encryption
---

## Status

> **This project is in early pre-draft stage.**
> The specification is incomplete and subject to breaking changes.
> Do not build production software against any document in this repository yet.

This repository is the **single source of truth** for the ONF specification. All normative decisions about the format are made here.

---

## Contributing

The format is not yet ready for formal contributions, but feedback on the goals and scope is welcome via [Issues](../../issues).

---

## License

The ONF specification and all documents in this repository are published under the **GNU Affero General Public License v3.0**.
See [`LICENSE`](./LICENSE) for the full license text.
