const test = require('node:test');
const assert = require('node:assert/strict');

const { GeometryEncoder, EncodedGeometryType, encodeF64, decodeF64 } = require('..');

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

test('GeometryEncoder roundtrip for all geometry types', () => {
    const encoder = new GeometryEncoder(6, 2);
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

    for (const geometry of geometries) {
        const encoded = encoder.encode(geometry);
        const decoded = GeometryEncoder.decode(encoded);

        assert.equal(decoded.version, 1);
        assert.equal(decoded.precision, 6);
        assert.equal(decoded.dimensions, 2);
        assertGeometryClose(decoded.geometry, geometry);
    }
});

test('GeometryEncoder header and bytes helpers', () => {
    const encoder = new GeometryEncoder(6, 2);
    const geometry = { type: 'LineString', coordinates: [[0.1, 0.2], [1.1, 1.2], [2.1, 2.2]] };

    const encodedBytes = encoder.encodeBytes(geometry);
    assert.ok(Buffer.isBuffer(encodedBytes));

    const [version, precision, dimensions, geometryType] = GeometryEncoder.decodeHeader(encodedBytes);
    assert.equal(version, 1);
    assert.equal(precision, 6);
    assert.equal(dimensions, 2);
    assert.equal(geometryType, EncodedGeometryType.LINESTRING);

    const decoded = encoder.decodeBytes(encodedBytes);
    assertGeometryClose(decoded.geometry, geometry);
});

test('GeometryEncoder enforces dimensions', () => {
    const encoder = new GeometryEncoder(6, 2);
    assert.throws(
        () => encoder.encode({ type: 'Point', coordinates: [174.776, -41.289, 123.4] }),
        /array of 2 numbers/i
    );
});

test('known polyline vectors match expected encodings', () => {
    const vectors = [
        {
            values: [1.0, 1.1, 1.2, 2.1, 2.2, 2.3],
            dimensions: 3,
            precisions: [3, 3, 3],
            expected: 'o}@wcA_jAwcAwcAwcA'
        },
        {
            values: [1.0, 1.1, 1.2, 2.1, 2.2, 2.3],
            dimensions: 3,
            precisions: [3],
            expected: 'o}@wcA_jAwcAwcAwcA'
        },
        {
            values: [4.712723, 7.846801, 36.651759, 9.693021],
            dimensions: 2,
            precisions: [5],
            expected: 'omw[oq{n@_b}aE{qgJ'
        },
        {
            values: [38.5, -120.2, 40.7, -120.95, 43.252, -126.453],
            dimensions: 2,
            precisions: [5],
            expected: '_p~iF~ps|U_ulLnnqC_mqNvxq`@'
        }
    ];

    for (const vector of vectors) {
        const encodedBytes = encodeF64(Float64Array.from(vector.values), vector.dimensions, vector.precisions);
        const encoded = Buffer.from(encodedBytes).toString('ascii');
        assert.equal(encoded, vector.expected);
    }
});

test('known 3d mixed precision value decodes without overflow', () => {
    const values = [175.26025, -37.79209, 1677818753];
    const precisions = [6, 6, 0];
    const encodedBytes = encodeF64(Float64Array.from(values), 3, precisions);
    const decoded = decodeF64(encodedBytes, 3, precisions);

    assert.ok(Math.abs(decoded[0] - values[0]) <= 1e-9);
    assert.ok(Math.abs(decoded[1] - values[1]) <= 1e-9);
    assert.ok(Math.abs(decoded[2] - values[2]) <= 1e-9);
});
