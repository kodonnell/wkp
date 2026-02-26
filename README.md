[![PyPI version fury.io](https://badge.fury.io/py/wkp.svg)](https://pypi.python.org/pypi/wkp/)

# WKP - Well Known Polylines

WKP is a compact string-based geometry encoding format inspired by Google Polyline, but extended for:

- configurable precision
- arbitrary dimensions (for example XYZ)
- polygon and multi-geometry support

Compared to WKT/WKB, WKP is typically smaller on the wire and competitive or faster for encode/decode in common line/polygon workloads.

## Architecture (ABI-first)

This repository is structured around a single native engine and thin language bindings.

- `core/` is the canonical implementation.
- `core/include/wkp/core.h` is the stable C ABI contract.
- all language packages call that C ABI.
- `bindings/cpp` is treated as a binding layer (not core internals).

### Key design decisions

1. **C ABI is source-of-truth** for cross-language stability.
2. **No binding-specific C++ classes in core internals** (for cleaner boundaries and lower coupling).
3. **Enum/value drift prevention** via explicit C↔C++ mapping and compile-time checks.
4. **One algorithm implementation** shared by Python/Node/Web/C++ bindings.

## Repository layout

- `core/`: C++ engine + C ABI tests
- `bindings/python/`: nanobind Python package (`wkp` on PyPI)
- `bindings/cpp/`: C++ convenience API + C++ tests/benchmarks
- `bindings/javascript/packages/node/`: Node-API addon (`@wkp/node`)
- `bindings/javascript/packages/web/`: Emscripten/WASM package (`@wkp/web`)

## Dependencies

### Core/C++

- CMake >= 3.18
- C++17 toolchain
- doctest (fetched by CMake for tests)

### Python binding

- Python >= 3.8
- `setuptools`, `wheel`, `nanobind`
- runtime: `numpy`, `shapely`

### JavaScript bindings

- Node.js >= 18
- `node-gyp` toolchain for `@wkp/node`
- Emscripten (`emcc`) for `@wkp/web`

## Quick start (Python)

```sh
pip install wkp
```

```python
from shapely import LineString
from wkp import GeometryEncoder

linestring = LineString([(1, 2), (3, 4), (5, 6)])
encoder = GeometryEncoder(dimensions=2, precision=5)
print(encoder.encode(linestring))
```

## Build everything locally

Use the root helper script:

```sh
python scripts/build_all.py
```

Useful flags:

```sh
python scripts/build_all.py --help
python scripts/build_all.py --skip-python
python scripts/build_all.py --skip-js
python scripts/build_all.py --skip-cpp-tests
```

The script performs:

1. C++ configure/build (and optional CTest)
2. Python editable install + tests
3. JavaScript workspace install + build (`@wkp/node`, `@wkp/web`)

## Benchmarks

- Python: `python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5`
- C++: `build/core/Release/wkp_cpp_benchmark 200000 2 5 20`
- Node addon: `npm --prefix bindings/javascript --workspace @wkp/node run benchmark -- --points=10000 --precision=5 --iterations=200`
- Web (WASM in Node): `npm --prefix bindings/javascript --workspace @wkp/web run benchmark -- --points=10000 --precision=5 --iterations=200`
- Web (browser): run `npm --prefix bindings/javascript --workspace @wkp/web run benchmark:serve`, then open `http://localhost:8080/benchmark/index.html`

## CI / Release model

- `.github/workflows/wheels.yml`: Python wheels + PyPI publish
- `.github/workflows/native-js-artifacts.yml`: C++ + JavaScript cross-platform artifact builds
- `.github/workflows/npm-publish.yml`: npm trusted publishing workflow (manual/tag-driven)

### Versioning and publishing

- `core/include/wkp/_version.h` is the single version source for core/C++/Python/JavaScript.
- Python package version is read from `core/include/wkp/_version.h`.
- JavaScript workspace/package versions are checked/synced to the same value.
- Python release remains tag-driven (`vX.Y.Z`) via `wheels.yml`.
- npm release is handled separately via trusted publishing (OIDC, `workflow_dispatch` or `npm-v*` tags).

## Detailed docs

- Core: `core/README.md`
- C++ binding: `bindings/cpp/README.md`
- Python binding: `bindings/python/README.md`
- JavaScript workspace: `bindings/javascript/README.md`
- Node binding: `bindings/javascript/packages/node/README.md`
- Web binding: `bindings/javascript/packages/web/README.md`