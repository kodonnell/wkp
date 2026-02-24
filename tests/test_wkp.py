from shapely.geometry import LineString, MultiLineString, MultiPoint, MultiPolygon, Point, Polygon

from wkp import GeometryEncoder


def test_geometry_encoder_point_2d():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    point = Point(174.776, -41.289)
    encoded = encoder.encode(point)

    # Check header format: version(2) + precision(2) + dimensions(2) + geom_type(2) = 8 chars
    assert len(encoded) >= 8
    assert encoded[:2] == "01"  # version 1
    assert encoded[2:4] == "06"  # precision 6
    assert encoded[4:6] == "02"  # 2 dimensions
    assert encoded[6:8] == "01"  # POINT type

    # Decode and verify
    decoded = GeometryEncoder.decode(encoded)
    assert decoded.version == 1
    assert decoded.precision == 6
    assert decoded.dimensions == 2
    assert isinstance(decoded.geometry, Point)
    assert decoded.geometry.equals(point)


def test_geometry_encoder_point_3d_truncated_precision():
    encoder = GeometryEncoder(precision=1, dimensions=3)
    point = Point(174.776, -41.289, 123.4)
    encoded = encoder.encode(point)

    assert encoded[2:4] == "01"  # precision 1
    assert encoded[4:6] == "03"  # 3 dimensions

    decoded = GeometryEncoder.decode(encoded)
    assert decoded.dimensions == 3
    assert abs(decoded.geometry.x - point.x) < 0.1
    assert abs(decoded.geometry.y - point.y) < 0.1
    assert abs(decoded.geometry.z - point.z) < 0.1


def test_geometry_encoder_linestring():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    expected_coords = [(174.776, -41.289), (174.777, -41.290), (174.778, -41.291)]
    linestring = LineString(expected_coords)
    encoded = encoder.encode(linestring)

    assert encoded[6:8] == "02"  # LINESTRING type

    decoded = GeometryEncoder.decode(encoded)
    assert isinstance(decoded.geometry, LineString)
    assert decoded.geometry.equals(linestring)


def test_geometry_encoder_linestring_bytes_roundtrip():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    expected_coords = [(174.776, -41.289), (174.777, -41.290), (174.778, -41.291)]
    linestring = LineString(expected_coords)
    encoded = encoder.encode_bytes(linestring)

    assert isinstance(encoded, bytes)
    assert encoded[6:8] == b"02"  # LINESTRING type

    decoded = encoder.decode_bytes(encoded)
    assert isinstance(decoded.geometry, LineString)
    assert decoded.geometry.equals(linestring)


def test_geometry_encoder_polygon_no_holes():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    expected_ring_coords = [(0, 0), (1, 0), (1, 1), (0, 1), (0, 0)]
    polygon = Polygon(expected_ring_coords)
    encoded = encoder.encode(polygon)

    assert encoded[6:8] == "03"  # POLYGON type

    decoded = GeometryEncoder.decode(encoded)
    assert isinstance(decoded.geometry, Polygon)
    assert decoded.geometry.equals(polygon)


def test_geometry_encoder_polygon_with_holes():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    exterior = [(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)]
    hole1 = [(2, 2), (4, 2), (4, 4), (2, 4), (2, 2)]
    hole2 = [(6, 6), (8, 6), (8, 8), (6, 8), (6, 6)]
    polygon = Polygon(exterior, [hole1, hole2])

    encoded = encoder.encode(polygon)
    assert "," in encoded  # Should have commas separating rings

    decoded = GeometryEncoder.decode(encoded)
    assert isinstance(decoded.geometry, Polygon)
    assert decoded.geometry.equals(polygon)


def test_geometry_encoder_multipoint():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    expected_coords = [(174.776, -41.289), (174.777, -41.290), (174.778, -41.291)]
    multipoint = MultiPoint(expected_coords)
    encoded = encoder.encode(multipoint)

    assert encoded[6:8] == "04"  # MULTIPOINT type
    assert ";" in encoded  # Should have semicolons separating points

    decoded = GeometryEncoder.decode(encoded)
    assert isinstance(decoded.geometry, MultiPoint)
    assert decoded.geometry.equals(multipoint)


