# WKP Encoding Specification

This document defines the WKP wire format used by the core encoder/decoder.

Status: normative for current implementation direction.

## 1. Overview

WKP encodes geometry as:

1. A 4-character ASCII geometry header.
2. A geometry body made of one or more encoded coordinate streams.
3. ASCII separators for multi-part geometry topology.

Coordinate streams use polyline-style signed varint deltas over quantized integer coordinates.

## 2. Primitive Float Stream Encoding

This section defines the raw numeric stream used by `encode_f64`/`decode_f64` and by geometry bodies.

### 2.1 Inputs

- `values`: flat array of doubles, laid out point-major and dimension-interleaved.
  - 2D example for 3 points: `[x0, y0, x1, y1, x2, y2]`
  - 3D example: `[x0, y0, z0, x1, y1, z1, ...]`
- `dimensions`: integer `1..16`.
- `precisions`: either:
  - one integer applied to all dimensions, or
  - one integer per dimension.

### 2.2 Quantization

For each dimension `d`, compute:

- `factor[d] = 10 ^ precision[d]`

For each input value `v[i]` at dimension `d = i % dimensions`:

- `scaled[i] = llround(v[i] * factor[d])`

`llround` semantics are used (round to nearest, halfway cases away from zero).

### 2.3 Delta State

Maintain per-dimension previous values:

- `previous[d]` initialized as described by the caller context.

For each `scaled[i]`:

- `delta[i] = scaled[i] - previous[d]`
- `previous[d] = scaled[i]`

### 2.4 Signed Varint Text Encoding

Encode each `delta` as follows:

1. ZigZag map signed to unsigned:
   - `u = (delta << 1)`
   - if `delta < 0`, then `u = ~u`
2. Emit 5-bit groups, least significant first:
   - while `u >= 0x20`: emit `((u & 0x1f) | 0x20) + 63`, then `u >>= 5`
   - emit final byte: `u + 63`
3. Append emitted bytes as ASCII chars.

Decoding performs the exact inverse and reconstructs values by cumulative per-dimension addition.

## 3. Geometry Header

The first 4 ASCII characters are:

- `VPDT`

Each character is decoded independently as:

- `field_value = (unsigned char)field_char - 63`

Each field value must be in `0..63`.

Where:

- `V`: format version (`0..63`)
- `P`: precision (`0..63`), uniform for all dimensions in geometry APIs
- `D`: dimensions (`1..16` in current implementation)
- `T`: geometry type (`1..6` in current implementation)

Current geometry type codes:

- `1` POINT
- `2` LINESTRING
- `3` POLYGON
- `4` MULTIPOINT
- `5` MULTILINESTRING
- `6` MULTIPOLYGON

Header encode rule:

- `field_char = static_cast<char>(field_value + 63)`

Notes:

- Header fields are fixed-width one-byte values and are not varint-decoded.
- This proposal keeps version value `V = 1` while changing body delta semantics for multi-part geometry.
- Geometry header precision is a single uniform precision for all dimensions.

## 4. Geometry Body Separators

Separators are literal ASCII characters and are not escaped:

- `,` ring separator (`kSepRing`)
- `;` multi-part separator (`kSepMulti`)

A separator divides coordinate streams. It is topology metadata only and does not reset any syntax state by itself.

## 5. Geometry Body Encoding Rules

For all geometry types, points are supplied as flat interleaved doubles.

Important change in this spec:

- For multi-segment geometry, delta state is continuous across segment boundaries.
- In other words, `previous[d]` is not reset at `,` or `;` boundaries.

This improves compression when adjacent parts/rings are spatially close.

### 5.1 POINT (`T=1`)

- Exactly one point.
- Body: one encoded stream for that point.
- Delta initialization: `previous[d] = 0`.

### 5.2 LINESTRING (`T=2`)

- At least two points.
- Body: one encoded stream for all points in order.
- Delta initialization: `previous[d] = 0`.

### 5.3 POLYGON (`T=3`)

- One or more rings.
- Rings are concatenated in body and separated by `,`.
- Ring point counts are carried out-of-band at API level (input args), not embedded in wire format.
- Delta initialization: `previous[d] = 0` before first ring.
- Across rings: `previous[d]` is carried forward (no reset).

Body shape:

- `ring0_stream[,ring1_stream[,ring2_stream...]]`

### 5.4 MULTIPOINT (`T=4`)

- One or more points.
- Each point is encoded as its own stream and separated by `;`.
- Delta initialization: `previous[d] = 0` before first point.
- Across points: `previous[d]` is carried forward (no reset).

Body shape:

- `point0_stream[;point1_stream[;point2_stream...]]`

### 5.5 MULTILINESTRING (`T=5`)

- One or more linestrings, each with at least two points.
- Each linestring stream is separated by `;`.
- Delta initialization: `previous[d] = 0` before first linestring.
- Across linestrings: `previous[d]` is carried forward (no reset).

Body shape:

- `line0_stream[;line1_stream[;line2_stream...]]`

### 5.6 MULTIPOLYGON (`T=6`)

- One or more polygons.
- Polygons separated by `;`.
- Rings inside each polygon separated by `,`.
- Polygon/ring counts are out-of-band at API level (input args), not embedded in wire format.
- Delta initialization: `previous[d] = 0` before first ring of first polygon.
- Across all boundaries (`;` and `,`): `previous[d]` is carried forward (no reset).

Body shape:

- `poly0_ring0[,poly0_ring1...][;poly1_ring0[,poly1_ring1...]... ]`

## 6. Decoding Rules

1. Parse `VPDT` header using per-character decode (`char - 63`) for each field.
2. Split body by topology separators according to `TT`.
3. Decode each segment stream with the float-stream decoder.
4. Reconstruct grouping shape from separators.
5. Apply continuous delta state across segments as defined in Section 5.

Malformed inputs include (non-exhaustive):

- header shorter than 4 chars
- invalid header field chars (decoded value outside `0..63`)
- unsupported `V`
- malformed varint stream
- decoded scalar count not divisible by `dimensions`
- empty segment between separators

## 7. Determinism and Compatibility Notes

- Encoding is deterministic for a fixed input and precision.
- Because separator boundaries no longer reset deltas, encoded bytes for multi-part geometry differ from older behavior.
- This spec intentionally keeps header version value `1` for this change.
- Implementations must ensure all participating encoders/decoders in the ecosystem are updated together.

## 8. Non-Goals

Not specified in wire format:

- CRS/SRID
- ring orientation normalization
- polygon validity checks beyond structural constraints
- NaN/Inf canonicalization policy
