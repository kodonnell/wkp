# WKP JavaScript bindings

Monorepo workspace for the JavaScript bindings over the WKP Rust core.

## Packages

- `@wkpjs/node`: Node-API native addon (`packages/node`, Rust in `packages/node/native`)
- `@wkpjs/web`: WebAssembly module (`packages/web`, Rust in `packages/web/native`)

## Architecture

- Both native layers are small Rust crates over `wkp-core` (`core/`), so the algorithm lives once
- The JS in each package handles argument normalisation, GeoJSON conversion and `GeometryFrame`
- Neither package has npm dependencies; `@wkpjs/node` ships prebuilt binaries for each platform

## Dependencies

- Node.js >= 18
- npm workspaces
- Rust (stable) to build either package from source, plus
  `rustup target add wasm32-unknown-unknown` for `@wkpjs/web`

## Versioning

Package versions follow the Cargo workspace version (currently `0.6.0`);
`node scripts/sync-version.mjs --write` updates them from `Cargo.toml`.
Both runtimes enforce WKP core compatibility at runtime (`0.6.0`).
The web package generates `packages/web/src/version.generated.js` from `packages/web/package.json` during build/test/pack flows, keeps it out of git, and falls back to a local dev version if that generated file is absent.

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
- Tag publish also creates a JavaScript-specific GitHub release with the packaged artifacts.
- Manual publish: run the workflow from Actions using `workflow_dispatch`.
    - `dry_run=true` validates packaging without publishing.
    - `dry_run=false` performs real publish.
- Product-level GitHub releases use the separate `wkp-vX.Y.Z` tag flow.

If you only publish via git tags, manual dispatch is optional and can be removed.

All JS packages in this workspace (`@wkpjs/node` and `@wkpjs/web`) use this same publish flow.

## Quick examples

### Node (`@wkpjs/node`)

```js
const { decode, decodeFrame, encode, GeometryFrame } = require('@wkpjs/node');

const geom = {
    type: 'LineString',
    coordinates: [
        [174.776, -41.289],
        [174.777, -41.290],
        [174.778, -41.291],
    ],
};

const encoded = encode(geom, 6);
const decoded = decode(encoded);
console.log(encoded, decoded.geometry.type);

// Frame access — flat typed arrays, transferable buffers
const frame = decodeFrame(encoded);
const buf = frame.toBuffer();                      // ArrayBuffer
const frame2 = GeometryFrame.fromBuffer(buf);      // round-trip
```

### Web (`@wkpjs/web`)

```js
import { createWkp } from '@wkpjs/web';

const wkp = await createWkp();
const encoded = wkp.encode({ type: 'LineString', coordinates: [[0, 0], [1, 1]] }, 6);
const decoded = wkp.decode(encoded);
console.log(decoded.geometry.type);

// Transfer a decoded frame to a worker with zero copy
const frame = wkp.decodeFrame(encoded);
const buf = frame.toBuffer();
worker.postMessage({ frame: buf }, [buf]);
```
