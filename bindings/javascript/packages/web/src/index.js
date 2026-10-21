import { loadVersionMetadata } from './version.js';

const TEXT_ENCODER = new TextEncoder();
const TEXT_DECODER = new TextDecoder();
// Hidden from bundlers so browser builds don't try to resolve Node built-ins.
const dynamicImport = new Function('specifier', 'return import(specifier);');

function isNodeRuntime() {
    return (
        typeof process === 'object'
        && typeof process.versions === 'object'
        && typeof process.versions.node === 'string'
    );
}

/**
 * Instantiates dist/wkp_core.wasm. Options (all optional):
 * - `wasmModule`: a compiled `WebAssembly.Module` to instantiate.
 * - `wasmBinary`: the module bytes (`ArrayBuffer` or typed array).
 * - `locateFile(path)`: returns the URL to fetch `wkp_core.wasm` from (browser).
 */
async function instantiateCore(options = {}) {
    if (options.wasmModule instanceof WebAssembly.Module) {
        return new WebAssembly.Instance(options.wasmModule, {}).exports;
    }
    if (options.wasmBinary) {
        return (await WebAssembly.instantiate(options.wasmBinary, {})).instance.exports;
    }
    if (isNodeRuntime() && typeof options.locateFile !== 'function') {
        const { readFile } = await dynamicImport('node:fs/promises');
        const bytes = await readFile(new URL('../dist/wkp_core.wasm', import.meta.url));
        return (await WebAssembly.instantiate(bytes, {})).instance.exports;
    }
    const url = typeof options.locateFile === 'function'
        ? options.locateFile('wkp_core.wasm')
        : new URL('../dist/wkp_core.wasm', import.meta.url);
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`Failed to fetch ${url}: ${response.status} ${response.statusText}`);
    }
    if (typeof WebAssembly.instantiateStreaming === 'function'
        && response.headers.get('content-type')?.startsWith('application/wasm')) {
        return (await WebAssembly.instantiateStreaming(response, {})).instance.exports;
    }
    return (await WebAssembly.instantiate(await response.arrayBuffer(), {})).instance.exports;
}

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

function formatMajorMinorCompatibility(version) {
    const [major, minor] = parseSemverMajorMinor(version);
    return `${major}.${minor}.x`;
}

function assertCoreCompatibility(runtimeCoreVersion, bindingVersion, coreCompatibility) {
    if (typeof coreCompatibility !== 'string') {
        throw new Error('Missing string field wkpCoreCompatibility in package.json');
    }
    const [coreMajor, coreMinor] = parseSemverMajorMinor(runtimeCoreVersion);
    const compatibilityMatch = /^([0-9]+)\.([0-9]+)\.(?:x|[0-9]+)$/.exec(coreCompatibility);
    if (!compatibilityMatch) {
        throw new Error(`Invalid compatibility range in package.json: ${coreCompatibility}`);
    }
    const requiredMajor = Number.parseInt(compatibilityMatch[1], 10);
    const requiredMinor = Number.parseInt(compatibilityMatch[2], 10);

    if (coreMajor !== requiredMajor || coreMinor !== requiredMinor) {
        throw new Error(
            `@wkpjs/web ${bindingVersion} requires WKP core ${coreCompatibility}, but loaded core is ${runtimeCoreVersion}`
        );
    }
}

function statusMessage(status, STATUS) {
    switch (status) {
        case STATUS.INVALID_ARGUMENT:
            return 'Invalid argument';
        case STATUS.MALFORMED_INPUT:
            return 'Malformed input';
        case STATUS.ALLOCATION_FAILED:
            return 'Allocation failed';
        case STATUS.BUFFER_TOO_SMALL:
            return 'Buffer too small';
        case STATUS.LIMIT_EXCEEDED:
            return 'Limit exceeded';
        case STATUS.INTERNAL_ERROR:
            return 'Internal error';
        default:
            return `WKP error (status ${status})`;
    }
}

function normalizeValues(values) {
    if (values instanceof Float64Array) {
        return values;
    }
    if (Array.isArray(values)) {
        return Float64Array.from(values);
    }
    throw new TypeError('values must be Float64Array or number[]');
}

