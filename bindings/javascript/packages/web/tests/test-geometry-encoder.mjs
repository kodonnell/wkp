import test from 'node:test';
import assert from 'node:assert/strict';

import { createWkp, WkpError } from '../src/index.js';

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

test('roundtrip for all geometry types using default context (web wasm)', async () => {
    const wkp = await createWkp();
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
        const encoded = wkp.encode(item, 6);
        const decoded = wkp.decode(encoded);

        assert.equal(decoded.version, 1);
        assert.equal(decoded.precision, 6);
        assert.equal(decoded.dimensions, 2);
        assertGeometryClose(decoded.geometry, item);
    }
});

test('roundtrip with explicit context (web wasm)', async () => {
    const wkp = await createWkp();
    const ctx = new wkp.Context();
    const geometry = { type: 'LineString', coordinates: [[0.1, 0.2], [1.1, 1.2], [2.1, 2.2]] };

    const encoded = wkp.encode(geometry, 6, ctx);
    const { version, precision, dimensions, geometryType } = wkp.decodeHeader(encoded);
    assert.equal(version, 1);
    assert.equal(precision, 6);
    assert.equal(dimensions, 2);
    assert.equal(geometryType, wkp.GeometryType.LINESTRING);
    assert.equal(wkp.EncodedGeometryType, wkp.GeometryType);

    const decoded = wkp.decode(encoded, ctx);
    assertGeometryClose(decoded.geometry, geometry);
    ctx.dispose();
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

test('encode rejects malformed coordinates with TypeError (web)', async () => {
    const wkp = await createWkp();
    for (const [geometry, message] of BAD_GEOMETRIES) {
        assert.throws(() => wkp.encode(geometry, 6), (e) => e instanceof TypeError && message.test(e.message), JSON.stringify(geometry));
    }
    assert.throws(() => wkp.encodeFloats([[0, 0], [NaN, 0]], [5, 5]), /^TypeError: floats must contain only finite numbers$/);
    assert.throws(() => wkp.encodeFloats([[0, 0], [0]], [5, 5]), /^TypeError: floats rows must each have exactly 2 values$/);
});

test('the core rejects inputs with a WkpError and a code (web)', async () => {
    const wkp = await createWkp();
    const other = await createWkp();
    const isWkpError = (code) => (e) => e instanceof WkpError && e instanceof other.WkpError && e.code === code;
    assert.throws(() => wkp.encode({ type: 'Point', coordinates: [0, 0] }, 9999), isWkpError('INVALID_ARGUMENT'));
    assert.throws(() => wkp.decode('@@'), isWkpError('MALFORMED_INPUT'));
    assert.throws(() => wkp.decodeHeader('@DAF??'), isWkpError('MALFORMED_INPUT'));
    assert.throws(() => wkp.encode({ type: 'LineString', coordinates: [[0, 0]] }, 5), isWkpError('INVALID_ARGUMENT'));
});

test('a fractional precision is rejected, not truncated (web)', async () => {
    const wkp = await createWkp();
    const point = { type: 'Point', coordinates: [1, 2] };
    assert.throws(() => wkp.encode(point, 5.7), /^TypeError: precision must be an integer$/);
    const frame = wkp.decodeFrame(wkp.encode(point, 5));
    frame.precision = 2.5;
    assert.throws(() => wkp.encodeFrame(frame), /^TypeError: precision must be an integer$/);
});

test('every geometry type round-trips in 2D, 3D and 4D (web)', async () => {
    const wkp = await createWkp();
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
            assert.deepEqual(wkp.decode(wkp.encode(geometry, 6)).geometry, geometry, `${geometry.type} ${d}D`);
        }
    }
    for (const d of [1, 2, 3, 4]) {
        const rows = [pos(d, 1), pos(d, -2), pos(d, 3)];
        assert.deepEqual(wkp.decodeFloats(wkp.encodeFloats(rows, new Array(d).fill(3)), new Array(d).fill(3)), rows, `${d}D floats`);
    }
});
