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

## How to release

Every registry is published from GitHub Actions with trusted publishing: the registry trusts one
workflow file in `kodonnell/wkp` (and its GitHub environment), so no API keys are stored in the
repository.

| Registry  | Package(s)                  | Tag             | Workflow     | GitHub environment |
| --------- | --------------------------- | --------------- | ------------ | ------------------ |
| PyPI      | `wkp`                       | `pypi-vX.Y.Z`   | `python.yml` | `pypi`             |
| npm       | `@wkpjs/node`, `@wkpjs/web` | `npm-vX.Y.Z`    | `js.yml`     | `npm`              |
| NuGet     | `Wkp`                       | `nuget-vX.Y.Z`  | `csharp.yml` | `nuget`            |
| crates.io | `wkp`                       | `crates-vX.Y.Z` | `crates.yml` | `crates`           |

1. Bump `version` under `[workspace.package]` in the root `Cargo.toml`.
2. Run `node bindings/javascript/scripts/sync-version.mjs --write`, then
   `python scripts/check_versions.py`, and update anything else it reports.
3. Update `CHANGELOG.md`, and move the entries of
   `bindings/csharp/src/Wkp/PublicAPI.Unshipped.txt` to `PublicAPI.Shipped.txt` (the .NET public API
   that is now released). Commit, push `main`, and wait for CI to pass.
4. Tag and push each package being released, **one tag per push** (GitHub starts no workflows
   when more than three tags are pushed at once):

   ```sh
   git tag pypi-vX.Y.Z && git push origin pypi-vX.Y.Z
   git tag npm-vX.Y.Z && git push origin npm-vX.Y.Z
   git tag nuget-vX.Y.Z && git push origin nuget-vX.Y.Z
   git tag crates-vX.Y.Z && git push origin crates-vX.Y.Z
   ```

Rehearsals: running a workflow manually (Actions, then **Run workflow**) behaves differently per
registry:

- `csharp.yml` and `crates.yml` build, test and package, but never publish.
- `js.yml` publishes unless the `dry_run` input is ticked.
- `python.yml` **publishes** (TestPyPI, then PyPI).

Published versions are permanent: NuGet versions can only be unlisted, crates.io versions only
yanked (existing users can still download them), and npm and PyPI never allow a version number
to be reused. Publishing the npm packages also redeploys GitHub Pages with the new
`@wkpjs/web`.

Only the `wkp` crate is published; the binding crates (`wkp-capi`, `wkp-py`, `wkp-node`,
`wkp-wasm`) are `publish = false`. The crate contains only `core/`: the golden and reference
tests travel with it, but `tests/fixtures.rs` and the benchmark example read `../data`, so they
only run in the repo.

## GitHub Pages

The demo (`docs/`) is deployed by `pages.yml` on every push to `main`, after each npm release,
and on demand (Actions, **Deploy GitHub Pages**, **Run workflow**, optionally with a
`@wkpjs/web` version). It loads `@wkpjs/web` from npm (the latest version unless one is given),
not from the repo.

## Rationale

Packages share one version number but are released independently, so package-tagged releases should describe only the deliverable that actually changed. This avoids re-announcing unchanged Python assets every time JavaScript ships, or unchanged JavaScript assets every time Python ships.

Use `wkp-vX.Y.Z` only when you want an intentional top-level product release note that describes the overall state of WKP across bindings.