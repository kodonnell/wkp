#!/usr/bin/env python3
"""Build and test the Rust core and every binding. Use --skip-* to leave parts out."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    print(f"[run] {' '.join(cmd)} (cwd={cwd})")
    subprocess.run(cmd, cwd=str(cwd), env=env, check=True)


def resolve_tool(name: str) -> str:
    candidates: list[str]
    if os.name == "nt":
        candidates = [f"{name}.exe", f"{name}.cmd", f"{name}.bat", name]
    else:
        candidates = [name]

    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved

    raise RuntimeError(f"Required tool '{name}' was not found on PATH. Install it or add it to PATH, then retry.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build WKP core + bindings")
    parser.add_argument("--skip-rust", action="store_true", help="Skip cargo tests of the core and C ABI")
    parser.add_argument("--skip-cpp", action="store_true", help="Skip the CMake C/C++ build")
    parser.add_argument("--skip-cpp-tests", action="store_true", help="Skip C/C++ tests")
    parser.add_argument("--skip-python", action="store_true", help="Skip Python binding build/tests")
    parser.add_argument("--skip-js", action="store_true", help="Skip JavaScript builds/tests")
    parser.add_argument("--skip-csharp", action="store_true", help="Skip .NET build/tests")
    parser.add_argument("--build-type", default="Release", choices=["Release", "Debug"], help="CMake build type")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    build_dir = repo / "build" / "cmake"
    cargo = resolve_tool("cargo")

    if not args.skip_rust:
        run([cargo, "test"], repo)
        run([sys.executable, "scripts/check_versions.py"], repo)
        run([sys.executable, "scripts/check_fixture_manifest.py"], repo)

    if not args.skip_cpp:
        cmake = resolve_tool("cmake")
        run(
            [
                cmake,
                "-S",
                ".",
                "-B",
                str(build_dir),
                f"-DCMAKE_BUILD_TYPE={args.build_type}",
                "-DWKP_BUILD_TESTS=ON",
                "-DWKP_BUILD_BENCHMARKS=ON",
            ],
            repo,
        )
        run([cmake, "--build", str(build_dir), "--config", args.build_type], repo)

        if not args.skip_cpp_tests:
            ctest = resolve_tool("ctest")
            run([ctest, "--test-dir", str(build_dir), "-C", args.build_type, "--output-on-failure"], repo)

    if not args.skip_python:
        py = sys.executable
        run([py, "-m", "pip", "install", "-e", "bindings/python[dev]"], repo)
        run([py, "-m", "pytest", "bindings/python/tests"], repo)

    if not args.skip_js:
        npm = resolve_tool("npm")
        js_root = repo / "bindings" / "javascript"
        run([npm, "install"], js_root)
        run([npm, "run", "build:node"], js_root)
        run([npm, "run", "build:web"], js_root)
        run([npm, "run", "test"], js_root)
        run([npm, "run", "check:runtime-compatibility"], js_root)

    if not args.skip_csharp:
        dotnet = resolve_tool("dotnet")
        run([dotnet, "test", "bindings/csharp/tests/Wkp.Tests", "-c", "Release"], repo)

    print("[ok] build_all complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
