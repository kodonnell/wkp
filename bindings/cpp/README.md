# WKP C++ binding

C++ convenience layer over the WKP C ABI.

## Architecture

- Public C ABI: `core/include/wkp/core.h`
- C++ wrapper header: `bindings/cpp/include/wkp/core.hpp`
- Wrapper methods are ABI-backed, keeping core internals binding-agnostic
- Context ownership is explicit at the ABI layer: initialize one `wkp_context`, reuse it, then free it

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

## Context-first example

```cpp
#include <cstdint>
#include <string>
#include <vector>
#include <wkp/core.h>

std::vector<double> line = {
	174.776, -41.289,
	174.777, -41.290,
	174.778, -41.291,
};

wkp_context ctx = {};
if (wkp_context_init(&ctx) != WKP_STATUS_OK) {
	throw std::runtime_error("Failed to initialize WKP context");
}

const int precisions[2] = {6, 6};
const uint8_t* encoded_data = nullptr;
size_t encoded_size = 0;

const wkp_status enc = wkp_encode_f64(
	&ctx,
	line.data(),
	line.size(),
	2,
	precisions,
	2,
	&encoded_data,
	&encoded_size);

if (enc != WKP_STATUS_OK) {
	wkp_context_free(&ctx);
	throw std::runtime_error("encode failed");
}

std::string encoded(reinterpret_cast<const char*>(encoded_data), encoded_size);

const double* decoded_values = nullptr;
size_t decoded_count = 0;
const wkp_status dec = wkp_decode_f64(
	&ctx,
	reinterpret_cast<const uint8_t*>(encoded.data()),
	encoded.size(),
	2,
	precisions,
	2,
	&decoded_values,
	&decoded_count);

if (dec != WKP_STATUS_OK) {
	wkp_context_free(&ctx);
	throw std::runtime_error("decode failed");
}

std::vector<double> decoded(decoded_values, decoded_values + decoded_count);
wkp_context_free(&ctx);
```
