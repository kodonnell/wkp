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
