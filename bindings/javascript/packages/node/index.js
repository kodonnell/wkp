const load = require('node-gyp-build');

const core = load(__dirname);
const packageInfo = require('./package.json');
const BINDING_VERSION = packageInfo.version;
const CORE_COMPATIBILITY = packageInfo.wkpCoreCompatibility;

function parseSemverMajorMinor(version) {
    const corePart = String(version).split('-', 1)[0];
    const parts = corePart.split('.');
    if (parts.length < 2) {
        throw new TypeError(`Invalid semantic version: ${version}`);
    }
    const major = Number.parseInt(parts[0], 10);
    const minor = Number.parseInt(parts[1], 10);
    if (!Number.isInteger(major) || !Number.isInteger(minor)) {
        throw new TypeError(`Invalid semantic version: ${version}`);
    }
    return [major, minor];
}

function assertCoreCompatibility(runtimeCoreVersion) {
    if (typeof CORE_COMPATIBILITY !== 'string') {
        throw new Error('Missing string field wkpCoreCompatibility in package.json');
    }
    const [coreMajor, coreMinor] = parseSemverMajorMinor(runtimeCoreVersion);
    const compatibilityMatch = /^([0-9]+)\.([0-9]+)\.(?:x|[0-9]+)$/.exec(CORE_COMPATIBILITY);
    if (!compatibilityMatch) {
        throw new Error(`Invalid compatibility range in package.json: ${CORE_COMPATIBILITY}`);
    }
    const requiredMajor = Number.parseInt(compatibilityMatch[1], 10);
    const requiredMinor = Number.parseInt(compatibilityMatch[2], 10);

    if (coreMajor !== requiredMajor || coreMinor !== requiredMinor) {
        throw new Error(
            `@wkpjs/node ${BINDING_VERSION} requires WKP core ${CORE_COMPATIBILITY}, but loaded core is ${runtimeCoreVersion}`
        );
    }
}

assertCoreCompatibility(core.coreVersion());
if (typeof core.runSelfTest !== 'function') {
    throw new Error('Native binding is missing runSelfTest()');
}
core.runSelfTest();

const EncodedGeometryType = Object.freeze({ ...core.EncodedGeometryType });

class Context { }

// Module-level default context — shared within this realm (process or worker thread).
// JS is single-threaded per realm, so this is safe. Each worker thread gets its own
// module instance and therefore its own default context.
const _defaultCtx = new Context();

function asContext(ctx) {
    if (!(ctx instanceof Context)) {
        throw new TypeError('ctx must be a Context instance');
    }
    return ctx;
}

function normalizeEncodedBytes(encodedValue) {
    if (typeof encodedValue === 'string') {
        return Buffer.from(encodedValue, 'ascii');
    }
    if (Buffer.isBuffer(encodedValue)) {
        return encodedValue;
    }
    if (encodedValue instanceof Uint8Array) {
        return Buffer.from(encodedValue);
    }
    throw new TypeError('encoded must be string, Buffer, or Uint8Array');
}

function normalizeCoord(coord, dimensions, label) {
    if (!Array.isArray(coord) || coord.length !== dimensions) {
        throw new TypeError(`${label} must be an array of ${dimensions} numbers`);
    }
    for (const value of coord) {
        if (typeof value !== 'number' || !Number.isFinite(value)) {
            throw new TypeError(`${label} contains non-finite coordinate values`);
        }
    }
    return coord;
}

function normalizePrecisions(precisions) {
    if (typeof precisions === 'number') {
        if (!Number.isInteger(precisions)) {
            throw new TypeError('precisions must contain only integers');
        }
        return [precisions];
    }
    if (!Array.isArray(precisions) || precisions.length === 0) {
        throw new TypeError('precisions must be a non-empty integer array or an integer');
    }
    const values = new Array(precisions.length);
    for (let i = 0; i < precisions.length; i += 1) {
        const value = precisions[i];
        if (!Number.isInteger(value)) {
            throw new TypeError('precisions must contain only integers');
        }
        values[i] = value;
    }
    return values;
}

