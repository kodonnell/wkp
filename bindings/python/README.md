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

Public functions:

- `Context() -> Context`
- `encode(ctx, geom, precision) -> bytes`
- `decode(ctx, encoded) -> DecodedGeometry`
- `decode_header(encoded) -> (version, precision, dimensions, geometry_type)`

Float helpers also require context:

- `encode_floats(ctx, floats, precisions) -> bytes`
- `decode_floats(ctx, encoded, precisions) -> list[tuple[float, ...]]`

`Context` is required for all encode/decode operations. Reuse one context per thread across repeated calls.

## Example

```python
from shapely.geometry import LineString
from wkp import Context, decode, decode_header, encode

geom = LineString([
	(174.776, -41.289),
	(174.777, -41.290),
	(174.778, -41.291),
])

ctx = Context()
encoded = encode(ctx, geom, precision=6)
decoded = decode(ctx, encoded)
header = decode_header(encoded)

print(encoded)
print(header)
print(decoded.geometry.wkt)
```

## Benchmark

From repo root:

```sh
python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5 --max-iterations=1000 --max-duration=1
```

Example results:

| Method          | Source   | kb    | Encode (ms) | Decode (ms) | Total (ms) | Iters |
| --------------- | -------- | ----- | ----------- | ----------- | ---------- | ----- |
| shapely_wkb     | nz-coast | 156.3 | 0.54 ± 0.04 | 0.10 ± 0.01 | 0.64       | 2000  |
| shapely_wkt     | nz-coast | 379.7 | 1.73 ± 0.09 | 5.16 ± 0.29 | 6.89       | 768   |
| shapely_wkt_5dp | nz-coast | 202.8 | 1.29 ± 0.07 | 2.63 ± 0.09 | 3.92       | 1154  |
| wkp-5p-bytes    | nz-coast | 42.9  | 0.27 ± 0.02 | 0.15 ± 0.01 | 0.42       | 2000  |
| wkp-5p-str      | nz-coast | 42.9  | 0.27 ± 0.02 | 0.15 ± 0.02 | 0.42       | 2000  |
| shapely_wkb     | random   | 156.3 | 0.53 ± 0.02 | 0.10 ± 0.01 | 0.63       | 2000  |
| shapely_wkt     | random   | 378.4 | 1.77 ± 0.08 | 4.87 ± 0.21 | 6.64       | 760   |
| shapely_wkt_5dp | random   | 163.9 | 1.37 ± 0.09 | 2.71 ± 0.12 | 4.08       | 1090  |
| wkp-5p-bytes    | random   | 72.2  | 0.35 ± 0.37 | 0.19 ± 0.02 | 0.54       | 2000  |
| wkp-5p-str      | random   | 72.2  | 0.31 ± 0.03 | 0.19 ± 0.04 | 0.50       | 2000  |


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
