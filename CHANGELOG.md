# Changelog

All notable changes to this project are documented here.

Release tags:
- `pypi-vX.Y.Z` — Python package (`wkp` on PyPI)
- `npm-vX.Y.Z` — JavaScript packages (`@wkpjs/node`, `@wkpjs/web` on npm)
- `nuget-vX.Y.Z` — .NET package (`Wkp` on NuGet)
- `crates-vX.Y.Z` — Rust crate (`wkp` on crates.io)
- `wkp-vX.Y.Z` — product-level release (no package publish)

---

## [0.7.0] — unreleased

An API cleanup ahead of 1.0, so that every language works the same way. The encoded format does
not change: anything 0.6 encoded decodes the same in 0.7.

### All languages
- **Encoders return strings everywhere** (WKP is ASCII text, usually stored in JSON or text
  columns): Python `encode` / `encode_frame` / `encode_floats` return `str` (were `bytes`),
  `@wkpjs/node` returns `string` (was `Buffer`), and `@wkpjs/web`'s `encodeFloats` returns
  `string` (was `Uint8Array`; its `encode` already returned a string). .NET and C++ already
  returned strings. Decoders still accept bytes too. In Node this also makes encoding faster
  (3D lines: 25% at 100 points, 4% at 1000, as creating a `Buffer` from native code has a fixed
  cost); in Python the difference is within noise (0-3%).
  - Python: drop `.decode("ascii")` on results; use `.encode("ascii")` where you need `bytes`.
  - Node: `.toString()` on the result still works; Buffer-only methods do not. Use
    `Buffer.from(s, 'latin1')` where you need bytes.
- **The geometry type enum is `GeometryType` everywhere** (Python and JavaScript called it
  `EncodedGeometryType`; that name still works in 0.7 as a deprecated alias, and warns in
  Python).
- **Headers have named fields everywhere.** Python's `decode_header` returns a `Header`
  NamedTuple (so tuple unpacking still works) whose `geometry_type` is a `GeometryType`;
  `decode_frame` also sets `GeometryFrame.geometry_type` to a `GeometryType` (an `IntEnum`, so it
  still compares equal to ints). JavaScript's `decodeHeader` returns
  `{ version, precision, dimensions, geometryType }` instead of an array: replace
  `const [v, p, d, t] = decodeHeader(s)` with `const { version, precision, dimensions,
  geometryType } = decodeHeader(s)`. .NET's `Header` is no longer a positional record (it keeps
  its constructor, properties and value equality, but has no `Deconstruct`).
- **Errors: `WkpError` with a `code`** (`INVALID_ARGUMENT` or `MALFORMED_INPUT`, the C ABI's
  status names) in Python and JavaScript.
  - Python: `wkp.WkpError` subclasses `ValueError`, so `except ValueError` still works. The
    package's own checks (array shapes, frame structure, M values, ...) raise it too; wrong
    argument types still raise `TypeError`.
  - JavaScript: `WkpError` (exported by both packages; one class across `createWkp()`
    instances in `@wkpjs/web`). Core errors were `TypeError` (invalid arguments) or `Error` in
    `@wkpjs/node` and plain `Error`s in `@wkpjs/web`; argument-type checks still throw
    `TypeError`.
- Encoding enforces the structure `SPEC.md` requires of each geometry type: one point for a
  Point, at least two points per linestring, one group for a Polygon, one point per MultiPoint
  part, one linestring per MultiLineString part. Anything else is an invalid argument; before, it
  was encoded (the C core did the same). Decoding still accepts such data.
- Decoding a geometry with an unknown geometry type (outside 1-6) is malformed input; before,
  the type code was passed through. Encoding one is an invalid argument (codes up to 63 were
  accepted).

- Frames are checked before encoding: a frame whose `version` is not 1 is an invalid argument
  everywhere (.NET already did this), and in Python so is one whose `dimensions` does not match
  its coordinates' columns (it was silently ignored). `GeometryFrame.from_buffer` / `fromBuffer`
  reject a buffer whose length does not match its header with `MALFORMED_INPUT` (they raised
  `struct.error` / `RangeError`, or read garbage).

