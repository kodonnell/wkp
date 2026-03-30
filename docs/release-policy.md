# Release policy

This repository has one core implementation and multiple independently versioned deliverables.

## Deliverables

- Python package: `wkp` on PyPI
- JavaScript packages: `@wkpjs/node` and `@wkpjs/web` on npm
- C++ binding and core artifacts: built in CI, not currently published as a standalone package

## Tag families

- `pypi-vX.Y.Z`
  - Runs `.github/workflows/python.yml`
  - Builds Python artifacts
  - Creates a Python-specific GitHub release
  - Publishes to TestPyPI, then PyPI

- `npm-vX.Y.Z`
  - Runs `.github/workflows/js.yml`
  - Builds JavaScript artifacts
  - Creates a JavaScript-specific GitHub release
  - Publishes `@wkpjs/node` and `@wkpjs/web` to npm

- `wkp-vX.Y.Z`
  - Runs `.github/workflows/wkp-release.yml`
  - Creates a product-level GitHub release only
  - Does not publish Python or JavaScript packages

## Rationale

Binding package versions are managed independently, so package-tagged releases should describe only the deliverable that actually changed. This avoids re-announcing unchanged Python assets every time JavaScript ships, or unchanged JavaScript assets every time Python ships.

Use `wkp-vX.Y.Z` only when you want an intentional top-level product release note that describes the overall state of WKP across bindings.