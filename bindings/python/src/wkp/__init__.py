from __future__ import annotations

import re
import struct
import threading
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
    "GeometryFrame",
    "decode",
    "decode_frame",
    "decode_header",
    "encode",
    "encode_frame",
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

# Thread-local context store — one Context per thread, created lazily.
_thread_locals = threading.local()


def _get_thread_context() -> Context:
    if not hasattr(_thread_locals, "ctx"):
        _thread_locals.ctx = Context()
    return _thread_locals.ctx


def _resolve_context(ctx) -> Context:
    if ctx is None:
        return _get_thread_context()
    return ctx


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


# ---------------------------------------------------------------------------
# GeometryFrame
# ---------------------------------------------------------------------------

# Buffer header format (little-endian): version, precision, dimensions, geometry_type (4×int32)
# followed by coord_value_count, segment_count, group_count (3×uint64) = 40 bytes total.
_FRAME_HEADER_FMT = "<iiii QQQ"
_FRAME_HEADER_SIZE = struct.calcsize(_FRAME_HEADER_FMT)  # 40 bytes


@dataclass
class GeometryFrame:
    """
    A flat representation of a decoded WKP geometry with explicit structure metadata.

    ``coords`` is a (total_points, dimensions) float64 numpy array.
    ``segment_point_counts[i]`` is the number of points in segment i.
    ``group_segment_counts[g]`` is the number of segments in group g.

    Geometry types map to groups/segments as follows:

    * POINT / LINESTRING: 1 group, 1 segment
    * POLYGON: 1 group, ring_count segments (shell first)
    * MULTIPOINT / MULTILINESTRING / MULTIPOLYGON: N groups, each with 1+ segments
    """

    version: int
    precision: int
    dimensions: int
    geometry_type: int
    coords: np.ndarray               # shape (total_points, dimensions), float64, C-contiguous
    segment_point_counts: np.ndarray # shape (segment_count,), uint64
    group_segment_counts: np.ndarray # shape (group_count,), uint64

    def to_geometry(self) -> shapely.geometry.base.BaseGeometry:
        """Convert to a Shapely geometry object."""
        return _geometry_from_frame_counts(
            self.geometry_type,
            self.dimensions,
            self.coords.ravel(),
            self.group_segment_counts.tolist(),
            self.segment_point_counts.tolist(),
        )

    def to_buffer(self) -> bytes:
        """
        Serialize to a compact binary buffer (little-endian).

        Layout (40-byte fixed header + variable data):
          int32  version
          int32  precision
          int32  dimensions
          int32  geometry_type
          uint64 coord_value_count  (= total_points × dimensions)
          uint64 segment_count
          uint64 group_count
          float64[coord_value_count]  flat coordinates
          uint32[segment_count]       segment_point_counts
          uint32[group_count]         group_segment_counts
        """
        coords_flat = np.ascontiguousarray(self.coords.ravel(), dtype=np.float64)
        seg = np.asarray(self.segment_point_counts, dtype=np.uint32)
        grp = np.asarray(self.group_segment_counts, dtype=np.uint32)
        header = struct.pack(
            _FRAME_HEADER_FMT,
            self.version,
            self.precision,
            self.dimensions,
            self.geometry_type,
            coords_flat.size,
            seg.size,
            grp.size,
        )
        return header + coords_flat.tobytes() + seg.tobytes() + grp.tobytes()

    @classmethod
    def from_buffer(cls, buffer) -> "GeometryFrame":
        """Deserialize from a binary buffer produced by :meth:`to_buffer`."""
        mv = memoryview(buffer) if not isinstance(buffer, (bytes, memoryview)) else buffer
        version, precision, dimensions, geometry_type, ncoords, nseg, ngrp = struct.unpack_from(
            _FRAME_HEADER_FMT, mv
        )
        offset = _FRAME_HEADER_SIZE
        coords_flat = np.frombuffer(mv, dtype=np.float64, count=ncoords, offset=offset).copy()
        offset += ncoords * 8
        seg = np.frombuffer(mv, dtype=np.uint32, count=nseg, offset=offset).astype(np.uint64)
        offset += nseg * 4
        grp = np.frombuffer(mv, dtype=np.uint32, count=ngrp, offset=offset).astype(np.uint64)
        coords = coords_flat.reshape(-1, dimensions) if dimensions > 0 else coords_flat.reshape(0, 0)
        return cls(
            version=version,
            precision=precision,
            dimensions=dimensions,
            geometry_type=geometry_type,
            coords=np.ascontiguousarray(coords, dtype=np.float64),
            segment_point_counts=seg,
            group_segment_counts=grp,
        )