function normalizeFloatRows(values, dimensions) {
    if (!Array.isArray(values) || values.length === 0) {
        throw new TypeError('floats must be a non-empty 2D number array');
    }

    const flat = [];
    for (const row of values) {
        if (!Array.isArray(row) || row.length !== dimensions) {
            throw new TypeError(`floats rows must each have exactly ${dimensions} values`);
        }
        for (const value of row) {
            if (typeof value !== 'number' || !Number.isFinite(value)) {
                throw new TypeError('floats must contain only finite numbers');
            }
            flat.push(value);
        }
    }

    return Float64Array.from(flat);
}

function inferDimensionsFromPosition(position, label) {
    if (!Array.isArray(position) || position.length === 0) {
        throw new TypeError(`${label} must contain at least one coordinate value`);
    }
    for (const value of position) {
        if (typeof value !== 'number' || !Number.isFinite(value)) {
            throw new TypeError(`${label} contains non-finite coordinate values`);
        }
    }
    return position.length;
}

function geometryDimensions(geometry) {
    if (!geometry || typeof geometry !== 'object' || typeof geometry.type !== 'string') {
        throw new TypeError('geometry must be an object with GeoJSON-like type and coordinates');
    }

    if (geometry.type === 'Point') {
        return inferDimensionsFromPosition(geometry.coordinates, 'point coordinate');
    }
    if (geometry.type === 'LineString' || geometry.type === 'MultiPoint') {
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError(`${geometry.type} coordinates must contain at least one position`);
        }
        return inferDimensionsFromPosition(geometry.coordinates[0], `${geometry.type} coordinate`);
    }
    if (geometry.type === 'Polygon' || geometry.type === 'MultiLineString') {
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0 || !Array.isArray(geometry.coordinates[0]) || geometry.coordinates[0].length === 0) {
            throw new TypeError(`${geometry.type} coordinates must contain at least one part with one position`);
        }
        return inferDimensionsFromPosition(geometry.coordinates[0][0], `${geometry.type} coordinate`);
    }
    if (geometry.type === 'MultiPolygon') {
        if (
            !Array.isArray(geometry.coordinates)
            || geometry.coordinates.length === 0
            || !Array.isArray(geometry.coordinates[0])
            || geometry.coordinates[0].length === 0
            || !Array.isArray(geometry.coordinates[0][0])
            || geometry.coordinates[0][0].length === 0
        ) {
            throw new TypeError('MultiPolygon coordinates must contain at least one polygon/ring/position');
        }
        return inferDimensionsFromPosition(geometry.coordinates[0][0][0], 'MultiPolygon coordinate');
    }

    throw new TypeError(`Unsupported geometry type: ${geometry.type}`);
}

function pushRows(rows, dimensions, label, flatCoords, segmentPointCounts) {
    if (!Array.isArray(rows) || rows.length === 0) {
        throw new TypeError(`${label} must contain at least one coordinate`);
    }
    for (const row of rows) {
        const coord = normalizeCoord(row, dimensions, label);
        for (let dim = 0; dim < dimensions; dim += 1) {
            flatCoords.push(coord[dim]);
        }
    }
    segmentPointCounts.push(rows.length);
}

