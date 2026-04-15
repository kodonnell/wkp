from random import random

import numpy as np
import wkp
from wkp import decode_floats, decode_floats_array, encode_floats


def test_array_encode_matches_list_encode_2d():
    out = [(round((random() - 0.5) * 1000, 5), round(random() * 1000, 5)) for _ in range(50)]
    arr = np.asarray(out, dtype=np.float64)
    encoded_arr = encode_floats(arr, [5, 5])
    encoded_list = encode_floats(out, [5, 5])
    assert encoded_arr == encoded_list


def test_array_encode_matches_list_encode_3d():
    out = [(round((random() - 0.5) * 1000, 6), round(random() * 1000, 6), round(random() * 1000)) for _ in range(50)]
    arr = np.asarray(out, dtype=np.float64)
    encoded_arr = encode_floats(arr, [6, 6, 0])
    encoded_list = encode_floats(out, [6, 6, 0])
    assert encoded_arr == encoded_list


def test_decode_roundtrip():
    out = [(round((random() - 0.5) * 1000, 3), round(random() * 1000, 4), round(random() * 1000, 2)) for _ in range(50)]
    encoded = encode_floats(out, [3, 4, 2])
    decoded = decode_floats(encoded, [3, 4, 2])
    assert isinstance(decoded, np.ndarray)
    assert decoded.shape == (50, 3)
    np.testing.assert_allclose(decoded, np.asarray(out, dtype=np.float64), rtol=0, atol=0)


def test_explicit_context_is_accepted():
    ctx = wkp.Context()
    out = [(round((random() - 0.5) * 1000, 5), round(random() * 1000, 5)) for _ in range(50)]
    encoded = encode_floats(out, [5, 5], ctx=ctx)
    decoded = decode_floats(encoded, [5, 5], ctx=ctx)
    assert isinstance(decoded, np.ndarray)
    assert decoded.shape == (50, 2)
    np.testing.assert_allclose(decoded, np.asarray(out, dtype=np.float64), rtol=0, atol=0)


def test_decode_floats_array_roundtrip():
    out = [(round((random() - 0.5) * 1000, 3), round(random() * 1000, 4)) for _ in range(50)]
    encoded = encode_floats(out, [3, 4])
    arr = decode_floats_array(encoded, [3, 4])
    assert isinstance(arr, np.ndarray)
    assert arr.ndim == 1                # flat 1D
    assert arr.shape == (100,)          # 50 points × 2 dims
    assert arr.dtype == np.float64
    np.testing.assert_allclose(arr.reshape(50, 2), np.asarray(out, dtype=np.float64), rtol=0, atol=0)


def test_decode_floats_array_tobytes_roundtrip():
    """Flat array can be shipped as raw bytes and re-interpreted."""
    out = [(1.0, 2.0), (3.0, 4.0), (5.0, 6.0)]
    encoded = encode_floats(out, [5, 5])
    flat = decode_floats_array(encoded, [5, 5])
    recovered = np.frombuffer(flat.tobytes(), dtype=np.float64)
    np.testing.assert_array_equal(flat, recovered)


def test_decode_floats_array_matches_decode_floats():
    out = [(round((random() - 0.5) * 1000, 5), round(random() * 1000, 5), round(random() * 1000)) for _ in range(30)]
    encoded = encode_floats(out, [5, 5, 0])
    flat = decode_floats_array(encoded, [5, 5, 0])
    arr2d = decode_floats(encoded, [5, 5, 0])
    assert flat.ndim == 1
    assert flat.shape == (90,)          # 30 points × 3 dims
    assert arr2d.ndim == 2
    assert arr2d.shape == (30, 3)
    np.testing.assert_allclose(flat.reshape(30, 3), arr2d, rtol=0, atol=0)


def test_run_core_self_test():
    assert wkp._core.run_self_test() is True
