from __future__ import annotations

import re
from dataclasses import dataclass
from importlib.metadata import PackageNotFoundError
from importlib.metadata import version as dist_version
from pathlib import Path

import numpy as np
import shapely
from shapely.geometry import GeometryCollection, LineString, MultiLineString, MultiPoint, MultiPolygon, Point, Polygon

from . import _core

__core_version__ = _core.core_version()
__core_compatibility__ = "0.4.x"

__all__ = [
    "Context",
    "DecodedGeometry",
    "decode",
    "decode_header",
    "encode",
    "encode_floats",
    "decode_floats",
]


EncodedGeometryType = _core.EncodedGeometryType
Context = _core.Context


@dataclass
class DecodedGeometry:
    version: int
    precision: int
    dimensions: int
    geometry: shapely.geometry.base.BaseGeometry


def _binding_version() -> str:
    try:
        return dist_version("wkp")
    except PackageNotFoundError:
        pyproject = Path(__file__).resolve().parents[2] / "pyproject.toml"
        content = pyproject.read_text(encoding="utf-8")
        match = re.search(r'^version\s*=\s*"([^"]+)"', content, flags=re.MULTILINE)
        if not match:
            raise RuntimeError(f"Could not parse [project].version from {pyproject}")
        return match.group(1)


__version__ = _binding_version()


def _parse_semver_major_minor(version: str) -> tuple[int, int]:
    core = version.split("-", 1)[0]
    parts = core.split(".")
    if len(parts) < 2:
        raise RuntimeError(f"Invalid semantic version: {version}")
    try:
        return int(parts[0]), int(parts[1])
    except ValueError as exc:
        raise RuntimeError(f"Invalid semantic version: {version}") from exc


def _parse_core_compatibility(compatibility: str) -> tuple[int, int]:
    match = re.fullmatch(r"(\d+)\.(\d+)\.x", compatibility)
    if not match:
        raise RuntimeError(f"Invalid core compatibility format: {compatibility}")
    return int(match.group(1)), int(match.group(2))


def _assert_core_compatibility() -> None:
    required_major, required_minor = _parse_core_compatibility(__core_compatibility__)
    core_major, core_minor = _parse_semver_major_minor(__core_version__)
    if (core_major, core_minor) != (required_major, required_minor):
        raise RuntimeError(
            f"wkp Python binding {__version__} requires WKP core {__core_compatibility__}, "
            f"but loaded core is {__core_version__}"
        )


_assert_core_compatibility()


def _as_encoded_bytes(bites) -> bytes:
    try:
        return bytes(bites)
    except TypeError as exc:
        raise TypeError("bites must be bytes-like") from exc


def _normalize_precisions(precisions) -> list[int]:
    if isinstance(precisions, int):
        return [int(precisions)]
    values = [int(p) for p in list(precisions)]
    if len(values) == 0:
        raise ValueError("precisions cannot be empty")
    return values


def decode_header(bites):
    encoded = _as_encoded_bytes(bites)
    return _core.decode_geometry_header(encoded)


def _geometry_from_groups(geom_type: int, groups):
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


def _geometry_from_frame_counts(
    geom_type: int,
    dimensions: int,
    coords_flat: np.ndarray,
    group_segment_counts: list[int],
    segment_point_counts: list[int],
):
    groups = []
    coord_index = 0
    segment_index = 0

    for group_segments in group_segment_counts:
        group = []
        for _ in range(group_segments):
            point_count = int(segment_point_counts[segment_index])
            segment_index += 1
            value_count = point_count * dimensions
            segment_flat = coords_flat[coord_index : coord_index + value_count]
            coord_index += value_count
            group.append(segment_flat.reshape((point_count, dimensions)))
        groups.append(group)

    return _geometry_from_groups(geom_type, groups)


def decode(ctx, bites):
    encoded = _as_encoded_bytes(bites)
    version, precision, dimensions, geom_type, groups = _core.decode_geometry_frame(ctx, encoded)
    return DecodedGeometry(
        version=version,
        precision=precision,
        dimensions=dimensions,
        geometry=_geometry_from_groups(geom_type, groups),
    )


def _geometry_dimensions(geom) -> int:
    dims = shapely.get_coordinate_dimension(geom)
    if dims <= 0 or dims > 16:
        raise ValueError(f"Unsupported geometry dimensions: {dims}")
    return dims


