# distutils: language = c++
# cython: language_level = 3

import enum
from dataclasses import dataclass

import numpy as np
cimport cython
from libc.math cimport round
from libc.stdlib cimport malloc, realloc, free
from libc.string cimport memcpy

import shapely
from shapely.geometry import Point


class EncodedGeometryType(enum.Enum):
    POINT = 1
    LINESTRING = 2
    POLYGON = 3
    MULTIPOINT = 4
    MULTILINESTRING = 5
    MULTIPOLYGON = 6


@dataclass
class DecodedGeometry:
    version: int
    precision: int
    dimensions: int
    geometry: shapely.geometry.base.BaseGeometry


cdef int _VERSION = 1
cdef char _SEP_RING = <char>44      # ','
cdef char _SEP_MULTI = <char>59     # ';'


@cython.inline
cdef int _ensure_capacity(char **buffer, Py_ssize_t *capacity, Py_ssize_t needed):
    cdef Py_ssize_t new_capacity
    if needed <= capacity[0]:
        return 0
    new_capacity = capacity[0] * 2
    if new_capacity < needed:
        new_capacity = needed
    buffer[0] = <char*>realloc(buffer[0], new_capacity * sizeof(char))
    if not buffer[0]:
        return 1
    capacity[0] = new_capacity
    return 0


@cython.inline
cdef int _encode_once(Py_ssize_t *idx, char **output, Py_ssize_t *outsize, double curr, double prev, double factor):
    cdef long long curr_scaled, prev_scaled, e2
    curr_scaled = <long long>round(curr * factor)
    prev_scaled = <long long>round(prev * factor)
    e2 = curr_scaled - prev_scaled
    e2 <<= 1
    if e2 < 0:
        e2 = ~e2

    while e2 >= 0x20:
        if _ensure_capacity(output, outsize, idx[0] + 1):
            return 1
        output[0][idx[0]] = <char>((0x20 | (e2 & 0x1F)) + 63)
        idx[0] += 1
        e2 >>= 5

    if _ensure_capacity(output, outsize, idx[0] + 1):
        return 1
    output[0][idx[0]] = <char>(e2 + 63)
    idx[0] += 1
    return 0


@cython.inline
cdef long long _decode_once_bounded(char *encoded, Py_ssize_t *idx, Py_ssize_t end):
    cdef char byte
    cdef int shift = 0
    cdef long long result = 0

    while True:
        if idx[0] >= end:
            raise ValueError("Malformed encoded input")
        byte = encoded[idx[0]] - 63
        idx[0] += 1
        result |= (<long long>(byte & 0x1F)) << shift
        shift += 5
        if byte < 0x20:
            break

    if result & 1:
        return ~(result >> 1)
    return result >> 1


@cython.inline
cdef int _count_values(char *encoded, Py_ssize_t start, Py_ssize_t end):
    cdef Py_ssize_t i
    cdef int count = 0
    cdef char byte
    for i in range(start, end):
        byte = encoded[i] - 63
        if byte < 0x20:
            count += 1
    return count