### Python
- The implementation moved to `wkp._api`; `import wkp` exposes only the API (`dir(wkp)` no
  longer lists `np`, `shapely`, `re`, ...). Classes still report themselves as `wkp.X`, and
  pickle under those names.

### JavaScript — breaking
- Both packages have an `exports` map: import the package itself (and `package.json`; for
  `@wkpjs/web` also `dist/wkp_core.wasm`). Deep imports such as `@wkpjs/web/src/version.js` no
  longer resolve.
- `@wkpjs/web`'s `Context` only exposes `dispose()` (and `Symbol.dispose` where supported); its
  undocumented `encodeF64`, `decodeF64`, `encodeGeometry`, `encodeGeometryFromFrame`,
  `decodeGeometry`, `initialCapacity` and `_`-prefixed members are gone. Use the top-level
  functions with a `ctx` argument.
- `@wkpjs/node`'s `Context` has a `dispose()` that does nothing, so code written for
  `@wkpjs/web` runs unchanged.
- `@wkpjs/web` ships TypeScript types (`src/index.d.ts`) and no longer ships its build
  `scripts/`. Both packages' types are type-checked in CI.

### Rust (`wkp` crate) — breaking
- `encode_geometry` takes a `GeometryType` instead of a `u8`, and its counts in the same order as
  `Frame`: segment point counts, then group segment counts (they were the other way round).
- `Header::geometry_type` is a `GeometryType` (was `u8`). `GeometryType` has `code()`,
  `TryFrom<u8>` and `From<GeometryType> for u8`.
- `Error`, `GeometryType`, `Header` and `Frame` are `#[non_exhaustive]`: match `Error` and
  `GeometryType` with a wildcard arm, and build a `Frame` from `Frame::default()`.
- `set_simd_enabled` is only available with the new `test-hooks` feature (for tests and
  benchmarks; not covered by semver).
- `Header::default()` has version 1 (was 0).
- Added `encode_frame` (the inverse of `decode_geometry`), `VERSION`, `MAX_PRECISION`, and
  `std::error::Error` for `Error`. Every public item is documented.

### C ABI (`core.h`) — breaking
- `wkp_context` is opaque: create it with `wkp_context_new()` (was a caller-allocated struct
  passed to `wkp_context_init`) and free it with `wkp_context_free`. Its internals can now change
  without breaking the ABI.
- `wkp_encode_geometry_frame` takes `segment_point_counts, segment_count` before
  `group_segment_counts, group_count`, matching `wkp_geometry_frame_f64`.
