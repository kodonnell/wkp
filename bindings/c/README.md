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

- Call `wkp_context_init` before any encode/decode call, reuse the context across calls on one
  thread, and call `wkp_context_free` when finished. Use one context per concurrent thread.
- Every function returns a `wkp_status`; outputs are only written on `WKP_STATUS_OK`.
- Encode/decode results point into buffers owned by the context. They stay valid until the next
  call on the same context; copy them if you need them longer.
- Input pointers may be NULL when their count is 0.
- `wkp_version()` returns the library version string.
- Limits: precision 0-63, with every `|value| × 10^precision` below 2^58 (e.g. ±2.8 × 10^12 at
  precision 5); 1-16 dimensions; values must be finite. Anything else is
  `WKP_STATUS_INVALID_ARGUMENT` when encoding.

```c
#include <wkp/core.h>

wkp_context ctx;
if (wkp_context_init(&ctx) != WKP_STATUS_OK) {
    /* handle error */
}

/* ... call wkp_encode_* / wkp_decode_* using &ctx ... */

wkp_context_free(&ctx);
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

| Type | Groups | `group_segment_counts` | `segment_point_counts` |
|---|---|---|---|
| `POINT` | 1 | `[1]` | `[1]` |
| `LINESTRING` | 1 | `[1]` | `[N]` |
| `POLYGON` | 1 | `[ring_count]` | `[shell_points, hole0_points, ...]` |
| `MULTIPOINT` | one per point | `[1, 1, ...]` | `[1, 1, ...]` |
| `MULTILINESTRING` | one per line | `[1, 1, ...]` | `[line0_points, line1_points, ...]` |
| `MULTIPOLYGON` | one per polygon | ring count per polygon | point count of every ring, in order |

The sum of all segment point counts, multiplied by `dimensions`, must equal `coord_value_count`,
and the sum of `group_segment_counts` must equal `segment_count`.

Binder recipe:

1. Flatten coordinates into one contiguous `coords` array.
2. Build `group_segment_counts` and `segment_point_counts`.
3. Call `wkp_encode_geometry_frame` with a caller-owned context.
4. Use the returned pointer and size immediately, or copy into binder-managed storage.

## Version

`include/wkp/_version.h` must match the Cargo workspace version; `cargo test -p wkp-capi` checks
it.