@cython.boundscheck(False)
@cython.wraparound(False)
cdef class GeometryEncoder:
    cdef public int precision
    cdef public int dimensions
    cdef tuple _precisions
    cdef double _factors[16]
    cdef bytes _header_prefix

    cdef char *_work
    cdef Py_ssize_t _work_capacity

    def __cinit__(self, int precision, int dimensions, Py_ssize_t initial_capacity=4096):
        cdef int i
        if dimensions <= 0 or dimensions > 16:
            raise ValueError("dimensions must be between 1 and 16")
        if initial_capacity < 64:
            initial_capacity = 64

        self.precision = precision
        self.dimensions = dimensions
        self._precisions = tuple([precision] * dimensions)

        for i in range(dimensions):
            self._factors[i] = 10 ** self._precisions[i]

        self._header_prefix = f"{_VERSION:02d}{precision:02d}{dimensions:02d}".encode("ascii")

        self._work_capacity = initial_capacity
        self._work = <char*>malloc(self._work_capacity * sizeof(char))
        if not self._work:
            raise MemoryError("Failed to allocate encoder working buffer")

    def __dealloc__(self):
        if self._work != NULL:
            free(self._work)
            self._work = NULL
            self._work_capacity = 0

    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef Py_ssize_t _append_byte(self, Py_ssize_t idx, char value) except -1:
        cdef char *work_ptr = self._work
        cdef Py_ssize_t capacity = self._work_capacity
        if _ensure_capacity(&work_ptr, &capacity, idx + 1):
            raise MemoryError("Failed to grow encoder working buffer")
        self._work = work_ptr
        self._work_capacity = capacity
        self._work[idx] = value
        return idx + 1

    cdef Py_ssize_t _append_header(self, int geom_type, Py_ssize_t idx) except -1:
        cdef bytes header = self._header_prefix + f"{geom_type:02d}".encode("ascii")
        cdef Py_ssize_t header_len = len(header)
        cdef char *work_ptr = self._work
        cdef Py_ssize_t capacity = self._work_capacity
        cdef char *header_ptr
        if _ensure_capacity(&work_ptr, &capacity, idx + header_len):
            raise MemoryError("Failed to grow encoder working buffer")
        self._work = work_ptr
        self._work_capacity = capacity
        header_ptr = header
        memcpy(self._work + idx, header_ptr, header_len)
        return idx + header_len

    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef Py_ssize_t _encode_coords(self, double[:, :] coords, Py_ssize_t idx) except -1:
        cdef int n_dims = self.dimensions
        cdef Py_ssize_t n_points = coords.shape[0]
        cdef Py_ssize_t i
        cdef long long prev_scaled[16]
        cdef long long curr_scaled
        cdef int j
        cdef char *work_ptr = self._work
        cdef Py_ssize_t capacity = self._work_capacity

        if coords.shape[1] != n_dims:
            raise ValueError(f"Expected coordinates with {n_dims} dimensions, got {coords.shape[1]}")

        for j in range(n_dims):
            prev_scaled[j] = 0

        if _ensure_capacity(&work_ptr, &capacity, idx + n_points * n_dims * 4 + 16):
            raise MemoryError("Failed to grow encoder working buffer")

        self._work = work_ptr
        self._work_capacity = capacity

        if n_dims == 2:
            for i in range(n_points):
                if _encode_once(&idx, &work_ptr, &capacity, coords[i, 0], prev_scaled[0] / self._factors[0], self._factors[0]):
                    raise MemoryError("Failed to encode")
                prev_scaled[0] = <long long>round(coords[i, 0] * self._factors[0])

                if _encode_once(&idx, &work_ptr, &capacity, coords[i, 1], prev_scaled[1] / self._factors[1], self._factors[1]):
                    raise MemoryError("Failed to encode")
                prev_scaled[1] = <long long>round(coords[i, 1] * self._factors[1])
        elif n_dims == 3:
            for i in range(n_points):
                if _encode_once(&idx, &work_ptr, &capacity, coords[i, 0], prev_scaled[0] / self._factors[0], self._factors[0]):
                    raise MemoryError("Failed to encode")
                prev_scaled[0] = <long long>round(coords[i, 0] * self._factors[0])

                if _encode_once(&idx, &work_ptr, &capacity, coords[i, 1], prev_scaled[1] / self._factors[1], self._factors[1]):
                    raise MemoryError("Failed to encode")
                prev_scaled[1] = <long long>round(coords[i, 1] * self._factors[1])

                if _encode_once(&idx, &work_ptr, &capacity, coords[i, 2], prev_scaled[2] / self._factors[2], self._factors[2]):
                    raise MemoryError("Failed to encode")
                prev_scaled[2] = <long long>round(coords[i, 2] * self._factors[2])
        else:
            for i in range(n_points):
                for j in range(n_dims):
                    if _encode_once(&idx, &work_ptr, &capacity, coords[i, j], prev_scaled[j] / self._factors[j], self._factors[j]):
                        raise MemoryError("Failed to encode")
                    prev_scaled[j] = <long long>round(coords[i, j] * self._factors[j])

        self._work = work_ptr
        self._work_capacity = capacity
        return idx

    cdef bytes _finalize(self, Py_ssize_t length):
        return <bytes>self._work[:length]

    cdef int _geom_type_from_geom(self, object geom):
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

    cdef void _check_dimensions(self, object geom):
        if shapely.get_coordinate_dimension(geom) != self.dimensions:
            raise ValueError(
                f"Geometry has {shapely.get_coordinate_dimension(geom)} dimensions but encoder is configured for {self.dimensions} dimensions"
            )

    cpdef bytes _encode_bytes(self, object geom):
        cdef int geom_type = self._geom_type_from_geom(geom)
        cdef Py_ssize_t idx = 0
        cdef object part
        cdef object ring
        cdef double[:, :] coords

        self._check_dimensions(geom)
        idx = self._append_header(geom_type, idx)

        if geom_type == EncodedGeometryType.POINT.value or geom_type == EncodedGeometryType.LINESTRING.value:
            coords = shapely.get_coordinates(geom, include_z=self.dimensions == 3)
            idx = self._encode_coords(coords, idx)
            return self._finalize(idx)

        if geom_type == EncodedGeometryType.POLYGON.value:
            for i, ring in enumerate([geom.exterior] + list(geom.interiors)):
                self._check_dimensions(ring)
                if i > 0:
                    idx = self._append_byte(idx, _SEP_RING)
                coords = shapely.get_coordinates(ring, include_z=self.dimensions == 3)
                idx = self._encode_coords(coords, idx)
            return self._finalize(idx)

        if geom_type == EncodedGeometryType.MULTIPOINT.value:
            for i, part in enumerate(geom.geoms):
                self._check_dimensions(part)
                if i > 0:
                    idx = self._append_byte(idx, _SEP_MULTI)
                coords = shapely.get_coordinates(part, include_z=self.dimensions == 3)
                idx = self._encode_coords(coords, idx)
            return self._finalize(idx)

        if geom_type == EncodedGeometryType.MULTILINESTRING.value:
            for i, part in enumerate(geom.geoms):
                self._check_dimensions(part)
                if i > 0:
                    idx = self._append_byte(idx, _SEP_MULTI)
                coords = shapely.get_coordinates(part, include_z=self.dimensions == 3)
                idx = self._encode_coords(coords, idx)
            return self._finalize(idx)

        if geom_type == EncodedGeometryType.MULTIPOLYGON.value:
            for i, part in enumerate(geom.geoms):
                self._check_dimensions(part)
                if i > 0:
                    idx = self._append_byte(idx, _SEP_MULTI)
                for j, ring in enumerate([part.exterior] + list(part.interiors)):
                    self._check_dimensions(ring)
                    if j > 0:
                        idx = self._append_byte(idx, _SEP_RING)
                    coords = shapely.get_coordinates(ring, include_z=self.dimensions == 3)
                    idx = self._encode_coords(coords, idx)
            return self._finalize(idx)

        raise ValueError("Unsupported geometry type")

    cpdef bytes encode_bytes(self, object geom):
        return self._encode_bytes(geom)

    cpdef str encode(self, object geom):
        return self._encode_bytes(geom).decode("ascii")

    def encode_str(self, object geom):
        return self.encode(geom)

    cdef object _decode_coords_segment(self, bytes encoded, Py_ssize_t start, Py_ssize_t end):
        cdef char *b = encoded
        cdef int values = _count_values(b, start, end)
        cdef int n_dims = self.dimensions
        cdef int n_points
        cdef Py_ssize_t idx = start
        cdef int i, j
        cdef long long prevs[16]
        cdef object arr
        cdef double[:, :] out

        if values == 0:
            return np.empty((0, n_dims), dtype=np.float64)

        if values % n_dims != 0:
            raise ValueError("Malformed encoded coordinate stream")

        n_points = values // n_dims
        arr = np.empty((n_points, n_dims), dtype=np.float64)
        out = arr

        for j in range(n_dims):
            prevs[j] = 0

        if n_dims == 2:
            for i in range(n_points):
                prevs[0] += _decode_once_bounded(b, &idx, end)
                out[i, 0] = prevs[0] / self._factors[0]
                prevs[1] += _decode_once_bounded(b, &idx, end)
                out[i, 1] = prevs[1] / self._factors[1]
        elif n_dims == 3:
            for i in range(n_points):
                prevs[0] += _decode_once_bounded(b, &idx, end)
                out[i, 0] = prevs[0] / self._factors[0]
                prevs[1] += _decode_once_bounded(b, &idx, end)
                out[i, 1] = prevs[1] / self._factors[1]
                prevs[2] += _decode_once_bounded(b, &idx, end)
                out[i, 2] = prevs[2] / self._factors[2]
        else:
            for i in range(n_points):
                for j in range(n_dims):
                    prevs[j] += _decode_once_bounded(b, &idx, end)
                    out[i, j] = prevs[j] / self._factors[j]

        return arr

    cdef list _split_ranges(self, bytes encoded, Py_ssize_t start, Py_ssize_t end, char sep):
        cdef list ranges = []
        cdef Py_ssize_t i
        cdef Py_ssize_t seg_start = start
        cdef char *b = encoded

        for i in range(start, end):
            if b[i] == sep:
                ranges.append((seg_start, i))
                seg_start = i + 1
        ranges.append((seg_start, end))
        return ranges

    cdef object _decode_geometry_body(self, bytes encoded, int geom_type):
        cdef Py_ssize_t body_start = 8
        cdef Py_ssize_t body_end = len(encoded)
        cdef object arr
        cdef list ranges
        cdef tuple r
        cdef list points
        cdef list lines
        cdef list polygons
        cdef list rings
        cdef object shell
        cdef list holes

        if geom_type == EncodedGeometryType.POINT.value:
            arr = self._decode_coords_segment(encoded, body_start, body_end)
            if arr.shape[0] != 1:
                raise ValueError(f"Expected 1 point for POINT geometry, got {arr.shape[0]}")
            return Point(arr[0])

        if geom_type == EncodedGeometryType.LINESTRING.value:
            arr = self._decode_coords_segment(encoded, body_start, body_end)
            if arr.shape[0] < 2:
                raise ValueError(f"Expected at least 2 points for LINESTRING geometry, got {arr.shape[0]}")
            return shapely.linestrings(arr)

        if geom_type == EncodedGeometryType.POLYGON.value:
            ranges = self._split_ranges(encoded, body_start, body_end, _SEP_RING)
            rings = [shapely.linearrings(self._decode_coords_segment(encoded, r[0], r[1])) for r in ranges]
            if len(rings) == 0:
                raise ValueError("Expected at least 1 ring for POLYGON")
            shell = rings[0]
            holes = rings[1:]
            return shapely.polygons(shell, holes=holes if holes else None)

        if geom_type == EncodedGeometryType.MULTIPOINT.value:
            ranges = self._split_ranges(encoded, body_start, body_end, _SEP_MULTI)
            points = []
            for r in ranges:
                arr = self._decode_coords_segment(encoded, r[0], r[1])
                if arr.shape[0] != 1:
                    raise ValueError(f"Expected 1 point segment in MULTIPOINT, got {arr.shape[0]}")
                points.append(arr[0])
            return shapely.multipoints(np.asarray(points, dtype=np.float64))

        if geom_type == EncodedGeometryType.MULTILINESTRING.value:
            ranges = self._split_ranges(encoded, body_start, body_end, _SEP_MULTI)
            lines = [self._decode_coords_segment(encoded, r[0], r[1]) for r in ranges]
            return shapely.multilinestrings(lines)

        if geom_type == EncodedGeometryType.MULTIPOLYGON.value:
            ranges = self._split_ranges(encoded, body_start, body_end, _SEP_MULTI)
            polygons = []
            for r in ranges:
                rings = self._split_ranges(encoded, r[0], r[1], _SEP_RING)
                rings = [shapely.linearrings(self._decode_coords_segment(encoded, rr[0], rr[1])) for rr in rings]
                if len(rings) == 0:
                    raise ValueError("Expected at least 1 ring in MULTIPOLYGON polygon")
                shell = rings[0]
                holes = rings[1:]
                polygons.append(shapely.polygons(shell, holes=holes if holes else None))
            return shapely.multipolygons(polygons)

        raise ValueError(f"Unsupported geometry type in header: {geom_type}")

    cpdef object decode_bytes(self, bytes encoded):
        cdef int version
        cdef int precision
        cdef int dimensions
        cdef int geom_type

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
    def decode_header(bytes encoded):
        version = int(encoded[0:2])
        if version != _VERSION:
            raise ValueError(f"Unsupported geometry encoding version: {version}")
        precision = int(encoded[2:4])
        dimensions = int(encoded[4:6])
        geom_type = int(encoded[6:8])
        return version, precision, dimensions, geom_type

    @staticmethod
    def decode(encoded):
        cdef bytes encoded_bytes
        if isinstance(encoded, str):
            encoded_bytes = encoded.encode("ascii")
        else:
            encoded_bytes = encoded
        version, precision, dimensions, _ = GeometryEncoder.decode_header(encoded_bytes)
        encoder = GeometryEncoder(precision=precision, dimensions=dimensions)
        decoded = encoder.decode_bytes(encoded_bytes)
        assert decoded.version == version
        return decoded

    def decode_str(self, str encoded):
        return self.decode_bytes(encoded.encode("ascii"))


