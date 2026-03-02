import random
import time
from collections import OrderedDict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import List

import shapely
import shapely.wkb
import shapely.wkt
from wkp import GeometryEncoder


@dataclass
class TestResult:
    method: str
    wkb_size: int
    encode_ms: float
    encode_size: float
    decode_ms: float
    iterations: int


def timeit(f, warmup=3, max_iterations=10, max_duration=None):
    for _ in range(warmup):
        f()
    t0 = time.time()
    iterations = 0
    x = None
    while iterations < max_iterations:
        if max_duration is not None and (time.time() - t0) >= max_duration:
            break
        x = f()
        iterations += 1
    elapsed = time.time() - t0
    encode_ms = (elapsed / iterations * 1000) if iterations > 0 else 0.0
    return encode_ms, x, iterations


def make_geom_pool(geom, count):
    """
    Make a bunch of geometries to encode. Why? Because it seems like shapely can cache some things e.g. wkb, and for
    fairness we want to avoid that. So we make a bunch of identical geometries, which should be different objects in
    memory, and encode those.
    """
    assert isinstance(geom, shapely.geometry.LineString)
    coords = shapely.get_coordinates(geom)
    return [shapely.linestrings(coords) for _ in range(count)]


def timeit_with_pool(pool, f, warmup=3, max_iterations=10, max_duration=None):
    for i in range(warmup):
        f(pool[i])
    t0 = time.time()
    iterations = 0
    x = None
    pool_len = len(pool)
    i = warmup
    while iterations < max_iterations:
        if max_duration is not None and (time.time() - t0) >= max_duration:
            break
        x = f(pool[i % pool_len])
        i += 1
        iterations += 1
    elapsed = time.time() - t0
    encode_ms = (elapsed / iterations * 1000) if iterations > 0 else 0.0
    return encode_ms, x, iterations


def bench_wkp(geom, dimensions, precision, warmup=3, max_iterations=10, max_duration=None, as_bytes=False):
    encoder = GeometryEncoder(precision=precision, dimensions=dimensions)
    geom_pool = make_geom_pool(geom, warmup + max_iterations)
    if as_bytes:
        encode_ms, payload, encode_iters = timeit_with_pool(
            geom_pool,
            lambda g: encoder.encode_bytes(g),
            warmup=warmup,
            max_iterations=max_iterations,
            max_duration=max_duration,
        )
    else:
        encode_ms, payload, encode_iters = timeit_with_pool(
            geom_pool,
            lambda g: encoder.encode(g),
            warmup=warmup,
            max_iterations=max_iterations,
            max_duration=max_duration,
        )

    if as_bytes:
        decode_ms, decoded, decode_iters = timeit(
            lambda: encoder.decode_bytes(payload), warmup=warmup, max_iterations=encode_iters, max_duration=max_duration
        )
    else:
        decode_ms, decoded, decode_iters = timeit(
            lambda: encoder.decode(payload), warmup=warmup, max_iterations=encode_iters, max_duration=max_duration
        )

    assert decoded.geometry.equals_exact(geom, tolerance=10 ** (-precision)), (
        f"Decoded geometry does not match original for test wkp-{dimensions}d-{precision}p"
    )
    yield TestResult(
        f"wkp-{precision}p{'-bytes' if as_bytes else '-str'}",
        wkb_size=len(geom.wkb),
        encode_ms=encode_ms,
        encode_size=len(payload),
        decode_ms=decode_ms,
        iterations=encode_iters + decode_iters,
    )


def bench_shapely_wkb(geom, warmup=3, max_iterations=10, max_duration=None):
    geom_pool = make_geom_pool(geom, warmup + max_iterations)
    encode_ms, bites, encode_iters = timeit_with_pool(
        geom_pool, lambda g: g.wkb, warmup=warmup, max_iterations=max_iterations, max_duration=max_duration
    )
    decode_ms, decoded, decode_iters = timeit(
        lambda: shapely.wkb.loads(bites), warmup=warmup, max_iterations=encode_iters, max_duration=max_duration
    )
    assert decoded.equals_exact(geom, tolerance=0), "Decoded geometry does not match original for test shapely_wkb"
    yield TestResult(
        "shapely_wkb",
        wkb_size=len(geom.wkb),
        encode_ms=encode_ms,
        encode_size=len(bites),
        decode_ms=decode_ms,
        iterations=encode_iters + decode_iters,
    )


def bench_shapely_wkt(geom, precision, warmup=3, max_iterations=10, max_duration=None):
    geom_pool = make_geom_pool(geom, warmup + max_iterations)
    encode_ms, bites, encode_iters = timeit_with_pool(
        geom_pool, lambda g: g.wkt, warmup=warmup, max_iterations=max_iterations, max_duration=max_duration
    )
    decode_ms, decoded, decode_iters = timeit(
        lambda: shapely.wkt.loads(bites), warmup=warmup, max_iterations=encode_iters, max_duration=max_duration
    )
    if precision is not None:
        assert decoded.equals_exact(geom, tolerance=10 ** (-precision)), (
            "Decoded geometry does not match original for test shapely_wkt"
        )
    yield TestResult(
        f"shapely_wkt_{precision}dp" if precision is not None else "shapely_wkt",
        wkb_size=len(geom.wkb),
        encode_ms=encode_ms,
        encode_size=len(bites),
        decode_ms=decode_ms,
        iterations=encode_iters + decode_iters,
    )


def create_random_geometry(num_points):
    coords = [(random.random(), random.random()) for _ in range(num_points)]
    return shapely.geometry.LineString(coords)


