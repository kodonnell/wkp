# WKP Core

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
- `src/core.c`: implementation
- `tests/test_c_api.cpp`: ABI-focused roundtrip and frame tests
- `tests/test_c_api_errors.cpp`: ABI error and validation tests
- `tests/test_c_api_geometry.cpp`: ABI geometry framing tests

## C ABI contract

The C ABI has one usage style: caller-managed context.

- Call `wkp_context_init` before any encode/decode call.
- Reuse the same `wkp_context` across repeated calls on one thread.
- Call `wkp_context_free` when finished.

Encode/decode calls return pointers to buffers owned by the provided context.
Those pointers remain valid until the next call that reuses the same context buffers.

Threading rule: use one `wkp_context` per concurrent thread.

### Minimal usage pattern

```c
#include <wkp/core.h>

wkp_context ctx = {0};
if (wkp_context_init(&ctx) != WKP_STATUS_OK) {
	/* handle error */
}

/* ... call wkp_encode_* / wkp_decode_* using &ctx ... */

wkp_context_free(&ctx);
```

## Float encode / decode API

`wkp_encode_f64` and `wkp_decode_f64` encode and decode a sequence of float64 values with per-dimension precision.

Input and output are always **flat interleaved buffers**:

```
[x0, y0, x1, y1, ...]           2D
[x0, y0, z0, x1, y1, z1, ...]  3D
```

`precisions` is an array of `int` with one entry per dimension. The `dimension` argument sets how many values constitute one point (must equal `precision_count`).

These functions carry no geometry type or structural metadata — they encode a sequence of N-dimensional floats without any notion of points, lines, or polygons. Use the geometry-frame API (`wkp_encode_geometry_frame` / `wkp_decode_geometry_frame`) when structure metadata is required.

Output from `wkp_decode_f64` is written into the context's internal `f64` buffer. The returned pointer is owned by the context and valid until the next call on the same context. Copy before reuse.

## Geometry framing model

All geometry encoding/decoding in the C ABI is based on a single frame shape:

- header: 4 fields
	- `version`
	- `precision`
	- `dimensions`
	- `geometry_type`
- flattened coordinates: `coords` (`coord_value_count` values)
- segment metadata: `segment_point_counts` (`segment_count` entries)
- group metadata: `group_segment_counts` (`group_count` entries)

When encoded as text:

- `,` separates segments within a group (ring separator)
- `;` separates groups (multi separator)

### Per-geometry framing

`POINT`
- `group_count = 1`
- `segment_count = 1`
- `group_segment_counts = [1]`
- `segment_point_counts = [1]`

`LINESTRING`
- `group_count = 1`
- `segment_count = 1`
- `group_segment_counts = [1]`
- `segment_point_counts = [N]`, where $N \ge 2$

`POLYGON`
- `group_count = 1`
- `segment_count = ring_count`
- `group_segment_counts = [ring_count]`
- `segment_point_counts = [ring_0_points, ring_1_points, ...]`
- encoded body uses `,` between rings

`MULTIPOINT`
- one group per point
- one segment per group
- `group_segment_counts = [1, 1, ...]`
- `segment_point_counts = [1, 1, ...]`
- encoded body uses `;` between points

`MULTILINESTRING`
- one group per linestring
- one segment per group
- `group_segment_counts = [1, 1, ...]`
- `segment_point_counts = [line_0_points, line_1_points, ...]`
- encoded body uses `;` between linestrings

`MULTIPOLYGON`
- one group per polygon
- each group has one or more segments (rings)
- `group_segment_counts` stores ring counts per polygon
- `segment_point_counts` stores point counts for every ring in group order
- encoded body uses:
	- `,` between rings in a polygon
	- `;` between polygons

Validation rule:
the sum of all segment point counts, multiplied by dimensions, must equal `coord_value_count`.

### Binder recipe (frame-first)

Most binders only need `wkp_encode_geometry_frame` and `wkp_decode_geometry_frame`.
Build frame metadata from the source geometry and call one function.

Examples:

- `POINT` with one coordinate tuple
	- `coords = [x, y]` (or `[x, y, z, ...]`)
	- `group_segment_counts = [1]`
	- `segment_point_counts = [1]`

- `LINESTRING` with `N` points
	- `coords = [x0, y0, x1, y1, ...]`
	- `group_segment_counts = [1]`
	- `segment_point_counts = [N]`

- `POLYGON` with shell and holes
	- `coords = concat(shell, hole0, hole1, ...)`
	- `group_segment_counts = [ring_count]`
	- `segment_point_counts = [shell_points, hole0_points, hole1_points, ...]`

- `MULTILINESTRING` with line point counts `[a, b, c]`
	- `coords = concat(line0, line1, line2)`
	- `group_segment_counts = [1, 1, 1]`
	- `segment_point_counts = [a, b, c]`

- `MULTIPOLYGON` with polygon ring counts `[2, 1]`
	- polygon 0 has 2 rings, polygon 1 has 1 ring
	- `group_segment_counts = [2, 1]`
	- `segment_point_counts = [poly0_ring0_points, poly0_ring1_points, poly1_ring0_points]`

Practical binder flow:

1. Flatten coordinates into one contiguous `coords` array.
2. Build `group_segment_counts` and `segment_point_counts`.
3. Call `wkp_encode_geometry_frame` with a caller-owned context.
4. Use returned pointer and size immediately or copy into binder-managed storage.

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
