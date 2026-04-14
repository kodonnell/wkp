"""Tests for GeometryFrame public API: decode_frame, encode_frame, to_geometry, to_buffer, from_buffer."""

import numpy as np
import pytest
import shapely
import wkp
from shapely.geometry import LineString, MultiPolygon, Point, Polygon


# ---------------------------------------------------------------------------
# decode_frame / encode_frame roundtrip
# ---------------------------------------------------------------------------

@pytest.mark.parametrize(
    "geom",
    [
        Point(174.776, -41.289),
        LineString([(0, 0), (1, 1), (2, 2)]),
        Polygon([(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)], [[(2, 2), (4, 2), (4, 4), (2, 4), (2, 2)]]),
        shapely.multipoints([(0, 0), (1, 1), (2, 2)]),
        shapely.multilinestrings([[(0, 0), (1, 1)], [(2, 2), (3, 3)]]),
        MultiPolygon([
            Polygon([(0, 0), (2, 0), (2, 2), (0, 2), (0, 0)]),
            Polygon([(4, 4), (8, 4), (8, 8), (4, 8), (4, 4)]),
        ]),
    ],
)
def test_decode_frame_roundtrip(geom):
    encoded = wkp.encode(geom, precision=6)
    frame = wkp.decode_frame(encoded)

    assert frame.version == 1
    assert frame.precision == 6
    assert frame.dimensions == 2
    assert isinstance(frame.coords, np.ndarray)
    assert frame.coords.dtype == np.float64
    assert frame.coords.ndim == 2
    assert frame.coords.shape[1] == 2
    assert isinstance(frame.segment_point_counts, np.ndarray)
    assert isinstance(frame.group_segment_counts, np.ndarray)

    # Re-encode from frame and compare
    re_encoded = wkp.encode_frame(frame)
    assert re_encoded == encoded


def test_decode_frame_explicit_context():
    ctx = wkp.Context()
    geom = LineString([(0, 0), (1, 1)])
    encoded = wkp.encode(geom, precision=5, ctx=ctx)
    frame = wkp.decode_frame(encoded, ctx=ctx)
    assert frame.precision == 5


def test_decode_frame_coords_shape():
    # Polygon with shell + hole: 5 + 5 = 10 points total
    geom = Polygon([(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)], [[(2, 2), (4, 2), (4, 4), (2, 4), (2, 2)]])
    encoded = wkp.encode(geom, precision=6)
    frame = wkp.decode_frame(encoded)

    assert frame.coords.shape == (10, 2)
    assert len(frame.segment_point_counts) == 2   # 2 rings
    assert len(frame.group_segment_counts) == 1   # 1 polygon group
    assert sum(frame.segment_point_counts) == 10


# ---------------------------------------------------------------------------
# to_geometry
# ---------------------------------------------------------------------------

def test_frame_to_geometry_roundtrip():
    geom = Polygon([(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)], [[(2, 2), (4, 2), (4, 4), (2, 4), (2, 2)]])
    encoded = wkp.encode(geom, precision=6)
    frame = wkp.decode_frame(encoded)
    recovered = frame.to_geometry()
    assert recovered.equals_exact(geom, tolerance=1e-6)


# ---------------------------------------------------------------------------
# to_buffer / from_buffer
# ---------------------------------------------------------------------------

def test_buffer_roundtrip_linestring():
    geom = LineString([(1.1, 2.2), (3.3, 4.4), (5.5, 6.6)])
    encoded = wkp.encode(geom, precision=6)
    frame = wkp.decode_frame(encoded)

    buf = frame.to_buffer()
    assert isinstance(buf, bytes)

    recovered = wkp.GeometryFrame.from_buffer(buf)
    assert recovered.version == frame.version
    assert recovered.precision == frame.precision
    assert recovered.dimensions == frame.dimensions
    assert recovered.geometry_type == frame.geometry_type
    np.testing.assert_array_equal(recovered.coords, frame.coords)
    np.testing.assert_array_equal(recovered.segment_point_counts, frame.segment_point_counts.astype(np.uint64))
    np.testing.assert_array_equal(recovered.group_segment_counts, frame.group_segment_counts.astype(np.uint64))


def test_buffer_roundtrip_multipolygon():
    geom = MultiPolygon([
        Polygon([(0, 0), (2, 0), (2, 2), (0, 2), (0, 0)]),
        Polygon([(4, 4), (8, 4), (8, 8), (4, 8), (4, 4)]),
    ])
    encoded = wkp.encode(geom, precision=5)
    frame = wkp.decode_frame(encoded)

    buf = frame.to_buffer()
    recovered = wkp.GeometryFrame.from_buffer(buf)

    # Verify geometry can be recovered
    geom_recovered = recovered.to_geometry()
    assert geom_recovered.equals_exact(geom, tolerance=1e-5)


def test_buffer_header_size():
    geom = Point(1.0, 2.0)
    encoded = wkp.encode(geom, precision=6)
    frame = wkp.decode_frame(encoded)
    buf = frame.to_buffer()

    # Header is 40 bytes: 4×int32 + 3×uint64 = 16 + 24 = 40
    assert len(buf) >= 40
    # Total = 40 + coord_value_count*8 + (segment_count + group_count)*4
    ncoords = frame.coords.size
    nseg = len(frame.segment_point_counts)
    ngrp = len(frame.group_segment_counts)
    expected_size = 40 + ncoords * 8 + nseg * 4 + ngrp * 4
    assert len(buf) == expected_size


def test_buffer_from_bytes_and_memoryview():
    geom = LineString([(0, 0), (1, 1)])
    encoded = wkp.encode(geom, precision=4)
    frame = wkp.decode_frame(encoded)
    buf = frame.to_buffer()

    # Works from bytes
    r1 = wkp.GeometryFrame.from_buffer(buf)
    # Works from memoryview
    r2 = wkp.GeometryFrame.from_buffer(memoryview(buf))
    np.testing.assert_array_equal(r1.coords, r2.coords)