def _coords_array(geom, dimensions: int) -> np.ndarray:
    include_z = dimensions == 3
    return np.ascontiguousarray(shapely.get_coordinates(geom, include_z=include_z), dtype=np.float64)


def _geometry_to_frame(geom):
    if isinstance(geom, GeometryCollection):
        raise ValueError("GeometryCollection is not supported by the current WKP core geometry frame ABI")

    dims = _geometry_dimensions(geom)
    include_z = dims == 3

    if isinstance(geom, Point):
        coords = _coords_array(geom, dims)
        return EncodedGeometryType.POINT.value, coords, [1], [1]

    if isinstance(geom, LineString):
        coords = _coords_array(geom, dims)
        return EncodedGeometryType.LINESTRING.value, coords, [1], [coords.shape[0]]

    if isinstance(geom, Polygon):
        rings = [geom.exterior, *list(geom.interiors)]
        segments = [
            np.ascontiguousarray(shapely.get_coordinates(r, include_z=include_z), dtype=np.float64) for r in rings
        ]
        coords = np.ascontiguousarray(np.vstack(segments), dtype=np.float64)
        return EncodedGeometryType.POLYGON.value, coords, [len(segments)], [arr.shape[0] for arr in segments]

    if isinstance(geom, MultiPoint):
        segments = [
            np.ascontiguousarray(shapely.get_coordinates(point, include_z=include_z), dtype=np.float64)
            for point in geom.geoms
        ]
        coords = np.ascontiguousarray(np.vstack(segments), dtype=np.float64)
        return EncodedGeometryType.MULTIPOINT.value, coords, [1] * len(segments), [1] * len(segments)

    if isinstance(geom, MultiLineString):
        segments = [
            np.ascontiguousarray(shapely.get_coordinates(line, include_z=include_z), dtype=np.float64)
            for line in geom.geoms
        ]
        coords = np.ascontiguousarray(np.vstack(segments), dtype=np.float64)
        return (
            EncodedGeometryType.MULTILINESTRING.value,
            coords,
            [1] * len(segments),
            [arr.shape[0] for arr in segments],
        )

    if isinstance(geom, MultiPolygon):
        all_segments: list[np.ndarray] = []
        group_counts: list[int] = []
        for poly in geom.geoms:
            rings = [poly.exterior, *list(poly.interiors)]
            poly_segments = [
                np.ascontiguousarray(shapely.get_coordinates(r, include_z=include_z), dtype=np.float64) for r in rings
            ]
            all_segments.extend(poly_segments)
            group_counts.append(len(poly_segments))
        coords = np.ascontiguousarray(np.vstack(all_segments), dtype=np.float64)
        return EncodedGeometryType.MULTIPOLYGON.value, coords, group_counts, [arr.shape[0] for arr in all_segments]

    raise TypeError("geom must be a Shapely Point, LineString, Polygon, MultiPoint, MultiLineString, or MultiPolygon")


def encode(ctx, geom, precision: int):
    if not isinstance(precision, int):
        raise TypeError("precision must be an int")

    if getattr(geom, "is_empty", False):
        raise ValueError("empty geometries are unsupported")

    geom_type, coords, group_segment_counts, segment_point_counts = _geometry_to_frame(geom)

    return _core.encode_geometry_frame(ctx, geom_type, coords, precision, group_segment_counts, segment_point_counts)


def encode_floats(ctx, floats, precisions):
    p = _normalize_precisions(precisions)
    dimensions = len(p)
    values = np.asarray(floats, dtype=np.float64)
    if values.ndim != 2:
        raise ValueError("floats must be a 2D array-like")
    if values.shape[1] != dimensions:
        raise ValueError(f"floats must have {dimensions} columns")
    values = np.ascontiguousarray(values, dtype=np.float64)

    return _core.encode_f64(ctx, values, dimensions, p)


def decode_floats(ctx, encoded, precisions):
    p = _normalize_precisions(precisions)
    dimensions = len(p)
    bites = _as_encoded_bytes(encoded)

    arr = np.asarray(_core.decode_f64(ctx, bites, dimensions, p), dtype=np.float64)
    return [tuple(row) for row in arr.tolist()]
