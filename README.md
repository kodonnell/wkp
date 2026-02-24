[![PyPI version fury.io](https://badge.fury.io/py/wkp.svg)](https://pypi.python.org/pypi/wkp/)

# WKP - Well Known Polylines

WKP is a simple encoding format for geometries that is:

- String based (unlike WKB) which is handy for transmitting to frontend etc.
- Smaller and fast:
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

From `python .\benchmark\benchmark.py --linestring-points=10000`

| Method          | Source   | WKB (kb) | Encode (ms) | Encode (kb) | Decode (ms) | Total (ms) |
| --------------- | -------- | -------- | ----------- | ----------- | ----------- | ---------- |
| shapely_wkb     | nz-coast | 156.3    | 0.600       | 156.3       | 0.121       | 0.721      |
| shapely_wkt     | nz-coast | 156.3    | 2.088       | 379.7       | 6.153       | 8.241      |
| shapely_wkt_5dp | nz-coast | 156.3    | 1.386       | 202.8       | 2.940       | 4.326      |
| wkp-5p-bytes    | nz-coast | 156.3    | 0.256       | 42.9        | 0.200       | 0.456      |
| wkp-5p-str      | nz-coast | 156.3    | 0.257       | 42.9        | 0.188       | 0.445      |
| shapely_wkb     | random   | 156.3    | 0.560       | 156.3       | 0.107       | 0.667      |
| shapely_wkt     | random   | 156.3    | 2.046       | 378.4       | 5.636       | 7.682      |
| shapely_wkt_5dp | random   | 156.3    | 2.015       | 163.9       | 3.870       | 5.885      |
| wkp-5p-bytes    | random   | 156.3    | 0.369       | 72.0        | 0.271       | 0.640      |
| wkp-5p-str      | random   | 156.3    | 0.313       | 72.0        | 0.241       | 0.554      |

## Other features

- Slightly faster `encode_bytes` path. Useful if you're e.g. writing straight to a file you can open as `wb` instead of opening as `w` and then `.decode('ascii')` before writing.
- Utilising functions like `wkp.encode_floats` - these can be used to encode non-geometry data e.g. encode waypoints with timestamps i.e. `wkp.encode_floats([(x0, y0, t0), (x1, y1, t1), ...])`.

## Developing

```sh
git clone --recursive https://github.com/kodonnell/wkp/
USE_CYTHON=1 pip install -e .[dev]
pytest .
```

We use `cibuildwheel` to build all the wheels, which runs in a Github action. If you want to check this succeeds locally, you can try (untested):

```sh
cibuildwheel --platform linux .
```

Finally, when you're happy, submit a PR.

### Publishing

When you're on `main` on your local, `git tag vX.X.X` then `git push origin vX.X.X`. This pushes the tag which triggers the full GitHub Action and:

- Builds source distribution and wheels (for various platforms)
- Pushes to PyPI
- Creates a new release with the appropriate artifacts attached.

### What's up with `./src`?

See [here](https://hynek.me/articles/testing-packaging/) and [here](https://blog.ionelmc.ro/2014/05/25/python-packaging/#the-structure). I didn't read all of it, but yeh, `import wkp` is annoying when there's also a folder called `wkp`.

### `USE_CYTHON=1`?

See [here](https://cython.readthedocs.io/en/latest/src/userguide/source_files_and_compilation.html#distributing-cython-modules). Fair point.

## TODO

- Make `benchmark.py` a CLI in setup.py?
- `setuptools_scm_git_archive`?
- Code completion with stubs?