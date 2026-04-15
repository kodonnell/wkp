import json
from pathlib import Path
from typing import Generator

import numpy as np
import pytest
import shapely
import wkp

ROOT = Path(__file__).resolve().parents[3]
CASES_ROOT = ROOT / "data" / "integration_tests"


def _get_cases(path: Path) -> Generator[tuple[Path, int, str], None, None]:
    for fpath in path.rglob("*.txt"):
        for idx, line in enumerate(fpath.read_text(encoding="utf-8").splitlines()):
            if not line.strip() or line.startswith("#"):
                continue
            yield fpath, idx, line.strip()


def _parse_floats_encode_case(line: str) -> tuple[list[int], str, str]:
    splat = line.split("\t")
    assert len(splat) == 3, f"invalid case format in {line}: expected 3 tab-separated fields, got {len(splat)}"
    precisions = json.loads(splat[0])
    decoded = json.loads(splat[1])
    encoded = splat[2]
    return precisions, decoded, encoded


@pytest.mark.parametrize("case", _get_cases(CASES_ROOT / "floats" / "encode"), ids=lambda p: f"{p[0].stem}_{p[1]}")
def test_floats_encode_cases(case):
    fpath, idx, line = case
    precisions, to_encode, expected = _parse_floats_encode_case(line)
    actual = wkp.encode_floats(to_encode, precisions).decode("ascii")
    assert actual == expected


def _parse_floats_decode_case(line: str) -> tuple[list[int], str, list[list[float]]]:
    splat = line.split("\t")
    assert len(splat) == 3, f"invalid case format in {line}: expected 3 tab-separated fields, got {len(splat)}"
    precisions = json.loads(splat[0])
    encoded = splat[1]
    decoded = json.loads(splat[2])
    return precisions, encoded, decoded


@pytest.mark.parametrize("case", _get_cases(CASES_ROOT / "floats" / "decode"), ids=lambda p: f"{p[0].stem}_{p[1]}")
def test_floats_decode_cases(case):
    fpath, idx, line = case
    precisions, encoded, expected = _parse_floats_decode_case(line)
    if encoded == "TODO":
        pytest.skip("fixture input is TODO")
    print(encoded, precisions)
    actual = wkp.decode_floats(encoded.encode("ascii"), precisions)
    print(actual)
    print(expected)
    np.testing.assert_allclose(np.asarray(actual), np.asarray(expected), rtol=0, atol=1e-12)


def _parse_geometry_encode_input(line: str) -> tuple[int, str, str]:
    splat = line.split("\t")
    assert len(splat) == 3, f"invalid case format in {line}: expected 3 tab-separated fields, got {len(splat)}"
    precision = int(splat[0])
    wkt = splat[1]
    case_output = splat[2]
    return precision, wkt, case_output


@pytest.mark.parametrize("case", _get_cases(CASES_ROOT / "geometry" / "encode"), ids=lambda p: f"{p[0].stem}_{p[1]}")
def test_geometry_encode_cases(case):
    fpath, idx, line = case
    precision, wkt, case_output = _parse_geometry_encode_input(line)
    geom = shapely.from_wkt(wkt)

    actual = wkp.encode(geom, precision=precision).decode("ascii")

    assert actual == case_output


def _parse_geometry_decode_input(line: str) -> tuple[str, str]:
    splat = line.split("\t")
    assert len(splat) == 2, f"invalid case format in {line}: expected 2 tab-separated fields, got {len(splat)}"
    case_input = splat[0]
    case_output = splat[1]
    return case_input, case_output


@pytest.mark.parametrize("case", _get_cases(CASES_ROOT / "geometry" / "decode"), ids=lambda p: f"{p[0].stem}_{p[1]}")
def test_geometry_decode_cases(case):
    fpath, idx, line = case
    case_input, case_output = _parse_geometry_decode_input(line)
    decoded = wkp.decode(case_input.encode("ascii"))
    expected_geom = shapely.from_wkt(case_output)
    assert decoded.geometry.equals_exact(expected_geom, tolerance=1e-9)
