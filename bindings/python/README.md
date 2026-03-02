# wkp (Python binding)

Python package that provides high-level geometry APIs on top of WKP core.

## Architecture

- Uses `nanobind` for the extension module
- Calls the C ABI in `core/include/wkp/core.h` directly
- Python package version is managed independently (currently `0.1.0`)
- Runtime compatibility check enforces WKP core `0.1.x`

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

- Wheels are built in `.github/workflows/wheels.yml` using `cibuildwheel`.
- Manual release/publish is available via `workflow_dispatch` in `.github/workflows/wheels.yml`.
- Select `publish_to` as `none`, `testpypi`, `pypi`, or `both` when running manually.
- Tag pushes matching `vX.Y.Z` also trigger release + publish flow.

### Manual publish from GitHub

1. Open Actions -> `Build and upload to PyPI`.
2. Run with `publish_to=none` to validate builds only.
3. Run with `publish_to=testpypi` (or `both`) before final PyPI publish.
