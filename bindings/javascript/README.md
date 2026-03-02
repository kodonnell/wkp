# WKP JavaScript bindings

Monorepo workspace for JavaScript bindings over the shared WKP C ABI.

## Packages

- `@wkpjs/node`: Node-API native addon (`packages/node`)
- `@wkpjs/web`: Emscripten/WebAssembly module (`packages/web`)

## Architecture

- Both packages consume `core/include/wkp/core.h`
- Core algorithm code lives once in `core/src/core.cpp`
- JS packages stay thin wrappers for language/runtime ergonomics

## Dependencies

- Node.js >= 18
- npm workspaces
- native build toolchain for `node-gyp` (`@wkpjs/node`)
- Emscripten (`emcc`) for `@wkpjs/web`

## Versioning

Node/Web package versions are managed independently (currently `0.1.0`).
Both runtimes enforce WKP core compatibility at runtime (`0.1.x`).

From `bindings/javascript`:

```sh
npm run check:runtime-compatibility
```

## Install

From `bindings/javascript`:

```sh
npm install
```

## Build / publish docs

- Node package: `packages/node/README.md`
- Web package: `packages/web/README.md`

## Quick examples

### Node (`@wkpjs/node`)

```js
const { GeometryEncoder } = require('@wkpjs/node');

const encoder = new GeometryEncoder(6, 2);
const geom = {
    type: 'LineString',
    coordinates: [
        [174.776, -41.289],
        [174.777, -41.290],
        [174.778, -41.291],
    ],
};

const encoded = encoder.encode(geom);
const decoded = GeometryEncoder.decode(encoded);
console.log(encoded, decoded.geometry.type);
```

### Web (`@wkpjs/web`)

```js
import { createWkp } from '@wkpjs/web';

const wkp = await createWkp();
const encoder = new wkp.GeometryEncoder(6, 2);
const encoded = encoder.encode({ type: 'Point', coordinates: [174.776, -41.289] });
const decoded = wkp.GeometryEncoder.decode(encoded);
console.log(decoded.geometry);
```
