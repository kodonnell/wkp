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

All JavaScript package versions are pinned to `core/include/wkp/_version.h`.

From `bindings/javascript`:

```sh
npm run check:version
npm run sync:version
```

## Install

From `bindings/javascript`:

```sh
npm install
```

## Build

```sh
npm run build:node
npm run build:web
```

## Benchmark

```sh
npm run benchmark:node -- --points=10000 --precision=5 --iterations=200
npm run benchmark:web:node -- --points=10000 --precision=5 --iterations=200
npm run benchmark:web:serve
```

Then open `http://localhost:8080/benchmark/index.html` for browser benchmarking.

## Publishing

- npm publishing automation lives in `.github/workflows/npm-publish.yml`
- designed for manual publish first (`workflow_dispatch`), with optional tag-trigger use
- uses npm trusted publishing (GitHub OIDC), so no long-lived `NPM_TOKEN` is required

### Trusted publishing setup (npm)

1. In npm, create packages `@wkpjs/node` and `@wkpjs/web` (or first publish manually once if needed).
2. In npm package settings, add a **Trusted Publisher** for this GitHub repo/workflow:
	- Repository: `kodonnell/wkp`
	- Workflow: `.github/workflows/npm-publish.yml`
	- Environment (if configured): `npm`
3. In GitHub, keep workflow permission `id-token: write`.
4. Run workflow manually with `dry_run=true` first, then run again with `dry_run=false`.
