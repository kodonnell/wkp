from random import random

import numpy as np
import wkp
from wkp import decode_floats, encode_floats


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
    np.testing.assert_allclose(np.asarray(decoded, dtype=np.float64), np.asarray(out, dtype=np.float64), rtol=0, atol=0)


def test_explicit_context_is_accepted():
    ctx = wkp.Context()
    out = [(round((random() - 0.5) * 1000, 5), round(random() * 1000, 5)) for _ in range(50)]
    encoded = encode_floats(out, [5, 5], ctx=ctx)
    decoded = decode_floats(encoded, [5, 5], ctx=ctx)
    np.testing.assert_allclose(np.asarray(decoded, dtype=np.float64), np.asarray(out, dtype=np.float64), rtol=0, atol=0)


def test_run_core_self_test():
    assert wkp._core.run_self_test() is True
