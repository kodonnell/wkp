from random import random

import polyline
import pytest
import shapely
import wkp


@pytest.mark.parametrize(
    "input,n,precision,expected",
    [
        ([[1.0, 1.1, 1.2], [2.1, 2.2, 2.3]], 3, (3, 3, 3), "o}@wcA_jAwcAwcAwcA"),
        ([[1.0, 1.1, 1.2], [2.1, 2.2, 2.3]], 3, 3, "o}@wcA_jAwcAwcAwcA"),
        ([[4.712723, 7.846801], [36.651759, 9.693021]], 2, 5, "omw[oq{n@_b}aE{qgJ"),
        ([(38.5, -120.2), (40.7, -120.95), (43.252, -126.453)], 2, 5, "_p~iF~ps|U_ulLnnqC_mqNvxq`@"),
    ],
)
def test_known_encodes(input, n, precision, expected):
    assert wkp.encode_floats(input, n, precision) == expected.encode("ascii")


def test_known_encode3():
    # This one pointed out we've got an overflow in the last, so check that
    input = [(175.26025, -37.79209, 1677818753)]
    s = wkp.encode_floats(input, 3, (6, 6, 0))
    decoded = wkp.decode_floats(s, 3, (6, 6, 0))
    assert decoded[0][0] == input[0][0]
    assert decoded[0][1] == input[0][1]
    assert decoded[0][2] == float(input[0][2])


def test_multi_encode_decode_noop():
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 1000, 3), round(random() * 1000, 4)) for i in range(10)]
    encoded = wkp.encode_floats(out, 3, [2, 3, 4])
    decoded = wkp.decode_floats(encoded, 3, [2, 3, 4])
    assert out == decoded


def test_multi_matches_polyline_package():
    """
    Compare out encoding of lat/lon pairs to the polyline package, which is a reference implementation of Google's polyline encoding.
    """
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 2)) for i in range(10)]
    encoded = wkp.encode_floats(out, 2, [2, 2])
    assert encoded.decode("ascii") == polyline.encode(out, 2)


def test_encode_decode_bytes_roundtrip():
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 1000, 3), round(random() * 1000, 4)) for i in range(10)]
    encoded = wkp.encode_floats(out, 3, [2, 3, 4])
    assert isinstance(encoded, bytes)
    decoded = wkp.decode_floats(encoded, 3, [2, 3, 4])
    assert out == decoded


def test_encode_array_matches_list():
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 2)) for i in range(10)]
    arr = shapely.get_coordinates(shapely.LineString(out), include_z=False)

    encoded_from_list = wkp.encode_floats(out, 2, [2, 2])
    encoded_from_array = wkp.encode_floats_array(arr, 2, [2, 2])

    assert encoded_from_array == encoded_from_list


def test_encode_array_bytes_roundtrip():
    out = [(round((random() - 0.5) * 1000, 2), round(random() * 2)) for i in range(10)]
    arr = shapely.get_coordinates(shapely.LineString(out), include_z=False)

    encoded = wkp.encode_floats_array(arr, 2, [2, 2])
    decoded = wkp.decode_floats(encoded, 2, [2, 2])

    assert decoded == out
