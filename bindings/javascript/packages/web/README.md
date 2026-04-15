# @wkpjs/web

Browser/Node WebAssembly bindings for WKP core built with Emscripten.

## Requirements

- Node.js >= 18
- Emscripten (`emcc`) available (for build)
- Browser usage requires HTTP(S) origin (not `file://`)

## Build

Requires Emscripten (`emcc`) in PATH.

From `bindings/javascript`:

```sh
npm install
npm --workspace @wkpjs/web run build
```

The build regenerates `src/version.generated.js` from `package.json`, so consumers do not need JSON import-assertion support in their bundler.

`src/version.generated.js` is generated during `build`, `test`, and `prepack`, and is intended to stay out of git.
If the generated file is missing in local dev, the package falls back to binding version `0.0.1` and derives a compatibility range from the loaded core at runtime.

## Test

From `bindings/javascript`:

```sh
npm --workspace @wkpjs/web run test
```

## Benchmark

### Run benchmark in Node

From `bindings/javascript`:

```sh
npm --workspace @wkpjs/web run benchmark -- --points=10000 --precision=5 --iterations=200
```

### Run benchmark in Browser

From `bindings/javascript`:

```sh
npm --workspace @wkpjs/web run benchmark:serve
```

or manually from `bindings/javascript/packages/web`:

```sh
python -m http.server 8080
```

Then open:

- `http://localhost:8080/benchmark/index.html`
- `http://localhost:8080/benchmark/index.html?points=10000&precision=5&iterations=200`

## API

Use `createWkp()` to load the WASM module. All functions are returned on the `wkp` object.

### Geometry encode / decode

```js
wkp.encode(geometry, precision, ctx?)    // → string
wkp.decode(encoded, ctx?)               // → { geometry, version, precision, dimensions }
wkp.decodeHeader(encoded)               // → { version, precision, dimensions, geometryType }
```

### GeometryFrame — low-level flat representation

```js
wkp.decodeFrame(encoded, ctx?)          // → GeometryFrame
wkp.encodeFrame(frame, ctx?)            // → string
```

`wkp.GeometryFrame` fields: `version`, `precision`, `dimensions`, `geometryType`, `coords` (Float64Array, flat `[x0,y0,x1,y1,…]`), `segmentPointCounts` (Uint32Array), `groupSegmentCounts` (Uint32Array).

Methods: `toGeometry()` → GeoJSON, `toBuffer()` → ArrayBuffer (transferable), `wkp.GeometryFrame.fromBuffer(buf)` → GeometryFrame.

### Float helpers

```js
wkp.encodeFloats(floats, precisions, ctx?)              // → string
wkp.decodeFloatsArray(encoded, precisions, ctx?)         // → Float64Array (flat [x0,y0,x1,y1,...])
wkp.decodeFloats(encoded, precisions, ctx?)              // → Array<Array<number>>
```

`decodeFloatsArray` returns the decoded values as a flat `Float64Array` with no intermediate array construction — a single copy from the WASM heap. `decodeFloats` is a convenience wrapper on top that slices it into an array of rows.

### Context

```js
new wkp.Context()
```

`Context` is a marker object — the WASM module manages its own buffer pool internally and does not hold state in the JS object itself.

**Default (omit ctx)** — uses a module-level context; fine for most use cases:

```js
const encoded = wkp.encode(geometry, precision);
const decoded = wkp.decode(encoded);
```

**Worker thread isolation** — each worker should call `createWkp()` independently. Each call produces an isolated WASM realm with its own memory and buffer pool; no state is shared across realms:

```js
// worker.js
import { createWkp } from '@wkpjs/web';
const wkp = await createWkp();   // isolated instance — no shared state with main thread
const decoded = wkp.decode(data.encoded);
```

**Memory control** — the WASM heap is fixed at module load time. If you need to release it entirely, drop the reference to the `wkp` object and allow it to be garbage-collected. There is no way to release a partial allocation short of discarding the whole module instance.

## Example

```js
import { createWkp } from '@wkpjs/web';

const wkp = await createWkp();

const encoded = wkp.encode({ type: 'LineString', coordinates: [[0, 0], [1, 1]] }, 6);
const decoded = wkp.decode(encoded);
const header = wkp.decodeHeader(encoded);

console.log(encoded);
console.log(decoded.geometry);
console.log(header);

// Low-level frame access — efficient for WebGL, workers, bulk processing
const frame = wkp.decodeFrame(encoded);
console.log(frame.coords);           // Float64Array [0, 0, 1, 1]
const buf = frame.toBuffer();        // ArrayBuffer — transferable to worker
worker.postMessage({ frame: buf }, [buf]);

// In the worker:
const frame2 = wkp.GeometryFrame.fromBuffer(data.frame);
```

## Publishing

Publishing is shared across JS packages and documented in `bindings/javascript/README.md`.
