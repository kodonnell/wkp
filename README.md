[![PyPI version fury.io](https://badge.fury.io/py/wkp.svg)](https://pypi.python.org/pypi/wkp/)

# WKP - Well Known Polylines

WKP is a simple encoding format for geometries that is:

- String based (unlike WKB) which is handy for transmitting to frontend etc.
- Small and fast:
  - WKT is generally 10x larger and 20x slower[^1].
  - WKB is generally 4x larger and 1.5-2x slower[^1].
- Based on the [Google Polyline algorithm](https://developers.google.com/maps/documentation/utilities/polylinealgorithm) but supports:
  - More coordinates e.g. Z.
  - Customisable precision whereas polylines only supports 5 d.p. This allows using coordinate systems other than WGS84.
  - Encoding polygons (as sequences of polylines).

[^1]: From `python .\benchmark\benchmark.py --linestring-points=10000` the realistic data from the `nz-coast` source has WKB: .72ms / 156kb, WKT (full precision): 8.24ms / 380kb, WKP: .45ms / 43kb. This assumes 5dp encoding (standard for Google Polyline) and that the WKT is exported at full-precision *not* truncated to 5dp (because this is the default behaviour when you just call `geom.wkt` in shapely).

## Install

```sh
pip install wkp
```

## Example

```python
from shapely import LineString
from wkp import GeometryEncoder

linestring = LineString([(1, 2), (3, 4), (5, 6)])
encoder = GeometryEncoder(dimensions=2, precision=5)
print(encoder.encode(linestring)) # 01050202_ibE_seK_seK_seK_seK_seK
```

## (Single-threaded) Benchmarks

From `python .\benchmark\benchmark.py --linestring-points=10000 --precisions=5`

| Method          | Source   | WKB (kb) | Encode (ms) | Encode (kb) | Decode (ms) | Total (ms) |
| --------------- | -------- | -------- | ----------- | ----------- | ----------- | ---------- |
| shapely_wkb     | nz-coast | 156.3    | 0.568       | 156.3       | 0.126       | 0.694      |
| shapely_wkt     | nz-coast | 156.3    | 1.900       | 379.7       | 5.459       | 7.359      |
| shapely_wkt_5dp | nz-coast | 156.3    | 1.421       | 202.8       | 3.126       | 4.547      |
| wkp-5p-bytes    | nz-coast | 156.3    | 0.275       | 42.9        | 0.157       | 0.432      |
| wkp-5p-str      | nz-coast | 156.3    | 0.247       | 42.9        | 0.160       | 0.407      |
| shapely_wkb     | random   | 156.3    | 0.495       | 156.3       | 0.095       | 0.590      |
| shapely_wkt     | random   | 156.3    | 1.840       | 378.4       | 5.037       | 6.877      |
| shapely_wkt_5dp | random   | 156.3    | 1.464       | 163.8       | 2.801       | 4.265      |
| wkp-5p-bytes    | random   | 156.3    | 0.309       | 72.0        | 0.182       | 0.491      |
| wkp-5p-str      | random   | 156.3    | 0.307       | 72.0        | 0.161       | 0.468      |

## Other features

- Slightly faster `encode_bytes` path. Useful if you're e.g. writing straight to a file you can open as `wb` instead of opening as `w` and then `.decode('ascii')` before writing.
- Utilising functions like `wkp.encode_floats` - these can be used to encode non-geometry data e.g. encode waypoints with timestamps i.e. `wkp.encode_floats([(x0, y0, t0), (x1, y1, t1), ...])`.

## Developing

```sh
git clone --recursive https://github.com/kodonnell/wkp/
pip install -e ./bindings/python[dev]
pytest bindings/python/tests
```

We use `cibuildwheel` to build all the wheels, which runs in a Github action. If you want to check this succeeds locally, you can try (untested):

```sh
cibuildwheel --platform linux bindings/python
```

Finally, when you're happy, submit a PR.

## C++ core

The repo now includes a standalone C++ core under `core/` with:

- `wkp_core_static` static library + `wkp_core` shared library
- C++ API (`core/include/wkp/core.hpp`)
- C ABI for future bindings (`core/include/wkp/core.h`)
- Cross-platform tests via CTest (`core/tests/test_vectors.cpp`, `core/tests/test_c_api.cpp`, `core/tests/test_errors.cpp`)
- Benchmark executable (`core/bench/benchmark_core.cpp`)

See `core/README.md` for complete core build, test, and benchmark documentation.

Build + test:

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DWKP_BUILD_TESTS=ON -DWKP_BUILD_BENCHMARKS=ON
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

Run benchmark executable directly:

```sh
build/core/Release/wkp_core_benchmark 200000 2 5 20
```

### Publishing

When you're on `main` on your local, `git tag vX.X.X` then `git push origin vX.X.X`. This pushes the tag which triggers the full GitHub Action and:

- Builds source distribution and wheels (for various platforms)
- Pushes to PyPI
- Creates a new release with the appropriate artifacts attached.

### What's up with `./src`?

See [here](https://hynek.me/articles/testing-packaging/) and [here](https://blog.ionelmc.ro/2014/05/25/python-packaging/#the-structure). I didn't read all of it, but yeh, `import wkp` is annoying when there's also a folder called `wkp`.

## Stubs

Generate stubs for the compiled core extension:

```sh
wkp-stubgen
```

This writes `bindings/python/src/wkp/_core.pyi`.

## TODO

- javascript support and add to `npm`.
- Make `benchmark.py` a CLI in bindings/python/setup.py?
- Code completion with stubs?