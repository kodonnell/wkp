# wkp (Python binding)

Python package with a minimal Shapely-first API on top of the WKP Rust core.

## Architecture

- `src/lib.rs`: the `wkp._core` extension, written with [pyo3](https://pyo3.rs) and
  [rust-numpy](https://github.com/PyO3/rust-numpy) over `wkp` (`core/`)
- `python/wkp/`: the pure-Python package (Shapely conversion, `GeometryFrame`, contexts)
- Built with [maturin](https://www.maturin.rs) as abi3 wheels: one wheel per platform works on
  every CPython from 3.9 up (including 3.14)
- The package version comes from the Cargo workspace (`Cargo.toml` at the repo root); a runtime
  check enforces core compatibility
- Large inputs are processed with the GIL released

## Dependencies

- Python >= 3.9
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
encode(geom, precision, *, ctx=None) -> bytes
decode(encoded, *, ctx=None) -> DecodedGeometry
decode_header(encoded) -> (version, precision, dimensions, geometry_type)
```

### GeometryFrame — low-level flat representation

```python
decode_frame(encoded, *, ctx=None) -> GeometryFrame
encode_frame(frame, *, ctx=None) -> bytes
```

`GeometryFrame` fields: `version`, `precision`, `dimensions`, `geometry_type`, `coords` (numpy float64 array, shape `(n_points, dimensions)`), `segment_point_counts`, `group_segment_counts`.

Methods: `to_geometry() -> Shapely geometry`, `to_buffer() -> bytes`, `GeometryFrame.from_buffer(buf) -> GeometryFrame`.

### Float helpers

```python
encode_floats(floats, precisions, *, ctx=None) -> bytes
decode_floats_array(encoded, precisions, *, ctx=None) -> np.ndarray  # flat (N*dims,) float64
decode_floats(encoded, precisions, *, ctx=None) -> np.ndarray        # shaped (N, dims) float64
```

`decode_floats_array` returns a flat 1D float64 array of interleaved values `[x0, y0, x1, y1, ...]` — a single allocation with no reshape. Use `arr.tobytes()` to treat it as a raw byte buffer for shipping over sockets or shared memory.

`decode_floats` wraps `decode_floats_array` and returns a `(N, dims)` 2D view (zero-copy reshape). Use `.tolist()` on the result if you need plain Python lists.

### Context

```python
Context()
```

A `Context` owns a reusable native C buffer pool. Buffers grow to fit the largest geometry processed, then stay allocated and get reused on subsequent calls — avoiding repeated allocations.

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
del ctx  # releases native buffers immediately
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
from wkp import decode, decode_frame, encode

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
frame2 = frame.from_buffer(buf)  # round-trip
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

Example results (wkp 0.6.0, Python 3.10, WSL2 on an i7-12700H, one core):

| Method          | Source   | kb    | Encode (ms) | Decode (ms) | Total (ms)  | Iters |
| --------------- | -------- | ----- | ----------- | ----------- | ----------- | ----- |
| shapely_wkb     | nz-coast | 156.3 | 0.24 ± 0.05 | 0.07 ± 0.03 | 0.30 ± 0.06 | 2000  |
| shapely_wkt     | nz-coast | 379.7 | 1.40 ± 0.29 | 4.62 ± 0.40 | 6.08 ± 0.62 | 931   |
| shapely_wkt_5dp | nz-coast | 202.8 | 1.47 ± 0.33 | 2.89 ± 0.34 | 4.33 ± 0.43 | 1028  |
| wkp-5p-bytes    | nz-coast | 42.9  | 0.12 ± 0.05 | 0.07 ± 0.01 | 0.20 ± 0.06 | 2000  |
| wkp-5p-str      | nz-coast | 42.9  | 0.10 ± 0.02 | 0.08 ± 0.02 | 0.18 ± 0.03 | 2000  |
| wkp-frame-5p    | nz-coast | 42.9  | 0.13 ± 0.14 | 0.06 ± 0.02 | 0.19 ± 0.14 | 2000  |
| shapely_wkb     | random   | 156.3 | 0.26 ± 0.15 | 0.07 ± 0.02 | 0.32 ± 0.15 | 2000  |
| shapely_wkt     | random   | 378.4 | 1.40 ± 0.19 | 4.42 ± 0.48 | 5.82 ± 0.51 | 938   |
| shapely_wkt_5dp | random   | 163.9 | 1.56 ± 0.29 | 2.73 ± 0.37 | 4.26 ± 0.42 | 1008  |
| wkp-5p-bytes    | random   | 72.2  | 0.12 ± 0.14 | 0.09 ± 0.03 | 0.21 ± 0.15 | 2000  |
| wkp-5p-str      | random   | 72.2  | 0.11 ± 0.02 | 0.09 ± 0.02 | 0.19 ± 0.03 | 2000  |
| wkp-frame-5p    | random   | 72.2  | 0.11 ± 0.04 | 0.07 ± 0.02 | 0.18 ± 0.04 | 2000  |

On the same machine, the 0.5.0 (nanobind + C core) build took 0.17 ms to encode and 0.18 ms to
decode `nz-coast` (`wkp-5p-str`).

## Packaging / release

- abi3 wheels (manylinux and musllinux x86_64/aarch64, Windows x64/arm64, macOS x86_64/arm64)
  and the sdist are built in `.github/workflows/python.yml` with `PyO3/maturin-action`, then
  tested on Python 3.9 and 3.14.
- Release/publish runs from tag pushes or `workflow_dispatch` in `.github/workflows/python.yml`.
- Tag publish (recommended): push a tag in the exact format `pypi-vX.Y.Z` (for example `pypi-v0.1.0`).
- Publish order is always TestPyPI first, then PyPI.
- Python releases are binding-specific. Product-level GitHub releases use the separate `wkp-vX.Y.Z` tag flow.

### Manual publish from GitHub

1. Open Actions -> `Build and upload to PyPI`.
2. Run workflow dispatch.
3. The workflow publishes to TestPyPI and then PyPI automatically.

If you only publish via git tags, manual dispatch is optional and can be removed.
