import test from 'node:test';
import assert from 'node:assert/strict';

import { createWkp } from '../src/index.js';

function assertClose(a, b, epsilon = 1e-6) {
    assert.ok(Math.abs(a - b) <= epsilon, `expected ${a} ≈ ${b} (epsilon ${epsilon})`);
}

function assertGeometryClose(actual, expected, epsilon = 1e-6) {
    assert.equal(actual.type, expected.type);
    function walk(a, e) {
        if (Array.isArray(e)) {
            assert.ok(Array.isArray(a));
            assert.equal(a.length, e.length);
            for (let i = 0; i < e.length; i += 1) walk(a[i], e[i]);
            return;
        }
        assertClose(a, e, epsilon);
    }
    walk(actual.coordinates, expected.coordinates);
}

const GEOMETRIES = [
    { type: 'Point', coordinates: [174.776, -41.289] },
    { type: 'LineString', coordinates: [[0, 0], [1, 1], [2, 2]] },
    { type: 'Polygon', coordinates: [[[0, 0], [1, 0], [1, 1], [0, 1], [0, 0]]] },
    { type: 'MultiPoint', coordinates: [[0, 0], [1, 1]] },
    { type: 'MultiLineString', coordinates: [[[0, 0], [1, 1]], [[2, 2], [3, 3]]] },
    {
        type: 'MultiPolygon',
        coordinates: [
            [[[0, 0], [1, 0], [1, 1], [0, 1], [0, 0]]],
            [[[2, 2], [3, 2], [3, 3], [2, 3], [2, 2]]]
        ]
    },
];

test('decodeFrame returns GeometryFrame for all geometry types (web)', async () => {
    const wkp = await createWkp();

    for (const geometry of GEOMETRIES) {
        const encoded = wkp.encode(geometry, 6);
        const frame = wkp.decodeFrame(encoded);

        assert.ok(frame instanceof wkp.GeometryFrame, `expected GeometryFrame for ${geometry.type}`);
        assert.equal(frame.version, 1);
        assert.equal(frame.precision, 6);
        assert.equal(frame.dimensions, 2);
        assert.ok(frame.coords instanceof Float64Array);
        assert.ok(frame.segmentPointCounts instanceof Uint32Array);
        assert.ok(frame.groupSegmentCounts instanceof Uint32Array);
    }
});

test('encodeFrame roundtrip produces same output as encode (web)', async () => {
    const wkp = await createWkp();

    for (const geometry of GEOMETRIES) {
        const encoded = wkp.encode(geometry, 6);
        const frame = wkp.decodeFrame(encoded);
        const reEncoded = wkp.encodeFrame(frame);

        assert.equal(reEncoded, encoded, `encodeFrame roundtrip failed for ${geometry.type}`);
    }
});

test('GeometryFrame.toGeometry() recovers original geometry (web)', async () => {
    const wkp = await createWkp();

    for (const geometry of GEOMETRIES) {
        const encoded = wkp.encode(geometry, 6);
        const frame = wkp.decodeFrame(encoded);
        const recovered = frame.toGeometry();
        assertGeometryClose(recovered, geometry);
    }
});

test('GeometryFrame.toBuffer() / fromBuffer() roundtrip (web)', async () => {
    const wkp = await createWkp();
    const geometry = {
        type: 'Polygon',
        coordinates: [[[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], [[2, 2], [4, 2], [4, 4], [2, 4], [2, 2]]]
    };
    const encoded = wkp.encode(geometry, 6);
    const frame = wkp.decodeFrame(encoded);

    const buf = frame.toBuffer();
    assert.ok(buf instanceof ArrayBuffer, 'toBuffer() should return ArrayBuffer');

    const recovered = wkp.GeometryFrame.fromBuffer(buf);
    assert.equal(recovered.version, frame.version);
    assert.equal(recovered.precision, frame.precision);
    assert.equal(recovered.dimensions, frame.dimensions);
    assert.equal(recovered.geometryType, frame.geometryType);
    assert.equal(recovered.coords.length, frame.coords.length);
    for (let i = 0; i < frame.coords.length; i += 1) {
        assertClose(recovered.coords[i], frame.coords[i]);
    }

    // Verify geometry survives buffer roundtrip
    const geomRecovered = recovered.toGeometry();
    assertGeometryClose(geomRecovered, geometry);
});

test('GeometryFrame buffer is 40-byte header + data (web)', async () => {
    const wkp = await createWkp();
    const geometry = { type: 'Point', coordinates: [1.0, 2.0] };
    const encoded = wkp.encode(geometry, 6);
    const frame = wkp.decodeFrame(encoded);
    const buf = frame.toBuffer();

    const expectedSize = 40
        + frame.coords.byteLength
        + frame.segmentPointCounts.byteLength
        + frame.groupSegmentCounts.byteLength;
    assert.equal(buf.byteLength, expectedSize);
});

test('cross-language buffer compatibility: Node GeometryFrame buffers (web)', async () => {
    // Verifies the buffer format is self-consistent within the web binding.
    // Buffer produced by toBuffer() must be readable by fromBuffer().
    const wkp = await createWkp();
    const geometry = { type: 'LineString', coordinates: [[1.1, 2.2], [3.3, 4.4]] };
    const frame1 = wkp.decodeFrame(wkp.encode(geometry, 5));
    const buf = frame1.toBuffer();
    const frame2 = wkp.GeometryFrame.fromBuffer(buf);

    assert.equal(frame2.precision, 5);
    assert.equal(frame2.coords.length, frame1.coords.length);
    for (let i = 0; i < frame1.coords.length; i += 1) {
        assertClose(frame1.coords[i], frame2.coords[i]);
    }
});
