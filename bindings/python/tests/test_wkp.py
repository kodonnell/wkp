import pytest
import wkp
from shapely.geometry import GeometryCollection, LineString, MultiLineString, MultiPoint, MultiPolygon, Point, Polygon


@pytest.mark.parametrize(
    "geom",
    [
        Point(174.776, -41.289),
        LineString([(174.776, -41.289), (174.777, -41.290), (174.778, -41.291)]),
        Polygon([(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)], [[(2, 2), (4, 2), (4, 4), (2, 4), (2, 2)]]),
        MultiPoint([(174.776, -41.289), (174.777, -41.290)]),
        MultiLineString([[(0, 0), (1, 1)], [(2, 2), (3, 3)]]),
        MultiPolygon(
            [
                Polygon([(0, 0), (2, 0), (2, 2), (0, 2), (0, 0)]),
                Polygon([(4, 4), (8, 4), (8, 8), (4, 8), (4, 4)]),
            ]
        ),
    ],
)
def test_roundtrip_supported_geometries(geom):
    encoded = wkp.encode(geom, precision=6)
    decoded = wkp.decode(encoded)
    assert decoded.geometry.equals_exact(geom, tolerance=1e-6)


def test_roundtrip_with_explicit_context(geom=None):
    ctx = wkp.Context()
    geom = LineString([(0, 0), (1, 1), (2, 2)])
    encoded = wkp.encode(geom, precision=5, ctx=ctx)
    decoded = wkp.decode(encoded, ctx=ctx)
    assert decoded.geometry.equals_exact(geom, tolerance=1e-5)


def test_decode_header_basic():
    geom = LineString([(0, 0), (1, 1)])
    encoded = wkp.encode(geom, precision=5)
    version, precision, dimensions, geom_type = wkp.decode_header(encoded)
    assert version == 1
    assert precision == 5
    assert dimensions == 2
    assert geom_type == wkp.EncodedGeometryType.LINESTRING.value


def test_invalid_precision_and_malformed_input():
    geom = Point(1, 2)
    with pytest.raises(Exception):
        wkp.encode(geom, precision=1000)

    with pytest.raises(Exception):
        wkp.decode(b"@@")


def test_geometry_collection_not_supported_current_core():
    geom = GeometryCollection([Point(0, 0), LineString([(0, 0), (1, 1)])])
    with pytest.raises(ValueError):
        wkp.encode(geom, precision=6)


@pytest.mark.parametrize("geom", [Point(), LineString(), Polygon(), MultiPoint(), MultiLineString(), MultiPolygon()])
def test_empty_geometries_raise_unsupported(geom):
    with pytest.raises(ValueError, match="empty geometries are unsupported"):
        wkp.encode(geom, precision=6)


def _square(x, y, size, z=None):
    pts = [(x, y), (x + size, y), (x + size, y + size), (x, y + size), (x, y)]
    # The closing point repeats the first one's z too, or Shapely closes the ring again.
    return [(*p, z + i % 4) for i, p in enumerate(pts)] if z is not None else pts


# Geometries with several rings/parts (2D and 3D), and the frame counts they must produce:
# (geometry, group_segment_counts, segment_point_counts).
STRUCTURED = [
    (Polygon(_square(0, 0, 10), [_square(1, 1, 2), _square(5, 5, 2)]), [3], [5, 5, 5]),
    (Polygon(_square(0, 0, 10, z=1.5), [_square(1, 1, 2, z=2.5)]), [2], [5, 5]),
    (MultiPoint([(0, 0, 1), (1, 1, 2), (2, 2, 3)]), [1, 1, 1], [1, 1, 1]),
    (MultiLineString([[(0, 0), (1, 1)], [(2, 2), (3, 3), (4, 5)], [(9, 9), (8, 8)]]), [1, 1, 1], [2, 3, 2]),
    (
        MultiPolygon(
            [
                Polygon(_square(0, 0, 10), [_square(1, 1, 2), _square(5, 5, 2)]),
                Polygon(_square(20, 20, 3)),
                Polygon(_square(30, 30, 9), [_square(31, 31, 1)]),
            ]
        ),
        [3, 1, 2],
        [5, 5, 5, 5, 5, 5],
    ),
    (MultiPolygon([Polygon(_square(0, 0, 4, z=7.0), [_square(1, 1, 1, z=8.0)])]), [2], [5, 5]),
]


@pytest.mark.parametrize("geom,groups,segments", STRUCTURED)
def test_multi_ring_and_multi_part_round_trip(geom, groups, segments):
    encoded = wkp.encode(geom, precision=6)
    frame = wkp.decode_frame(encoded)
    assert frame.group_segment_counts.tolist() == groups
    assert frame.segment_point_counts.tolist() == segments
    decoded = wkp.decode(encoded).geometry
    assert decoded.geom_type == geom.geom_type
    assert decoded.has_z == geom.has_z
    assert decoded.equals_exact(geom, tolerance=1e-6)
    assert frame.to_geometry().equals_exact(geom, tolerance=1e-6)
    assert wkp.encode_frame(frame) == encoded


def test_linear_ring_encodes_as_linestring():
    from shapely.geometry import LinearRing

    ring = LinearRing(_square(0, 0, 1))
    decoded = wkp.decode(wkp.encode(ring, precision=6)).geometry
    assert decoded.geom_type == "LineString"
    assert decoded.equals_exact(LineString(ring.coords), tolerance=1e-6)


def test_bad_inputs_and_inconsistent_frames():
    with pytest.raises(TypeError, match="geom must be a Shapely"):
        wkp.encode([(0, 0), (1, 1)], precision=6)
    frame = wkp.decode_frame(wkp.encode(MultiPoint([(0, 0), (1, 1)]), precision=6))
    bad = wkp.GeometryFrame(**{**frame.__dict__, "segment_point_counts": frame.segment_point_counts[:1]})
    with pytest.raises(ValueError, match="do not match"):
        bad.to_geometry()
    two_point_parts = wkp.GeometryFrame(
        **{**frame.__dict__, "segment_point_counts": [2], "group_segment_counts": [1]}
    )
    with pytest.raises(ValueError, match="Expected 1 point segment in MULTIPOINT, got 2"):
        two_point_parts.to_geometry()
