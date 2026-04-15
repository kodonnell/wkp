"""Type stubs for the public wkp Python API."""

from __future__ import annotations

import enum
from typing import Sequence, Union

import numpy as np
from numpy.typing import NDArray
import shapely.geometry.base

__version__: str
__core_version__: str
__core_compatibility__: str

__all__: list[str]


class EncodedGeometryType(enum.Enum):
    POINT = 1
    LINESTRING = 2
    POLYGON = 3
    MULTIPOINT = 4
    MULTILINESTRING = 5
    MULTIPOLYGON = 6


class Context:
    """
    A native buffer pool.  Buffers grow to fit the largest geometry processed,
    then stay allocated and are reused on subsequent calls.

    Pass ``ctx=None`` (the default) to use a per-thread lazily-created context.
    Pass an explicit ``Context()`` instance for fine-grained lifetime control.
    """

    def __init__(self) -> None: ...


class DecodedGeometry:
    """Result of :func:`decode`."""

    version: int
    precision: int
    dimensions: int
    geometry: shapely.geometry.base.BaseGeometry

    def __init__(
        self,
        version: int,
        precision: int,
        dimensions: int,
        geometry: shapely.geometry.base.BaseGeometry,
    ) -> None: ...


class GeometryFrame:
    """
    Low-level flat representation of a decoded WKP geometry.

    ``coords`` shape is ``(total_points, dimensions)``, ``dtype=float64``,
    C-contiguous.  ``segment_point_counts[i]`` is the number of points in
    segment *i*.  ``group_segment_counts[g]`` is the number of segments in
    group *g*.
    """

    version: int
    precision: int
    dimensions: int
    geometry_type: int
    coords: NDArray[np.float64]
    segment_point_counts: NDArray[np.uint64]
    group_segment_counts: NDArray[np.uint64]

    def __init__(
        self,
        version: int,
        precision: int,
        dimensions: int,
        geometry_type: int,
        coords: NDArray[np.float64],
        segment_point_counts: NDArray[np.uint64],
        group_segment_counts: NDArray[np.uint64],
    ) -> None: ...

    def to_geometry(self) -> shapely.geometry.base.BaseGeometry:
        """Convert to a Shapely geometry object."""
        ...

    def to_buffer(self) -> bytes:
        """
        Serialize to a compact binary buffer (little-endian, 40-byte header).

        The buffer is cross-language compatible with ``GeometryFrame.fromBuffer()``
        in ``@wkpjs/node`` and ``@wkpjs/web``.
        """
        ...

    @classmethod
    def from_buffer(
        cls, buffer: Union[bytes, bytearray, memoryview]
    ) -> GeometryFrame:
        """Deserialize from a binary buffer produced by :meth:`to_buffer`."""
        ...


def encode(
    geom: shapely.geometry.base.BaseGeometry,
    precision: int,
    *,
    ctx: Context | None = None,
) -> bytes:
    """Encode a Shapely geometry to WKP bytes."""
    ...


def decode(
    bites: Union[bytes, bytearray, memoryview],
    *,
    ctx: Context | None = None,
) -> DecodedGeometry:
    """Decode WKP bytes to a Shapely geometry."""
    ...


def decode_frame(
    bites: Union[bytes, bytearray, memoryview],
    *,
    ctx: Context | None = None,
) -> GeometryFrame:
    """
    Decode WKP bytes to a :class:`GeometryFrame` without Shapely conversion.

    More efficient than :func:`decode` when you only need the raw coordinate
    arrays (e.g. for numpy / bulk processing / buffer transfer).
    """
    ...


def encode_frame(
    frame: GeometryFrame,
    *,
    ctx: Context | None = None,
) -> bytes:
    """Encode a :class:`GeometryFrame` to WKP bytes."""
    ...


def decode_header(
    bites: Union[bytes, bytearray, memoryview],
) -> tuple[int, int, int, int]:
    """
    Decode only the WKP header without decoding coordinates.

    Returns ``(version, precision, dimensions, geometry_type)``.
    """
    ...


def encode_floats(
    floats: Union[NDArray[np.float64], Sequence[Sequence[float]]],
    precisions: Union[int, Sequence[int]],
    *,
    ctx: Context | None = None,
) -> bytes:
    """Encode a 2-D array of floats with per-column precisions."""
    ...


def decode_floats(
    encoded: Union[bytes, bytearray, memoryview],
    precisions: Union[int, Sequence[int]],
    *,
    ctx: Context | None = None,
) -> list[tuple[float, ...]]:
    """Decode floats encoded by :func:`encode_floats`."""
    ...
