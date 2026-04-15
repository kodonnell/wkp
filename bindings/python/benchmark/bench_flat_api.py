#!/usr/bin/env python3
"""
Micro-benchmark for the flat-array API paths.

Measures:
  - encode / decode (Shapely round-trip)
  - decode_frame    (numpy round-trip, no Shapely)
  - encode_floats / decode_floats   (list-of-tuples)
  - decode_floats_array             (numpy, if available)

Run before and after the flat-API changes to confirm no regression.

Usage:
    python scripts/bench_flat_api.py [--points=N] [--iterations=N]
"""

import argparse
import sys
import time

import numpy as np
from shapely.geometry import LineString

import wkp


def timeit_ms(fn, warmup: int = 20, n: int = 500) -> float:
    """Return mean elapsed time in ms over n calls (after warmup)."""
    for _ in range(warmup):
        fn()
    t0 = time.perf_counter_ns()
    for _ in range(n):
        fn()
    t1 = time.perf_counter_ns()
    return (t1 - t0) / n / 1_000_000


def header(title: str) -> None:
    print(f"\n{title}")
    print("-" * len(title))


def row(label: str, ms: float, note: str = "") -> None:
    suffix = f"  ({note})" if note else ""
    print(f"  {label:<34} {ms:.3f} ms{suffix}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--points", type=int, default=10_000, help="LineString point count (default: 10000)")
    parser.add_argument("--iterations", type=int, default=500, help="Timed iterations per measurement (default: 500)")
    parser.add_argument("--warmup", type=int, default=20, help="Warmup iterations (default: 20)")
    args = parser.parse_args()

    n = args.iterations
    warmup = args.warmup
    pts = args.points

    print(f"wkp {wkp.__version__}  |  points={pts}  iterations={n}  warmup={warmup}")

    # Build test data
    coords = [[i * 0.001, np.sin(i * 0.001) * 90] for i in range(pts)]
    arr_2d = np.array(coords, dtype=np.float64)
    linestring = LineString(coords)

    enc_geom = wkp.encode(linestring, 5)
    enc_floats = wkp.encode_floats(arr_2d, [5, 5])

    print(f"  encoded geometry size: {len(enc_geom):,} bytes")
    print(f"  encoded floats size:   {len(enc_floats):,} bytes")

    # --- Geometry paths ---
    header("Geometry encode / decode")

    row("encode (->bytes)", timeit_ms(lambda: wkp.encode(linestring, 5), warmup, n))
    row("decode (->Shapely)", timeit_ms(lambda: wkp.decode(enc_geom), warmup, n))
    row("decode_frame (->numpy)", timeit_ms(lambda: wkp.decode_frame(enc_geom), warmup, n))

    # --- Float paths ---
    header("Float encode / decode")

    row("encode_floats (numpy input)", timeit_ms(lambda: wkp.encode_floats(arr_2d, [5, 5]), warmup, n))
    row("decode_floats (->list[tuple])", timeit_ms(lambda: wkp.decode_floats(enc_floats, [5, 5]), warmup, n))

    if hasattr(wkp, "decode_floats_array"):
        row("decode_floats_array (->numpy)", timeit_ms(lambda: wkp.decode_floats_array(enc_floats, [5, 5]), warmup, n))
    else:
        row("decode_floats_array (->numpy)", 0.0, note="NOT YET IMPLEMENTED")

    # --- Overhead breakdown: reshape + list-of-tuples conversion cost ---
    # Measure just the reshape + tolist cost to quantify the overhead in decode_floats
    dims = 2
    flat_arr = wkp.decode_floats_array(enc_floats, [5, 5]) if hasattr(wkp, "decode_floats_array") else \
               np.array(wkp.decode_floats(enc_floats, [5, 5]), dtype=np.float64).ravel()
    header("Overhead: flat (N*dims,) -> list[tuple] (decode_floats cost)")
    row("reshape + tolist", timeit_ms(lambda: [tuple(r) for r in flat_arr.reshape(-1, dims).tolist()], warmup, n),
        note=f"len={len(flat_arr)}")


if __name__ == "__main__":
    main()
