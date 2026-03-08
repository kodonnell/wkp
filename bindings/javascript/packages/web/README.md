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

### Run WASM benchmark in Node

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

- `Workspace`
- `decodeHeader(encoded)`
- `decode(encoded, workspace?)`
- `encodePoint/encodeLineString/encodePolygon/encodeMultiPoint/encodeMultiLineString/encodeMultiPolygon`
- `encodeF64(values, dimensions, precisions, workspace?)`
- `decodeF64(encoded, dimensions, precisions, workspace?)`

## Example

```js
import { createWkp } from '@wkpjs/web';

const wkp = await createWkp();
const workspace = new wkp.Workspace();
const encoded = wkp.encodeLineString({ type: 'LineString', coordinates: [[0, 0], [1, 1]] }, 6, workspace);
const decoded = wkp.decode(encoded, workspace);
const header = wkp.decodeHeader(encoded);

console.log(encoded);
console.log(decoded.geometry);
console.log(header);
```

You can omit `workspace` for convenience, but reusing an explicit workspace is faster for repeated operations.

## Publishing

Publishing is shared across JS packages and documented in `bindings/javascript/README.md`.
