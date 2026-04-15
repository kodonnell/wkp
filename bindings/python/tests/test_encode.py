from random import random

import numpy as np
import polyline
import shapely
import wkp


def test_known_encode3():
    # This one pointed out we've got an overflow in the last, so check that
    input = [(175.26025, -37.79209, 1677818753)]
    s = wkp.encode_floats(input, (6, 6, 0))
    decoded = wkp.decode_floats(s, (6, 6, 0))
    assert decoded[0, 0] == input[0][0]
    assert decoded[0, 1] == input[0][1]
    assert decoded[0, 2] == float(input[0][2])


def test_multi_encode_decode_noop():
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 1000, 3), round(random() * 1000, 4)) for i in range(10)]
    encoded = wkp.encode_floats(out, [2, 3, 4])
    decoded = wkp.decode_floats(encoded, [2, 3, 4])
    assert isinstance(decoded, np.ndarray)
    assert decoded.shape == (10, 3)
    np.testing.assert_array_equal(decoded, np.asarray(out))


def test_multi_matches_polyline_package():
    """
    Compare our encoding of lat/lon pairs to the polyline package, which is a reference implementation of Google's polyline encoding.
    """
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 2)) for i in range(10)]
    encoded = wkp.encode_floats(out, [2, 2])
    assert encoded.decode("ascii") == polyline.encode(out, 2)


def test_encode_decode_bytes_roundtrip():
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 1000, 3), round(random() * 1000, 4)) for i in range(10)]
    encoded = wkp.encode_floats(out, [2, 3, 4])
    assert isinstance(encoded, bytes)
    decoded = wkp.decode_floats(encoded, [2, 3, 4])
    np.testing.assert_array_equal(decoded, np.asarray(out))


def test_encode_array_matches_list():
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 2)) for i in range(10)]
    arr = shapely.get_coordinates(shapely.LineString(out), include_z=False)

    encoded_from_list = wkp.encode_floats(out, [2, 2])
    encoded_from_array = wkp.encode_floats(arr, [2, 2])

    assert encoded_from_array == encoded_from_list


def test_encode_array_bytes_roundtrip():
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 2)) for i in range(10)]
    arr = shapely.get_coordinates(shapely.LineString(out), include_z=False)

    encoded = wkp.encode_floats(arr, [2, 2])
    decoded = wkp.decode_floats(encoded, [2, 2])

    np.testing.assert_array_equal(decoded, arr)


def test_explicit_context_is_accepted():
    ctx = wkp.Context()
    out = [(1.0, 2.0), (3.0, 4.0)]
    encoded = wkp.encode_floats(out, [2, 2], ctx=ctx)
    decoded = wkp.decode_floats(encoded, [2, 2], ctx=ctx)
    np.testing.assert_array_equal(decoded, np.asarray(out))