function buildGeometryFrame(geometry, dimensions) {
    const flatCoords = [];
    const segmentPointCounts = [];
    const groupSegmentCounts = [];
    let geometryType = 0;

    if (geometry.type === 'Point') {
        geometryType = EncodedGeometryType.POINT;
        pushRows([geometry.coordinates], dimensions, 'point coordinate', flatCoords, segmentPointCounts);
        groupSegmentCounts.push(1);
    } else if (geometry.type === 'LineString') {
        geometryType = EncodedGeometryType.LINESTRING;
        pushRows(geometry.coordinates, dimensions, 'linestring coordinates', flatCoords, segmentPointCounts);
        groupSegmentCounts.push(1);
    } else if (geometry.type === 'Polygon') {
        geometryType = EncodedGeometryType.POLYGON;
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError('polygon coordinates must contain at least one ring');
        }
        for (const ring of geometry.coordinates) {
            pushRows(ring, dimensions, 'polygon ring', flatCoords, segmentPointCounts);
        }
        groupSegmentCounts.push(geometry.coordinates.length);
    } else if (geometry.type === 'MultiPoint') {
        geometryType = EncodedGeometryType.MULTIPOINT;
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError('multipoint coordinates must contain at least one point');
        }
        for (const point of geometry.coordinates) {
            pushRows([point], dimensions, 'multipoint point', flatCoords, segmentPointCounts);
            groupSegmentCounts.push(1);
        }
    } else if (geometry.type === 'MultiLineString') {
        geometryType = EncodedGeometryType.MULTILINESTRING;
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError('multilinestring coordinates must contain at least one line');
        }
        for (const line of geometry.coordinates) {
            pushRows(line, dimensions, 'multilinestring line', flatCoords, segmentPointCounts);
            groupSegmentCounts.push(1);
        }
    } else if (geometry.type === 'MultiPolygon') {
        geometryType = EncodedGeometryType.MULTIPOLYGON;
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError('multipolygon coordinates must contain at least one polygon');
        }
        for (const polygon of geometry.coordinates) {
            if (!Array.isArray(polygon) || polygon.length === 0) {
                throw new TypeError('each multipolygon part must contain at least one ring');
            }
            groupSegmentCounts.push(polygon.length);
            for (const ring of polygon) {
                pushRows(ring, dimensions, 'multipolygon ring', flatCoords, segmentPointCounts);
            }
        }
    } else {
        throw new TypeError(`Unsupported geometry type: ${geometry.type}`);
    }

    return {
        geometryType,
        coords: Float64Array.from(flatCoords),
        segmentPointCounts,   // plain Array — native addon requires this
        groupSegmentCounts    // plain Array — native addon requires this
    };
}

function frameToGeometry(geometryType, dimensions, coords, segmentPointCounts, groupSegmentCounts) {
    let coordIndex = 0;
    let segmentIndex = 0;

    const readSegment = () => {
        if (segmentIndex >= segmentPointCounts.length) {
            throw new TypeError('Decoded geometry frame has invalid segment metadata');
        }
        const pointCount = segmentPointCounts[segmentIndex];
        segmentIndex += 1;

        const neededValues = pointCount * dimensions;
        if ((coordIndex + neededValues) > coords.length) {
            throw new TypeError('Decoded geometry frame has invalid coordinate metadata');
        }

        const rows = new Array(pointCount);
        for (let rowIndex = 0; rowIndex < pointCount; rowIndex += 1) {
            const row = new Array(dimensions);
            for (let dim = 0; dim < dimensions; dim += 1) {
                row[dim] = coords[coordIndex + (rowIndex * dimensions) + dim];
            }
            rows[rowIndex] = row;
        }
        coordIndex += neededValues;
        return rows;
    };

    if (geometryType === EncodedGeometryType.POINT) {
        const point = readSegment();
        if (point.length !== 1) {
            throw new TypeError('Decoded POINT frame must contain exactly one coordinate');
        }
        return { type: 'Point', coordinates: point[0] };
    }

    if (geometryType === EncodedGeometryType.LINESTRING) {
        return { type: 'LineString', coordinates: readSegment() };
    }

    if (geometryType === EncodedGeometryType.POLYGON) {
        const rings = [];
        const ringCount = groupSegmentCounts[0] ?? 0;
        for (let i = 0; i < ringCount; i += 1) {
            rings.push(readSegment());
        }
        return { type: 'Polygon', coordinates: rings };
    }

    if (geometryType === EncodedGeometryType.MULTIPOINT) {
        const points = [];
        for (let i = 0; i < groupSegmentCounts.length; i += 1) {
            const point = readSegment();
            if (point.length !== 1) {
                throw new TypeError('Decoded MULTIPOINT segment must contain exactly one coordinate');
            }
            points.push(point[0]);
        }
        return { type: 'MultiPoint', coordinates: points };
    }

    if (geometryType === EncodedGeometryType.MULTILINESTRING) {
        const lines = [];
        for (let i = 0; i < groupSegmentCounts.length; i += 1) {
            lines.push(readSegment());
        }
        return { type: 'MultiLineString', coordinates: lines };
    }

    if (geometryType === EncodedGeometryType.MULTIPOLYGON) {
        const polygons = [];
        for (let groupIndex = 0; groupIndex < groupSegmentCounts.length; groupIndex += 1) {
            const ringCount = groupSegmentCounts[groupIndex];
            const rings = [];
            for (let ringIndex = 0; ringIndex < ringCount; ringIndex += 1) {
                rings.push(readSegment());
            }
            polygons.push(rings);
        }
        return { type: 'MultiPolygon', coordinates: polygons };
    }

    throw new TypeError(`Unsupported geometry type in frame: ${geometryType}`);
}