- `wkp_status` is an `int32_t` with `#define`d values (an enum's size is compiler-dependent).
  The never-returned `WKP_STATUS_ALLOCATION_FAILED`, `BUFFER_TOO_SMALL` and `LIMIT_EXCEEDED` are
  gone.
- Removed `wkp_basic_self_test` and the unused `wkp_size_buffer`.
- `core.h` includes `_version.h` (`WKP_CORE_VERSION`). The header documents that results stay
  valid until the next call on the context even if it fails, and that out-of-memory aborts.

### C++ (`core.hpp`) — breaking
- `throw_for_status` and `thread_ctx` moved to `wkp::core::detail`; `ensure_basic_self_test`
  and `normalize_precisions` are gone (the core validates precisions). Exceptions say what went
  wrong ("WKP: invalid argument" / "WKP: malformed input").

### .NET — breaking
- `Encode(...)` takes `segmentPointCounts` before `groupSegmentCounts`, matching
  `GeometryFrame`.
- `WkpContext` holds the native context in a `SafeHandle`, so it is freed exactly once even if
  `Dispose` races a finalizer.

---

## [0.6.1] — 2026-10-22

### Fixed
- Encoding a geometry with an empty part (a segment with no points, or a group with no
  segments) succeeded but produced output that no decoder accepts. It is now an
  invalid-argument error.
- A malformed geometry header (too short, a char outside `?`..`~`, or an unsupported version)
  was reported as an invalid argument instead of malformed input. In C# that is now
  `FormatException` instead of `ArgumentException`; in JavaScript (Node) a plain `Error` instead
  of `TypeError`.
- @wkpjs/node: `import { coreVersion, runSelfTest, EncodedGeometryType } from '@wkpjs/node'`
  failed in ES modules; only the exports before those were visible to `import`.
- Python: encoding a geometry with M values silently dropped them (and Z too, for XYZM), or
  failed with an unhelpful "invalid argument" (XYM). Shapely cannot build such geometries when
  decoding, so this is now a clear `ValueError`. Decoding a 4+ dimensional geometry to Shapely
  also explains itself and points to `decode_frame`.
- Python: `GeometryFrame == GeometryFrame` raised a numpy "truth value is ambiguous" error; frames
  now compare field by field. `GeometryFrame.to_buffer()` failed for list coordinates, and
  silently wrapped counts above 2³²-1 (the buffer stores them as uint32); it now raises
  `ValueError` for those.
- Python: `EncodedGeometryType` is in `__all__`, and the decoders' first parameter is named
  `encoded` (it was `bites`), as documented. Code passing it by keyword as `bites=` must change.
- .NET: `new GeometryFrame(...)` without a precision silently encoded at 0 decimal places;
  `precision` is now a required argument. Encoding a frame whose `Version` is not 1 throws
  `ArgumentException` instead of silently writing version 1.
- JavaScript: a fractional precision was silently truncated by `@wkpjs/web`'s `encode` and
  `encodeFrame` and by `@wkpjs/node`'s `encodeFrame` (`encode(g, 5.7)` encoded at 5). Both
  packages now throw `TypeError`, as `@wkpjs/node`'s `encode` already did.
- C++ (`wkp/core.hpp`): each thread's context was never freed, leaking its buffers (sized to the
  largest input) when the thread exited.

### Documentation
- The limits (precision 0-63, `|value| × 10^precision` below 2^58, 1-16 dimensions, finite
  values, no empty parts) are documented in the README and every binding's README.
- Corrected: `@wkpjs/web` `encodeFloats` returns a `Uint8Array`, not a string; the C++ README
  documents the `core.hpp` wrapper (it showed raw C ABI calls); the Python README's
  `GeometryFrame.from_buffer` example, its context wording, and M-value support; the .NET
  README's `Header` output and exceptions.

---

## [0.6.0]

The core is now written in Rust (`core/`, crate `wkp`) and every binding uses it. Encoded
output is unchanged: it is byte-for-byte identical to 0.5.x for all valid inputs (checked
against 600 golden vectors generated by the C core, plus the shared fixtures).

### Added
- **.NET binding** (`bindings/csharp`, `Wkp` on NuGet) over the C ABI: `WkpCodec` /
  `WkpContext` with `Encode`, `Decode`, `DecodeHeader`, `EncodeFloats`, `DecodeFloats`, and a
  `GeometryFrame` with the cross-language `ToBuffer` / `FromBuffer` format. Ships native
  libraries for linux-x64/arm64 (glibc and musl), osx-x64/arm64 and win-x64/arm64. Released with
  `nuget-vX.Y.Z` tags.
- `wkp_version()` in the C ABI.
- Differential fuzzing (`fuzz/`, cargo-fuzz with AddressSanitizer) of the decoders and encoders
  against a naive SPEC.md decoder, with the SIMD paths on and off; run in CI on every change and
  for longer weekly.
- `wkp` crate on crates.io (`crates-vX.Y.Z` tags). NuGet and crates.io publish via trusted
  publishing.
- Python: wheels for Windows arm64 and musllinux; the sdist is now published too.

### Changed
- **Faster everywhere.** Core encode is 1.7–2.2x and decode 1.5–1.9x faster than the C core.
  On x86-64, decoding path-like data (2D/3D lines and rings) is a further 1.3–1.8x faster with
  a SIMD decoder (SSSE3/SSE4.1), and encoding ~2x faster with a SIMD encoder (AVX2); both are
  detected at runtime, and other platforms are unchanged.
  Python `encode`/`decode` of a 10k-point linestring: 0.17/0.18 ms → 0.06/0.05 ms. Node
  `decode` (to GeoJSON) is ~12x faster and `decodeFloats` ~16x faster.
