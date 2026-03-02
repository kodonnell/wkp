# WKP Core (C++)

`core/` is the shared native engine for all bindings.

## Responsibilities

- Implements encoding/decoding algorithms
- Exposes the public C ABI (`include/wkp/core.h`)
- Owns ABI-level tests (`tests/test_c_api.cpp`)

## Boundaries

- Core is **binding-agnostic**.
- Binding-specific C++ wrappers live under `bindings/cpp`.
- Python/Node/Web all consume the same C ABI.

## Layout

- `include/wkp/core.h`: stable C ABI
- `include/wkp/_version.h`: shared version source
- `src/core.cpp`: implementation
- `tests/test_c_api.cpp`: ABI-focused tests

## C ABI output buffer contract

For C ABI encode/decode `_into` functions, the caller owns output buffers.

- Set `out_encoded.data` / `out_values.data` to your buffer pointer.
- Set `out_encoded.size` / `out_values.size` to buffer capacity (bytes for `u8`, element count for `f64`).
- On success (`WKP_STATUS_OK`), `size` is updated to the number of bytes/elements written.
- If buffer is too small, status is `WKP_STATUS_BUFFER_TOO_SMALL` and `size` is updated to the required capacity.
- Bindings (Python/Node/Web) follow this contract by retrying with a larger buffer.

`wkp_free_u8_buffer` / `wkp_free_f64_buffer` are retained for ABI compatibility and are no-op reset helpers in this model.

Note: this no-allocation guarantee applies to the C ABI encode/decode entry points. Internal C++ helpers under `wkp::core` still use `std::string` / `std::vector` and may allocate.

## Version source of truth

`core/include/wkp/_version.h` is the version source for:

- C ABI/C++ core runtime version
- CMake project version (`project(wkp_native VERSION ...)`)

Binding package versions are managed independently in each binding package.
Bindings enforce core compatibility at runtime.

## Dependencies

- CMake >= 3.18
- C++17 compiler
- doctest (fetched by CMake when tests are enabled)

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

List discovered tests:

```sh
ctest -N --test-dir build/core -C Release
```

## Related artifacts

- Static library: `wkp_core_static`
- Shared library: `wkp_core`
- C++ benchmark executable: `wkp_cpp_benchmark` (from `bindings/cpp/bench`)