def load_realistic_geometry(num_points):
    """
    In this case, load the largest component of the coastline of NZ.
    Source: https://data-niwa.opendata.arcgis.com/datasets/NIWA::coastline/explore
    Grabbed the North Island then simplified as `.simplify(tolerance=0.001, preserve_topology=False)`
    """
    with open(Path(__file__).parents[3] / "data" / "nz-north-island-coastline-simp-0.001.bin", "rb") as f:
        wkb = f.read()
    geom = shapely.wkb.loads(wkb)
    assert isinstance(geom, shapely.geometry.LineString)
    coords = list(geom.coords)
    if len(coords) > num_points:
        # return first N:
        return shapely.geometry.LineString(coords[:num_points])
    else:
        # Just repeat them - but go along linestring, then back (in reverse) and so on, until we have enough:
        coords = coords + coords[::-1]
        while len(coords) < num_points:
            coords = coords + coords[::-1]
        coords = coords[:num_points]
        return shapely.geometry.LineString(coords)


def bench_methods(linestring, precisions, warmup=3, max_iterations=10, max_duration=None):
    for precision in sorted(precisions):
        print(f"{time.monotonic()} testing wkp with precision={precision}", end="\r")
        yield from bench_wkp(
            geom=linestring,
            dimensions=2,
            precision=precision,
            warmup=warmup,
            max_iterations=max_iterations,
            max_duration=max_duration,
        )
        yield from bench_wkp(
            geom=linestring,
            dimensions=2,
            precision=precision,
            warmup=warmup,
            max_iterations=max_iterations,
            max_duration=max_duration,
            as_bytes=True,
        )

        # Now round it:
        rounded = shapely.geometry.LineString(
            [(round(x, precision), round(y, precision)) for x, y in linestring.coords]
        )
        print(f"{time.monotonic()} testing rounded shapely wkt at precision={precision}", end="\r")
        yield from bench_shapely_wkt(
            geom=rounded, precision=precision, warmup=warmup, max_iterations=max_iterations, max_duration=max_duration
        )

    # Shapely wkb and wkt on the *unrounded* geometry - but it's unaffected by precision.
    print(f"{time.monotonic()} testing shapely wkb", end="\r")
    yield from bench_shapely_wkb(
        geom=linestring, warmup=warmup, max_iterations=max_iterations, max_duration=max_duration
    )

    # Shapely wkt on the unrounded geometry - but it's unaffected by precision.
    print(f"{time.monotonic()} testing shapely wkt at full precision", end="\r")
    yield from bench_shapely_wkt(
        geom=linestring, precision=None, warmup=warmup, max_iterations=max_iterations, max_duration=max_duration
    )

    print(f"{time.monotonic()} done testing", end="\r")


def totable(results: List[TestResult]):
    # Sort:
    results = sorted(results, key=lambda x: (x["source"], x["method"]))

    pretty_names = OrderedDict(
        method="Method",
        source="Source",
        wkb_size="WKB (kb)",
        encode_ms="Encode (ms)",
        encode_size="Encode (kb)",
        decode_ms="Decode (ms)",
        total_ms="Total (ms)",
        iterations="Iters",
    )

    # Convert to dicts of strings
    items = []
    for res in results:
        item = {}
        for name, v in res.items():
            if name.endswith("_size"):
                v = f"{v / 1024:.1f}"
            elif name.endswith("_ms"):
                v = f"{v:.3f}"
            item[name] = str(v)
        item["total_ms"] = f"{float(item['encode_ms']) + float(item['decode_ms']):.3f}"
        items.append(item)

    # Add header:
    items.insert(0, pretty_names)

    # Get max length:
    max_lens = {}
    for k in items[0]:
        max_lens[k] = max(len(item[k]) for item in items)

    # Print:
    for idx, item in enumerate(items):
        vals = []
        for k in pretty_names:
            vals.append(item.get(k, "").ljust(max_lens[k]))
        print("| " + " | ".join(vals) + " |")
        if idx == 0:
            vals = []
            for k in pretty_names:
                vals.append("-" * max_lens[k])
            print("| " + " | ".join(vals) + " |")


def progress(iter):
    for res in iter:
        s = f"method={res.method}"
        print(s.ljust(80), end="\r")
        yield res


def benchmark(warmup, max_iterations, max_duration, linestring_points, precisions):
    random_linestring = create_random_geometry(linestring_points)
    realistic_linestring = load_realistic_geometry(linestring_points)
    results = []
    for src_name, linestring in [("random", random_linestring), ("nz-coast", realistic_linestring)]:
        print(f"{time.monotonic()} testing source={src_name}", end="\r")
        for r in progress(
            bench_methods(
                linestring=linestring,
                precisions=precisions,
                warmup=warmup,
                max_iterations=max_iterations,
                max_duration=max_duration,
            )
        ):
            results.append({**asdict(r), "source": src_name})
    totable(results)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--linestring-points", type=int, default=1000, help="Number of points in the linestring")
    parser.add_argument(
        "--precisions",
        type=int,
        nargs="+",
        default=[5],
        help="Number of decimal places to preserve in encoding",
    )
    parser.add_argument("--warmup", type=int, default=10, help="Warmup iterations")
    parser.add_argument("--max-iterations", type=int, default=1000, help="Maximum test iterations")
    parser.add_argument("--max-duration", type=float, default=1, help="Maximum duration per test (seconds)")
    args = parser.parse_args()
    benchmark(
        linestring_points=args.linestring_points,
        precisions=args.precisions,
        warmup=args.warmup,
        max_iterations=args.max_iterations,
        max_duration=args.max_duration,
    )