function normalizePrecisions(precisions) {
    if (precisions instanceof Int32Array) {
        return precisions;
    }
    if (!Array.isArray(precisions)) {
        throw new TypeError('precisions must be an array of integers');
    }
    return Int32Array.from(precisions);
}

function normalizePrecisionList(precisions) {
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

function flattenFloatRows(values, dimensions) {
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

function normalizeEncoded(encoded) {
    if (typeof encoded === 'string') {
        return TEXT_ENCODER.encode(encoded);
    }
    if (encoded instanceof Uint8Array) {
        return encoded;
    }
    throw new TypeError('encoded must be string or Uint8Array');
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

function buildGeometryFrame(geometry, dimensions, EncodedGeometryType) {
    if (!geometry || typeof geometry !== 'object' || typeof geometry.type !== 'string') {
        throw new TypeError('geometry must be an object with GeoJSON-like type and coordinates');
    }

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
        segmentPointCounts: Uint32Array.from(segmentPointCounts),
        groupSegmentCounts: Uint32Array.from(groupSegmentCounts)
    };
}

function frameToGeometry(geometryType, dimensions, coords, segmentPointCounts, groupSegmentCounts, EncodedGeometryType) {
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

export async function createWkp(options) {
    const versionMetadata = await loadVersionMetadata();
    const native = await instantiateCore(options);
    const memory = native.memory;

    const STATUS = Object.freeze({
        OK: 0,
        INVALID_ARGUMENT: 1,
        MALFORMED_INPUT: 2,
        ALLOCATION_FAILED: 3,
        BUFFER_TOO_SMALL: 4,
        LIMIT_EXCEEDED: 5,
        INTERNAL_ERROR: 255
    });

    const EncodedGeometryType = Object.freeze({
        POINT: 1,
        LINESTRING: 2,
        POLYGON: 3,
        MULTIPOINT: 4,
        MULTILINESTRING: 5,
        MULTIPOLYGON: 6
    });

    const checkStatus = (status) => {
        if (status !== STATUS.OK) {
            throw new Error(statusMessage(status, STATUS));
        }
    };

    // Output struct written by the module (see native/src/lib.rs): ten u32 fields.
    const readOutput = (ctxPtr) => {
        const o = new Uint32Array(memory.buffer, native.wkp_ctx_output(ctxPtr), 10);
        return {
            dataPtr: o[0], dataLen: o[1],
            segmentsPtr: o[2], segmentsLen: o[3],
            groupsPtr: o[4], groupsLen: o[5],
            version: o[6], precision: o[7], dimensions: o[8], geometryType: o[9]
        };
    };

    // Lays out typed arrays back to back (8-byte aligned) in the context's input scratch and
    // returns their addresses.
    const stage = (ctxPtr, arrays) => {
        const offsets = [];
        let total = 0;
        for (const a of arrays) {
            offsets.push(total);
            total += Math.ceil(a.byteLength / 8) * 8;
        }
        const base = native.wkp_ctx_input(ctxPtr, Math.max(total, 8));
        const bytes = new Uint8Array(memory.buffer);
        arrays.forEach((a, i) => bytes.set(new Uint8Array(a.buffer, a.byteOffset, a.byteLength), base + offsets[i]));
        return offsets.map((offset) => base + offset);
    };

    class Context {
        constructor(initialCapacity = 4096) {
            if (!Number.isInteger(initialCapacity) || initialCapacity <= 0) {
                throw new TypeError('initialCapacity must be a positive integer');
            }
            this.initialCapacity = initialCapacity;
            this._disposed = false;
            this._ptr = native.wkp_ctx_new();
            native.wkp_ctx_input(this._ptr, initialCapacity);
        }

        _checkNotDisposed() {
            if (this._disposed || !this._ptr) {
                throw new Error('Context has been disposed');
            }
        }

        _bytesResult(status) {
            checkStatus(status);
            const out = readOutput(this._ptr);
            return new Uint8Array(memory.buffer, out.dataPtr, out.dataLen).slice();
        }

        encodeF64(values, dimensions, precisions) {
            this._checkNotDisposed();
            const v = normalizeValues(values);
            const p = normalizePrecisions(precisions);
            const [valuesPtr, precisionsPtr] = stage(this._ptr, [v, p]);
            return this._bytesResult(native.wkp_encode_f64(this._ptr, valuesPtr, v.length, dimensions, precisionsPtr, p.length));
        }

        decodeF64(encoded, dimensions, precisions) {
            this._checkNotDisposed();
            const e = normalizeEncoded(encoded);
            const p = normalizePrecisions(precisions);
            const [encodedPtr, precisionsPtr] = stage(this._ptr, [e, p]);
            checkStatus(native.wkp_decode_f64(this._ptr, encodedPtr, e.byteLength, dimensions, precisionsPtr, p.length));
            const out = readOutput(this._ptr);
            return new Float64Array(memory.buffer, out.dataPtr, out.dataLen).slice();
        }

        _encodeFrame(geometryType, coords, dimensions, precision, groupSegmentCounts, segmentPointCounts) {
            this._checkNotDisposed();
            const [coordsPtr, groupsPtr, segmentsPtr] = stage(this._ptr, [coords, groupSegmentCounts, segmentPointCounts]);
            const encoded = this._bytesResult(native.wkp_encode_geometry(
                this._ptr,
                geometryType,
                coordsPtr,
                coords.length,
                dimensions,
                precision,
                groupsPtr,
                groupSegmentCounts.length,
                segmentsPtr,
                segmentPointCounts.length
            ));
            return TEXT_DECODER.decode(encoded);
        }

        encodeGeometry(geometry, precision) {
            const dimensions = geometryDimensions(geometry);
            const frame = buildGeometryFrame(geometry, dimensions, EncodedGeometryType);
            return this._encodeFrame(frame.geometryType, frame.coords, dimensions, precision, frame.groupSegmentCounts, frame.segmentPointCounts);
        }

        encodeGeometryFromFrame(frame) {
            return this._encodeFrame(
                frame.geometryType,
                frame.coords,
                frame.dimensions,
                frame.precision,
                frame.groupSegmentCounts,
                frame.segmentPointCounts
            );
        }

        _decodeGeometryFrameRaw(encoded) {
            this._checkNotDisposed();
            const e = normalizeEncoded(encoded);
            const [encodedPtr] = stage(this._ptr, [e]);
            checkStatus(native.wkp_decode_geometry(this._ptr, encodedPtr, e.byteLength));
            const out = readOutput(this._ptr);
            return {
                version: out.version,
                precision: out.precision,
                dimensions: out.dimensions,
                geometryType: out.geometryType,
                coords: new Float64Array(memory.buffer, out.dataPtr, out.dataLen).slice(),
                segmentPointCounts: new Uint32Array(memory.buffer, out.segmentsPtr, out.segmentsLen).slice(),
                groupSegmentCounts: new Uint32Array(memory.buffer, out.groupsPtr, out.groupsLen).slice()
            };
        }

        decodeGeometry(encoded) {
            const raw = this._decodeGeometryFrameRaw(encoded);
            return {
                version: raw.version,
                precision: raw.precision,
                dimensions: raw.dimensions,
                geometry: frameToGeometry(raw.geometryType, raw.dimensions, raw.coords, raw.segmentPointCounts, raw.groupSegmentCounts, EncodedGeometryType)
            };
        }

        dispose() {
            if (this._disposed) {
                return;
            }
            this._disposed = true;
            native.wkp_ctx_free(this._ptr);
            this._ptr = 0;
        }
    }

    function coreVersion() {
        return TEXT_DECODER.decode(new Uint8Array(memory.buffer, native.wkp_version_ptr(), native.wkp_version_len()));
    }

    function runSelfTest() {
        if (native.wkp_self_test() !== STATUS.OK) {
            throw new Error('WKP core self-test failed');
        }
        return true;
    }

    const runtimeCoreVersion = coreVersion();
    const coreCompatibility = versionMetadata.coreCompatibility ?? formatMajorMinorCompatibility(runtimeCoreVersion);

    assertCoreCompatibility(runtimeCoreVersion, versionMetadata.bindingVersion, coreCompatibility);
    runSelfTest();

    // Module-level default context — shared within this WASM instance (one per realm/worker).
    // JS is single-threaded per realm, so this is safe.
    const _defaultCtx = new Context();

    function decodeHeader(encoded) {
        const e = normalizeEncoded(encoded);
        const [encodedPtr, outPtr] = stage(_defaultCtx._ptr, [e, new Uint32Array(4)]);
        checkStatus(native.wkp_decode_header(encodedPtr, e.byteLength, outPtr));
        return Array.from(new Uint32Array(memory.buffer, outPtr, 4));
    }

    function asContext(ctx) {
        if (!(ctx instanceof Context)) {
            throw new TypeError('ctx must be a Context instance');
        }
        return ctx;
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
            return frameToGeometry(this.geometryType, this.dimensions, this.coords, this.segmentPointCounts, this.groupSegmentCounts, EncodedGeometryType);
        }

        /**
         * Serialize to a compact binary ArrayBuffer (little-endian).
         * The buffer is transferable between workers via postMessage.
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
            // A view can start at any offset in its ArrayBuffer, and the typed arrays below need
            // 8-byte alignment, so copy views into an ArrayBuffer of their own.
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
            const coords = new Float64Array(ab, o, ncoords); o += ncoords * 8;
            const segmentPointCounts = new Uint32Array(ab, o, nseg); o += nseg * 4;
            const groupSegmentCounts = new Uint32Array(ab, o, ngrp);
            return new GeometryFrame({ version, precision, dimensions, geometryType, coords, segmentPointCounts, groupSegmentCounts });
        }
    }

    // ---------------------------------------------------------------------------
    // Public functions
    // ---------------------------------------------------------------------------

    function decode(encoded, ctx = _defaultCtx) {
        return asContext(ctx).decodeGeometry(encoded);
    }

    function decodeFrame(encoded, ctx = _defaultCtx) {
        // Use the private raw-frame method that preserves flat typed arrays.
        const raw = asContext(ctx)._decodeGeometryFrameRaw(encoded);
        return new GeometryFrame({
            version: raw.version,
            precision: raw.precision,
            dimensions: raw.dimensions,
            geometryType: raw.geometryType,
            coords: raw.coords,
            segmentPointCounts: raw.segmentPointCounts,
            groupSegmentCounts: raw.groupSegmentCounts,
        });
    }

    function encode(geometry, precision, ctx = _defaultCtx) {
        return asContext(ctx).encodeGeometry(geometry, precision);
    }

    function encodeFrame(frame, ctx = _defaultCtx) {
        if (!(frame instanceof GeometryFrame)) {
            throw new TypeError('frame must be a GeometryFrame instance');
        }
        return asContext(ctx).encodeGeometryFromFrame(frame);
    }

    function encodeFloats(floats, precisions, ctx = _defaultCtx) {
        const precisionList = normalizePrecisionList(precisions);
        const dimensions = precisionList.length;
        const values = flattenFloatRows(floats, dimensions);
        return asContext(ctx).encodeF64(values, dimensions, precisionList);
    }

    function decodeFloatsArray(encoded, precisions, ctx = _defaultCtx) {
        const precisionList = normalizePrecisionList(precisions);
        const dimensions = precisionList.length;
        return asContext(ctx).decodeF64(encoded, dimensions, precisionList);
    }

    function decodeFloats(encoded, precisions, ctx = _defaultCtx) {
        const precisionList = normalizePrecisionList(precisions);
        const dimensions = precisionList.length;
        const decoded = asContext(ctx).decodeF64(encoded, dimensions, precisionList);
        const rows = new Array(decoded.length / dimensions);
        for (let r = 0, i = 0; i < decoded.length; r += 1) {
            const row = new Array(dimensions);
            for (let d = 0; d < dimensions; d += 1, i += 1) {
                row[d] = decoded[i];
            }
            rows[r] = row;
        }
        return rows;
    }

    return {
        bindingVersion: versionMetadata.bindingVersion,
        coreCompatibility,
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
}