cdef tuple _normalize_precisions(int n, precisions):
    if isinstance(precisions, int):
        return tuple([precisions] * n)
    if len(precisions) != n:
        raise ValueError(f"Expected {n} precisions, got {len(precisions)}")
    return tuple(precisions)


cdef bytes _encode_with_precisions(double[:, :] floats, int n, tuple precisions):
    cdef Py_ssize_t num_values = floats.shape[0]
    cdef Py_ssize_t outsize = num_values * n * 4 + 16
    cdef char *output = NULL
    cdef Py_ssize_t idx = 0
    cdef int i, j
    cdef double factors[16]
    cdef double prevs[16]
    cdef bytes out

    if n <= 0 or n > 16:
        raise ValueError("n must be between 1 and 16")
    if floats.shape[1] != n:
        raise ValueError(f"Expected coordinates with {n} dimensions, got {floats.shape[1]}")

    if outsize < 64:
        outsize = 64

    output = <char*>malloc(outsize * sizeof(char))
    if not output:
        raise MemoryError("Failed to allocate encode buffer")

    for j in range(n):
        factors[j] = 10 ** precisions[j]
        prevs[j] = 0.0

    for i in range(num_values):
        for j in range(n):
            if _encode_once(&idx, &output, &outsize, floats[i, j], prevs[j], factors[j]):
                if output != NULL:
                    free(output)
                raise MemoryError("Failed to grow encode buffer")
            prevs[j] = floats[i, j]

    out = <bytes>output[:idx]
    if output != NULL:
        free(output)
    return out


