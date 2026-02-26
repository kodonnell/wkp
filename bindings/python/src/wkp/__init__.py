from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import shapely
from shapely.geometry import Point

from . import _core

__core_version__ = _core.core_version()
__version__ = __core_version__

__all__ = [
    "__version__",
    "__core_version__",
    "EncodedGeometryType",
    "DecodedGeometry",
    "GeometryEncoder",
    "encode_floats",
    "encode_floats_array",
    "decode_floats",
]


EncodedGeometryType = _core.EncodedGeometryType


@dataclass
class DecodedGeometry:
    version: int
    precision: int
    dimensions: int
    geometry: shapely.geometry.base.BaseGeometry


def _normalize_precisions(n: int, precisions):
    if isinstance(precisions, int):
        return [precisions] * n
    p = list(precisions)
    if len(p) != n:
        raise ValueError(f"Expected {n} precisions, got {len(p)}")
    return p


def _encode_floats_core(floats, n: int, precisions) -> bytes:
    arr = np.asarray(floats, dtype=np.float64)
    if arr.ndim != 2:
        raise ValueError("Expected 2D array-like floats")
    if arr.shape[1] != n:
        raise ValueError(f"Expected coordinates with {n} dimensions, got {arr.shape[1]}")

    p = _normalize_precisions(n, precisions)
    arr = np.ascontiguousarray(arr, dtype=np.float64)
    return _core.encode_floats(arr, n, p)


def _decode_floats_core(encoded: bytes, n: int, precisions) -> np.ndarray:
    if not isinstance(encoded, (bytes, bytearray)):
        raise TypeError("encoded must be bytes or bytearray")

    p = _normalize_precisions(n, precisions)
    arr = _core.decode_floats(bytes(encoded), n, p)
    if not isinstance(arr, np.ndarray):
        arr = np.asarray(arr, dtype=np.float64)
    if arr.size % n != 0:
        raise RuntimeError("decoded output has invalid length")
    return arr.reshape((-1, n))


def encode_floats_array(floats: np.ndarray, n: int, precisions):
    arr = np.asarray(floats, dtype=np.float64)
    if arr.ndim != 2:
        raise ValueError("Expected a 2D coordinate array")
    if arr.shape[1] != n:
        raise ValueError(f"Expected coordinates with {n} dimensions, got {arr.shape[1]}")
    p = _normalize_precisions(n, precisions)
    return _encode_floats_core(arr, n, p)


def encode_floats(floats, n: int, precisions):
    arr = np.asarray(floats, dtype=np.float64)
    return encode_floats_array(arr, n, precisions)


def decode_floats(encoded: bytes, n: int, precisions):
    p = _normalize_precisions(n, precisions)
    arr = _decode_floats_core(encoded, n, p)
    return [tuple(row) for row in arr.tolist()]