// ---------------------------------------------------------------------------
// GeometryFrame
// ---------------------------------------------------------------------------

// Buffer header format (little-endian):
//   int32 version, int32 precision, int32 dimensions, int32 geometry_type  (16 bytes)
//   uint64 coord_value_count, uint64 segment_count, uint64 group_count     (24 bytes)
// Total header: 40 bytes
// Followed by: float64[coord_value_count], uint32[segment_count], uint32[group_count]
const FRAME_HEADER_SIZE = 40;

class GeometryFrame {
    /**
     * @param {object} opts
     * @param {number} opts.version
     * @param {number} opts.precision
     * @param {number} opts.dimensions
     * @param {number} opts.geometryType  - EncodedGeometryType value
     * @param {Float64Array} opts.coords  - flat coordinates [x0,y0,x1,y1,...]
     * @param {Uint32Array}  opts.segmentPointCounts
     * @param {Uint32Array}  opts.groupSegmentCounts
     */
    constructor({ version, precision, dimensions, geometryType, coords, segmentPointCounts, groupSegmentCounts }) {
        this.version = version;
        this.precision = precision;
        this.dimensions = dimensions;
        this.geometryType = geometryType;
        this.coords = coords instanceof Float64Array ? coords : Float64Array.from(coords);
        this.segmentPointCounts = segmentPointCounts instanceof Uint32Array ? segmentPointCounts : Uint32Array.from(segmentPointCounts);
        this.groupSegmentCounts = groupSegmentCounts instanceof Uint32Array ? groupSegmentCounts : Uint32Array.from(groupSegmentCounts);
    }

    /** Convert to a GeoJSON geometry object. */
    toGeometry() {
        return frameToGeometry(
            this.geometryType,
            this.dimensions,
            this.coords,
            this.segmentPointCounts,
            this.groupSegmentCounts
        );
    }

    /**
     * Serialize to a compact binary ArrayBuffer (little-endian).
     * The buffer is transferable between workers via postMessage.
     *
     * Layout (40-byte fixed header + variable data):
     *   int32  version
     *   int32  precision
     *   int32  dimensions
     *   int32  geometry_type
     *   uint64 coord_value_count  (= coords.length)
     *   uint64 segment_count
     *   uint64 group_count
     *   float64[coord_value_count]  flat coordinates
     *   uint32[segment_count]       segment_point_counts
     *   uint32[group_count]         group_segment_counts
     */
    toBuffer() {
        const coordByteLen = this.coords.byteLength;
        const segByteLen = this.segmentPointCounts.byteLength;
        const grpByteLen = this.groupSegmentCounts.byteLength;
        const buf = new ArrayBuffer(FRAME_HEADER_SIZE + coordByteLen + segByteLen + grpByteLen);
        const view = new DataView(buf);
        let o = 0;
        view.setInt32(o, this.version, true);      o += 4;
        view.setInt32(o, this.precision, true);    o += 4;
        view.setInt32(o, this.dimensions, true);   o += 4;
        view.setInt32(o, this.geometryType, true); o += 4;
        view.setBigUint64(o, BigInt(this.coords.length), true);               o += 8;
        view.setBigUint64(o, BigInt(this.segmentPointCounts.length), true);   o += 8;
        view.setBigUint64(o, BigInt(this.groupSegmentCounts.length), true);   o += 8;
        new Float64Array(buf, o, this.coords.length).set(this.coords);       o += coordByteLen;
        new Uint32Array(buf, o, this.segmentPointCounts.length).set(this.segmentPointCounts); o += segByteLen;
        new Uint32Array(buf, o, this.groupSegmentCounts.length).set(this.groupSegmentCounts);
        return buf;
    }

