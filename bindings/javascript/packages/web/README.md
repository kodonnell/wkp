# @wkp/web

Browser/Node WebAssembly bindings for WKP core built with Emscripten.

## Dependencies

- Node.js >= 18
- Emscripten (`emcc`) available (for build)
- Browser usage requires HTTP(S) origin (not `file://`)

## API

Use `createWkp()` to load the WASM module and get:

- `encodeF64(values, dimensions, precisions) -> Uint8Array`
- `decodeF64(encoded, dimensions, precisions) -> Float64Array`

## Build

Requires Emscripten (`emcc`) in PATH.

From `bindings/javascript`:

```sh
npm install
npm --workspace @wkp/web run build
```

## Benchmark

### Run WASM benchmark in Node

From `bindings/javascript`:

```sh
npm --workspace @wkp/web run benchmark -- --points=10000 --precision=5 --iterations=200
```

### Run benchmark in Browser

From `bindings/javascript`:

```sh
npm --workspace @wkp/web run benchmark:serve
```

or manually from `bindings/javascript/packages/web`:

```sh
python -m http.server 8080
```

Then open:

- `http://localhost:8080/benchmark/index.html`
- `http://localhost:8080/benchmark/index.html?points=10000&precision=5&iterations=200`

## Publish

Package publishing is automated by `.github/workflows/npm-publish.yml`.

## Example

```js
import { createWkp } from '@wkp/web';

const wkp = await createWkp();
const encoded = wkp.encodeF64([1.0, 2.0, 3.0, 4.0], 2, [5]);
const decoded = wkp.decodeF64(encoded, 2, [5]);
```
