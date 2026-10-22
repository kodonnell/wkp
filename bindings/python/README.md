# wkp (Python binding)

Python package with a minimal Shapely-first API on top of the WKP Rust core.

## Architecture

- `src/lib.rs`: the `wkp._core` extension, written with [pyo3](https://pyo3.rs) and
  [rust-numpy](https://github.com/PyO3/rust-numpy) over `wkp` (`core/`)
- `python/wkp/`: the pure-Python package (Shapely conversion, `GeometryFrame`, contexts)
- Built with [maturin](https://www.maturin.rs) as abi3 wheels: one wheel per platform works on
  every CPython from 3.10 up (including 3.14)
- The package version comes from the Cargo workspace (`Cargo.toml` at the repo root); a runtime
  check enforces core compatibility
- Large inputs are processed with the GIL released

## Dependencies

- Python >= 3.10
- build: Rust (stable) and `maturin` (pulled in automatically by `pip install`)
- runtime: `numpy`, `shapely`
- dev/test: `pytest`, `polyline`

## Install (dev)

From repo root:

```sh
pip install -e "bindings/python[dev]"     # or: cd bindings/python && maturin develop --release
```

Re-run after changing Rust code (Python changes are picked up directly).

## Build

```sh
cd bindings/python
maturin build --release      # wheel for this platform, in ../../target/wheels
maturin sdist                # source distribution
```

## Test

```sh
pytest bindings/python/tests
```

## API

### Geometry encode / decode

```python
encode(geom, precision, *, ctx=None) -> str
decode(encoded, *, ctx=None) -> DecodedGeometry
decode_header(encoded) -> Header  # NamedTuple: (version, precision, dimensions, geometry_type)
```

`encoded` can be `bytes`, `str` (e.g. read from a text column or JSON) or any other bytes-like
object. Encoders return `str` (use `.encode("ascii")` if you need `bytes`).

Supported geometries are Point, LineString (and LinearRing, which decodes as a LineString),
Polygon, MultiPoint, MultiLineString and MultiPolygon, in 2D or 3D (XYZ). Geometries with M
values raise `ValueError`: Shapely cannot rebuild them when decoding. Data with more than 3
dimensions can be encoded with `encode_frame` and decoded with `decode_frame`.

### Limits

Precision is an integer from 0 to 63, and every `|value| × 10^precision` must be below 2^58
(e.g. ±2.8 × 10^12 at precision 5). Geometries and float streams have 1 to 16 dimensions, and
values must be finite. Geometries must have the structure their type requires: one point for a Point, at
least two per linestring, no empty parts.

### Errors

Inputs wkp cannot encode or decode raise `wkp.WkpError`, a `ValueError` whose `code` is
`INVALID_ARGUMENT` or `MALFORMED_INPUT` (the encoded data is not valid WKP). Arguments of the
wrong type raise `TypeError`.

### GeometryFrame — low-level flat representation

```python
decode_frame(encoded, *, ctx=None) -> GeometryFrame
encode_frame(frame, *, ctx=None) -> str
```

`GeometryFrame` fields: `version`, `precision`, `dimensions`, `geometry_type`, `coords` (numpy float64 array, shape `(n_points, dimensions)`), `segment_point_counts`, `group_segment_counts`.

Methods: `to_geometry() -> Shapely geometry`, `to_buffer() -> bytes`, `GeometryFrame.from_buffer(buf) -> GeometryFrame`.

### Float helpers

```python
encode_floats(floats, precisions, *, ctx=None) -> str
decode_floats_array(encoded, precisions, *, ctx=None) -> np.ndarray  # flat (N*dims,) float64
decode_floats(encoded, precisions, *, ctx=None) -> np.ndarray        # shaped (N, dims) float64
```

`decode_floats_array` returns a flat 1D float64 array of interleaved values `[x0, y0, x1, y1, ...]` — a single allocation with no reshape. Use `arr.tobytes()` to treat it as a raw byte buffer for shipping over sockets or shared memory.

`decode_floats` wraps `decode_floats_array` and returns a `(N, dims)` 2D view (zero-copy reshape). Use `.tolist()` on the result if you need plain Python lists.

### Context

```python
Context()
```

A `Context` owns reusable native buffers. Buffers grow to fit the largest geometry processed, then stay allocated and get reused on subsequent calls — avoiding repeated allocations.

**Thread-local (default)** — one context per thread, created lazily, lives for the thread's lifetime:

```python
encoded = encode(geom, precision)   # ctx=None uses the thread-local pool
decoded = decode(encoded)
```

**Explicit reuse** — create one context and pass it to a batch of calls; the pool is freed when the object is garbage-collected or explicitly deleted:

```python
ctx = wkp.Context()
for geom in large_collection:
    encoded = encode(geom, precision, ctx=ctx)
    ...
del ctx  # in CPython, releases the native buffers once nothing else refers to ctx
```

A `Context` belongs to one thread at a time: using the same one from two threads concurrently
raises `RuntimeError: Already borrowed` (use the default thread-local contexts, or one `Context`
per thread). Inputs of 16k+ values are processed with the GIL released, so threads encoding or
decoding large geometries run in parallel.

**Per-call (no persistent memory)** — create a fresh context for each call; buffers are freed as soon as the call returns:

```python
encoded = encode(geom, precision, ctx=wkp.Context())
```

This costs one allocation/free per call but guarantees no native memory persists between calls — useful when peak RSS matters more than throughput.

## Example

```python
from shapely.geometry import LineString
from wkp import GeometryFrame, decode, decode_frame, encode

geom = LineString([
    (174.776, -41.289),
    (174.777, -41.290),
    (174.778, -41.291),
])

encoded = encode(geom, precision=6)
decoded = decode(encoded)

print(encoded)
print(decoded.geometry.wkt)

# Access the flat frame directly (useful for numpy / bulk processing)
frame = decode_frame(encoded)
print(frame.coords)          # numpy float64 array, shape (3, 2)
buf = frame.to_buffer()      # compact binary blob
frame2 = GeometryFrame.from_buffer(buf)  # round-trip
assert frame2 == frame
```

## Benchmark

Full geometry encode/decode comparison against Shapely WKB/WKT:

```sh
python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5 --max-iterations=1000 --max-duration=1
```

Flat-array API micro-benchmark (encode/decode/decode_frame/decode_floats_array):

```sh
python bindings/python/benchmark/bench_flat_api.py
python bindings/python/benchmark/bench_flat_api.py --points=50000 --iterations=200
```

Example results (wkp 0.6.0, Python 3.10, WSL2 on an i7-12700H, one core; x86-64, so the SIMD
paths are used):

| Method          | Source   | kb    | Encode (ms) | Decode (ms) | Total (ms)  | Iters |
| --------------- | -------- | ----- | ----------- | ----------- | ----------- | ----- |
| shapely_wkb     | nz-coast | 156.3 | 0.23 ± 0.03 | 0.06 ± 0.02 | 0.29 ± 0.04 | 2000  |
| shapely_wkt     | nz-coast | 379.7 | 1.32 ± 0.15 | 4.37 ± 0.33 | 5.70 ± 0.35 | 988   |
| shapely_wkt_5dp | nz-coast | 202.8 | 1.38 ± 0.12 | 2.76 ± 0.20 | 4.15 ± 0.24 | 1088  |
| wkp-5p-bytes    | nz-coast | 42.9  | 0.06 ± 0.02 | 0.05 ± 0.01 | 0.11 ± 0.02 | 2000  |
| wkp-5p-str      | nz-coast | 42.9  | 0.06 ± 0.01 | 0.05 ± 0.01 | 0.11 ± 0.02 | 2000  |
| wkp-frame-5p    | nz-coast | 42.9  | 0.07 ± 0.03 | 0.03 ± 0.01 | 0.10 ± 0.04 | 2000  |
| shapely_wkb     | random   | 156.3 | 0.22 ± 0.03 | 0.06 ± 0.06 | 0.29 ± 0.07 | 2000  |
| shapely_wkt     | random   | 378.4 | 1.35 ± 0.35 | 4.13 ± 0.27 | 5.53 ± 0.62 | 983   |
| shapely_wkt_5dp | random   | 163.9 | 1.49 ± 0.19 | 2.57 ± 0.25 | 4.06 ± 0.31 | 1058  |
| wkp-5p-bytes    | random   | 72.2  | 0.06 ± 0.02 | 0.07 ± 0.01 | 0.13 ± 0.02 | 2000  |
| wkp-5p-str      | random   | 72.2  | 0.06 ± 0.02 | 0.07 ± 0.03 | 0.13 ± 0.04 | 2000  |
| wkp-frame-5p    | random   | 72.2  | 0.06 ± 0.01 | 0.05 ± 0.01 | 0.11 ± 0.02 | 2000  |

On the same machine, the 0.5.0 (nanobind + C core) build took 0.17 ms to encode and 0.18 ms to
decode `nz-coast` (`wkp-5p-str`): 0.6.0 is about 2.8x / 3.6x faster.

## Packaging / release

- abi3 wheels (manylinux and musllinux x86_64/aarch64, Windows x64/arm64, macOS x86_64/arm64)
  and the sdist are built in `.github/workflows/python.yml` with `PyO3/maturin-action`, then
  tested on Python 3.10 and 3.14.
- Release/publish runs from tag pushes or `workflow_dispatch` in `.github/workflows/python.yml`.
- Tag publish (recommended): push a tag in the exact format `pypi-vX.Y.Z` (for example `pypi-v0.1.0`).
- Publish order is always TestPyPI first, then PyPI.
- Python releases are binding-specific. Product-level GitHub releases use the separate `wkp-vX.Y.Z` tag flow.

### Manual publish from GitHub

1. Open Actions -> `Build and upload to PyPI`.
2. Run workflow dispatch.
3. The workflow publishes to TestPyPI and then PyPI automatically.

If you only publish via git tags, manual dispatch is optional and can be removed.
