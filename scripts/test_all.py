#!/usr/bin/env python3
"""
Run the full WKP test suite across all language bindings.

Usage:
    python scripts/test_all.py
    python scripts/test_all.py --skip-web        # skip web/WASM (requires built dist)
    python scripts/test_all.py --skip-cpp        # skip C++ ctest
    python scripts/test_all.py --integration     # include slow integration fixture tests
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
JS_ROOT = REPO / "bindings" / "javascript"
NODE_PKG = JS_ROOT / "packages" / "node"
WEB_PKG = JS_ROOT / "packages" / "web"
CPP_BUILD = REPO / "build" / "core"


def _run(label: str, cmd: list[str], cwd: Path) -> tuple[bool, float]:
    print(f"\n{'='*60}")
    print(f"  {label}")
    print(f"{'='*60}")
    t0 = time.monotonic()
    result = subprocess.run(cmd, cwd=str(cwd))
    elapsed = time.monotonic() - t0
    ok = result.returncode == 0
    status = "PASSED" if ok else "FAILED"
    print(f"\n[{status}] {label}  ({elapsed:.1f}s)")
    return ok, elapsed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--skip-python", action="store_true", help="Skip Python tests")
    parser.add_argument("--skip-node", action="store_true", help="Skip Node.js tests")
    parser.add_argument("--skip-web", action="store_true", help="Skip Web/WASM tests")
    parser.add_argument("--skip-cpp", action="store_true", help="Skip C++ ctest")
    parser.add_argument("--integration", action="store_true", help="Include slow integration fixture tests (Python)")
    args = parser.parse_args()

    results: list[tuple[str, bool, float]] = []

    # --- Python ---
    if not args.skip_python:
        pytest_cmd = [sys.executable, "-m", "pytest", "bindings/python/tests", "-v"]
        if not args.integration:
            pytest_cmd += ["--ignore=bindings/python/tests/test_integration_csv.py"]
        ok, t = _run("Python (pytest)", pytest_cmd, REPO)
        results.append(("Python", ok, t))

    # --- Node ---
    if not args.skip_node:
        ok, t = _run(
            "Node.js (node --test)",
            ["node", "--test", "./tests/*.js"],
            NODE_PKG,
        )
        results.append(("Node.js", ok, t))

    # --- Web ---
    if not args.skip_web:
        web_dist = WEB_PKG / "dist" / "wkp_core.js"
        if not web_dist.exists():
            print(f"\n[SKIP] Web/WASM — dist not built ({web_dist} missing). Run `npm --workspace @wkpjs/web run build` first.")
            results.append(("Web/WASM", False, 0.0))
        else:
            # pretest generates the version module
            subprocess.run(["node", "./scripts/generate-version-module.mjs"], cwd=str(WEB_PKG), check=True)
            ok, t = _run(
                "Web/WASM (node --test)",
                ["node", "--test", "./tests/*.mjs"],
                WEB_PKG,
            )
            results.append(("Web/WASM", ok, t))

    # --- C++ ---
    if not args.skip_cpp:
        if not CPP_BUILD.exists():
            print(f"\n[SKIP] C++ — build dir not found ({CPP_BUILD}). Run `python scripts/build_all.py` first.")
            results.append(("C++", False, 0.0))
        else:
            ok, t = _run(
                "C++ (ctest)",
                ["ctest", "--test-dir", str(CPP_BUILD), "-C", "Release", "--output-on-failure"],
                REPO,
            )
            results.append(("C++", ok, t))

    # --- Summary ---
    print(f"\n{'='*60}")
    print("  Summary")
    print(f"{'='*60}")
    all_passed = True
    for name, ok, elapsed in results:
        status = "PASSED" if ok else "FAILED"
        print(f"  {status:<8}  {name}  ({elapsed:.1f}s)")
        if not ok:
            all_passed = False

    print()
    if all_passed:
        print("All test suites passed.")
        return 0
    else:
        print("One or more test suites FAILED.")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
