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

### Run benchmark in Node

From `bindings/javascript`:

```sh
npm --workspace @wkpjs/node run benchmark -- --points=10000 --precision=5 --iterations=200
```

## API

Exports:

- `Workspace`
- `decodeHeader(encoded)`
- `decode(encoded, workspace?)`
- `encodePoint/encodeLineString/encodePolygon/encodeMultiPoint/encodeMultiLineString/encodeMultiPolygon`
- `encodeF64(values, dimensions, precisions, workspace?)`
- `decodeF64(encoded, dimensions, precisions, workspace?)`

## Example

```js
const { Workspace, decode, encodeLineString } = require('@wkpjs/node');

const workspace = new Workspace();
const geometry = {
  type: 'LineString',
  coordinates: [[174.776, -41.289], [174.777, -41.290], [174.778, -41.291]],
};

const encoded = encodeLineString(geometry, 6, workspace);
const decoded = decode(encoded, workspace);

console.log(encoded);
console.log(decoded.geometry);
```

Convenience path (no workspace):

```js
const { decode, encodeLineString } = require('@wkpjs/node');

const encoded = encodeLineString({ type: 'LineString', coordinates: [[0, 0], [1, 1]] }, 6);
const decoded = decode(encoded);
```

Omitting `workspace` is simpler, but slower for repeated operations because buffers are not reused as effectively.

## Publishing

Publishing is shared across JS packages and documented in `bindings/javascript/README.md`.
