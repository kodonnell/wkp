# @wkpjs/node

Node-API native bindings for WKP core.

## Requirements

- Node.js >= 18
- Native build toolchain for `node-gyp` (C/C++ compiler and Python)

## Build

From `bindings/javascript`:

```sh
npm install
npm --workspace @wkpjs/node run build
```

## Test

From `bindings/javascript`:

```sh
npm --workspace @wkpjs/node run test
```

## Benchmark

Full geometry encode/decode benchmark:

```sh
npm --workspace @wkpjs/node run benchmark -- --points=10000 --precision=5 --iterations=200
```

Flat-array API micro-benchmark (encode/decode/decodeFrame/decodeFloatsArray):

```sh
node bindings/javascript/packages/node/benchmark/bench_flat_api.mjs
node bindings/javascript/packages/node/benchmark/bench_flat_api.mjs --points=50000 --iterations=200
```

## API

### Geometry encode / decode

```js
encode(geometry, precision, ctx?)     // → string
decode(encoded, ctx?)                 // → { geometry, version, precision, dimensions }
decodeHeader(encoded)                 // → { version, precision, dimensions, geometryType }
```

### GeometryFrame — low-level flat representation

```js
decodeFrame(encoded, ctx?)            // → GeometryFrame
encodeFrame(frame, ctx?)              // → string
```

`GeometryFrame` fields: `version`, `precision`, `dimensions`, `geometryType`, `coords` (Float64Array, flat `[x0,y0,x1,y1,…]`), `segmentPointCounts` (Uint32Array), `groupSegmentCounts` (Uint32Array).

Methods: `toGeometry()` → GeoJSON, `toBuffer()` → ArrayBuffer (transferable), `GeometryFrame.fromBuffer(buf)` → GeometryFrame.

### Float helpers

```js
encodeFloats(floats, precisions, ctx?)                 // → string
decodeFloatsArray(encoded, precisions, ctx?)            // → Float64Array (flat [x0,y0,x1,y1,...])
decodeFloats(encoded, precisions, ctx?)                 // → Array<Array<number>>
```

`decodeFloatsArray` returns the decoded values as a flat `Float64Array` with no intermediate array construction — the cost is a single memcpy from the native buffer. `decodeFloats` is a convenience wrapper on top that slices it into an array of rows. Use `decodeFloatsArray` when feeding into typed-array pipelines (WebGL, DataView, workers).

### Context

```js
new Context()
```

`Context` is a marker object used for API consistency — the native addon manages its own buffer pool internally per thread and does not hold state in the JS object itself.

**Default (omit ctx)** — uses a module-level context; fine for most use cases:

```js
const encoded = encode(geometry, precision);
const decoded = decode(encoded);
```

**Explicit context** — pass your own `Context` instance; behaviour is identical at the native level, but makes the intent explicit and future-proofs your code if native context isolation is added later:

```js
const ctx = new Context();
const encoded = encode(geometry, precision, ctx);
```

**Worker thread isolation** — each worker thread gets its own module instance and therefore its own native buffer pool. No sharing occurs between threads regardless of `ctx`.

## Example

```js
const { decode, decodeFrame, encode, GeometryFrame } = require('@wkpjs/node');

const geometry = {
    type: 'LineString',
    coordinates: [[174.776, -41.289], [174.777, -41.290], [174.778, -41.291]],
};

const encoded = encode(geometry, 6);
const decoded = decode(encoded);
console.log(encoded);
console.log(decoded.geometry);

// Low-level frame access (efficient for bulk processing / worker transfer)
const frame = decodeFrame(encoded);
console.log(frame.coords);           // Float64Array [174.776, -41.289, ...]
const buf = frame.toBuffer();        // ArrayBuffer — transferable to worker
const frame2 = GeometryFrame.fromBuffer(buf);
```

## Publishing

Publishing is shared across JS packages and documented in `bindings/javascript/README.md`.
