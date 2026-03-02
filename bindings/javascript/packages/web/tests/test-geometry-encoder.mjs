import test from 'node:test';
import assert from 'node:assert/strict';

import { createWkp } from '../src/index.js';

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

test('GeometryEncoder roundtrip for all geometry types (web wasm)', async () => {
    const wkp = await createWkp();
    const encoder = new wkp.GeometryEncoder(6, 2);
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
        const decoded = wkp.GeometryEncoder.decode(encoded);

        assert.equal(decoded.version, 1);
        assert.equal(decoded.precision, 6);
        assert.equal(decoded.dimensions, 2);
        assertGeometryClose(decoded.geometry, geometry);
    }
});

test('GeometryEncoder header and bytes helpers (web wasm)', async () => {
    const wkp = await createWkp();
    const encoder = new wkp.GeometryEncoder(6, 2);
    const geometry = { type: 'LineString', coordinates: [[0.1, 0.2], [1.1, 1.2], [2.1, 2.2]] };

    const encodedBytes = encoder.encodeBytes(geometry);
    assert.ok(encodedBytes instanceof Uint8Array);

    const [version, precision, dimensions, geometryType] = wkp.GeometryEncoder.decodeHeader(encodedBytes);
    assert.equal(version, 1);
    assert.equal(precision, 6);
    assert.equal(dimensions, 2);
    assert.equal(geometryType, wkp.EncodedGeometryType.LINESTRING);

    const decoded = encoder.decodeBytes(encodedBytes);
    assertGeometryClose(decoded.geometry, geometry);
});

test('GeometryEncoder enforces dimensions (web wasm)', async () => {
    const wkp = await createWkp();
    const encoder = new wkp.GeometryEncoder(6, 2);

    assert.throws(
        () => encoder.encode({ type: 'Point', coordinates: [174.776, -41.289, 123.4] }),
        /array of 2 numbers/i
    );
});
