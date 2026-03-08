from __future__ import annotations

import re
from dataclasses import dataclass
from importlib.metadata import PackageNotFoundError
from importlib.metadata import version as dist_version
from pathlib import Path

import numpy as np
import shapely
from shapely.geometry import LineString, MultiLineString, MultiPoint, MultiPolygon, Point, Polygon

from . import _core

__core_version__ = _core.core_version()
__core_compatibility__ = "0.2.x"

__all__ = [
    "__version__",
    "__core_version__",
    "__core_compatibility__",
    "EncodedGeometryType",
    "DecodedGeometry",
    "Workspace",
    "decode_header",
    "decode",
    "encode_point",
    "encode_linestring",
    "encode_polygon",
    "encode_multipoint",
    "encode_multilinestring",
    "encode_multipolygon",
    "encode_floats",
    "encode_floats_array",
    "encode_floats_into",
    "decode_floats",
    "decode_floats_into",
]


EncodedGeometryType = _core.EncodedGeometryType


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


@dataclass
class DecodedGeometry:
    version: int
    precision: int
    dimensions: int
    geometry: shapely.geometry.base.BaseGeometry


class Workspace:
    def __init__(
        self,
        initial_u8_capacity: int = 4096,
        initial_f64_capacity: int = 256,
        max_u8_size: int = -1,
        max_f64_size: int = -1,
    ) -> None:
        self._core = _core.WorkspaceCore(initial_u8_capacity, initial_f64_capacity, max_u8_size, max_f64_size)

    def encode_floats(self, floats, n: int, precisions):
        return encode_floats(floats, n, precisions, workspace=self)

    def decode_floats(self, encoded: bytes, n: int, precisions):
        return decode_floats(encoded, n, precisions, workspace=self)


_default_workspace: Workspace | None = None


def _resolve_workspace(workspace: Workspace | None) -> Workspace:
    global _default_workspace
    if workspace is None:
        if _default_workspace is None:
            _default_workspace = Workspace()
        return _default_workspace
    if not isinstance(workspace, Workspace):
        raise TypeError("workspace must be a wkp.Workspace or None")
    return workspace


def _normalize_precisions(n: int, precisions):
    if isinstance(precisions, int):
        return [precisions] * n
    p = list(precisions)
    if len(p) != n:
        raise ValueError(f"Expected {n} precisions, got {len(p)}")
    return p


def _decode_floats_core(encoded: bytes, n: int, precisions, workspace: Workspace | None = None) -> np.ndarray:
    if not isinstance(encoded, (bytes, bytearray)):
        raise TypeError("encoded must be bytes or bytearray")

    p = _normalize_precisions(n, precisions)
    ws = _resolve_workspace(workspace)
    arr = ws._core.decode_floats(bytes(encoded), n, p)
    if not isinstance(arr, np.ndarray):
        arr = np.asarray(arr, dtype=np.float64)
    if arr.size % n != 0:
        raise RuntimeError("decoded output has invalid length")
    return arr.reshape((-1, n))


def encode_floats_array(floats: np.ndarray, n: int, precisions, workspace: Workspace | None = None):
    arr = np.asarray(floats, dtype=np.float64)
    if arr.ndim != 2:
        raise ValueError("Expected a 2D coordinate array")
    if arr.shape[1] != n:
        raise ValueError(f"Expected coordinates with {n} dimensions, got {arr.shape[1]}")
    p = _normalize_precisions(n, precisions)
    ws = _resolve_workspace(workspace)
    arr = np.ascontiguousarray(arr, dtype=np.float64)
    return ws._core.encode_floats(arr, n, p)


def encode_floats(floats, n: int, precisions, workspace: Workspace | None = None):
    arr = np.asarray(floats, dtype=np.float64)
    return encode_floats_array(arr, n, precisions, workspace=workspace)


def decode_floats(encoded: bytes, n: int, precisions, workspace: Workspace | None = None):
    arr = _decode_floats_core(encoded, n, precisions, workspace=workspace)
    return [tuple(row) for row in arr.tolist()]


def encode_floats_into(floats, n: int, precisions, out_buffer: np.ndarray) -> int:
    arr = np.asarray(floats, dtype=np.float64)
    if arr.ndim != 2:
        raise ValueError("Expected 2D array-like floats")
    if arr.shape[1] != n:
        raise ValueError(f"Expected coordinates with {n} dimensions, got {arr.shape[1]}")
    if not isinstance(out_buffer, np.ndarray):
        raise TypeError("out_buffer must be a numpy.ndarray")
    if out_buffer.dtype != np.uint8 or out_buffer.ndim != 1:
        raise ValueError("out_buffer must be a 1D numpy.ndarray with dtype=uint8")
    if not out_buffer.flags.c_contiguous:
        raise ValueError("out_buffer must be C-contiguous")

    p = _normalize_precisions(n, precisions)
    arr = np.ascontiguousarray(arr, dtype=np.float64)
    return int(_core.encode_floats_into(arr, n, p, out_buffer))