cpdef bytes encode_floats_array(double[:, :] floats, int n, precisions):
    return _encode_with_precisions(floats, n, _normalize_precisions(n, precisions))


cpdef bytes encode_floats(floats, int n, precisions):
    arr = np.asarray(floats, dtype=np.float64)
    return _encode_with_precisions(arr, n, _normalize_precisions(n, precisions))


cpdef list decode_floats(bytes encoded, int n, precisions):
    cdef tuple p = _normalize_precisions(n, precisions)
    cdef int i, j
    cdef int values
    cdef int n_points
    cdef double factors[16]
    cdef long long prevs[16]
    cdef char *b = encoded
    cdef Py_ssize_t idx = 0
    cdef list out = []
    cdef list row

    if n <= 0 or n > 16:
        raise ValueError("n must be between 1 and 16")

    values = _count_values(b, 0, len(encoded))
    if values % n != 0:
        raise ValueError("Malformed encoded coordinate stream")
    n_points = values // n

    for j in range(n):
        factors[j] = 10 ** p[j]
        prevs[j] = 0

    for i in range(n_points):
        row = []
        for j in range(n):
            prevs[j] += _decode_once_bounded(b, &idx, len(encoded))
            row.append(prevs[j] / factors[j])
        out.append(tuple(row))

    return out
