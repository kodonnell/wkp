import wkp
from shapely.geometry import LineString, MultiLineString, MultiPoint, MultiPolygon, Point, Polygon


def _header_field(encoded: bytes, index: int) -> int:
    return encoded[index] - 63


def test_geometry_encode_decode_point_2d():
    point = Point(174.776, -41.289)
    encoded = wkp.encode_point(point, precision=6)

    assert len(encoded) >= 4
    assert _header_field(encoded, 0) == 1
    assert _header_field(encoded, 1) == 6
    assert _header_field(encoded, 2) == 2
    assert _header_field(encoded, 3) == 1

    decoded = wkp.decode(encoded)
    assert decoded.version == 1
    assert decoded.precision == 6
    assert decoded.dimensions == 2
    assert isinstance(decoded.geometry, Point)
    assert decoded.geometry.equals(point)


def test_geometry_encode_decode_point_3d_truncated_precision():
    point = Point(174.776, -41.289, 123.4)
    encoded = wkp.encode_point(point, precision=1)

    assert _header_field(encoded, 1) == 1
    assert _header_field(encoded, 2) == 3

    decoded = wkp.decode(encoded)
    assert decoded.dimensions == 3
    assert abs(decoded.geometry.x - point.x) < 0.1
    assert abs(decoded.geometry.y - point.y) < 0.1
    assert abs(decoded.geometry.z - point.z) < 0.1


def test_geometry_encode_decode_linestring():
    geom = LineString([(174.776, -41.289), (174.777, -41.290), (174.778, -41.291)])
    encoded = wkp.encode_linestring(geom, precision=6)
    assert _header_field(encoded, 3) == 2

    decoded = wkp.decode(encoded)
    assert isinstance(decoded.geometry, LineString)
    assert decoded.geometry.equals(geom)


def test_geometry_encode_decode_polygon_with_holes():
    exterior = [(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)]
    hole1 = [(2, 2), (4, 2), (4, 4), (2, 4), (2, 2)]
    hole2 = [(6, 6), (8, 6), (8, 8), (6, 8), (6, 6)]
    geom = Polygon(exterior, [hole1, hole2])

    encoded = wkp.encode_polygon(geom, precision=6)
    assert _header_field(encoded, 3) == 3
    assert b"," in encoded

    decoded = wkp.decode(encoded)
    assert isinstance(decoded.geometry, Polygon)
    assert decoded.geometry.equals(geom)


def test_geometry_encode_decode_multipoint():
    geom = MultiPoint([(174.776, -41.289), (174.777, -41.290), (174.778, -41.291)])
    encoded = wkp.encode_multipoint(geom, precision=6)

    assert _header_field(encoded, 3) == 4
    assert b";" in encoded

    decoded = wkp.decode(encoded)
    assert isinstance(decoded.geometry, MultiPoint)
    assert decoded.geometry.equals(geom)


def test_geometry_encode_decode_multilinestring():
    geom = MultiLineString([[(0, 0), (1, 1), (2, 2)], [(3, 3), (4, 4), (5, 5)]])
    encoded = wkp.encode_multilinestring(geom, precision=6)

    assert _header_field(encoded, 3) == 5
    assert b";" in encoded

    decoded = wkp.decode(encoded)
    assert isinstance(decoded.geometry, MultiLineString)
    assert decoded.geometry.equals(geom)


def test_geometry_encode_decode_multipolygon():
    polygon1 = Polygon([(0, 0), (2, 0), (2, 2), (0, 2), (0, 0)])
    polygon2 = Polygon([(4, 4), (8, 4), (8, 8), (4, 8), (4, 4)], [[(5, 5), (7, 5), (7, 7), (5, 7), (5, 5)]])
    geom = MultiPolygon([polygon1, polygon2])

    encoded = wkp.encode_multipolygon(geom, precision=6)
    assert _header_field(encoded, 3) == 6
    assert b";" in encoded
    assert b"," in encoded

    decoded = wkp.decode(encoded)
    assert isinstance(decoded.geometry, MultiPolygon)
    assert decoded.geometry.equals(geom)


def test_workspace_reuse_geometry_and_floats():
    ws = wkp.Workspace()
    geom = LineString([(0.1, 0.2), (1.1, 1.2), (2.1, 2.2)])

    encoded_geom = wkp.encode_linestring(geom, precision=5, workspace=ws)
    decoded_geom = wkp.decode(encoded_geom, workspace=ws)
    assert decoded_geom.geometry.equals(geom)

    floats = [(0.1, 0.2), (1.1, 1.2), (2.1, 2.2)]
    encoded_floats = wkp.encode_floats(floats, 2, [5, 5], workspace=ws)
    decoded_floats = wkp.decode_floats(encoded_floats, 2, [5, 5], workspace=ws)
    assert decoded_floats == floats
