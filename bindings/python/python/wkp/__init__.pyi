"""Type stubs for the public wkp Python API."""

from __future__ import annotations

import enum
from dataclasses import dataclass
from typing import ClassVar, NamedTuple, Sequence, Union, final

import numpy as np
from numpy.typing import NDArray
import shapely.geometry.base
from typing_extensions import deprecated

__version__: str
__core_version__: str
__core_compatibility__: str

__all__ = [
    "Context",
    "DecodedGeometry",
    "GeometryFrame",
    "GeometryType",
    "Header",
    "WkpError",
    "decode",
    "decode_frame",
    "decode_header",
    "encode",
    "encode_frame",
    "encode_floats",
    "decode_floats",
    "decode_floats_array",
]


class WkpError(ValueError):
    """
    An input wkp cannot encode or decode. ``code`` is ``"INVALID_ARGUMENT"`` (an argument is
    out of range or inconsistent) or ``"MALFORMED_INPUT"`` (the encoded data is not valid WKP).
    A ``ValueError``, so existing ``except ValueError`` handlers still catch it. Arguments of the
    wrong type raise ``TypeError`` instead.
    """

    code: str

    def __init__(self, message: str, code: str = "INVALID_ARGUMENT") -> None: ...


class GeometryType(enum.IntEnum):
    POINT = 1
    LINESTRING = 2
    POLYGON = 3
    MULTIPOINT = 4
    MULTILINESTRING = 5
    MULTIPOLYGON = 6


@deprecated("EncodedGeometryType was renamed GeometryType in 0.7")
class EncodedGeometryType(enum.IntEnum):
    POINT = 1
    LINESTRING = 2
    POLYGON = 3
    MULTIPOINT = 4
    MULTILINESTRING = 5
    MULTIPOLYGON = 6


class Header(NamedTuple):
    """The four header fields of an encoded geometry (a tuple, so it also unpacks)."""

    version: int
    precision: int
    dimensions: int
    geometry_type: GeometryType


# A native (pyo3) class: it cannot be subclassed.
@final
class Context:
    """
    A native buffer pool.  Buffers grow to fit the largest geometry processed,
    then stay allocated and are reused on subsequent calls.

    Pass ``ctx=None`` (the default) to use a per-thread lazily-created context.
    Pass an explicit ``Context()`` instance for fine-grained lifetime control.
    """

    def __init__(self) -> None: ...


@dataclass
class DecodedGeometry:
    """Result of :func:`decode`."""

    version: int
    precision: int
    dimensions: int
    geometry: shapely.geometry.base.BaseGeometry


@dataclass(eq=False)
class GeometryFrame:
    """
    Low-level flat representation of a decoded WKP geometry.

    ``coords`` shape is ``(total_points, dimensions)``, ``dtype=float64``,
    C-contiguous.  ``segment_point_counts[i]`` is the number of points in
    segment *i*.  ``group_segment_counts[g]`` is the number of segments in
    group *g*.  Frames compare equal when all fields (arrays by value) are equal.
    """

    version: int
    precision: int
    dimensions: int
    geometry_type: int
    coords: NDArray[np.float64]
    segment_point_counts: NDArray[np.uint64]
    group_segment_counts: NDArray[np.uint64]

    def __eq__(self, other: object) -> bool: ...
    __hash__: ClassVar[None]  # type: ignore[assignment]

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
) -> str:
    """Encode a Shapely geometry to a WKP string."""
    ...


def decode(
    encoded: Union[str, bytes, bytearray, memoryview],
    *,
    ctx: Context | None = None,
) -> DecodedGeometry:
    """Decode a WKP string (or bytes) to a Shapely geometry."""
    ...


def decode_frame(
    encoded: Union[str, bytes, bytearray, memoryview],
    *,
    ctx: Context | None = None,
) -> GeometryFrame:
    """
    Decode a WKP string (or bytes) to a :class:`GeometryFrame` without Shapely conversion.

    More efficient than :func:`decode` when you only need the raw coordinate
    arrays (e.g. for numpy / bulk processing / buffer transfer).
    """
    ...


def encode_frame(
    frame: GeometryFrame,
    *,
    ctx: Context | None = None,
) -> str:
    """Encode a :class:`GeometryFrame` to a WKP string."""
    ...


def decode_header(
    encoded: Union[str, bytes, bytearray, memoryview],
) -> Header:
    """
    Decode only the WKP header without decoding coordinates.

    Returns a :class:`Header`, ``(version, precision, dimensions, geometry_type)``.
    """
    ...


def encode_floats(
    floats: Union[NDArray[np.float64], Sequence[Sequence[float]]],
    precisions: Union[int, Sequence[int]],
    *,
    ctx: Context | None = None,
) -> str:
    """
    Encode a 2-D array of floats (one column per dimension) to a headerless WKP string.

    ``precisions`` has one int per column; a single int means one column.
    """
    ...


def decode_floats(
    encoded: Union[str, bytes, bytearray, memoryview],
    precisions: Union[int, Sequence[int]],
    *,
    ctx: Context | None = None,
) -> NDArray[np.float64]:
    """
    Decode floats encoded by :func:`encode_floats` to a 2-D numpy array of shape ``(N, dims)``.

    Use ``.tolist()`` to convert to a list of plain Python lists if needed.
    """
    ...


def decode_floats_array(
    encoded: Union[str, bytes, bytearray, memoryview],
    precisions: Union[int, Sequence[int]],
    *,
    ctx: Context | None = None,
) -> NDArray[np.float64]:
    """
    Decode floats encoded by :func:`encode_floats` to a flat 1-D numpy array.

    Returns interleaved values ``[x0, y0, x1, y1, ...]`` as a C-contiguous 1-D
    float64 array.  Use ``arr.reshape(-1, dims)`` to get a 2-D view, or
    ``arr.tobytes()`` to treat it as a raw byte buffer — useful for zero-copy
    transfer over sockets or shared memory.
    """
    ...
