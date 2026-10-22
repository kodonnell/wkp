"""WKP - Well Known Polylines: compact, fast encoding of geometries to strings.

The implementation is in ``wkp._api`` (and the native ``wkp._core``); this module re-exports the
public API.
"""

import warnings as _warnings

from ._api import (
    Context,
    DecodedGeometry,
    GeometryFrame,
    GeometryType,
    Header,
    WkpError,
    __core_compatibility__,
    __core_version__,
    __version__,
    decode,
    decode_floats,
    decode_floats_array,
    decode_frame,
    decode_header,
    encode,
    encode_floats,
    encode_frame,
)

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


def __getattr__(name: str):
    # EncodedGeometryType was renamed GeometryType in 0.7.
    if name == "EncodedGeometryType":
        _warnings.warn("wkp.EncodedGeometryType is deprecated; use wkp.GeometryType", DeprecationWarning, stacklevel=2)
        return GeometryType
    raise AttributeError(f"module 'wkp' has no attribute {name!r}")
