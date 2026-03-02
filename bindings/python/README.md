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

## GeometryEncoder example

```python
from shapely.geometry import LineString
from wkp import GeometryEncoder

encoder = GeometryEncoder(precision=6, dimensions=2)

geom = LineString([
	(174.776, -41.289),
	(174.777, -41.290),
	(174.778, -41.291),
])

encoded = encoder.encode(geom)
decoded = GeometryEncoder.decode(encoded)

print(encoded)
print(decoded.version, decoded.precision, decoded.dimensions)
print(decoded.geometry.wkt)
```

## Benchmark

From repo root:

```sh
python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5
```

You can also run with defaults:

```sh
python bindings/python/benchmark/benchmark.py
```

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
