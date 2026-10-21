const test = require('node:test');
const assert = require('node:assert/strict');

const {
    Context,
    decode,
    decodeFloats,
    decodeFloatsArray,
    decodeFrame,
    decodeHeader,
    encode,
    encodeFloats,
    encodeFrame,
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

test('roundtrip for all supported geometry types (default context)', () => {
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
        const encoded = encode(item, 6);
        const decoded = decode(encoded);

        assert.equal(decoded.version, 1);
        assert.equal(decoded.precision, 6);
        assert.equal(decoded.dimensions, 2);
        assertGeometryClose(decoded.geometry, item);
    }
});

test('roundtrip with explicit context', () => {
    const ctx = new Context();
    const geometry = { type: 'LineString', coordinates: [[0.1, 0.2], [1.1, 1.2]] };
    const encoded = encode(geometry, 6, ctx);
    const decoded = decode(encoded, ctx);
    assertGeometryClose(decoded.geometry, geometry);
});

test('decodeHeader and generic encode/decode path', () => {
    const geometry = { type: 'LineString', coordinates: [[0.1, 0.2], [1.1, 1.2], [2.1, 2.2]] };

    const encoded = encode(geometry, 6);
    const [version, precision, dimensions, geometryType] = decodeHeader(encoded);
    assert.equal(version, 1);
    assert.equal(precision, 6);
    assert.equal(dimensions, 2);
    assert.equal(geometryType, EncodedGeometryType.LINESTRING);

    const decoded = decode(encoded);
    assertGeometryClose(decoded.geometry, geometry);
});

test('invalid precision and malformed input surface clear errors', () => {
    assert.throws(() => encode({ type: 'Point', coordinates: [0, 0] }, 9999));
    assert.throws(() => decode('@@'));
});

test('a fractional precision is rejected, not truncated', () => {
    const point = { type: 'Point', coordinates: [1, 2] };
    assert.throws(() => encode(point, 5.7), /^TypeError: precision must be an integer$/);
    const frame = decodeFrame(encode(point, 5));
    frame.precision = 2.5;
    assert.throws(() => encodeFrame(frame), /^TypeError: precision must be an integer$/);
});

test('unsupported geometry collection raises', () => {
    const geometryCollection = {
        type: 'GeometryCollection',
        geometries: [{ type: 'Point', coordinates: [0, 0] }]
    };
    assert.throws(() => encode(geometryCollection, 6));
});

test('encodeFloats/decodeFloats roundtrip (default context)', () => {
    const rows = [[1.25, 2.5, 3.75], [4.5, 5.25, 6.0]];
    const precisions = [2, 2, 2];

    const encoded = encodeFloats(rows, precisions);
    const decoded = decodeFloats(encoded, precisions);

    assert.equal(decoded.length, rows.length);
    for (let i = 0; i < rows.length; i += 1) {
        for (let j = 0; j < rows[i].length; j += 1) {
            assert.ok(Math.abs(decoded[i][j] - rows[i][j]) <= 1e-6);
        }
    }
});

test('encodeFloats/decodeFloats roundtrip with explicit context', () => {
    const ctx = new Context();
    const rows = [[1.0, 2.0], [3.0, 4.0]];
    const precisions = [2, 2];

    const encoded = encodeFloats(rows, precisions, ctx);
    const decoded = decodeFloats(encoded, precisions, ctx);

    assert.equal(decoded.length, rows.length);
    for (let i = 0; i < rows.length; i += 1) {
        for (let j = 0; j < rows[i].length; j += 1) {
            assert.ok(Math.abs(decoded[i][j] - rows[i][j]) <= 1e-6);
        }
    }
});

test('decodeFloatsArray returns Float64Array matching decodeFloats (2D)', () => {
    const rows = [[1.25, 2.5], [3.75, 4.0], [5.125, 6.25]];
    const precisions = [3, 3];
    const encoded = encodeFloats(rows, precisions);

    const arr = decodeFloatsArray(encoded, precisions);
    assert.ok(arr instanceof Float64Array, 'decodeFloatsArray must return Float64Array');
    assert.equal(arr.length, rows.length * precisions.length);

    const tuples = decodeFloats(encoded, precisions);
    for (let i = 0; i < rows.length; i += 1) {
        for (let j = 0; j < precisions.length; j += 1) {
            assert.ok(Math.abs(arr[i * precisions.length + j] - tuples[i][j]) <= 1e-9);
        }
    }
});

