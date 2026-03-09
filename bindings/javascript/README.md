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

Node/Web package versions are managed independently (currently `0.3.0`).
Both runtimes enforce WKP core compatibility at runtime (`0.3.x`).

From `bindings/javascript`:

```sh
npm run check:runtime-compatibility
```

## Install

From `bindings/javascript`:

```sh
npm install
```

## Package docs

- Node package: `packages/node/README.md`
- Web package: `packages/web/README.md`

## Common commands

From `bindings/javascript`:

```sh
npm run build
npm run test
```

Run package-specific benchmarks:

```sh
npm run benchmark:node -- --points=10000 --precision=5 --iterations=200
npm run benchmark:web:node -- --points=10000 --precision=5 --iterations=200
```

Run web benchmark in browser:

```sh
npm run benchmark:web:serve
```

## Publishing (shared)

GitHub Actions workflow: `.github/workflows/js.yml`.

- Tag publish (recommended): push a tag in the exact format `npm-vX.Y.Z` (for example `npm-v0.1.0`).
- Manual publish: run the workflow from Actions using `workflow_dispatch`.
    - `dry_run=true` validates packaging without publishing.
    - `dry_run=false` performs real publish.

If you only publish via git tags, manual dispatch is optional and can be removed.

All JS packages in this workspace (`@wkpjs/node` and `@wkpjs/web`) use this same publish flow.

## Quick examples

### Node (`@wkpjs/node`)

```js
const { Workspace, decode, encodeLineString } = require('@wkpjs/node');

const workspace = new Workspace();
const geom = {
    type: 'LineString',
    coordinates: [
        [174.776, -41.289],
        [174.777, -41.290],
        [174.778, -41.291],
    ],
};

const encoded = encodeLineString(geom, 6, workspace);
const decoded = decode(encoded, workspace);
console.log(encoded, decoded.geometry.type);
```

Convenience path (no workspace):

```js
const { decode, encodeLineString } = require('@wkpjs/node');

const encoded = encodeLineString({ type: 'LineString', coordinates: [[0, 0], [1, 1]] }, 6);
const decoded = decode(encoded);
```

Omitting `workspace` is simpler, but slower for repeated encode/decode workloads.

### Web (`@wkpjs/web`)

```js
import { createWkp } from '@wkpjs/web';

const wkp = await createWkp();
const workspace = new wkp.Workspace();
const encoded = wkp.encodeLineString({ type: 'LineString', coordinates: [[0, 0], [1, 1]] }, 6, workspace);
const decoded = wkp.decode(encoded, workspace);
console.log(decoded.geometry.type);
```
