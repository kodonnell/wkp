# wkp (Python binding)

Python package that provides high-level geometry APIs on top of WKP core.

## Architecture

- Uses `nanobind` for the extension module
- Calls the C ABI in `core/include/wkp/core.h` directly
- Shares version from `core/include/wkp/_version.h`

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

## Benchmark

```sh
python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5
```

## Packaging / release

- Wheels are built in `.github/workflows/wheels.yml` using `cibuildwheel`.
- Tagging `vX.Y.Z` triggers release and publishing flow.