class GeometryEncoder:
    def __init__(self, precision: int, dimensions: int, initial_capacity: int = 4096):
        if dimensions <= 0 or dimensions > 16:
            raise ValueError("dimensions must be between 1 and 16")
        self.precision = precision
        self.dimensions = dimensions
        self._precisions = [precision] * dimensions
        self._initial_capacity = initial_capacity
        self._core = _core.GeometryEncoderCore(precision, dimensions, initial_capacity)

    def _geom_type_from_geom(self, geom) -> int:
        if isinstance(geom, shapely.geometry.Point):
            return EncodedGeometryType.POINT.value
        if isinstance(geom, shapely.geometry.LineString):
            return EncodedGeometryType.LINESTRING.value
        if isinstance(geom, shapely.geometry.Polygon):
            return EncodedGeometryType.POLYGON.value
        if isinstance(geom, shapely.geometry.MultiPoint):
            return EncodedGeometryType.MULTIPOINT.value
        if isinstance(geom, shapely.geometry.MultiLineString):
            return EncodedGeometryType.MULTILINESTRING.value
        if isinstance(geom, shapely.geometry.MultiPolygon):
            return EncodedGeometryType.MULTIPOLYGON.value
        raise ValueError(f"Unsupported geometry type: {type(geom)}")

    def _check_dimensions(self, geom):
        if shapely.get_coordinate_dimension(geom) != self.dimensions:
            raise ValueError(
                f"Geometry has {shapely.get_coordinate_dimension(geom)} dimensions but encoder is configured for {self.dimensions} dimensions"
            )

    def _encode_bytes(self, geom) -> bytes:
        geom_type = self._geom_type_from_geom(geom)
        self._check_dimensions(geom)

        if geom_type in (EncodedGeometryType.POINT.value, EncodedGeometryType.LINESTRING.value):
            coords = shapely.get_coordinates(geom, include_z=self.dimensions == 3)
            if geom_type == EncodedGeometryType.POINT.value:
                return self._core.encode_point(np.ascontiguousarray(coords, dtype=np.float64))
            return self._core.encode_linestring(np.ascontiguousarray(coords, dtype=np.float64))

        if geom_type == EncodedGeometryType.POLYGON.value:
            rings = []
            for i, ring in enumerate([geom.exterior] + list(geom.interiors)):
                self._check_dimensions(ring)
                coords = shapely.get_coordinates(ring, include_z=self.dimensions == 3)
                rings.append(np.ascontiguousarray(coords, dtype=np.float64))
            return self._core.encode_polygon(rings)

        if geom_type in (EncodedGeometryType.MULTIPOINT.value, EncodedGeometryType.MULTILINESTRING.value):
            parts = []
            for i, part in enumerate(geom.geoms):
                self._check_dimensions(part)
                coords = shapely.get_coordinates(part, include_z=self.dimensions == 3)
                parts.append(np.ascontiguousarray(coords, dtype=np.float64))
            if geom_type == EncodedGeometryType.MULTIPOINT.value:
                return self._core.encode_multipoint(parts)
            return self._core.encode_multilinestring(parts)

        if geom_type == EncodedGeometryType.MULTIPOLYGON.value:
            polygons = []
            for i, part in enumerate(geom.geoms):
                self._check_dimensions(part)
                rings = []
                for j, ring in enumerate([part.exterior] + list(part.interiors)):
                    self._check_dimensions(ring)
                    coords = shapely.get_coordinates(ring, include_z=self.dimensions == 3)
                    rings.append(np.ascontiguousarray(coords, dtype=np.float64))
                polygons.append(rings)
            return self._core.encode_multipolygon(polygons)

        raise ValueError("Unsupported geometry type")

    def encode_bytes(self, geom) -> bytes:
        return self._encode_bytes(geom)

    def encode(self, geom) -> str:
        return self._encode_bytes(geom).decode("ascii")

    def encode_str(self, geom):
        return self.encode(geom)

    def _decode_geometry_body(self, encoded: bytes, geom_type: int):
        _, _, _, framed_type, groups = _core.decode_geometry_frame(encoded)
        if framed_type != geom_type:
            raise ValueError("Geometry type mismatch while decoding frame")

        if geom_type == EncodedGeometryType.POINT.value:
            if len(groups) != 1 or len(groups[0]) != 1:
                raise ValueError("Expected 1 segment for POINT geometry")
            arr = groups[0][0]
            if arr.shape[0] != 1:
                raise ValueError(f"Expected 1 point for POINT geometry, got {arr.shape[0]}")
            return Point(arr[0])

        if geom_type == EncodedGeometryType.LINESTRING.value:
            if len(groups) != 1 or len(groups[0]) != 1:
                raise ValueError("Expected 1 segment for LINESTRING geometry")
            arr = groups[0][0]
            if arr.shape[0] < 2:
                raise ValueError(f"Expected at least 2 points for LINESTRING geometry, got {arr.shape[0]}")
            return shapely.linestrings(arr)

        if geom_type == EncodedGeometryType.POLYGON.value:
            if len(groups) != 1:
                raise ValueError("Expected 1 polygon group")
            rings = [shapely.linearrings(arr) for arr in groups[0]]
            if len(rings) == 0:
                raise ValueError("Expected at least 1 ring for POLYGON")
            shell = rings[0]
            holes = rings[1:]
            return shapely.polygons(shell, holes=holes if holes else None)

        if geom_type == EncodedGeometryType.MULTIPOINT.value:
            points = []
            for group in groups:
                if len(group) != 1:
                    raise ValueError("Expected exactly one segment per MULTIPOINT part")
                arr = group[0]
                if arr.shape[0] != 1:
                    raise ValueError(f"Expected 1 point segment in MULTIPOINT, got {arr.shape[0]}")
                points.append(arr[0])
            return shapely.multipoints(np.asarray(points, dtype=np.float64))

        if geom_type == EncodedGeometryType.MULTILINESTRING.value:
            lines = []
            for group in groups:
                if len(group) != 1:
                    raise ValueError("Expected exactly one segment per MULTILINESTRING part")
                lines.append(group[0])
            return shapely.multilinestrings(lines)

        if geom_type == EncodedGeometryType.MULTIPOLYGON.value:
            polygons = []
            for group in groups:
                rings = [shapely.linearrings(arr) for arr in group]
                if len(rings) == 0:
                    raise ValueError("Expected at least 1 ring in MULTIPOLYGON polygon")
                shell = rings[0]
                holes = rings[1:]
                polygons.append(shapely.polygons(shell, holes=holes if holes else None))
            return shapely.multipolygons(polygons)

        raise ValueError(f"Unsupported geometry type in header: {geom_type}")

    def decode_bytes(self, encoded: bytes):
        version, precision, dimensions, geom_type = GeometryEncoder.decode_header(encoded)
        if precision != self.precision or dimensions != self.dimensions:
            raise ValueError(
                f"Encoded geometry has precision={precision}, dimensions={dimensions}, but this encoder is precision={self.precision}, dimensions={self.dimensions}"
            )
        return DecodedGeometry(
            version=version,
            precision=precision,
            dimensions=dimensions,
            geometry=self._decode_geometry_body(encoded, geom_type),
        )

    @staticmethod
    def decode_header(encoded: bytes):
        if isinstance(encoded, str):
            encoded = encoded.encode("ascii")
        return _core.decode_geometry_header(encoded)

    @staticmethod
    def decode(encoded):
        if isinstance(encoded, str):
            encoded_bytes = encoded.encode("ascii")
        else:
            encoded_bytes = encoded
        version, precision, dimensions, _ = GeometryEncoder.decode_header(encoded_bytes)
        encoder = GeometryEncoder(precision=precision, dimensions=dimensions)
        decoded = encoder.decode_bytes(encoded_bytes)
        assert decoded.version == version
        return decoded

    def decode_str(self, encoded: str):
        return self.decode_bytes(encoded.encode("ascii"))
