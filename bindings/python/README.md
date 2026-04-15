# wkp (Python binding)

Python package with a minimal Shapely-first API on top of WKP core geometry-frame C ABI.

## Architecture

- Uses `nanobind` for the extension module
- Calls the C ABI in `core/include/wkp/core.h` directly
- Python package version is managed independently
- Runtime compatibility check enforces WKP core

## Dependencies

- Python >= 3.8
- build: `setuptools`, `wheel`, `nanobind`
- runtime: `numpy`, `shapely`
- dev/test: `pytest`, `build`, `twine`, `polyline`

## Install (editable dev)

From repo root:

```sh
pip install -e bindings/python[dev]
```

## Build

```sh
python -m build bindings/python
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

Example results:

| Method          | Source   | kb    | Encode (ms) | Decode (ms) | Total (ms)  | Iters |
| --------------- | -------- | ----- | ----------- | ----------- | ----------- | ----- |
| shapely_wkb     | nz-coast | 156.3 | 0.50 ± 0.04 | 0.10 ± 0.02 | 0.60 ± 0.05 | 2000  |
| shapely_wkt     | nz-coast | 379.7 | 1.88 ± 0.09 | 5.09 ± 0.18 | 6.96 ± 0.21 | 721   |
| shapely_wkt_5dp | nz-coast | 202.8 | 1.30 ± 0.08 | 2.71 ± 0.16 | 4.01 ± 0.18 | 1128  |
| wkp-5p-bytes    | nz-coast | 42.9  | 0.23 ± 0.03 | 0.15 ± 0.03 | 0.39 ± 0.04 | 2000  |
| wkp-5p-str      | nz-coast | 42.9  | 0.24 ± 0.03 | 0.15 ± 0.03 | 0.39 ± 0.04 | 2000  |
| shapely_wkb     | random   | 156.3 | 0.50 ± 0.04 | 0.10 ± 0.02 | 0.60 ± 0.05 | 2000  |
| shapely_wkt     | random   | 378.4 | 1.86 ± 0.28 | 5.09 ± 0.20 | 6.89 ± 0.21 | 724   |
| shapely_wkt_5dp | random   | 163.9 | 1.38 ± 0.08 | 2.77 ± 0.14 | 4.15 ± 0.18 | 1073  |
| wkp-5p-bytes    | random   | 72.2  | 0.31 ± 0.10 | 0.19 ± 0.02 | 0.50 ± 0.10 | 2000  |
| wkp-5p-str      | random   | 72.2  | 0.28 ± 0.03 | 0.18 ± 0.02 | 0.46 ± 0.04 | 2000  |


## Packaging / release

- Wheels are built in `.github/workflows/python.yml` using `cibuildwheel`.
- Release/publish runs from tag pushes or `workflow_dispatch` in `.github/workflows/python.yml`.
- Tag publish (recommended): push a tag in the exact format `pypi-vX.Y.Z` (for example `pypi-v0.1.0`).
- Publish order is always TestPyPI first, then PyPI.
- Python releases are binding-specific. Product-level GitHub releases use the separate `wkp-vX.Y.Z` tag flow.

### Manual publish from GitHub

1. Open Actions -> `Build and upload to PyPI`.
2. Run workflow dispatch.
3. The workflow publishes to TestPyPI and then PyPI automatically.

If you only publish via git tags, manual dispatch is optional and can be removed.
