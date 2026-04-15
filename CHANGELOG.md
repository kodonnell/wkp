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
- **`decode_floats_array` / `decodeFloatsArray`** — new function in all bindings that returns decoded float values as a flat typed array with no intermediate allocation:
  - Python: returns a 1D `(N×dims,)` float64 numpy array directly from the C buffer. Use `.tobytes()` for zero-copy wire transfer; use `.reshape(-1, dims)` for a 2D view.
  - Node/Web: returns a flat `Float64Array` directly from the native layer.
- **Python type stubs** (`__init__.pyi`) — full type coverage for IDE completion and mypy across all public symbols.
- **`scripts/test_all.py`** — single command to run all language test suites (Python pytest, Node, Web/WASM). Supports `--skip-*` flags and `--integration` for the slow fixture CSV tests.
- **Flat-array micro-benchmarks** — `bindings/python/benchmark/bench_flat_api.py` and `bindings/javascript/packages/node/benchmark/bench_flat_api.mjs` measure all encode/decode paths and serve as regression baselines.

### Changed
- **Context is now optional in all bindings** — `ctx` moved to a keyword-only trailing argument with a sensible default:
  - Python: thread-local `Context` created lazily per thread (matches C++ binding behaviour).
  - Node/Web: module-level default context (safe because JS is single-threaded per realm).
  - Old positional `encode(ctx, geom, precision)` form still accepted in Python with a `DeprecationWarning`.
- **`decode_floats` Python return type changed** — now returns a `(N, dims)` float64 numpy array instead of `list[tuple[float, ...]]`. Call `.tolist()` explicitly if Python lists are needed.
- **`decodeFrame` Node.js performance** — previously decoded to a GeoJSON object then re-flattened in JavaScript (~3.2 ms for 10 k points). Now calls a new native `DecodeGeometryFrameFlat` function that copies the flat frame directly into typed arrays, skipping GeoJSON construction entirely (~0.13 ms, ~24× faster).
- **musllinux wheels now built** — Alpine / musl-based Linux environments get pre-built wheels instead of falling back to source compilation.

### Internal
- `DecodeGeometryFrameFlat` added to Node native addon (`addon.cc`) — returns `{ coords: Float64Array, segmentPointCounts: Uint32Array, groupSegmentCounts: Uint32Array }` without building a GeoJSON intermediate.
- `decode_f64` nanobind binding changed to return a 1D numpy array (`OutputArray1D`) instead of 2D, removing an implicit reshape from the C extension layer.
- Integration fixture manifest (`data/integration_tests/MANIFEST.json`) checked in CI on every push — prevents fixture files from drifting out of sync with declared counts.
- Fast Python test job added to CI — runs `pytest` directly (no wheel build) for quick PR feedback alongside the slower `cibuildwheel` matrix.

---

## [0.4.x] and earlier

Earlier versions did not maintain a changelog. See the GitHub release notes for each tag.
