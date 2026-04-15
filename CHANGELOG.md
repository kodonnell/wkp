# Changelog

All notable changes to this project are documented here.

Release tags:
- `pypi-vX.Y.Z` — Python package (`wkp` on PyPI)
- `npm-vX.Y.Z` — JavaScript packages (`@wkpjs/node`, `@wkpjs/web` on npm)
- `wkp-vX.Y.Z` — product-level release (no package publish)

---

## [0.5.0] — current

### Added
- **`GeometryFrame` public API** across all bindings — low-level flat access to decoded geometry coordinates without going through a geometry object. Useful for bulk processing, WebGL, numpy pipelines, and worker transfer.
  - Python: `decode_frame()`, `encode_frame()`, `GeometryFrame` dataclass with `coords` as a `(N, dims)` float64 numpy array.
  - Node/Web: `decodeFrame()`, `encodeFrame()`, `GeometryFrame` class with `coords` as `Float64Array`.
- **`GeometryFrame.to_buffer()` / `from_buffer()`** — compact binary serialization in a shared cross-language format (40-byte little-endian header + typed arrays). JavaScript `ArrayBuffer` is transferable between workers with zero copy.
- **Python type stubs** (`__init__.pyi`) — full type coverage for IDE completion and mypy across all public symbols.

### Changed
- **Context is now optional in all bindings** — `ctx` moved to a keyword-only trailing argument with a sensible default:
  - Python: thread-local `Context` created lazily per thread (matches C++ binding behaviour).
  - Node/Web: module-level default context (safe because JS is single-threaded per realm).
  - Old positional `encode(ctx, geom, precision)` form still accepted in Python with a `DeprecationWarning`.
- **musllinux wheels now built** — Alpine / musl-based Linux environments get pre-built wheels instead of falling back to source compilation.

### Internal
- Integration fixture manifest (`data/integration_tests/MANIFEST.json`) checked in CI on every push — prevents fixture files from drifting out of sync with declared counts.
- Fast Python test job added to CI — runs `pytest` directly (no wheel build) for quick PR feedback alongside the slower `cibuildwheel` matrix.

---

## [0.4.x] and earlier

Earlier versions did not maintain a changelog. See the GitHub release notes for each tag.
