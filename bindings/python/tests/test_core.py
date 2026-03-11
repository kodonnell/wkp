from random import random

import numpy as np
from wkp import decode_floats, encode_floats


def test_array_encode_matches_list_encode_2d():
    from wkp import Context

    ctx = Context()
    out = [(round((random() - 0.5) * 1000, 5), round(random() * 1000, 5)) for _ in range(50)]
    arr = np.asarray(out, dtype=np.float64)
    encoded_arr = encode_floats(ctx, arr, [5, 5])
    encoded_list = encode_floats(ctx, out, [5, 5])
    assert encoded_arr == encoded_list


def test_array_encode_matches_list_encode_3d():
    from wkp import Context

    ctx = Context()
    out = [(round((random() - 0.5) * 1000, 6), round(random() * 1000, 6), round(random() * 1000)) for _ in range(50)]
    arr = np.asarray(out, dtype=np.float64)
    encoded_arr = encode_floats(ctx, arr, [6, 6, 0])
    encoded_list = encode_floats(ctx, out, [6, 6, 0])
    assert encoded_arr == encoded_list


def test_decode_roundtrip():
    from wkp import Context

    ctx = Context()
    out = [(round((random() - 0.5) * 1000, 3), round(random() * 1000, 4), round(random() * 1000, 2)) for _ in range(50)]
    encoded = encode_floats(ctx, out, [3, 4, 2])
    decoded = decode_floats(ctx, encoded, [3, 4, 2])
    np.testing.assert_allclose(np.asarray(decoded, dtype=np.float64), np.asarray(out, dtype=np.float64), rtol=0, atol=0)