- **Python**: built with pyo3 + maturin as abi3 wheels, so one wheel per platform supports
  CPython 3.10 through 3.14 and later. Requires Python >= 3.10 (was 3.8; 3.9 is end-of-life). Large inputs are
  processed with the GIL released. Using one explicit `Context` from two threads at once now
  raises `RuntimeError` instead of racing. The package source moved to `bindings/python/python/wkp`.
- **@wkpjs/node**: the addon is Rust (napi-rs) and ships prebuilt binaries for eight platforms
  inside the package, including Alpine (musl) x64/arm64; installing no longer compiles anything
  (no node-gyp / Python / C++ toolchain needed), and the package has no npm dependencies. glibc
  Linux binaries need glibc 2.17+.
- **@wkpjs/web**: the WebAssembly module is built with cargo instead of Emscripten.
  `dist/wkp_core.js` is gone; `createWkp()` accepts `wasmBinary`, `wasmModule` or `locateFile`
  options. `Context` now owns buffers inside the module (call `dispose()` when done).
- **C/C++**: the C ABI (`bindings/c/include/wkp/core.h`, moved from `core/include`) is
  implemented in Rust and built as `libwkp` (was `wkp_core`); CMake builds it via Corrosion.
  Input pointers may now be NULL when their count is 0.
- Versioning: the Cargo workspace version is the single source of truth;
  `scripts/check_versions.py` checks the others.
- Node `index.d.ts` now matches the actual API (trailing optional `ctx`, `decodeFrame`,
  `GeometryFrame`, `decodeFloatsArray`, Buffer return types).
- Python decoders accept `str` as well as bytes-like input. JS decoders no longer make an
  intermediate copy of the input: Node reads strings natively and `Uint8Array`s in place, and the
  web package writes strings straight into wasm memory. Decoding 100-1000-point 3D geometries is
  ~1.1-1.3x faster in JS.
- JS `encode` of GeoJSON is 2-3x faster (3D LineString, 1000 points: Node 48 -> 15 us, web 56
  -> 23 us): coordinates are validated and copied in one indexed pass into a preallocated
  `Float64Array` instead of being pushed to an array and converted. Error messages are unchanged.
- Python Shapely conversion uses a constant number of vectorised Shapely calls instead of one
  per part/ring/point: encode/decode of a 20-part MultiPolygon 298/138 -> 65/55 us, of a
  1000-point MultiPoint 4.5 ms/545 us -> 63/200 us, of a 1000-point Polygon 25/16 -> 13/15 us;
  simple lines are slightly faster too. Output is unchanged.

### Fixed
- Decoding a geometry whose header has 0 or more than 16 dimensions crashed the C core (division
  by zero / stack buffer overrun). It is now a malformed-input error.
- Python: float arrays that are misaligned (e.g. `np.frombuffer` at an odd offset) are copied
  instead of being rejected with a misleading "must be C-contiguous" error.
- Python: decoding an integer (e.g. `decode(5)`) raises `TypeError` instead of decoding that many
  zero bytes.
- Python: `decode` of a MultiLineString whose lines have different numbers of points failed with
  a numpy "inhomogeneous shape" error.
- Python type stubs now match the module (checked in CI with `mypy.stubtest`): the full `__all__`,
  `DecodedGeometry`/`GeometryFrame` as dataclasses, `EncodedGeometryType` as an `IntEnum`, and
  `Context` as final.

### Behaviour changes (inputs that previously produced garbage)
- Encoding NaN/infinite values, or values with |value × 10^precision| ≥ 2^58, is an
  invalid-argument error.
- Float-stream precisions must be in 0..63 (geometry precisions already were).
- Decoding rejects chars above `~` (126) as malformed.

### Removed
- The C implementation (`core/src/core.c`), nanobind extension, `setup.py`, node-gyp addon and
  Emscripten build.

---

## [0.5.0]

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
