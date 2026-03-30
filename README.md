[![PyPI version fury.io](https://badge.fury.io/py/wkp.svg)](https://pypi.python.org/pypi/wkp/)
[![npm version @wkpjs/node](https://badge.fury.io/js/%40wkpjs%2Fnode.svg)](https://badge.fury.io/js/%40wkpjs%2Fnode)
[![npm version @wkpjs/web](https://badge.fury.io/js/%40wkpjs%2Fweb.svg)](https://badge.fury.io/js/%40wkpjs%2Fweb)
[![GitHub Pages](https://img.shields.io/website?url=https%3A%2F%2Fkodonnell.github.io%2Fwkp%2F&up_message=live&down_message=down&label=github%20pages)](https://kodonnell.github.io/wkp/)

# WKP - Well Known Polylines

WKP is a compact string-based geometry encoding format inspired by Google Polyline, but extended for:

- configurable precision
- arbitrary dimensions (for example XYZ)
- polygon and multi-geometry support

Compared to WKT, WKP is typically 10x smaller and encodes/decodes 8x/20x faster. Compared to WKB, WKP is typically 4x smaller and encodes/decodes 2x/0.7x faster. See here[^1].

[^1]: In python run `python .\bindings\python\benchmark\benchmark.py --linestring-points=10000 --precisions=5 --max-iterations=1000 --max-duration=1`. Use 5dp as that's Google Polyline standard. Compare with WKT full precision (as that's usual behaviour as it's uncommon for users to round all coordinates to 5dp before encoding as wkt). Use a realistic example of NZ coastline vs a random example. Encode: WKP=0.31ms, WKB=0.65ms, WKT=2.13ms. Decode: WKP=.15ms, WKB=0.11ms, WKT=6.18ms. Size: WKP=43kb, WKB=156kb, WKT=380kb.

## Demo

GitHub Pages: https://kodonnell.github.io/wkp/

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
- `bindings/javascript/packages/node/`: Node-API addon (`@wkpjs/node`)
- `bindings/javascript/packages/web/`: Emscripten/WASM package (`@wkpjs/web`)

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
- `node-gyp` toolchain for `@wkpjs/node`
- Emscripten (`emcc`) for `@wkpjs/web`

## Quick start (Python)

```sh
pip install wkp
```

```python
from shapely import LineString
from wkp import Context, decode, encode

linestring = LineString([(1, 2), (3, 4), (5, 6)])
ctx = Context()
encoded = encode(ctx, linestring, precision=5)
decoded = decode(ctx, encoded)
print(encoded)
print(decoded.geometry.wkt)
```

`Context` is required for encode/decode operations. Reuse one context per thread across repeated calls for best performance.

## Benchmarks

- Python: `python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5 --max-iterations=1000 --max-duration=1`
- C++: `build/core/Release/wkp_cpp_benchmark 10000 2 5 1000`
- Node addon: `npm --prefix bindings/javascript --workspace @wkpjs/node run benchmark -- --points=10000 --precision=5 --iterations=1000`
- Web (WASM in Node): `npm --prefix bindings/javascript --workspace @wkpjs/web run benchmark -- --points=10000 --precision=5 --iterations=1000`
- Web (browser): run `npm --prefix bindings/javascript --workspace @wkpjs/web run benchmark:serve`, then open `http://localhost:8080/benchmark/index.html`

## Developing

```
python ./scripts/build_all.py
```

## Release model

- `pypi-vX.Y.Z`: publish the Python package, create a Python GitHub release.
- `npm-vX.Y.Z`: publish the JavaScript packages, create a JavaScript GitHub release.
- `wkp-vX.Y.Z`: create a product-level GitHub release only; it does not publish Python or JavaScript packages.
- C++ currently has CI artifacts, but no standalone package/release workflow.

See `docs/release-policy.md` for the full release policy.

## Detailed docs

- Core: `core/README.md`
- C++ binding: `bindings/cpp/README.md`
- Python binding: `bindings/python/README.md`
- JavaScript workspace: `bindings/javascript/README.md`
- Node binding: `bindings/javascript/packages/node/README.md`
- Web binding: `bindings/javascript/packages/web/README.md`