def decode_floats_into(encoded: bytes, n: int, precisions, out_buffer: np.ndarray) -> int:
    if not isinstance(encoded, (bytes, bytearray)):
        raise TypeError("encoded must be bytes or bytearray")
    if not isinstance(out_buffer, np.ndarray):
        raise TypeError("out_buffer must be a numpy.ndarray")
    if out_buffer.dtype != np.float64 or out_buffer.ndim != 2:
        raise ValueError("out_buffer must be a 2D numpy.ndarray with dtype=float64")
    if out_buffer.shape[1] != n:
        raise ValueError(f"out_buffer must have {n} columns")
    if not out_buffer.flags.c_contiguous:
        raise ValueError("out_buffer must be C-contiguous")

    p = _normalize_precisions(n, precisions)
    return int(_core.decode_floats_into(bytes(encoded), n, p, out_buffer))


def decode_header(encoded: bytes | str):
    if isinstance(encoded, str):
        encoded = encoded.encode("ascii")
    return _core.decode_geometry_header(bytes(encoded))


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


def decode(encoded: bytes | str, workspace: Workspace | None = None) -> DecodedGeometry:
    encoded_bytes = encoded.encode("ascii") if isinstance(encoded, str) else bytes(encoded)
    ws = _resolve_workspace(workspace)
    version, precision, dimensions, geom_type, groups = ws._core.decode_geometry_frame(encoded_bytes)
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


def encode_point(geom: Point, precision: int, workspace: Workspace | None = None) -> str:
    if not isinstance(geom, Point):
        raise TypeError("geom must be shapely.geometry.Point")
    dims = _geometry_dimensions(geom)
    ws = _resolve_workspace(workspace)
    encoded = ws._core.encode_point(_coords_array(geom, dims), precision)
    return encoded.decode("ascii")


def encode_linestring(geom: LineString, precision: int, workspace: Workspace | None = None) -> str:
    if not isinstance(geom, LineString):
        raise TypeError("geom must be shapely.geometry.LineString")
    dims = _geometry_dimensions(geom)
    ws = _resolve_workspace(workspace)
    encoded = ws._core.encode_linestring(_coords_array(geom, dims), precision)
    return encoded.decode("ascii")


def encode_polygon(geom: Polygon, precision: int, workspace: Workspace | None = None) -> str:
    if not isinstance(geom, Polygon):
        raise TypeError("geom must be shapely.geometry.Polygon")
    dims = _geometry_dimensions(geom)
    include_z = dims == 3
    rings = []
    for ring in [geom.exterior] + list(geom.interiors):
        rings.append(np.ascontiguousarray(shapely.get_coordinates(ring, include_z=include_z), dtype=np.float64))
    ws = _resolve_workspace(workspace)
    encoded = ws._core.encode_polygon(rings, precision)
    return encoded.decode("ascii")


def encode_multipoint(geom: MultiPoint, precision: int, workspace: Workspace | None = None) -> str:
    if not isinstance(geom, MultiPoint):
        raise TypeError("geom must be shapely.geometry.MultiPoint")
    dims = _geometry_dimensions(geom)
    include_z = dims == 3
    parts = []
    for point in geom.geoms:
        parts.append(np.ascontiguousarray(shapely.get_coordinates(point, include_z=include_z), dtype=np.float64))
    ws = _resolve_workspace(workspace)
    encoded = ws._core.encode_multipoint(parts, precision)
    return encoded.decode("ascii")


def encode_multilinestring(geom: MultiLineString, precision: int, workspace: Workspace | None = None) -> str:
    if not isinstance(geom, MultiLineString):
        raise TypeError("geom must be shapely.geometry.MultiLineString")
    dims = _geometry_dimensions(geom)
    include_z = dims == 3
    parts = []
    for line in geom.geoms:
        parts.append(np.ascontiguousarray(shapely.get_coordinates(line, include_z=include_z), dtype=np.float64))
    ws = _resolve_workspace(workspace)
    encoded = ws._core.encode_multilinestring(parts, precision)
    return encoded.decode("ascii")


def encode_multipolygon(geom: MultiPolygon, precision: int, workspace: Workspace | None = None) -> str:
    if not isinstance(geom, MultiPolygon):
        raise TypeError("geom must be shapely.geometry.MultiPolygon")
    dims = _geometry_dimensions(geom)
    include_z = dims == 3
    polygons = []
    for poly in geom.geoms:
        rings = []
        for ring in [poly.exterior] + list(poly.interiors):
            rings.append(np.ascontiguousarray(shapely.get_coordinates(ring, include_z=include_z), dtype=np.float64))
        polygons.append(rings)
    ws = _resolve_workspace(workspace)
    encoded = ws._core.encode_multipolygon(polygons, precision)
    return encoded.decode("ascii")
