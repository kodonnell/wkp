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

test('cross-language buffer format matches Python and Node bindings (web)', async () => {
    // Geometry: LineString [(0,0),(1,1)] at precision=6.
    // Coordinates 0.0 and 1.0 are exact IEEE-754 float64 values and survive
    // WKP encode/decode unchanged, making the expected bytes deterministic.
    const wkp = await createWkp();
    const geometry = { type: 'LineString', coordinates: [[0, 0], [1, 1]] };
    const encoded = wkp.encode(geometry, 6);
    const frame = wkp.decodeFrame(encoded);
    const buf = frame.toBuffer();

    // prettier-ignore
    const expected = new Uint8Array([
        0x01, 0x00, 0x00, 0x00,                          // version=1        (int32 LE)
        0x06, 0x00, 0x00, 0x00,                          // precision=6      (int32 LE)
        0x02, 0x00, 0x00, 0x00,                          // dimensions=2     (int32 LE)
        0x02, 0x00, 0x00, 0x00,                          // geometry_type=2  (int32 LE, LINESTRING)
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // coord_value_count=4 (uint64 LE)
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // segment_count=1  (uint64 LE)
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // group_count=1    (uint64 LE)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // coords[0][0]=0.0 (float64 LE)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // coords[0][1]=0.0 (float64 LE)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F, // coords[1][0]=1.0 (float64 LE)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F, // coords[1][1]=1.0 (float64 LE)
        0x02, 0x00, 0x00, 0x00,                          // segment_point_counts=[2] (uint32 LE)
        0x01, 0x00, 0x00, 0x00,                          // group_segment_counts=[1] (uint32 LE)
    ]);

    assert.equal(buf.byteLength, 80);
    const actual = new Uint8Array(buf);
    for (let i = 0; i < expected.length; i += 1) {
        assert.equal(actual[i], expected[i], `byte ${i} mismatch — cross-language buffer format has changed`);
    }

    // Verify fromBuffer recovers the geometry from these canonical bytes
    const recovered = wkp.GeometryFrame.fromBuffer(expected.buffer);
    assert.equal(recovered.version, 1);
    assert.equal(recovered.precision, 6);
    assert.equal(recovered.dimensions, 2);
    assertGeometryClose(recovered.toGeometry(), geometry, 1e-9);
});
