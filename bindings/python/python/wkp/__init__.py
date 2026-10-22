from __future__ import annotations

import re
import struct
import threading
import warnings
from dataclasses import dataclass
from importlib.metadata import PackageNotFoundError
from importlib.metadata import version as dist_version
from typing import NamedTuple

import numpy as np
import shapely
from shapely.geometry import GeometryCollection, LineString, MultiLineString, MultiPoint, MultiPolygon, Point, Polygon

from . import _core

__core_version__ = _core.core_version()
__core_compatibility__ = "0.6.x"

__all__ = [
    "Context",
    "DecodedGeometry",
    "GeometryType",
    "Header",
    "GeometryFrame",
    "decode",
    "decode_frame",
    "decode_header",
    "encode",
    "encode_frame",
    "encode_floats",
    "decode_floats",
    "decode_floats_array",
]


GeometryType = _core.GeometryType
_GEOMETRY_TYPE_BY_CODE = {t.value: t for t in GeometryType}


class Header(NamedTuple):
    """The four header fields of an encoded geometry (a tuple, so it also unpacks)."""

    version: int
    precision: int
    dimensions: int
    geometry_type: GeometryType


def __getattr__(name: str):
    # EncodedGeometryType was renamed GeometryType in 0.7.
    if name == "EncodedGeometryType":
        warnings.warn("wkp.EncodedGeometryType is deprecated; use wkp.GeometryType", DeprecationWarning, stacklevel=2)
        return GeometryType
    raise AttributeError(f"module 'wkp' has no attribute {name!r}")
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
        # Not installed as a distribution (e.g. imported from a source tree). The package and
        # extension are built from the same Cargo workspace, so they share a version.
        return _core.__version__


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


def _as_f64_c(values) -> np.ndarray:
    """A C-contiguous, aligned float64 array, copying only if needed.

    `np.ascontiguousarray` keeps misaligned arrays (e.g. `np.frombuffer` at an odd offset)
    as they are, which the native module rejects.
    """
    return np.require(values, dtype=np.float64, requirements=("C", "A"))


def _as_encoded_bytes(encoded) -> bytes | str:
    """`str` and `bytes` go to the native module as they are; other bytes-like objects
    (bytearray, memoryview, ...) are converted to `bytes`."""
    if type(encoded) is bytes or type(encoded) is str:
        return encoded
    if isinstance(encoded, str):
        return str(encoded)
    try:
        # Via memoryview, so only real bytes-like objects are accepted (`bytes(5)` would be five
        # zero bytes).
        return bytes(memoryview(encoded))
    except TypeError as exc:
        raise TypeError("encoded must be str or bytes-like") from exc


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


_UINT32_MAX = 0xFFFFFFFF


