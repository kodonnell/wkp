[![PyPI version fury.io](https://badge.fury.io/py/wkp.svg)](https://pypi.python.org/pypi/wkp/)
[![npm version @wkpjs/node](https://badge.fury.io/js/%40wkpjs%2Fnode.svg)](https://badge.fury.io/js/%40wkpjs%2Fnode)
[![npm version @wkpjs/web](https://badge.fury.io/js/%40wkpjs%2Fweb.svg)](https://badge.fury.io/js/%40wkpjs%2Fweb)
[![NuGet](https://img.shields.io/nuget/v/Wkp.svg)](https://www.nuget.org/packages/Wkp)
[![GitHub Pages](https://img.shields.io/website?url=https%3A%2F%2Fkodonnell.github.io%2Fwkp%2F&up_message=live&down_message=down&label=github%20pages)](https://kodonnell.github.io/wkp/)

# WKP - Well Known Polylines

WKP is a compact string-based geometry encoding format inspired by Google Polyline, but extended for:

- configurable precision
- arbitrary dimensions (for example XYZ)
- polygon and multi-geometry support

Compared to WKT, WKP is typically 9x smaller and encodes/decodes 14x/50x faster. Compared to WKB, WKP is typically 3.5x smaller, encodes 2x faster and decodes at about the same speed. See here[^1].

[^1]: In python run `python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5 --max-iterations=1000 --max-duration=1`. Use 5dp as that's Google Polyline standard. Compare with WKT full precision (as that's usual behaviour as it's uncommon for users to round all coordinates to 5dp before encoding as wkt). Use a realistic example of NZ coastline vs a random example. Encode: WKP=0.10ms, WKB=0.24ms, WKT=1.40ms. Decode: WKP=0.08ms, WKB=0.07ms, WKT=4.62ms. Size: WKP=43kb, WKB=156kb, WKT=380kb. (Measured with wkp 0.6.0, Python 3.10, WSL2 on an i7-12700H, one core.)

## Demo

GitHub Pages: https://kodonnell.github.io/wkp/

## Architecture

One Rust implementation of the format, with thin bindings for each language:

```
core/ (crate `wkp`, Rust)
 ├─ bindings/python                      pyo3 extension      -> wkp (PyPI)
 ├─ bindings/javascript/packages/node    Node-API addon      -> @wkpjs/node (npm)
 ├─ bindings/javascript/packages/web     WebAssembly module  -> @wkpjs/web (npm)
 └─ bindings/c                           C ABI (core.h)      -> libwkp
     ├─ bindings/cpp                     C++ header wrapper
     └─ bindings/csharp                  P/Invoke            -> Wkp (NuGet)
```

### Key design decisions

1. **One algorithm implementation** (`core/`), shared by every binding. Each binding links it in directly; only C, C++ and C# go through the C ABI.
2. **Byte-for-byte stability.** Encoded output is fixed by `SPEC.md`. `core/tests/data/c_core_golden.tsv` pins the output of the original C implementation, and `data/integration_tests/` holds fixtures that every binding runs.
3. **Similar APIs everywhere.** `encode` / `decode` / `decode_frame` / `encode_floats` / `decode_floats` (cased per language), with optional reusable contexts.
4. **One version.** `Cargo.toml` (`[workspace.package]`) is the version for every package; `scripts/check_versions.py` checks the rest match.

### Why Rust?

The core moved from C to Rust in 0.6.0:

- **Faster.** The Rust core is 1.7–2.2x faster than the C core at encoding and 1.5–1.9x faster at decoding, with identical output (measured in one process on the same data, gcc -O3 vs rustc). The gains are algorithmic: values are encoded and decoded 8 bytes at a time instead of one char at a time, and rounding no longer calls into libm. In Python, end-to-end encode/decode of a 10k-point linestring is about 1.7x/2.2x faster. In Node, `decode` to GeoJSON is about 12x faster, because the GeoJSON is now built in JavaScript instead of through per-element Node-API calls.
- **Simpler builds.** Python uses maturin with abi3 wheels: one wheel per platform covers every CPython from 3.10 up, including 3.14. Node prebuilt binaries come from cargo, so there is no node-gyp or compiler at install time. The web build is `cargo build --target wasm32-unknown-unknown`, with no Emscripten. C# packs the same C ABI library.
- **Safer.** The decoder is memory-safe on untrusted input. The C core could be crashed by a crafted header (division by zero, stack buffer overrun).

## Repository layout

- `core/`: the Rust implementation (`wkp`), unit tests, golden vectors and fixture tests, `examples/bench.rs`
- `bindings/c/`: C ABI (`include/wkp/core.h`), built as `libwkp`; C ABI tests
- `bindings/cpp/`: C++ convenience header, C++ tests and benchmark (built with CMake from the repo root)
- `bindings/python/`: pyo3 extension + Python package (`wkp` on PyPI)
- `bindings/javascript/packages/node/`: Node-API addon (`@wkpjs/node`)
- `bindings/javascript/packages/web/`: WebAssembly package (`@wkpjs/web`)
- `bindings/csharp/`: .NET binding (`Wkp` on NuGet)
- `data/integration_tests/`: cross-language fixtures
- `docs/`: GitHub Pages demo

## Dependencies

- Rust (stable; the core and C ABI support 1.85+): https://rustup.rs
- Python binding: Python >= 3.10, `maturin` to build; runtime `numpy`, `shapely`
- JavaScript bindings: Node.js >= 18; `rustup target add wasm32-unknown-unknown` for `@wkpjs/web`
- C/C++: CMake >= 3.22 and a C++17 compiler (Corrosion and doctest are fetched by CMake)
- C#: .NET 8 SDK

## Quick start (Python)

```sh
pip install wkp
```

```python
from shapely import LineString
from wkp import decode, encode

linestring = LineString([(1, 2), (3, 4), (5, 6)])
encoded = encode(linestring, precision=5)
decoded = decode(encoded)
print(encoded)
print(decoded.geometry.wkt)
```

A thread-local context is managed automatically. Pass `ctx=Context()` explicitly for fine-grained control.

See the binding READMEs below for JavaScript, C/C++ and C# usage.

## Benchmarks

Core (Rust, no bindings):

- `cargo run --release -p wkp --example bench`

Geometry encode/decode comparison (WKP vs WKB/WKT):

- Python: `python bindings/python/benchmark/benchmark.py --linestring-points=10000 --precisions=5 --max-iterations=1000 --max-duration=1`
- C++: `build/cmake/wkp_cpp_benchmark 10000 2 5 1000` (after the CMake build below; `build/cmake/Release/` on Windows)
- Node addon: `npm --prefix bindings/javascript --workspace @wkpjs/node run benchmark -- --points=10000 --precision=5 --iterations=1000`
- Web (WASM in Node): `npm --prefix bindings/javascript --workspace @wkpjs/web run benchmark -- --points=10000 --precision=5 --iterations=1000`
- Web (browser): run `npm --prefix bindings/javascript --workspace @wkpjs/web run benchmark:serve`, then open `http://localhost:8080/benchmark/index.html`

Flat-array API micro-benchmark (all paths, useful for regression checking):

- Python: `python bindings/python/benchmark/bench_flat_api.py`
- Node: `node bindings/javascript/packages/node/benchmark/bench_flat_api.mjs`

## Developing

Build and test everything (skip parts with `--skip-*`):

```sh
python ./scripts/build_all.py
```

Or per part:

```sh
cargo test                                                     # core + C ABI
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build/cmake --config Release && ctest --test-dir build/cmake -C Release
pip install -e "bindings/python[dev]" && pytest bindings/python/tests
cd bindings/javascript && npm install && npm run build && npm test
dotnet test bindings/csharp/tests/Wkp.Tests
```

The decoders and encoders are also fuzzed against a naive SPEC.md decoder: see `fuzz/README.md`.

## Release model

- `pypi-vX.Y.Z`: publish the Python package, create a Python GitHub release.
- `npm-vX.Y.Z`: publish the JavaScript packages, create a JavaScript GitHub release.
- `nuget-vX.Y.Z`: publish the .NET package, create a .NET GitHub release.
- `crates-vX.Y.Z`: publish the Rust core (`wkp`) to crates.io.
- `wkp-vX.Y.Z`: create a product-level GitHub release only; it does not publish any packages.
- C/C++ have CI artifacts, but no standalone package/release workflow.

See `docs/release-policy.md` for the full release policy, and `docs/publishing.md` for registry
accounts and trusted-publishing setup.

## Detailed docs

- Core: `core/README.md`
- Wire format: `SPEC.md`
- C ABI: `bindings/c/README.md`
- C++ binding: `bindings/cpp/README.md`
- Python binding: `bindings/python/README.md`
- JavaScript workspace: `bindings/javascript/README.md`
- Node binding: `bindings/javascript/packages/node/README.md`
- Web binding: `bindings/javascript/packages/web/README.md`
- .NET binding: `bindings/csharp/README.md`
