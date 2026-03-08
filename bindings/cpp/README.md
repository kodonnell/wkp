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

## Workspace API example

```cpp
#include <vector>
#include <wkp/core.hpp>

std::vector<double> line = {
	174.776, -41.289,
	174.777, -41.290,
	174.778, -41.291,
};

wkp::core::Workspace workspace;
std::string encoded = wkp::core::encode_linestring(line.data(), 3, 2, 6, &workspace);
auto frame = wkp::core::decode(encoded, &workspace);
```

Convenience path (no workspace):

```cpp
#include <vector>
#include <wkp/core.hpp>

std::string encoded;
wkp::core::encode_f64_into(line.data(), line.size(), 2, {6, 6}, encoded);

std::vector<double> decoded;
wkp::core::decode_f64_into(encoded, 2, {6, 6}, decoded);
```

No-workspace calls are simpler, but reusing `Workspace` is faster in repeated encode/decode workloads.
