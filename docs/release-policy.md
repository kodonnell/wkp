# Release policy

This repository has one core implementation and multiple independently versioned deliverables.

## Deliverables

- Python package: `wkp` on PyPI
- JavaScript packages: `@wkpjs/node` and `@wkpjs/web` on npm
- .NET package: `Wkp` on NuGet
- Rust crate: `wkp` on crates.io
- C ABI (`libwkp`) and C++ header: built and tested in CI (`core.yml`), not currently published as a standalone package

All packages share one version, set in `Cargo.toml` (`[workspace.package]`); see
`scripts/check_versions.py`.

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

- `nuget-vX.Y.Z`
  - Runs `.github/workflows/csharp.yml`
  - Builds native libraries for each platform, packs and smoke-tests the NuGet package
  - Publishes `Wkp` to nuget.org (trusted publishing)
  - Creates a .NET-specific GitHub release

- `crates-vX.Y.Z`
  - Runs `.github/workflows/crates.yml`
  - Tests and packages `wkp`, then publishes it to crates.io (trusted publishing)

- `wkp-vX.Y.Z`
  - Runs `.github/workflows/wkp-release.yml`
  - Creates a product-level GitHub release only
  - Does not publish any packages

## Rationale

Packages share one version number but are released independently, so package-tagged releases should describe only the deliverable that actually changed. This avoids re-announcing unchanged Python assets every time JavaScript ships, or unchanged JavaScript assets every time Python ships.

Use `wkp-vX.Y.Z` only when you want an intentional top-level product release note that describes the overall state of WKP across bindings.