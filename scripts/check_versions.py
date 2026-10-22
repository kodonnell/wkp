#!/usr/bin/env python3
"""
Check that every package version matches the Cargo workspace version (Cargo.toml
[workspace.package]), which is the single source of truth.

Python takes its version from Cargo automatically (maturin). Everything else is checked here:
the C header, the JavaScript packages and lockfile, and the .NET project, plus the runtime
"core compatibility" ranges in the Python and JavaScript bindings (major.minor must match).

To bump the version: edit Cargo.toml, run `node bindings/javascript/scripts/sync-version.mjs
--write`, then update the remaining files this script reports.

Usage:
    python scripts/check_versions.py
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def find(path: str, pattern: str) -> str:
    match = re.search(pattern, read(path), flags=re.MULTILINE | re.DOTALL)
    if not match:
        raise SystemExit(f"Could not find {pattern!r} in {path}")
    return match.group(1)


def main() -> int:
    version = find("Cargo.toml", r"\[workspace\.package\][^\[]*?^version\s*=\s*\"([^\"]+)\"")
    major_minor = ".".join(version.split(".")[:2])

    exact = {
        "bindings/c/include/wkp/_version.h": find("bindings/c/include/wkp/_version.h", r'WKP_CORE_VERSION\s+"([^"]+)"'),
        "bindings/csharp/src/Wkp/Wkp.csproj": find("bindings/csharp/src/Wkp/Wkp.csproj", r"<Version>([^<]+)</Version>"),
    }
    lock = json.loads(read("bindings/javascript/package-lock.json"))
    exact["bindings/javascript/package-lock.json"] = lock["version"]
    for package in ("", "packages/node", "packages/web"):
        exact[f"bindings/javascript/package-lock.json [{package or 'root'}]"] = lock["packages"][package]["version"]
    for package in ("package.json", "packages/node/package.json", "packages/web/package.json"):
        exact[f"bindings/javascript/{package}"] = json.loads(read(f"bindings/javascript/{package}"))["version"]

    compatible = {
        "bindings/python/python/wkp/_api.py": find(
            "bindings/python/python/wkp/_api.py", r'__core_compatibility__\s*=\s*"([^"]+)"'
        ),
    }
    for package in ("node", "web"):
        path = f"bindings/javascript/packages/{package}/package.json"
        compatible[path] = json.loads(read(path))["wkpCoreCompatibility"]

    errors = [f"{path}: {found} (expected {version})" for path, found in exact.items() if found != version]
    errors += [
        f"{path}: compatibility {found} (expected {major_minor}.x or {version})"
        for path, found in compatible.items()
        if ".".join(found.split(".")[:2]) != major_minor
    ]
    if errors:
        print(f"Version drift from Cargo.toml ({version}):")
        for error in errors:
            print(f"  - {error}")
        return 1
    print(f"All versions match {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
