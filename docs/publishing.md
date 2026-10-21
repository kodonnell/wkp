# Publishing

Every registry is published from GitHub Actions with **trusted publishing**: the registry trusts
a specific workflow file in `kodonnell/wkp`, and the workflow exchanges a short-lived GitHub OIDC
token for a one-off credential. No long-lived API keys are stored in the repository.

Releases are triggered by pushing a tag (see [release-policy.md](release-policy.md)). All
packages share the version in the root `Cargo.toml`; bump it there, run
`python scripts/check_versions.py`, commit, then tag.

| Registry  | Package(s)                    | Tag               | Workflow       | GitHub environment |
| --------- | ----------------------------- | ----------------- | -------------- | ------------------ |
| PyPI      | `wkp`                         | `pypi-vX.Y.Z`     | `python.yml`   | `pypi`             |
| npm       | `@wkpjs/node`, `@wkpjs/web`   | `npm-vX.Y.Z`      | `js.yml`       | `npm`              |
| NuGet     | `Wkp`                         | `nuget-vX.Y.Z`    | `csharp.yml`   | `nuget`            |
| crates.io | `wkp`                         | `crates-vX.Y.Z`   | `crates.yml`   | `crates`           |

PyPI and npm are already set up. NuGet and crates.io need the one-time setup below.

## NuGet (one-time setup)

1. **Account.** Sign in at <https://www.nuget.org> with a Microsoft account (personal is fine).
   Your nuget.org *username* (profile name, not the email) is what the workflow sends.
2. **Trusted publishing policy.** Click your username, then **Trusted Publishing**, then add a policy:
   - Repository owner: `kodonnell`
   - Repository: `wkp`
   - Workflow file: `csharp.yml` (file name only)
   - Environment: `nuget`
   - Policy owner: your user (or an organization, if you make one)

   Policies are scoped to the owner, not to a package, so this also covers the **first** push of
   the new `Wkp` package ID. No manual first upload is needed. For a private repo, the policy is
   only provisionally active for 7 days until the first successful publish locks it to the repo.
3. **GitHub.** In the repo settings:
   - Create an environment named `nuget`. Optionally add a required reviewer, so each release
     waits for a click.
   - Add a repository *variable* (not a secret) `NUGET_USER` set to your nuget.org username.
4. **Release.** Push a `nuget-vX.Y.Z` tag. The workflow builds the native libraries for six
   platforms, packs, smoke-tests the package on Linux, Windows and macOS, then pushes.

Notes:

- Package IDs are first-come, first-served and claimed by the first push. `Wkp` was free at the
  time of writing.
- A verified `Wkp.*` *prefix reservation* (the blue tick) is optional. It is requested by email
  to account@nuget.org and is granted only for recognisable owners/projects.
- A pushed version cannot be deleted, only *unlisted*, and its version number can never be
  reused. Practise with `workflow_dispatch` (which packs and tests but does not publish) first.
- Fallback without trusted publishing: create an API key on nuget.org (scoped to `Wkp`), store it
  as a `NUGET_API_KEY` secret in the `nuget` environment, and pass that to `dotnet nuget push`
  instead of `steps.login.outputs.NUGET_API_KEY`.

## crates.io (one-time setup)

crates.io cannot configure trusted publishing for a crate that does not exist yet, so the first
release is done by hand.

1. **Account.** Sign in at <https://crates.io> with GitHub, and verify your email address
   (**Account Settings**). Publishing is refused until the email is verified.
2. **First publish (manual).** Create an API token under **Account Settings → API Tokens** with
   the `publish-new` scope and a short expiry. Then, from a clean checkout of the release commit:

   ```bash
   cargo login            # paste the token
   cargo test -p wkp --release
   cargo publish -p wkp --dry-run
   cargo publish -p wkp
   ```

   Revoke the token afterwards.
3. **Trusted publishing.** On the crate's page on crates.io, open **Settings → Trusted
   Publishing**, then add:
   - Repository owner: `kodonnell`
   - Repository name: `wkp`
   - Workflow filename: `crates.yml`
   - Environment: `crates`

   Optionally, also tick the setting that disables token-based publishing for the crate.
4. **GitHub.** Create an environment named `crates`.
5. **Later releases.** Push a `crates-vX.Y.Z` tag.

Notes:

- Only `wkp` is published. The binding crates (`wkp-capi`, `wkp-py`, `wkp-node`, `wkp-wasm`)
  are `publish = false`.
- Crate names are first-come, first-served. `wkp` was free at the time of writing; it is claimed
  by the first publish.
- Published versions cannot be deleted, only *yanked*. Yanking stops new dependents but keeps
  the download available.
- `cargo publish` only packages files inside `core/`. The golden and reference tests travel with
  the crate. `tests/fixtures.rs` and the benchmark example read `../data`, so they only run in
  the repo.

## PyPI and npm (already configured)

These are recorded here so the setup can be recreated:

- **PyPI.** Trusted publishers for `wkp` on pypi.org and test.pypi.org: owner `kodonnell`, repo
  `wkp`, workflow `python.yml`, environment `pypi`.
- **npm.** Trusted publishers for `@wkpjs/node` and `@wkpjs/web`: repo `kodonnell/wkp`,
  workflow `js.yml`. The `npm` environment gates the publish job. `js.yml` has a `dry_run` input
  for rehearsals.
