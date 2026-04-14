#!/usr/bin/env python3
"""
Verify that the fixture files on disk match the counts declared in MANIFEST.json.
Run this in CI to catch fixture additions that weren't registered in the manifest,
or manifest entries that no longer have matching files.

Usage:
    python scripts/check_fixture_manifest.py
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CASES_ROOT = ROOT / "data" / "integration_tests"
MANIFEST_PATH = CASES_ROOT / "MANIFEST.json"


def count_cases(filepath: Path) -> int:
    """Count non-blank, non-comment lines in a fixture file."""
    count = 0
    for line in filepath.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            count += 1
    return count


def main() -> int:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    fixtures = manifest["fixtures"]

    errors = []
    all_ok = True

    for category, files in fixtures.items():
        category_path = CASES_ROOT / category
        for filename, expected_count in files.items():
            filepath = category_path / filename
            if not filepath.exists():
                errors.append(f"  MISSING  {category}/{filename} (expected {expected_count} cases)")
                all_ok = False
                continue
            actual_count = count_cases(filepath)
            if actual_count != expected_count:
                errors.append(
                    f"  COUNT MISMATCH  {category}/{filename}: "
                    f"manifest says {expected_count}, file has {actual_count}"
                )
                all_ok = False
            else:
                print(f"  OK  {category}/{filename} ({actual_count} cases)")

    # Check for fixture files on disk that aren't in the manifest
    for category in ["floats/encode", "floats/decode", "geometry/encode", "geometry/decode"]:
        category_path = CASES_ROOT / category
        if not category_path.exists():
            continue
        for filepath in sorted(category_path.glob("*.txt")):
            if filepath.name not in fixtures.get(category, {}):
                errors.append(
                    f"  UNREGISTERED  {category}/{filepath.name} exists on disk but is not in MANIFEST.json"
                )
                all_ok = False

    if errors:
        print("\nFixture manifest check FAILED:")
        for err in errors:
            print(err)
        print(
            "\nIf you added or removed fixture cases, update data/integration_tests/MANIFEST.json "
            "and ensure all language bindings cover the new fixtures."
        )
        return 1

    totals = manifest.get("totals", {})
    print("\nTotals:")
    for category, files in fixtures.items():
        total = sum(files.values())
        declared = totals.get(category, "?")
        status = "OK" if total == declared else f"MISMATCH (manifest says {declared})"
        print(f"  {category}: {total} cases — {status}")

    print("\nFixture manifest check PASSED.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