    /** Deserialize from an ArrayBuffer produced by {@link toBuffer}. */
    static fromBuffer(buffer) {
        const view = new DataView(buffer instanceof ArrayBuffer ? buffer : buffer.buffer);
        let o = 0;
        const version = view.getInt32(o, true);       o += 4;
        const precision = view.getInt32(o, true);     o += 4;
        const dimensions = view.getInt32(o, true);    o += 4;
        const geometryType = view.getInt32(o, true);  o += 4;
        const ncoords = Number(view.getBigUint64(o, true)); o += 8;
        const nseg = Number(view.getBigUint64(o, true));    o += 8;
        const ngrp = Number(view.getBigUint64(o, true));    o += 8;
        const coords = new Float64Array(buffer instanceof ArrayBuffer ? buffer : buffer.buffer, o, ncoords);
        o += ncoords * 8;
        const segmentPointCounts = new Uint32Array(buffer instanceof ArrayBuffer ? buffer : buffer.buffer, o, nseg);
        o += nseg * 4;
        const groupSegmentCounts = new Uint32Array(buffer instanceof ArrayBuffer ? buffer : buffer.buffer, o, ngrp);
        return new GeometryFrame({ version, precision, dimensions, geometryType, coords, segmentPointCounts, groupSegmentCounts });
    }
}

// ---------------------------------------------------------------------------
// Core public functions
// ---------------------------------------------------------------------------

function decodeHeader(encodedValue) {
    const encoded = normalizeEncodedBytes(encodedValue);
    const header = core.decodeGeometryHeader(encoded);
    return [header[0], header[1], header[2], header[3]];
}

function decode(encodedValue, ctx = _defaultCtx) {
    asContext(ctx);
    return core.decodeGeometryFrame(normalizeEncodedBytes(encodedValue));
}

function decodeFrame(encodedValue, ctx = _defaultCtx) {
    asContext(ctx);
    // Decode via native, then rebuild flat frame from the GeoJSON geometry.
    // (A future native binding update could return flat data directly.)
    const decoded = core.decodeGeometryFrame(normalizeEncodedBytes(encodedValue));
    const dimensions = decoded.dimensions;
    const flat = buildGeometryFrame(decoded.geometry, dimensions);
    return new GeometryFrame({
        version: decoded.version,
        precision: decoded.precision,
        dimensions,
        geometryType: flat.geometryType,
        coords: flat.coords,
        segmentPointCounts: flat.segmentPointCounts,
        groupSegmentCounts: flat.groupSegmentCounts,
    });
}

function encode(geometry, precision, ctx = _defaultCtx) {
    asContext(ctx);
    if (!Number.isInteger(precision)) {
        throw new TypeError('precision must be an integer');
    }
    const dimensions = geometryDimensions(geometry);
    const frame = buildGeometryFrame(geometry, dimensions);
    return core.encodeGeometryFrameF64(
        frame.geometryType,
        frame.coords,
        dimensions,
        precision,
        frame.groupSegmentCounts,
        frame.segmentPointCounts
    );
}

function encodeFrame(frame, ctx = _defaultCtx) {
    asContext(ctx);
    if (!(frame instanceof GeometryFrame)) {
        throw new TypeError('frame must be a GeometryFrame instance');
    }
    return core.encodeGeometryFrameF64(
        frame.geometryType,
        frame.coords,
        frame.dimensions,
        frame.precision,
        Array.from(frame.groupSegmentCounts),
        Array.from(frame.segmentPointCounts)
    );
}

function encodeFloats(floats, precisions, ctx = _defaultCtx) {
    asContext(ctx);
    const precisionList = normalizePrecisions(precisions);
    const dimensions = precisionList.length;
    const flatValues = normalizeFloatRows(floats, dimensions);
    return core.encodeF64(flatValues, dimensions, precisionList);
}

function decodeFloats(encodedValue, precisions, ctx = _defaultCtx) {
    asContext(ctx);
    const precisionList = normalizePrecisions(precisions);
    const dimensions = precisionList.length;
    const decoded = core.decodeF64(normalizeEncodedBytes(encodedValue), dimensions, precisionList);
    const rows = [];
    for (let i = 0; i < decoded.length; i += dimensions) {
        rows.push(Array.from(decoded.slice(i, i + dimensions)));
    }
    return rows;
}

module.exports = {
    bindingVersion: BINDING_VERSION,
    coreCompatibility: CORE_COMPATIBILITY,
    Context,
    GeometryFrame,
    decodeHeader,
    decode,
    decodeFrame,
    encode,
    encodeFrame,
    encodeFloats,
    decodeFloats,
    runSelfTest: core.runSelfTest,
    coreVersion: core.coreVersion,
    EncodedGeometryType
};
