from random import random

import numpy as np
from wkp import decode_floats, decode_floats_into, encode_floats, encode_floats_array, encode_floats_into


def test_array_encode_matches_list_encode_2d():
    out = [(round((random() - 0.5) * 1000, 5), round(random() * 1000, 5)) for _ in range(50)]
    arr = np.asarray(out, dtype=np.float64)
    encoded_arr = encode_floats_array(arr, 2, [5, 5])
    encoded_list = encode_floats(out, 2, [5, 5])
    assert encoded_arr == encoded_list


def test_array_encode_matches_list_encode_3d():
    out = [(round((random() - 0.5) * 1000, 6), round(random() * 1000, 6), round(random() * 1000)) for _ in range(50)]
    arr = np.asarray(out, dtype=np.float64)
    encoded_arr = encode_floats_array(arr, 3, [6, 6, 0])
    encoded_list = encode_floats(out, 3, [6, 6, 0])
    assert encoded_arr == encoded_list


def test_decode_roundtrip():
    out = [(round((random() - 0.5) * 1000, 3), round(random() * 1000, 4), round(random() * 1000, 2)) for _ in range(50)]
    encoded = encode_floats(out, 3, [3, 4, 2])
    decoded = decode_floats(encoded, 3, [3, 4, 2])
    np.testing.assert_allclose(np.asarray(decoded, dtype=np.float64), np.asarray(out, dtype=np.float64), rtol=0, atol=0)


def test_encode_into_and_decode_into_roundtrip():
    out = [(round((random() - 0.5) * 1000, 3), round(random() * 1000, 4), round(random() * 1000, 2)) for _ in range(50)]
    expected_encoded = encode_floats(out, 3, [3, 4, 2])

    encode_buffer = np.empty(len(expected_encoded), dtype=np.uint8)
    encoded_size = encode_floats_into(out, 3, [3, 4, 2], encode_buffer)
    assert encoded_size == len(expected_encoded)
    encoded = bytes(encode_buffer[:encoded_size])
    assert encoded == expected_encoded

    decode_buffer = np.empty((len(out), 3), dtype=np.float64)
    decoded_rows = decode_floats_into(encoded, 3, [3, 4, 2], decode_buffer)
    assert decoded_rows == len(out)
    np.testing.assert_allclose(decode_buffer[:decoded_rows], np.asarray(out, dtype=np.float64), rtol=0, atol=0)


def test_encode_into_small_buffer_raises():
    out = [(1.0, 2.0), (3.0, 4.0)]
    tiny = np.empty(1, dtype=np.uint8)
    try:
        encode_floats_into(out, 2, [5, 5], tiny)
        assert False, "Expected ValueError for too-small output buffer"
    except ValueError as exc:
        assert "too small" in str(exc)


def test_decode_into_small_buffer_raises():
    out = [(1.0, 2.0), (3.0, 4.0)]
    encoded = encode_floats(out, 2, [5, 5])
    tiny = np.empty((1, 2), dtype=np.float64)
    try:
        decode_floats_into(encoded, 2, [5, 5], tiny)
        assert False, "Expected ValueError for too-small output buffer"
    except ValueError as exc:
        assert "too small" in str(exc)