def test_geometry_encoder_multilinestring():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    line1 = [(0, 0), (1, 1), (2, 2)]
    line2 = [(3, 3), (4, 4), (5, 5)]
    multilinestring = MultiLineString([line1, line2])

    encoded = encoder.encode(multilinestring)
    assert encoded[6:8] == "05"  # MULTILINESTRING type
    assert ";" in encoded  # Should have semicolons separating linestrings

    decoded = GeometryEncoder.decode(encoded)
    assert isinstance(decoded.geometry, MultiLineString)
    assert decoded.geometry.equals(multilinestring)


def test_geometry_encoder_multipolygon():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    polygon1_exterior = [(0, 0), (2, 0), (2, 2), (0, 2), (0, 0)]
    polygon1 = Polygon(polygon1_exterior)
    polygon2_hole_coords = [(5, 5), (7, 5), (7, 7), (5, 7), (5, 5)]
    polygon2_exterior = [(4, 4), (8, 4), (8, 8), (4, 8), (4, 4)]
    polygon2 = Polygon(polygon2_exterior, [polygon2_hole_coords])
    multipolygon = MultiPolygon([polygon1, polygon2])

    encoded = encoder.encode(multipolygon)
    assert encoded[6:8] == "06"  # MULTIPOLYGON type
    assert ";" in encoded  # Should have semicolons separating polygons
    assert "," in encoded  # Should have commas for rings (polygon2 has a hole)

    decoded = GeometryEncoder.decode(encoded)
    assert isinstance(decoded.geometry, MultiPolygon)
    assert decoded.geometry.equals(multipolygon)


def test_geometry_encoder_dimension_mismatch():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    point_3d = Point(174.776, -41.289, 123.4)

    try:
        encoder.encode(point_3d)
        assert False, "Should have raised ValueError for dimension mismatch"
    except ValueError as e:
        assert "dimensions" in str(e).lower()


def test_geometry_encoder_precision_preservation():
    # Test that higher precision preserves more detail
    high_precision_encoder = GeometryEncoder(precision=6, dimensions=2)
    low_precision_encoder = GeometryEncoder(precision=1, dimensions=2)

    point = Point(174.776543, -41.289876)

    high_encoded = high_precision_encoder.encode(point)
    low_encoded = low_precision_encoder.encode(point)

    high_decoded = GeometryEncoder.decode(high_encoded)
    low_decoded = GeometryEncoder.decode(low_encoded)

    # High precision should be closer
    high_error = abs(high_decoded.geometry.x - point.x)
    low_error = abs(low_decoded.geometry.x - point.x)

    assert high_error < low_error
    assert high_error < 0.000001
    assert low_error < 0.1


def test_geometry_encoder_roundtrip_all_types():
    """Test that all geometry types can be encoded and decoded correctly"""
    encoder = GeometryEncoder(precision=6, dimensions=2)

    geometries = [
        Point(174.776, -41.289),
        LineString([(174.776, -41.289), (174.777, -41.290)]),
        Polygon([(0, 0), (1, 0), (1, 1), (0, 1), (0, 0)]),
        MultiPoint([(0, 0), (1, 1)]),
        MultiLineString([[(0, 0), (1, 1)], [(2, 2), (3, 3)]]),
        MultiPolygon(
            [Polygon([(0, 0), (1, 0), (1, 1), (0, 1), (0, 0)]), Polygon([(2, 2), (3, 2), (3, 3), (2, 3), (2, 2)])]
        ),
    ]

    for geom in geometries:
        encoded = encoder.encode(geom)
        decoded = GeometryEncoder.decode(encoded)

        # Verify type matches
        assert type(decoded.geometry) is type(geom)
        assert decoded.geometry.equals(geom)


def test_geometry_encoder_str_helpers():
    encoder = GeometryEncoder(precision=6, dimensions=2)
    geom = LineString([(0.1, 0.2), (1.1, 1.2), (2.1, 2.2)])

    encoded_str = encoder.encode_str(geom)
    assert isinstance(encoded_str, str)

    decoded_via_static = GeometryEncoder.decode(encoded_str)
    decoded_via_instance = encoder.decode_str(encoded_str)

    assert decoded_via_static.geometry.equals(geom)
    assert decoded_via_instance.geometry.equals(geom)
