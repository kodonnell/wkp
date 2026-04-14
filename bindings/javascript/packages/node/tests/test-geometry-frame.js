const test = require('node:test');
const assert = require('node:assert/strict');

const { GeometryFrame, decode, decodeFrame, encode, encodeFrame, EncodedGeometryType } = require('..');

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

test('decodeFrame returns GeometryFrame for all geometry types', () => {
    for (const geometry of GEOMETRIES) {
        const encoded = encode(geometry, 6);
        const frame = decodeFrame(encoded);

        assert.ok(frame instanceof GeometryFrame, `expected GeometryFrame for ${geometry.type}`);
        assert.equal(frame.version, 1);
        assert.equal(frame.precision, 6);
        assert.equal(frame.dimensions, 2);
        assert.ok(frame.coords instanceof Float64Array);
        assert.ok(frame.segmentPointCounts instanceof Uint32Array);
        assert.ok(frame.groupSegmentCounts instanceof Uint32Array);
    }
});

test('encodeFrame roundtrip produces same bytes as encode', () => {
    for (const geometry of GEOMETRIES) {
        const encoded = encode(geometry, 6);
        const frame = decodeFrame(encoded);
        const reEncoded = encodeFrame(frame);

        assert.equal(
            Buffer.from(reEncoded).toString('ascii'),
            Buffer.from(encoded).toString('ascii'),
            `encodeFrame roundtrip failed for ${geometry.type}`
        );
    }
});

test('GeometryFrame.toGeometry() recovers original geometry', () => {
    for (const geometry of GEOMETRIES) {
        const encoded = encode(geometry, 6);
        const frame = decodeFrame(encoded);
        const recovered = frame.toGeometry();
        assertGeometryClose(recovered, geometry);
    }
});

test('GeometryFrame.toBuffer() / fromBuffer() roundtrip', () => {
    const geometry = { type: 'Polygon', coordinates: [[[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], [[2, 2], [4, 2], [4, 4], [2, 4], [2, 2]]] };
    const encoded = encode(geometry, 6);
    const frame = decodeFrame(encoded);

    const buf = frame.toBuffer();
    assert.ok(buf instanceof ArrayBuffer, 'toBuffer() should return ArrayBuffer');

    const recovered = GeometryFrame.fromBuffer(buf);
    assert.equal(recovered.version, frame.version);
    assert.equal(recovered.precision, frame.precision);
    assert.equal(recovered.dimensions, frame.dimensions);
    assert.equal(recovered.geometryType, frame.geometryType);
    assert.equal(recovered.coords.length, frame.coords.length);
    for (let i = 0; i < frame.coords.length; i += 1) {
        assertClose(recovered.coords[i], frame.coords[i]);
    }
    assert.deepEqual(Array.from(recovered.segmentPointCounts), Array.from(frame.segmentPointCounts));
    assert.deepEqual(Array.from(recovered.groupSegmentCounts), Array.from(frame.groupSegmentCounts));
});

test('GeometryFrame buffer header is 40 bytes', () => {
    const geometry = { type: 'Point', coordinates: [1.0, 2.0] };
    const encoded = encode(geometry, 6);
    const frame = decodeFrame(encoded);
    const buf = frame.toBuffer();

    // 40-byte header + coord_count*8 + seg_count*4 + grp_count*4
    const expectedSize = 40
        + frame.coords.byteLength
        + frame.segmentPointCounts.byteLength
        + frame.groupSegmentCounts.byteLength;
    assert.equal(buf.byteLength, expectedSize);
});

test('encodeFrame throws for non-GeometryFrame argument', () => {
    assert.throws(() => encodeFrame({ type: 'LineString' }), TypeError);
});
