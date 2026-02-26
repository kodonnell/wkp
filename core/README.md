# WKP Core (C++)

This directory contains the C++ implementation of WKP encoding/decoding.

## Layout

- `include/wkp/core.hpp`: C++ API
- `include/wkp/core.h`: C ABI
- `src/core.cpp`: implementation
- `tests/`: C++ tests (doctest + CTest discovery)
- `bench/benchmark_core.cpp`: standalone C++ benchmark executable

## Versioning

Core uses a hard-coded version header at `core/include/wkp/_version.h`.

- C++ reads it directly via `wkp/_version.h`.
- Python packaging reads the same header value from `bindings/python/setup.py`.

## Build

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DWKP_BUILD_TESTS=ON -DWKP_BUILD_BENCHMARKS=ON
cmake --build build/core --config Release
```

## Run tests

```sh
ctest --test-dir build/core -C Release --output-on-failure
```

List discovered test cases:

```sh
ctest -N --test-dir build/core -C Release
```

## Run benchmark

No Python build wrapper is required.

```sh
build/core/Release/wkp_core_benchmark 200000 2 5 20
```