@dataclass(eq=False)
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

    # Field by field, comparing the arrays by value (the generated __eq__ would compare them
    # with ==, which raises for numpy arrays).
    def __eq__(self, other: object) -> bool:
        if not isinstance(other, GeometryFrame):
            return NotImplemented
        return (
            (self.version, self.precision, self.dimensions, self.geometry_type)
            == (other.version, other.precision, other.dimensions, other.geometry_type)
            and np.array_equal(self.coords, other.coords)
            and np.array_equal(self.segment_point_counts, other.segment_point_counts)
            and np.array_equal(self.group_segment_counts, other.group_segment_counts)
        )

    __hash__ = None  # mutable, like a dataclass with eq=True

    def to_geometry(self) -> shapely.geometry.base.BaseGeometry:
        """Convert to a Shapely geometry object."""
        _check_shapely_dimensions(self.dimensions)
        coords = _as_f64_c(self.coords).reshape(-1, self.dimensions)
        seg = np.asarray(self.segment_point_counts, dtype=np.int64)
        grp = np.asarray(self.group_segment_counts, dtype=np.int64)
        # A frame built by hand may be inconsistent; one from the decoder never is.
        if len(seg) == 0 or seg.sum() != len(coords) or grp.sum() != len(seg):
            raise ValueError("Frame segment/group counts do not match its coordinates")
        return _geometry_from_frame(self.geometry_type, coords, seg, grp)

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

        Raises ``ValueError`` if a count does not fit in uint32.
        """
        coords_flat = _as_f64_c(self.coords).ravel()
        seg = np.asarray(self.segment_point_counts, dtype=np.uint64)
        grp = np.asarray(self.group_segment_counts, dtype=np.uint64)
        # The buffer format stores counts as uint32; a plain cast would silently wrap.
        if (seg.size and seg.max() > _UINT32_MAX) or (grp.size and grp.max() > _UINT32_MAX):
            raise ValueError("a segment or group count is too large for the buffer format (uint32)")
        seg = seg.astype(np.uint32)
        grp = grp.astype(np.uint32)
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
            coords=_as_f64_c(coords),
            segment_point_counts=seg,
            group_segment_counts=grp,
        )


# ---------------------------------------------------------------------------
# Header
# ---------------------------------------------------------------------------

def decode_header(encoded) -> Header:
    """The :class:`Header` of a WKP geometry, without decoding the rest of it."""
    version, precision, dimensions, geometry_type = _core.decode_geometry_header(_as_encoded_bytes(encoded))
    return Header(version, precision, dimensions, _GEOMETRY_TYPE_BY_CODE[geometry_type])


# ---------------------------------------------------------------------------
# Geometry ↔ frame conversion helpers
# ---------------------------------------------------------------------------

_POINT = GeometryType.POINT.value
_LINESTRING = GeometryType.LINESTRING.value
_POLYGON = GeometryType.POLYGON.value
_MULTIPOINT = GeometryType.MULTIPOINT.value
_MULTILINESTRING = GeometryType.MULTILINESTRING.value
_MULTIPOLYGON = GeometryType.MULTIPOLYGON.value


def _geometry_from_frame(geom_type: int, coords: np.ndarray, segment_point_counts, group_segment_counts):
    """The Shapely geometry for a frame (counts as from the decoder, so consistent with
    ``coords``), built with a constant number of vectorised Shapely calls: rings, lines and parts
    are created in one call each, grouped by ``indices``, however many there are."""
    seg = segment_point_counts
    grp = group_segment_counts

    # Points and lines first, without numpy work: they are the common case and small calls.
    if geom_type == _POINT:
        if len(grp) != 1 or grp[0] != 1:
            raise ValueError("Expected 1 segment for POINT geometry")
        if seg[0] != 1:
            raise ValueError(f"Expected 1 point for POINT geometry, got {seg[0]}")
        return shapely.points(coords[0])

    if geom_type == _LINESTRING:
        if len(grp) != 1 or grp[0] != 1:
            raise ValueError("Expected 1 segment for LINESTRING geometry")
        if seg[0] < 2:
            raise ValueError(f"Expected at least 2 points for LINESTRING geometry, got {seg[0]}")
        return shapely.linestrings(coords)

    if geom_type == _POLYGON and len(grp) == 1 and grp[0] == 1:
        # The common polygon without holes: straight from the shell coordinates.
        return shapely.polygons(coords)

    seg = np.asarray(seg, dtype=np.int64)
    grp = np.asarray(grp, dtype=np.int64)
    # Which segment each coordinate belongs to, and which group each segment belongs to.
    segment_of_coord = np.repeat(np.arange(len(seg)), seg)
    group_of_segment = np.repeat(np.arange(len(grp)), grp)

    if geom_type == _POLYGON:
        if len(grp) != 1:
            raise ValueError("Expected 1 polygon group")
        if grp[0] == 0:
            raise ValueError("Expected at least 1 ring for POLYGON")
        rings = shapely.linearrings(coords, indices=segment_of_coord)
        return shapely.polygons(rings[0], holes=rings[1:] if len(rings) > 1 else None)

    if geom_type == _MULTIPOINT:
        if (grp != 1).any():
            raise ValueError("Expected exactly one segment per MULTIPOINT part")
        if (seg != 1).any():
            raise ValueError(f"Expected 1 point segment in MULTIPOINT, got {seg[seg != 1][0]}")
        return shapely.multipoints(coords)

    if geom_type == _MULTILINESTRING:
        if (grp != 1).any():
            raise ValueError("Expected exactly one segment per MULTILINESTRING part")
        return shapely.multilinestrings(shapely.linestrings(coords, indices=segment_of_coord))

    if geom_type == _MULTIPOLYGON:
        if (grp == 0).any():
            raise ValueError("Expected at least 1 ring in MULTIPOLYGON polygon")
        rings = shapely.linearrings(coords, indices=segment_of_coord)
        return shapely.multipolygons(shapely.polygons(rings, indices=group_of_segment))

    raise ValueError(f"Unsupported geometry type in header: {geom_type}")


_GEOMETRY_TYPE_ERROR = (
    "geom must be a Shapely Point, LineString, Polygon, MultiPoint, MultiLineString, or MultiPolygon"
)
_M_ERROR = (
    "geometries with M values are not supported (Shapely cannot rebuild them when decoding); "
    "drop M first, e.g. with shapely.force_2d or shapely.force_3d"
)
_has_m = getattr(shapely, "has_m", None)  # Shapely >= 2.1; older versions drop M when reading


def _check_shapely_dimensions(dimensions: int) -> None:
    if dimensions > 3:
        raise ValueError(
            f"cannot make a Shapely geometry with {dimensions} dimensions (at most 3: XYZ); "
            "use decode_frame to get the coordinates"
        )


def _geometry_to_frame(geom):
    """``(geometry_type, coords, group_segment_counts, segment_point_counts)`` for a Shapely
    geometry. All coordinates come from one ``get_coordinates`` call (already in WKP order: rings
    in order within polygons, parts in order), and the counts from vectorised calls, so the cost
    does not grow with the number of parts and rings."""
    # isinstance checks: much cheaper than comparing shapely.get_type_id with GeometryType.
    if isinstance(geom, GeometryCollection):
        raise ValueError("GeometryCollection is not supported by the current WKP core geometry frame ABI")
    if not isinstance(geom, (Point, LineString, Polygon, MultiPoint, MultiLineString, MultiPolygon)):
        raise TypeError(_GEOMETRY_TYPE_ERROR)
    dims = shapely.get_coordinate_dimension(geom)
    if dims == 4:  # XYZM; XYM is 3 and caught in encode, where it fails
        raise ValueError(_M_ERROR)
    if dims <= 0 or dims > 16:
        raise ValueError(f"Unsupported geometry dimensions: {dims}")
    # A new C-contiguous float64 array.
    coords = shapely.get_coordinates(geom, include_z=dims == 3)

    # Counts as lists for the small cases, numpy arrays (read directly by the native module) for
    # the ones that can be long.
    if isinstance(geom, Point):
        return _POINT, coords, [1], [1]
    if isinstance(geom, LineString):  # includes LinearRing
        return _LINESTRING, coords, [1], [len(coords)]
    if isinstance(geom, Polygon):
        # Most polygons have no holes; then no rings need to be extracted (get_rings copies them).
        if shapely.get_num_interior_rings(geom) == 0:
            return _POLYGON, coords, [1], [len(coords)]
        rings = shapely.get_rings(geom)
        return _POLYGON, coords, [len(rings)], shapely.get_num_coordinates(rings).astype(np.uint64)
    if isinstance(geom, MultiPoint):
        ones = np.ones(len(coords), dtype=np.uint64)
        return _MULTIPOINT, coords, ones, ones
    if isinstance(geom, MultiLineString):
        parts = shapely.get_parts(geom)
        return (
            _MULTILINESTRING,
            coords,
            np.ones(len(parts), dtype=np.uint64),
            shapely.get_num_coordinates(parts).astype(np.uint64),
        )
    parts = shapely.get_parts(geom)
    rings, polygon_of_ring = shapely.get_rings(parts, return_index=True)
    return (
        _MULTIPOLYGON,
        coords,
        np.bincount(polygon_of_ring, minlength=len(parts)).astype(np.uint64),
        shapely.get_num_coordinates(rings).astype(np.uint64),
    )


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def decode_frame(encoded, *, ctx=None) -> GeometryFrame:
    """
    Decode a WKP-encoded geometry into a :class:`GeometryFrame` without converting
    to a Shapely object. Useful for bulk coordinate processing or buffer transfer.
    """
    ctx = _resolve_context(ctx)
    version, precision, dimensions, geom_type, coords, seg_counts, grp_counts = (
        _core.decode_geometry_frame_flat(ctx, _as_encoded_bytes(encoded))
    )
    return GeometryFrame(
        version=version,
        precision=precision,
        dimensions=dimensions,
        geometry_type=_GEOMETRY_TYPE_BY_CODE[geom_type],
        coords=_as_f64_c(coords),
        segment_point_counts=np.asarray(seg_counts, dtype=np.uint64),
        group_segment_counts=np.asarray(grp_counts, dtype=np.uint64),
    )


def decode(encoded, *, ctx=None) -> DecodedGeometry:
    """Decode a WKP-encoded geometry to a Shapely geometry."""
    ctx = _resolve_context(ctx)
    version, precision, dimensions, geom_type, coords, seg_counts, grp_counts = (
        _core.decode_geometry_frame_flat(ctx, _as_encoded_bytes(encoded))
    )
    _check_shapely_dimensions(dimensions)
    return DecodedGeometry(
        version=version,
        precision=precision,
        dimensions=dimensions,
        geometry=_geometry_from_frame(geom_type, coords, seg_counts, grp_counts),
    )


def encode_frame(frame: GeometryFrame, *, ctx=None) -> str:
    """Encode a :class:`GeometryFrame` to a WKP string."""
    ctx = _resolve_context(ctx)
    coords = _as_f64_c(frame.coords)
    if coords.ndim != 2:
        raise ValueError("frame.coords must be a 2D array")
    grp = np.asarray(frame.group_segment_counts, dtype=np.uint64)
    seg = np.asarray(frame.segment_point_counts, dtype=np.uint64)
    return _core.encode_geometry_frame(ctx, frame.geometry_type, coords, frame.precision, seg, grp)


def encode(geom, precision: int, *, ctx=None) -> str:
    """Encode a Shapely geometry to a WKP string."""
    ctx = _resolve_context(ctx)
    if not isinstance(precision, int):
        raise TypeError("precision must be an int")

    if getattr(geom, "is_empty", False):
        raise ValueError("empty geometries are unsupported")

    geom_type, coords, group_segment_counts, segment_point_counts = _geometry_to_frame(geom)

    try:
        return _core.encode_geometry_frame(
            ctx, geom_type, coords, precision, segment_point_counts, group_segment_counts
        )
    except ValueError:
        # An XYM geometry reads as XYZ with NaN Z values, which the core rejects. Checked only
        # after a failure, so valid 3D input pays nothing for it.
        if coords.shape[1] == 3 and _has_m is not None and _has_m(geom):
            raise ValueError(_M_ERROR) from None
        raise


def encode_floats(floats, precisions, *, ctx=None) -> str:
    """Encode rows of floats (one column per dimension) to a headerless WKP string.
    ``precisions`` has one int per column; a single int means one column."""
    ctx = _resolve_context(ctx)
    p = _normalize_precisions(precisions)
    dimensions = len(p)
    values = np.asarray(floats, dtype=np.float64)
    if values.ndim != 2:
        raise ValueError("floats must be a 2D array-like")
    if values.shape[1] != dimensions:
        raise ValueError(f"floats must have {dimensions} columns")
    values = _as_f64_c(values)

    return _core.encode_f64(ctx, values, dimensions, p)


def decode_floats_array(encoded, precisions, *, ctx=None) -> np.ndarray:
    """
    Decode WKP-encoded floats to a flat 1D float64 numpy array.

    Returns interleaved values ``[x0, y0, x1, y1, ...]`` (or ``[x0, y0, z0, ...]``
    for higher dimensions) as a C-contiguous 1D array backed by a single allocation.
    Use ``arr.reshape(-1, dims)`` to get a 2D view, or ``arr.tobytes()`` to treat
    it as a raw byte buffer.
    """
    ctx = _resolve_context(ctx)
    p = _normalize_precisions(precisions)
    dimensions = len(p)
    return np.asarray(_core.decode_f64(ctx, _as_encoded_bytes(encoded), dimensions, p), dtype=np.float64)


def decode_floats(encoded, precisions, *, ctx=None) -> np.ndarray:
    """
    Decode WKP-encoded floats to a 2D float64 numpy array of shape (N, dims).

    Use ``.tolist()`` to convert to a list of plain Python lists if needed.
    """
    p = _normalize_precisions(precisions)
    flat = decode_floats_array(encoded, precisions, ctx=ctx)
    return flat.reshape(-1, len(p))
