const test = require('node:test');
const assert = require('node:assert/strict');

const {
    Context,
    decode,
    decodeFloats,
    decodeHeader,
    encode,
    encodeFloats,
    EncodedGeometryType
} = require('..');

function assertGeometryClose(actual, expected, epsilon = 1e-6) {
    assert.equal(actual.type, expected.type);

    function walk(a, e) {
        if (Array.isArray(e)) {
            assert.ok(Array.isArray(a));
            assert.equal(a.length, e.length);
            for (let i = 0; i < e.length; i += 1) {
                walk(a[i], e[i]);
            }
            return;
        }
        assert.ok(Math.abs(a - e) <= epsilon, `expected ${a} to be within ${epsilon} of ${e}`);
    }

    walk(actual.coordinates, expected.coordinates);
}

test('roundtrip for all supported geometry types', () => {
    const ctx = new Context();
    const geometries = [
        { type: 'Point', coordinates: [174.776, -41.289] },
        { type: 'LineString', coordinates: [[174.776, -41.289], [174.777, -41.29], [174.778, -41.291]] },
        { type: 'Polygon', coordinates: [[[0, 0], [1, 0], [1, 1], [0, 1], [0, 0]]] },
        { type: 'MultiPoint', coordinates: [[0, 0], [1, 1]] },
        { type: 'MultiLineString', coordinates: [[[0, 0], [1, 1]], [[2, 2], [3, 3]]] },
        {
            type: 'MultiPolygon',
            coordinates: [
                [[[0, 0], [1, 0], [1, 1], [0, 1], [0, 0]]],
                [[[2, 2], [3, 2], [3, 3], [2, 3], [2, 2]]]
            ]
        }
    ];

    for (const item of geometries) {
        const encoded = encode(ctx, item, 6);
        const decoded = decode(ctx, encoded);

        assert.equal(decoded.version, 1);
        assert.equal(decoded.precision, 6);
        assert.equal(decoded.dimensions, 2);
        assertGeometryClose(decoded.geometry, item);
    }
});

test('decodeHeader and generic encode/decode path', () => {
    const ctx = new Context();
    const geometry = { type: 'LineString', coordinates: [[0.1, 0.2], [1.1, 1.2], [2.1, 2.2]] };

    const encoded = encode(ctx, geometry, 6);
    const [version, precision, dimensions, geometryType] = decodeHeader(encoded);
    assert.equal(version, 1);
    assert.equal(precision, 6);
    assert.equal(dimensions, 2);
    assert.equal(geometryType, EncodedGeometryType.LINESTRING);

    const decoded = decode(ctx, encoded);
    assertGeometryClose(decoded.geometry, geometry);
});

test('invalid precision and malformed input surface clear errors', () => {
    const ctx = new Context();
    assert.throws(() => encode(ctx, { type: 'Point', coordinates: [0, 0] }, 9999));
    assert.throws(() => decode(ctx, '@@'));
});

test('unsupported geometry collection raises', () => {
    const ctx = new Context();
    const geometryCollection = {
        type: 'GeometryCollection',
        geometries: [{ type: 'Point', coordinates: [0, 0] }]
    };
    assert.throws(() => encode(ctx, geometryCollection, 6));
});

test('encodeFloats/decodeFloats roundtrip', () => {
    const ctx = new Context();
    const rows = [[1.25, 2.5, 3.75], [4.5, 5.25, 6.0]];
    const precisions = [2, 2, 2];

    const encoded = encodeFloats(ctx, rows, precisions);
    const decoded = decodeFloats(ctx, encoded, precisions);

    assert.equal(decoded.length, rows.length);
    for (let i = 0; i < rows.length; i += 1) {
        for (let j = 0; j < rows[i].length; j += 1) {
            assert.ok(Math.abs(decoded[i][j] - rows[i][j]) <= 1e-6);
        }
    }
});
