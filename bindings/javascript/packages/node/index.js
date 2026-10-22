const fs = require('node:fs');
const path = require('node:path');

const packageInfo = require('./package.json');
const BINDING_VERSION = packageInfo.version;
const CORE_COMPATIBILITY = packageInfo.wkpCoreCompatibility;

function isMusl() {
    if (process.platform !== 'linux') {
        return false;
    }
    if (typeof process.report?.getReport !== 'function') {
        return false;
    }
    // Skip the (slow) network section of the report; restore the setting afterwards.
    const excludeNetwork = process.report.excludeNetwork;
    process.report.excludeNetwork = true;
    try {
        return !process.report.getReport()?.header?.glibcVersionRuntime;
    } finally {
        process.report.excludeNetwork = excludeNetwork;
    }
}

function loadNative() {
    // Published packages ship prebuilt binaries per platform; local builds land in build/Release.
    const target = `${process.platform}-${process.arch}${isMusl() ? '-musl' : ''}`;
    const candidates = [
        path.join(__dirname, 'prebuilds', target, 'wkp.node'),
        path.join(__dirname, 'build', 'Release', 'wkp.node')
    ];
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return require(candidate);
        }
    }
    throw new Error(
        `@wkpjs/node has no native binary for ${target}. Looked in:\n  ${candidates.join('\n  ')}\n`
        + 'Use @wkpjs/web (WebAssembly) on this platform, or build the addon from a checkout of '
        + 'https://github.com/kodonnell/wkp (bindings/javascript: `npm run build:node`).'
    );
}

const native = loadNative();

// Invalid arguments surface from the native layer with code "InvalidArg"; keep the historical
// contract of throwing TypeError for those.
function wrapNative(fn) {
    return (...args) => {
        try {
            return fn(...args);
        } catch (error) {
            if (error && error.code === 'InvalidArg') {
                throw new TypeError(error.message);
            }
            throw error;
        }
    };
}

const core = Object.fromEntries(
    Object.entries(native).map(([name, value]) => [name, typeof value === 'function' ? wrapNative(value) : value])
);

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

const EncodedGeometryType = Object.freeze({ ...core.geometryTypes() });

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

