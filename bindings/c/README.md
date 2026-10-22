# WKP C ABI

`include/wkp/core.h` is the stable C ABI for WKP, implemented in Rust by this crate (`wkp-capi`)
on top of `wkp`. It is what the C++ header (`bindings/cpp`) and the .NET binding
(`bindings/csharp`) call. (Python and JavaScript use `wkp` directly.)

## Build

```sh
cargo build --release -p wkp-capi
```

produces, in `target/release/`:

- shared: `libwkp.so` / `libwkp.dylib` / `wkp.dll` (+ `wkp.dll.lib` import library)
- static: `libwkp.a` / `wkp.lib`

When linking the static library yourself, also link the platform libraries Rust's standard
library needs (`cargo rustc -p wkp-capi --release --crate-type staticlib -- --print native-static-libs`
lists them). From CMake, the root `CMakeLists.txt` does all of this through
[Corrosion](https://github.com/corrosion-rs/corrosion) and exposes a `wkp` target:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --config Release
ctest --test-dir build/cmake -C Release --output-on-failure
```

The C ABI tests are in `tests/` (doctest); `cargo test -p wkp-capi` runs the Rust-side checks.

## Contract

- A `wkp_context` is opaque: create one with `wkp_context_new`, reuse it across calls on one
  thread, and free it with `wkp_context_free`. Use one context per concurrent thread.
- Every function that can fail returns a `wkp_status` (an `int32_t`: `WKP_STATUS_OK`,
  `WKP_STATUS_INVALID_ARGUMENT`, `WKP_STATUS_MALFORMED_INPUT`, or `WKP_STATUS_INTERNAL_ERROR`);
  outputs are only written on `WKP_STATUS_OK`.
- Encode/decode results point into buffers owned by the context. They stay valid until the next
  call on the same context, **whether or not that call succeeds**; copy them if you need them
  longer.
- Input pointers may be NULL when their count is 0.
- `wkp_version()` returns the library version string; `WKP_CORE_VERSION` is the version of the
  header.
- Limits: precision 0-63, with every `|value| × 10^precision` below 2^58 (e.g. ±2.8 × 10^12 at
  precision 5); 1-16 dimensions; values must be finite; geometries must have the structure
  their type requires (see below). Anything else is `WKP_STATUS_INVALID_ARGUMENT` when encoding.
- Running out of memory aborts the process.

```c
#include <wkp/core.h>

wkp_context *ctx = wkp_context_new();

const double line[] = {38.5, -120.2, 40.7, -120.95, 43.252, -126.453};
const size_t segment_point_counts[] = {3};
const size_t group_segment_counts[] = {1};
const uint8_t *encoded;
size_t encoded_size;
if (wkp_encode_geometry_frame(ctx, WKP_GEOMETRY_LINESTRING, line, 6, 2, 5,
                              segment_point_counts, 1, group_segment_counts, 1,
                              &encoded, &encoded_size) == WKP_STATUS_OK) {
    /* encoded[0..encoded_size) is "@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@" */
}

wkp_context_free(ctx);
```

## Float encode / decode API

`wkp_encode_f64` and `wkp_decode_f64` encode and decode a sequence of float64 values with
per-dimension precision. Input and output are **flat interleaved buffers**:

```
[x0, y0, x1, y1, ...]           2D
[x0, y0, z0, x1, y1, z1, ...]   3D
```

`precisions` has either one entry (used for every dimension) or one per dimension, each in
0..63. These functions carry no geometry type or structure; use the geometry-frame API
(`wkp_encode_geometry_frame` / `wkp_decode_geometry_frame`) when you need that.

## Geometry framing model

All geometry encoding/decoding is based on a single frame shape:

- header: `version`, `precision`, `dimensions`, `geometry_type`
- flattened coordinates: `coords` (`coord_value_count` values)
- segment metadata: `segment_point_counts` (`segment_count` entries)
- group metadata: `group_segment_counts` (`group_count` entries)

When encoded as text, `,` separates segments within a group (rings) and `;` separates groups.

| Type | Groups | `segment_point_counts` | `group_segment_counts` |
|---|---|---|---|
| `POINT` | 1 | `[1]` | `[1]` |
| `LINESTRING` | 1 | `[N]`, N ≥ 2 | `[1]` |
| `POLYGON` | 1 | `[shell_points, hole0_points, ...]` | `[ring_count]` |
| `MULTIPOINT` | one per point | `[1, 1, ...]` | `[1, 1, ...]` |
| `MULTILINESTRING` | one per line | `[line0_points, line1_points, ...]`, each ≥ 2 | `[1, 1, ...]` |
| `MULTIPOLYGON` | one per polygon | point count of every ring, in order | ring count per polygon |

The encoder enforces this table (and that no count is 0). The sum of all segment point counts,
multiplied by `dimensions`, must equal `coord_value_count`, and the sum of
`group_segment_counts` must equal `segment_count`. The decoder only checks the syntax, so data
written by older, laxer encoders still decodes.

Binder recipe:

1. Flatten coordinates into one contiguous `coords` array.
2. Build `segment_point_counts` and `group_segment_counts`.
3. Call `wkp_encode_geometry_frame` with a context.
4. Use the returned pointer and size immediately, or copy into binder-managed storage.

## Version

`include/wkp/_version.h` must match the Cargo workspace version; `cargo test -p wkp-capi` checks
it.
