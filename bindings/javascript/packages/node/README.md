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

- `Context`
- `decodeHeader(encoded)`
- `decode(ctx, encoded)`
- `encode(ctx, geometry, precision)`
- `encodeFloats(ctx, floats, precisions)`
- `decodeFloats(ctx, encoded, precisions)`

## Example

```js
const { Context, decode, encode } = require('@wkpjs/node');

const ctx = new Context();
const geometry = {
  type: 'LineString',
  coordinates: [[174.776, -41.289], [174.777, -41.290], [174.778, -41.291]],
};

const encoded = encode(ctx, geometry, 6);
const decoded = decode(ctx, encoded);

console.log(encoded);
console.log(decoded.geometry);
```

`ctx` is required for encode/decode operations.

## Publishing

Publishing is shared across JS packages and documented in `bindings/javascript/README.md`.
