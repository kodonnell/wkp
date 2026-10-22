# Wkp (.NET binding)

.NET binding for WKP (Well Known Polylines): compact, fast string encoding of geometries using
the Google Polyline algorithm, extended with configurable precision, arbitrary dimensions and
(multi) polygons. It calls the Rust core through its C ABI (`bindings/c`); the NuGet package ships
the native library for each supported platform under `runtimes/<rid>/native/`.

## Install

```sh
dotnet add package Wkp
```

Targets `net8.0`. Native binaries are included for `linux-x64`, `linux-arm64`,
`linux-musl-x64`, `linux-musl-arm64` (Alpine), `osx-x64`, `osx-arm64`, `win-x64` and `win-arm64`.

## Usage

Geometries are passed as flat frames: interleaved coordinates plus topology (points per segment,
segments per group). See `GeometryFrame` for how each geometry type maps onto that.

```csharp
using Wkp;

// LINESTRING (38.5 -120.2, 40.7 -120.95, 43.252 -126.453) at 5 decimal places.
var coords = new[] { 38.5, -120.2, 40.7, -120.95, 43.252, -126.453 };
string encoded = WkpCodec.Encode(GeometryType.LineString, coords, dimensions: 2, precision: 5,
    segmentPointCounts: new[] { 3 }, groupSegmentCounts: new[] { 1 });
// "@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@"

GeometryFrame frame = WkpCodec.Decode(encoded);
// frame.Coords, frame.SegmentPointCounts, frame.GroupSegmentCounts (read-only ReadOnlyMemory<T>;
// use .Span to read them), frame.Precision, ...

Header header = WkpCodec.DecodeHeader(encoded);
// Header { Version = 1, Precision = 5, Dimensions = 2, GeometryType = LineString }

// Headerless float streams (one precision per dimension):
string floats = WkpCodec.EncodeFloats(coords, new[] { 5, 5 });
double[] values = WkpCodec.DecodeFloats(floats, new[] { 5, 5 });
```

`WkpCodec` keeps one native context per thread. For explicit lifetime control, create a
`WkpContext` (not thread-safe; `Dispose` it when done) and call the same methods on it.

`GeometryFrame.ToBuffer()` / `FromBuffer()` use the same binary layout as the Python and
JavaScript bindings.

Errors: invalid arguments throw `ArgumentException`; malformed encoded input (including a
non-ASCII char) throws `FormatException`. `GeometryFrame.ToBuffer` / `FromBuffer` throw
`OverflowException` for counts that do not fit the buffer format.

Limits: precision 0-63, with every `|value| × 10^precision` below 2^58 (e.g. ±2.8 × 10^12 at
precision 5); 1-16 dimensions; values must be finite.

## Build and test from source

Requires the .NET 8+ SDK and Rust (`cargo` on `PATH`). Building the project compiles the native
library for the current machine:

```sh
dotnet test bindings/csharp/tests/Wkp.Tests
```
