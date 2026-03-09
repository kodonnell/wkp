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

## C ABI workspace contract

The C ABI is workspace-first. Callers create one workspace and reuse it across operations.

Workspace APIs provide auto-resize behavior and avoid explicit retry loops:

- `wkp_workspace_create` / `wkp_workspace_destroy`
- `wkp_workspace_encode_f64` / `wkp_workspace_decode_f64`
- `wkp_workspace_encode_geometry_frame_f64`
- `wkp_workspace_decode_geometry_frame_f64`
- Workspace functions grow internal buffers automatically and avoid `WKP_STATUS_BUFFER_TOO_SMALL` retry loops.
- Optional `max_size` limits are enforced; overflow returns `WKP_STATUS_LIMIT_EXCEEDED`.
- Use `-1` for unlimited max size.

Note: internal C++ helpers under `wkp::core` still use `std::string` / `std::vector` and may allocate.

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
