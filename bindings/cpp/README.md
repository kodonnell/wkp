# WKP C++ binding

C++ convenience layer over the WKP C ABI.

## Architecture

- Public C ABI: `core/include/wkp/core.h`
- C++ wrapper header: `bindings/cpp/include/wkp/core.hpp`
- Wrapper methods are ABI-backed, keeping core internals binding-agnostic

## Layout

- `include/wkp/core.hpp`: C++ API
- `tests/`: C++ API tests
- `bench/benchmark_cpp.cpp`: C++ benchmark

## Dependencies

- CMake >= 3.18
- C++17 compiler
- doctest (fetched by root CMake when tests enabled)

## Build

From repo root:

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DWKP_BUILD_TESTS=ON -DWKP_BUILD_BENCHMARKS=ON
cmake --build build/core --config Release
```

## Test

```sh
ctest --test-dir build/core -C Release --output-on-failure
```

## Benchmark

```sh
build/core/Release/wkp_cpp_benchmark 10000 2 5 1000
```