test('decodeFloatsArray roundtrip (3D)', () => {
    const rows = [[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]];
    const precisions = [2, 2, 2];
    const encoded = encodeFloats(rows, precisions);
    const arr = decodeFloatsArray(encoded, precisions);

    assert.ok(arr instanceof Float64Array);
    assert.equal(arr.length, 6);
    for (let i = 0; i < rows.length; i += 1) {
        for (let j = 0; j < 3; j += 1) {
            assert.ok(Math.abs(arr[i * 3 + j] - rows[i][j]) <= 1e-6);
        }
    }
});

// Validation and round trips of the GeoJSON <-> flat-array conversion, for every geometry type
// and dimension count. (Messages are part of the contract: they match earlier releases.)
const BAD_GEOMETRIES = [
    [{ type: 'LineString', coordinates: [[0, 0], [NaN, 1]] }, /^linestring coordinates contains non-finite coordinate values$/],
    [{ type: 'LineString', coordinates: [[0, 0], [1, Infinity]] }, /non-finite/],
    [{ type: 'LineString', coordinates: [[0, 0], [-Infinity, 1]] }, /non-finite/],
    [{ type: 'LineString', coordinates: [[0, 0], ['1', 1]] }, /non-finite/],
    [{ type: 'LineString', coordinates: [[0, 0], [null, 1]] }, /non-finite/],
    [{ type: 'LineString', coordinates: [[0, 0], [1]] }, /^linestring coordinates must be an array of 2 numbers$/],
    [{ type: 'LineString', coordinates: [[0, 0], 5] }, /must be an array of 2 numbers/],
    [{ type: 'Polygon', coordinates: [[[0, 0], [1, 0], [0, 1], [0, 0]], []] }, /^polygon ring must contain at least one coordinate$/],
    [{ type: 'Polygon', coordinates: [] }, /^Polygon coordinates must contain at least one part with one position$/],
    [{ type: 'MultiPoint', coordinates: [[0, 0], [1, 'x']] }, /^multipoint point contains non-finite coordinate values$/],
    [{ type: 'MultiLineString', coordinates: [[[0, 0]], []] }, /^multilinestring line must contain at least one coordinate$/],
    [{ type: 'MultiPolygon', coordinates: [[[[0, 0]]], []] }, /^each multipolygon part must contain at least one ring$/],
    [{ type: 'MultiPolygon', coordinates: [[[[0, 0, 1]], [[0, 0]]]] }, /^multipolygon ring must be an array of 3 numbers$/],
    [{ type: 'Point', coordinates: [0, NaN] }, /^point coordinate contains non-finite coordinate values$/]
];

test('encode rejects malformed coordinates with TypeError', () => {
    for (const [geometry, message] of BAD_GEOMETRIES) {
        assert.throws(() => encode(geometry, 6), (e) => e instanceof TypeError && message.test(e.message), JSON.stringify(geometry));
    }
    assert.throws(() => encodeFloats([[0, 0], [NaN, 0]], [5, 5]), /^TypeError: floats must contain only finite numbers$/);
    assert.throws(() => encodeFloats([[0, 0], [0]], [5, 5]), /^TypeError: floats rows must each have exactly 2 values$/);
});

test('every geometry type round-trips in 2D, 3D and 4D', () => {
    const pos = (d, i) => Array.from({ length: d }, (_, k) => i + k / 10);
    for (const d of [2, 3, 4]) {
        const ring = [pos(d, 0), pos(d, 1), pos(d, 2), pos(d, 0)];
        const geometries = [
            { type: 'Point', coordinates: pos(d, 5) },
            { type: 'LineString', coordinates: ring.slice(0, 3) },
            { type: 'Polygon', coordinates: [ring, ring] },
            { type: 'MultiPoint', coordinates: ring.slice(0, 2) },
            { type: 'MultiLineString', coordinates: [ring.slice(0, 2), ring] },
            { type: 'MultiPolygon', coordinates: [[ring], [ring, ring]] }
        ];
        for (const geometry of geometries) {
            assert.deepEqual(decode(encode(geometry, 6)).geometry, geometry, `${geometry.type} ${d}D`);
        }
    }
    for (const d of [1, 2, 3, 4]) {
        const rows = [pos(d, 1), pos(d, -2), pos(d, 3)];
        assert.deepEqual(decodeFloats(encodeFloats(rows, new Array(d).fill(3)), new Array(d).fill(3)), rows, `${d}D floats`);
    }
});
