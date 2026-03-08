# wkp (Python binding)

Python package that provides high-level geometry APIs on top of WKP core.

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

## Workspace API example

```python
from shapely.geometry import LineString
from wkp import Workspace, decode, encode_linestring

workspace = Workspace()

geom = LineString([
	(174.776, -41.289),
	(174.777, -41.290),
	(174.778, -41.291),
])

encoded = encode_linestring(geom, precision=6, workspace=workspace)
decoded = decode(encoded, workspace=workspace)

print(encoded)
print(decoded.version, decoded.precision, decoded.dimensions)
print(decoded.geometry.wkt)
```

This workspace-first pattern is recommended for performance because it reuses internal buffers across calls.

## Convenience usage (no workspace)

You can call the same functions without passing a workspace:

```python
from shapely.geometry import LineString
from wkp import decode, encode_linestring

geom = LineString([(0, 0), (1, 1), (2, 2)])
encoded = encode_linestring(geom, precision=6)
decoded = decode(encoded)
```

This is simpler, but slower for repeated operations because it does not let you explicitly reuse a dedicated workspace.

## Benchmark

From repo root:

```sh
python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5
```

Example results:

| Method          | Source   | WKB (kb) | Encode (ms) | Encode (kb) | Decode (ms) | Total (ms) | Iters |
| --------------- | -------- | -------- | ----------- | ----------- | ----------- | ---------- | ----- |
| shapely_wkb     | nz-coast | 156.3    | 0.657       | 156.3       | 0.111       | 0.768      | 2000  |
| shapely_wkt     | nz-coast | 156.3    | 2.133       | 379.7       | 6.180       | 8.313      | 635   |
| shapely_wkt_5dp | nz-coast | 156.3    | 1.525       | 202.8       | 3.101       | 4.626      | 982   |
| wkp-5p-bytes    | nz-coast | 156.3    | 0.307       | 42.9        | 0.152       | 0.459      | 2000  |
| wkp-5p-str      | nz-coast | 156.3    | 0.307       | 42.9        | 0.158       | 0.465      | 2000  |
| shapely_wkb     | random   | 156.3    | 0.624       | 156.3       | 0.115       | 0.739      | 2000  |
| shapely_wkt     | random   | 156.3    | 2.061       | 378.5       | 5.498       | 7.559      | 676   |
| shapely_wkt_5dp | random   | 156.3    | 1.585       | 163.8       | 3.217       | 4.802      | 946   |
| wkp-5p-bytes    | random   | 156.3    | 0.339       | 72.0        | 0.193       | 0.532      | 2000  |
| wkp-5p-str      | random   | 156.3    | 0.339       | 72.0        | 0.200       | 0.539      | 2000  |


## Packaging / release

- Wheels are built in `.github/workflows/python.yml` using `cibuildwheel`.
- Release/publish runs from tag pushes or `workflow_dispatch` in `.github/workflows/python.yml`.
- Tag publish (recommended): push a tag in the exact format `pypi-vX.Y.Z` (for example `pypi-v0.1.0`).
- Publish order is always TestPyPI first, then PyPI.

### Manual publish from GitHub

1. Open Actions -> `Build and upload to PyPI`.
2. Run workflow dispatch.
3. The workflow publishes to TestPyPI and then PyPI automatically.

If you only publish via git tags, manual dispatch is optional and can be removed.
