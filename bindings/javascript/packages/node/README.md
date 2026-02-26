# @wkp/node

Node.js native bindings for WKP core using Node-API.

## Dependencies

- Node.js >= 18
- npm workspace install from `bindings/javascript`
- C++ build toolchain compatible with `node-gyp`

## API

- `encodeF64(values, dimensions, precisions) -> Buffer`
- `decodeF64(encoded, dimensions, precisions) -> Float64Array`

`values` can be `Float64Array` or `number[]`.
`encoded` can be `Buffer`, `Uint8Array`, or `string`.

## Build

From `bindings/javascript`:

```sh
npm install
npm --workspace @wkp/node run build
```

## Smoke test

```sh
node -e "const w=require('./bindings/javascript/packages/node'); const b=w.encodeF64([1,2,3,4],2,[5]); const d=w.decodeF64(b,2,[5]); console.log(b.length, d.length)"
```

## Benchmark

From `bindings/javascript`:

```sh
npm --workspace @wkp/node run benchmark -- --points=10000 --precision=5 --iterations=200
```

## Publish

Package publishing is automated by `.github/workflows/npm-publish.yml`.
