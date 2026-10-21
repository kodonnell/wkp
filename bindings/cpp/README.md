# WKP C++ binding

C++ convenience layer over the WKP C ABI.

## Architecture

- Public C ABI: `bindings/c/include/wkp/core.h` (implemented in Rust, see `bindings/c/README.md`)
- C++ wrapper header: `bindings/cpp/include/wkp/core.hpp`, header-only, namespace `wkp::core`
- The wrapper keeps one C ABI context per thread (created on first use, freed when the thread
  exits) and turns error statuses into exceptions. Use the C ABI directly for explicit control
  of context lifetime.

## Layout

- `include/wkp/core.hpp`: C++ API
- `tests/`: C++ API tests
- `bench/benchmark_cpp.cpp`: C++ benchmark

## Dependencies

- CMake >= 3.22
- C++17 compiler
- Rust (stable), to build the C ABI library
- Corrosion and doctest (fetched by the root CMake)

## Build

From repo root (this also builds the Rust C ABI library through Corrosion):

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release -DWKP_BUILD_TESTS=ON -DWKP_BUILD_BENCHMARKS=ON
cmake --build build/cmake --config Release
```

## Test

```sh
ctest --test-dir build/cmake -C Release --output-on-failure
```

## Benchmark

```sh
build/cmake/wkp_cpp_benchmark 10000 2 5 1000   # build/cmake/Release/ on Windows
```

## API

```cpp
namespace wkp::core {
std::string encode_f64(const double* values, std::size_t value_count, std::size_t dimensions,
                       const std::vector<int>& precisions);
std::vector<double> decode_f64(std::string_view encoded, std::size_t dimensions,
                               const std::vector<int>& precisions);
GeometryHeader decode_geometry_header(std::string_view encoded);  // version, precision, dimensions, geometry_type
}
```

Values are flat and interleaved: `[x0, y0, x1, y1, ...]` for 2D, `[x0, y0, z0, x1, y1, z1, ...]`
for 3D, and so on. `precisions` is either one value for every dimension (`{5}`) or one per
dimension (`{6, 6, 0}`: 6 decimal places for X and Y, whole numbers for Z).

Errors throw: `std::invalid_argument` for invalid arguments and malformed input,
`std::runtime_error` otherwise. For geometries (with polygon and multi-part structure), use the C
ABI's `wkp_encode_geometry_frame` / `wkp_decode_geometry_frame` (see `bindings/c/README.md`).

Limits: precision 0-63, with every `|value| × 10^precision` below 2^58; 1-16 dimensions; values
must be finite.

## Example

```cpp
#include <vector>
#include <wkp/core.hpp>

const std::vector<double> line = {
    174.776, -41.289,
    174.777, -41.290,
    174.778, -41.291,
};

const std::string encoded = wkp::core::encode_f64(line.data(), line.size(), 2, {6});
const std::vector<double> decoded = wkp::core::decode_f64(encoded, 2, {6});
```
