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

Use `createWkp()` to load the WASM module, then call:

- `Context`
- `decodeHeader(encoded)`
- `decode(ctx, encoded)`
- `encode(ctx, geometry, precision)`
- `encodeFloats(ctx, floats, precisions)`
- `decodeFloats(ctx, encoded, precisions)`

## Example

```js
import { createWkp } from '@wkpjs/web';

const wkp = await createWkp();
const ctx = new wkp.Context();
const encoded = wkp.encode(ctx, { type: 'LineString', coordinates: [[0, 0], [1, 1]] }, 6);
const decoded = wkp.decode(ctx, encoded);
const header = wkp.decodeHeader(encoded);

console.log(encoded);
console.log(decoded.geometry);
console.log(header);
```

`ctx` is required for encode/decode operations.

## Publishing

Publishing is shared across JS packages and documented in `bindings/javascript/README.md`.
