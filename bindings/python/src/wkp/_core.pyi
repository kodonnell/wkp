"""WKP nanobind core wrapper"""

import enum
from collections.abc import Sequence
from typing import Annotated

import numpy
from numpy.typing import NDArray

def core_version() -> str: ...

class EncodedGeometryType(enum.Enum):
    POINT = 1

    LINESTRING = 2

    POLYGON = 3

    MULTIPOINT = 4

    MULTILINESTRING = 5

    MULTIPOLYGON = 6

def decode_geometry_header(encoded: bytes) -> tuple: ...
def decode_geometry_frame(encoded: bytes) -> tuple: ...

class WorkspaceCore:
    def __init__(
        self,
        initial_u8_capacity: int = 4096,
        initial_f64_capacity: int = 256,
        max_u8_size: int = -1,
        max_f64_size: int = -1,
    ) -> None: ...
    def encode_floats(
        self,
        values: Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)],
        dimensions: int,
        precisions: Sequence[int],
    ) -> bytes: ...
    def decode_floats(
        self, encoded: bytes, dimensions: int, precisions: Sequence[int]
    ) -> Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu")]: ...
    def encode_point(
        self,
        coords: Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)],
        precision: int,
    ) -> bytes: ...
    def encode_linestring(
        self,
        coords: Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)],
        precision: int,
    ) -> bytes: ...
    def encode_polygon(
        self,
        rings: Sequence[
            Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)]
        ],
        precision: int,
    ) -> bytes: ...
    def encode_multipoint(
        self,
        points: Sequence[
            Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)]
        ],
        precision: int,
    ) -> bytes: ...
    def encode_multilinestring(
        self,
        lines: Sequence[
            Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)]
        ],
        precision: int,
    ) -> bytes: ...
    def encode_multipolygon(
        self,
        polygons: Sequence[
            Sequence[
                Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)]
            ]
        ],
        precision: int,
    ) -> bytes: ...
    def decode_geometry_frame(self, encoded: bytes) -> tuple: ...

def encode_floats(
    values: Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)],
    dimensions: int,
    precisions: Sequence[int],
) -> bytes: ...
def decode_floats(
    encoded: bytes, dimensions: int, precisions: Sequence[int]
) -> Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu")]: ...
def encode_floats_into(
    values: Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu", writable=False)],
    dimensions: int,
    precisions: Sequence[int],
    out_buffer: Annotated[NDArray[numpy.uint8], dict(shape=(None,), order="C", device="cpu")],
) -> int: ...
def decode_floats_into(
    encoded: bytes,
    dimensions: int,
    precisions: Sequence[int],
    out_buffer: Annotated[NDArray[numpy.float64], dict(shape=(None, None), order="C", device="cpu")],
) -> int: ...