# ---------------------------------------------------------------------------
# Header
# ---------------------------------------------------------------------------

def decode_header(bites):
    encoded = _as_encoded_bytes(bites)
    return _core.decode_geometry_header(encoded)


# ---------------------------------------------------------------------------
# Geometry ↔ frame conversion helpers
# ---------------------------------------------------------------------------

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
    group_segment_counts,
    segment_point_counts,
):
    groups = []
    coord_index = 0
    segment_index = 0

    for group_segments in group_segment_counts:
        group = []
        for _ in range(int(group_segments)):
            point_count = int(segment_point_counts[segment_index])
            segment_index += 1
            value_count = point_count * dimensions
            segment_flat = coords_flat[coord_index : coord_index + value_count]
            coord_index += value_count
            group.append(segment_flat.reshape((point_count, dimensions)))
        groups.append(group)

    return _geometry_from_groups(geom_type, groups)


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


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def decode_frame(bites, *, ctx=None) -> GeometryFrame:
    """
    Decode a WKP-encoded geometry into a :class:`GeometryFrame` without converting
    to a Shapely object. Useful for bulk coordinate processing or buffer transfer.
    """
    ctx = _resolve_context(ctx)
    encoded = _as_encoded_bytes(bites)
    version, precision, dimensions, geom_type, coords, seg_counts, grp_counts = (
        _core.decode_geometry_frame_flat(ctx, encoded)
    )
    return GeometryFrame(
        version=version,
        precision=precision,
        dimensions=dimensions,
        geometry_type=geom_type,
        coords=np.ascontiguousarray(coords, dtype=np.float64),
        segment_point_counts=np.asarray(seg_counts, dtype=np.uint64),
        group_segment_counts=np.asarray(grp_counts, dtype=np.uint64),
    )


def decode(bites, *, ctx=None) -> DecodedGeometry:
    """Decode a WKP-encoded geometry to a Shapely geometry."""
    ctx = _resolve_context(ctx)
    encoded = _as_encoded_bytes(bites)
    version, precision, dimensions, geom_type, groups = _core.decode_geometry_frame(ctx, encoded)
    return DecodedGeometry(
        version=version,
        precision=precision,
        dimensions=dimensions,
        geometry=_geometry_from_groups(geom_type, groups),
    )


def encode_frame(frame: GeometryFrame, *, ctx=None) -> bytes:
    """Encode a :class:`GeometryFrame` to WKP bytes."""
    ctx = _resolve_context(ctx)
    coords = np.ascontiguousarray(frame.coords, dtype=np.float64)
    if coords.ndim != 2:
        raise ValueError("frame.coords must be a 2D array")
    grp = [int(x) for x in frame.group_segment_counts]
    seg = [int(x) for x in frame.segment_point_counts]
    return _core.encode_geometry_frame(ctx, frame.geometry_type, coords, frame.precision, grp, seg)


def encode(geom, precision: int, *, ctx=None) -> bytes:
    """Encode a Shapely geometry to WKP bytes."""
    ctx = _resolve_context(ctx)
    if not isinstance(precision, int):
        raise TypeError("precision must be an int")

    if getattr(geom, "is_empty", False):
        raise ValueError("empty geometries are unsupported")

    geom_type, coords, group_segment_counts, segment_point_counts = _geometry_to_frame(geom)

    return _core.encode_geometry_frame(ctx, geom_type, coords, precision, group_segment_counts, segment_point_counts)


def encode_floats(floats, precisions, *, ctx=None) -> bytes:
    ctx = _resolve_context(ctx)
    p = _normalize_precisions(precisions)
    dimensions = len(p)
    values = np.asarray(floats, dtype=np.float64)
    if values.ndim != 2:
        raise ValueError("floats must be a 2D array-like")
    if values.shape[1] != dimensions:
        raise ValueError(f"floats must have {dimensions} columns")
    values = np.ascontiguousarray(values, dtype=np.float64)

    return _core.encode_f64(ctx, values, dimensions, p)


def decode_floats(encoded, precisions, *, ctx=None):
    ctx = _resolve_context(ctx)
    p = _normalize_precisions(precisions)
    dimensions = len(p)
    bites = _as_encoded_bytes(encoded)

    arr = np.asarray(_core.decode_f64(ctx, bites, dimensions, p), dtype=np.float64)
    return [tuple(row) for row in arr.tolist()]
