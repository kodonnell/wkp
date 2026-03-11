#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run(cmd: list[str], cwd: Path) -> None:
    subprocess.run(cmd, cwd=str(cwd), check=True, shell=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build WKP web bindings and serve the docs demo from repo root.")
    parser.add_argument("--port", type=int, default=8080, help="HTTP port (default: 8080)")
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip npm build step for @wkpjs/web",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    js_root = repo_root / "bindings" / "javascript"

    if not args.skip_build:
        print("[docs-demo] Building @wkpjs/web...")
        run(["npm", "--workspace", "@wkpjs/web", "run", "build"], cwd=js_root)

    print(f"[docs-demo] Serving from {repo_root}")
    print(f"[docs-demo] Open: http://localhost:{args.port}/docs/")
    print("[docs-demo] Press Ctrl+C to stop.")

    try:
        run([sys.executable, "-m", "http.server", str(args.port)], cwd=repo_root)
    except KeyboardInterrupt:
        print("\n[docs-demo] Stopped.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
