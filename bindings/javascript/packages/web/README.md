# @wkpjs/web

Browser/Node WebAssembly bindings for WKP core built with Emscripten.

## Dependencies

- Node.js >= 18
- Emscripten (`emcc`) available (for build)
- Browser usage requires HTTP(S) origin (not `file://`)

## API

Use `createWkp()` to load the WASM module and get:

- `encodeF64(values, dimensions, precisions) -> Uint8Array`
- `decodeF64(encoded, dimensions, precisions) -> Float64Array`
- `GeometryEncoder` high-level geometry API

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

## Publish

`@wkpjs/web` is published by `.github/workflows/npm-publish.yml` (manual `workflow_dispatch` or `npm-v*` tag trigger).

### Manual publish from GitHub

1. Open Actions -> `Publish JavaScript packages to npm`.
2. Run with `dry_run=true` first.
3. Re-run with `dry_run=false` to publish.

## Example

```js
import { createWkp } from '@wkpjs/web';

const wkp = await createWkp();
const encoder = new wkp.GeometryEncoder(6, 2);

const encoded = encoder.encode({
	type: 'Point',
	coordinates: [174.776, -41.289],
});

const decoded = wkp.GeometryEncoder.decode(encoded);
```