// Strings and Uint8Arrays (including Buffers) go to the addon as they are: it reads strings as
// UTF-8 into a reused buffer (so a non-ASCII char becomes bytes >= 0x80, which are rejected,
// rather than being truncated into a valid WKP char) and reads byte arrays in place.
function normalizeEncodedBytes(encodedValue) {
    if (typeof encodedValue === 'string' || encodedValue instanceof Uint8Array) {
        return encodedValue;
    }
    throw new TypeError('encoded must be string, Buffer, or Uint8Array');
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

    const flat = new Float64Array(values.length * dimensions);
    let k = 0;
    for (let r = 0; r < values.length; r += 1) {
        const row = values[r];
        if (!Array.isArray(row) || row.length !== dimensions) {
            throw new TypeError(`floats rows must each have exactly ${dimensions} values`);
        }
        for (let d = 0; d < dimensions; d += 1) {
            const value = row[d];
            // `value - value` is NaN for NaN and +-Infinity.
            if (typeof value !== 'number' || value - value !== 0) {
                throw new TypeError('floats must contain only finite numbers');
            }
            flat[k] = value;
            k += 1;
        }
    }
    return flat;
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

// Copies the positions of `segments` (arrays of positions) into the preallocated `coords`,
// validating each one. One indexed pass into a Float64Array is much faster than pushing to an
// array and converting that.
function copyPositions(segments, dimensions, label, coords) {
    let k = 0;
    for (let s = 0; s < segments.length; s += 1) {
        const rows = segments[s];
        for (let r = 0; r < rows.length; r += 1) {
            const row = rows[r];
            if (!Array.isArray(row) || row.length !== dimensions) {
                throw new TypeError(`${label} must be an array of ${dimensions} numbers`);
            }
            for (let d = 0; d < dimensions; d += 1) {
                const value = row[d];
                // `value - value` is NaN for NaN and +-Infinity.
                if (typeof value !== 'number' || value - value !== 0) {
                    throw new TypeError(`${label} contains non-finite coordinate values`);
                }
                coords[k] = value;
                k += 1;
            }
        }
    }
}

function buildGeometryFrame(geometry, dimensions) {
    // The geometry's segments (each an array of positions) and the number of segments per group.
    let segments;
    let groupSegmentCounts;
    let geometryType;
    let label;
    const parts = geometry.coordinates;

    if (geometry.type === 'Point') {
        geometryType = EncodedGeometryType.POINT;
        label = 'point coordinate';
        segments = [[parts]];
        groupSegmentCounts = [1];
    } else if (geometry.type === 'LineString') {
        geometryType = EncodedGeometryType.LINESTRING;
        label = 'linestring coordinates';
        segments = [parts];
        groupSegmentCounts = [1];
    } else if (geometry.type === 'Polygon') {
        geometryType = EncodedGeometryType.POLYGON;
        label = 'polygon ring';
        if (!Array.isArray(parts) || parts.length === 0) {
            throw new TypeError('polygon coordinates must contain at least one ring');
        }
        segments = parts;
        groupSegmentCounts = [parts.length];
    } else if (geometry.type === 'MultiPoint') {
        geometryType = EncodedGeometryType.MULTIPOINT;
        label = 'multipoint point';
        if (!Array.isArray(parts) || parts.length === 0) {
            throw new TypeError('multipoint coordinates must contain at least one point');
        }
        segments = parts.map((point) => [point]);
        groupSegmentCounts = new Array(parts.length).fill(1);
    } else if (geometry.type === 'MultiLineString') {
        geometryType = EncodedGeometryType.MULTILINESTRING;
        label = 'multilinestring line';
        if (!Array.isArray(parts) || parts.length === 0) {
            throw new TypeError('multilinestring coordinates must contain at least one line');
        }
        segments = parts;
        groupSegmentCounts = new Array(parts.length).fill(1);
    } else if (geometry.type === 'MultiPolygon') {
        geometryType = EncodedGeometryType.MULTIPOLYGON;
        label = 'multipolygon ring';
        if (!Array.isArray(parts) || parts.length === 0) {
            throw new TypeError('multipolygon coordinates must contain at least one polygon');
        }
        segments = [];
        groupSegmentCounts = [];
        for (const polygon of parts) {
            if (!Array.isArray(polygon) || polygon.length === 0) {
                throw new TypeError('each multipolygon part must contain at least one ring');
            }
            groupSegmentCounts.push(polygon.length);
            for (const ring of polygon) {
                segments.push(ring);
            }
        }
    } else {
        throw new TypeError(`Unsupported geometry type: ${geometry.type}`);
    }

    const segmentPointCounts = new Uint32Array(segments.length);
    let points = 0;
    for (let s = 0; s < segments.length; s += 1) {
        const rows = segments[s];
        if (!Array.isArray(rows) || rows.length === 0) {
            throw new TypeError(`${label} must contain at least one coordinate`);
        }
        segmentPointCounts[s] = rows.length;
        points += rows.length;
    }
    const coords = new Float64Array(points * dimensions);
    copyPositions(segments, dimensions, label, coords);

    return {
        geometryType,
        coords,
        segmentPointCounts,
        groupSegmentCounts: Uint32Array.from(groupSegmentCounts)
    };
}

// `count` rows of `dimensions` values from flat `coords`, starting at `start`. Rows are array
// literals for 2D and 3D, which V8 stores as packed doubles (filling `new Array(n)` makes slower
// holey arrays).
function toRows(coords, start, count, dimensions) {
    const rows = new Array(count);
    let i = start;
    if (dimensions === 2) {
        for (let r = 0; r < count; r += 1, i += 2) {
            rows[r] = [coords[i], coords[i + 1]];
        }
    } else if (dimensions === 3) {
        for (let r = 0; r < count; r += 1, i += 3) {
            rows[r] = [coords[i], coords[i + 1], coords[i + 2]];
        }
    } else {
        for (let r = 0; r < count; r += 1) {
            const row = [];
            for (let d = 0; d < dimensions; d += 1, i += 1) {
                row.push(coords[i]);
            }
            rows[r] = row;
        }
    }
    return rows;
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

        const rows = toRows(coords, coordIndex, pointCount, dimensions);
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
        // A view (e.g. a pooled Node Buffer) can start at any offset in its ArrayBuffer, and the
        // typed arrays below need 8-byte alignment, so copy views into an ArrayBuffer of their own.
        const ab = buffer instanceof ArrayBuffer
            ? buffer
            : buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
        const view = new DataView(ab);
        let o = 0;
        const version = view.getInt32(o, true);       o += 4;
        const precision = view.getInt32(o, true);     o += 4;
        const dimensions = view.getInt32(o, true);    o += 4;
        const geometryType = view.getInt32(o, true);  o += 4;
        const ncoords = Number(view.getBigUint64(o, true)); o += 8;
        const nseg = Number(view.getBigUint64(o, true));    o += 8;
        const ngrp = Number(view.getBigUint64(o, true));    o += 8;
        const coords = new Float64Array(ab, o, ncoords);
        o += ncoords * 8;
        const segmentPointCounts = new Uint32Array(ab, o, nseg);
        o += nseg * 4;
        const groupSegmentCounts = new Uint32Array(ab, o, ngrp);
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

function decodeFrameRaw(encodedValue) {
    const [version, precision, dimensions, geometryType, coords, segmentPointCounts, groupSegmentCounts] =
        core.decodeGeometryFrameFlat(normalizeEncodedBytes(encodedValue));
    return { version, precision, dimensions, geometryType, coords, segmentPointCounts, groupSegmentCounts };
}

function decode(encodedValue, ctx = _defaultCtx) {
    asContext(ctx);
    const raw = decodeFrameRaw(encodedValue);
    return {
        version: raw.version,
        precision: raw.precision,
        dimensions: raw.dimensions,
        geometry: frameToGeometry(raw.geometryType, raw.dimensions, raw.coords, raw.segmentPointCounts, raw.groupSegmentCounts)
    };
}

function decodeFrame(encodedValue, ctx = _defaultCtx) {
    asContext(ctx);
    return new GeometryFrame(decodeFrameRaw(encodedValue));
}

function decodeFloatsArray(encodedValue, precisions, ctx = _defaultCtx) {
    asContext(ctx);
    const precisionList = normalizePrecisions(precisions);
    const dimensions = precisionList.length;
    return core.decodeF64(normalizeEncodedBytes(encodedValue), dimensions, precisionList);
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
        frame.segmentPointCounts,
        frame.groupSegmentCounts
    );
}

function encodeFrame(frame, ctx = _defaultCtx) {
    asContext(ctx);
    if (!(frame instanceof GeometryFrame)) {
        throw new TypeError('frame must be a GeometryFrame instance');
    }
    if (!Number.isInteger(frame.precision)) {
        throw new TypeError('precision must be an integer');
    }
    return core.encodeGeometryFrameF64(
        frame.geometryType,
        frame.coords,
        frame.dimensions,
        frame.precision,
        frame.segmentPointCounts,
        frame.groupSegmentCounts
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
    return toRows(decoded, 0, decoded.length / dimensions, dimensions);
}

const { runSelfTest, coreVersion } = core;

// Keep every entry `name` or `name: identifier`: Node's ESM loader finds the named exports of a
// CommonJS module by scanning this literal, and stops at anything else (e.g. `x: core.x`).
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
    decodeFloatsArray,
    runSelfTest,
    coreVersion,
    EncodedGeometryType
};
