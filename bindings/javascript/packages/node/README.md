# @wkpjs/node

Node-API native bindings for WKP core.

## Dependencies

- Node.js >= 18
- Native build toolchain for `node-gyp` (C/C++ compiler and Python)

## Install / build

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

From `bindings/javascript`:

```sh
npm --workspace @wkpjs/node run benchmark -- --points=10000 --precision=5 --iterations=200
```

## Publish

`@wkpjs/node` is published by `.github/workflows/npm-publish.yml` (manual `workflow_dispatch` or `npm-v*` tag trigger).

### Manual publish from GitHub

1. Open Actions -> `Publish JavaScript packages to npm`.
2. Run with `dry_run=true` first.
3. Re-run with `dry_run=false` to publish.

## Example

```js
const { GeometryEncoder } = require('@wkpjs/node');

const encoder = new GeometryEncoder(6, 2);
const geometry = {
  type: 'LineString',
  coordinates: [[174.776, -41.289], [174.777, -41.290]],
};

const encoded = encoder.encode(geometry);
const decoded = GeometryEncoder.decode(encoded);

console.log(encoded);
console.log(decoded.geometry);
```
