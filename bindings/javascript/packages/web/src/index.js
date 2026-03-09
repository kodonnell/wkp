import initWkpCore from '../dist/wkp_core.js';
import packageInfo from '../package.json' with { type: 'json' };

const BINDING_VERSION = packageInfo.version;
const CORE_COMPATIBILITY = packageInfo.wkpCoreCompatibility;

const STATUS = {
    OK: 0,
    INVALID_ARGUMENT: 1,
    MALFORMED_INPUT: 2,
    ALLOCATION_FAILED: 3,
    INTERNAL_ERROR: 255
};

const DEFAULT_ERROR_CAPACITY = 512;
const TEXT_ENCODER = new TextEncoder();
const TEXT_DECODER = new TextDecoder();

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
    const compatibilityMatch = /^([0-9]+)\.([0-9]+)\.x$/.exec(CORE_COMPATIBILITY);
    if (!compatibilityMatch) {
        throw new Error(`Invalid compatibility range in package.json: ${CORE_COMPATIBILITY}`);
    }
    const requiredMajor = Number.parseInt(compatibilityMatch[1], 10);
    const requiredMinor = Number.parseInt(compatibilityMatch[2], 10);

    if (coreMajor !== requiredMajor || coreMinor !== requiredMinor) {
        throw new Error(
            `@wkpjs/web ${BINDING_VERSION} requires WKP core ${CORE_COMPATIBILITY}, but loaded core is ${runtimeCoreVersion}`
        );
    }
}

function encodeHeaderField(value, name) {
    if (!Number.isInteger(value) || value < 0 || value > 63) {
        throw new TypeError(`${name} must be an integer in [0, 63]`);
    }
    return String.fromCharCode(value + 63);
}

function statusMessage(status) {
    switch (status) {
        case STATUS.INVALID_ARGUMENT:
            return 'Invalid argument';
        case STATUS.MALFORMED_INPUT:
            return 'Malformed input';
        case STATUS.ALLOCATION_FAILED:
            return 'Allocation failed';
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

function normalizeEncoded(encoded) {
    if (typeof encoded === 'string') {
        return TEXT_ENCODER.encode(encoded);
    }
    if (encoded instanceof Uint8Array) {
        return encoded;
    }
    throw new TypeError('encoded must be string or Uint8Array');
}

function toAscii(encoded) {
    if (typeof encoded === 'string') {
        return encoded;
    }
    return TEXT_DECODER.decode(normalizeEncoded(encoded));
}

function splitBody(body, separator, label) {
    const parts = body.split(separator);
    if (parts.some((p) => p.length === 0)) {
        throw new TypeError(`Malformed encoded geometry (${label})`);
    }
    return parts;
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

function flattenCoordRows(rows, dimensions, label) {
    if (!Array.isArray(rows) || rows.length === 0) {
        throw new TypeError(`${label} must contain at least one coordinate`);
    }
    const out = new Float64Array(rows.length * dimensions);
    for (let rowIndex = 0; rowIndex < rows.length; rowIndex += 1) {
        const row = normalizeCoord(rows[rowIndex], dimensions, label);
        for (let dim = 0; dim < dimensions; dim += 1) {
            out[(rowIndex * dimensions) + dim] = row[dim];
        }
    }
    return out;
}

function flattenCoordRowsInto(rows, dimensions, label, state) {
    if (!Array.isArray(rows) || rows.length === 0) {
        throw new TypeError(`${label} must contain at least one coordinate`);
    }
    const needed = rows.length * dimensions;
    if (state.values.length < needed) {
        let next = Math.max(state.values.length, 1);
        while (next < needed) {
            next *= 2;
        }
        state.values = new Float64Array(next);
    }
    const out = state.values;
    for (let rowIndex = 0; rowIndex < rows.length; rowIndex += 1) {
        const row = normalizeCoord(rows[rowIndex], dimensions, label);
        for (let dim = 0; dim < dimensions; dim += 1) {
            out[(rowIndex * dimensions) + dim] = row[dim];
        }
    }
    return out.subarray(0, needed);
}

function rowsFromFlatValues(values, dimensions) {
    if (values.length % dimensions !== 0) {
        throw new TypeError('Decoded coordinate vector has invalid length');
    }
    const rowCount = values.length / dimensions;
    const rows = new Array(rowCount);
    for (let rowIndex = 0; rowIndex < rowCount; rowIndex += 1) {
        const row = new Array(dimensions);
        for (let dim = 0; dim < dimensions; dim += 1) {
            row[dim] = values[(rowIndex * dimensions) + dim];
        }
        rows[rowIndex] = row;
    }
    return rows;
}

function decodePolylineSegment(segment, dimensions, precision) {
    const factor = 10 ** precision;
    const state = new Array(dimensions).fill(0);
    const rows = [];
    let index = 0;

    while (index < segment.length) {
        const row = new Array(dimensions);
        for (let dim = 0; dim < dimensions; dim += 1) {
            let result = 0;
            let shift = 0;
            let byte = 0;

            do {
                if (index >= segment.length) {
                    throw new TypeError('Malformed encoded polyline segment');
                }
                byte = segment.charCodeAt(index) - 63;
                index += 1;
                result |= (byte & 0x1f) << shift;
                shift += 5;
            } while (byte >= 0x20);

            const delta = (result & 1) ? (~(result >> 1)) : (result >> 1);
            state[dim] += delta;
            row[dim] = state[dim] / factor;
        }
        rows.push(row);
    }

    return rows;
}

function readCString(module, ptr) {
    if (!ptr) {
        return '';
    }
    const bytes = [];
    let offset = ptr;
    while (module.HEAPU8[offset] !== 0) {
        bytes.push(module.HEAPU8[offset]);
        offset += 1;
    }
    return new TextDecoder().decode(new Uint8Array(bytes));
}

function readPtr(module, ptrToPtr) {
    return module.HEAPU32[ptrToPtr >>> 2] >>> 0;
}

function readSize(module, ptrToSize) {
    return module.HEAPU32[ptrToSize >>> 2] >>> 0;
}

export async function createWkp(options) {
    const module = await initWkpCore(options);

    const checkedMalloc = (size, label) => {
        const ptr = module._malloc(size);
        if (ptr === 0) {
            throw new Error(`Allocation failed for ${label}`);
        }
        return ptr;
    };

    const encodeNative = module.cwrap(
        'wkp_wasm_encode_f64',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const decodeNative = module.cwrap(
        'wkp_wasm_decode_f64',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const decodeHeaderNative = module.cwrap(
        'wkp_wasm_decode_geometry_header',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const freeU8 = module.cwrap('wkp_wasm_free_u8', null, ['number', 'number']);
    const freeF64 = module.cwrap('wkp_wasm_free_f64', null, ['number', 'number']);
    const coreVersionNative = module.cwrap('wkp_wasm_core_version', 'number', []);

    const EncodedGeometryType = Object.freeze({
        POINT: module.cwrap('wkp_wasm_geometry_point', 'number', [])(),
        LINESTRING: module.cwrap('wkp_wasm_geometry_linestring', 'number', [])(),
        POLYGON: module.cwrap('wkp_wasm_geometry_polygon', 'number', [])(),
        MULTIPOINT: module.cwrap('wkp_wasm_geometry_multipoint', 'number', [])(),
        MULTILINESTRING: module.cwrap('wkp_wasm_geometry_multilinestring', 'number', [])(),
        MULTIPOLYGON: module.cwrap('wkp_wasm_geometry_multipolygon', 'number', [])()
    });

    function createEncodeScratch(initialCapacity) {
        const baseCapacity = Number.isInteger(initialCapacity) && initialCapacity > 0
            ? initialCapacity
            : 4096;

        const scratch = {
            valuesPtr: 0,
            valuesCapacity: 0,
            precisionsPtr: 0,
            precisionsCapacity: 0,
            outDataPtrPtr: checkedMalloc(4, 'encode out_data pointer'),
            outSizePtr: checkedMalloc(4, 'encode out_size pointer'),
            errPtr: checkedMalloc(DEFAULT_ERROR_CAPACITY, 'encode error message'),
            disposed: false
        };

        const ensureCapacity = (kind, requiredBytes, label) => {
            const ptrKey = `${kind}Ptr`;
            const capKey = `${kind}Capacity`;
            if (scratch[capKey] >= requiredBytes) {
                return;
            }
            let next = Math.max(scratch[capKey], baseCapacity);
            while (next < requiredBytes) {
                next *= 2;
            }
            const nextPtr = checkedMalloc(next, label);
            if (scratch[ptrKey] !== 0) {
                module._free(scratch[ptrKey]);
            }
            scratch[ptrKey] = nextPtr;
            scratch[capKey] = next;
        };

        scratch.ensureForEncode = (valueBytes, precisionBytes) => {
            if (scratch.disposed) {
                throw new Error('Workspace has been disposed');
            }
            ensureCapacity('values', valueBytes, 'encode values scratch');
            ensureCapacity('precisions', precisionBytes, 'encode precisions scratch');
        };

        scratch.dispose = () => {
            if (scratch.disposed) {
                return;
            }
            scratch.disposed = true;
            if (scratch.valuesPtr !== 0) {
                module._free(scratch.valuesPtr);
                scratch.valuesPtr = 0;
            }
            if (scratch.precisionsPtr !== 0) {
                module._free(scratch.precisionsPtr);
                scratch.precisionsPtr = 0;
            }
            module._free(scratch.outDataPtrPtr);
            module._free(scratch.outSizePtr);
            module._free(scratch.errPtr);
        };

        return scratch;
    }

    function createDecodeScratch(initialCapacity) {
        const baseCapacity = Number.isInteger(initialCapacity) && initialCapacity > 0
            ? initialCapacity
            : 4096;

        const scratch = {
            encodedPtr: 0,
            encodedCapacity: 0,
            precisionsPtr: 0,
            precisionsCapacity: 0,
            outDataPtrPtr: checkedMalloc(4, 'decode out_data pointer'),
            outSizePtr: checkedMalloc(4, 'decode out_size pointer'),
            errPtr: checkedMalloc(DEFAULT_ERROR_CAPACITY, 'decode error message'),
            disposed: false
        };

        const ensureCapacity = (kind, requiredBytes, label) => {
            const ptrKey = `${kind}Ptr`;
            const capKey = `${kind}Capacity`;
            if (scratch[capKey] >= requiredBytes) {
                return;
            }
            let next = Math.max(scratch[capKey], baseCapacity);
            while (next < requiredBytes) {
                next *= 2;
            }
            const nextPtr = checkedMalloc(next, label);
            if (scratch[ptrKey] !== 0) {
                module._free(scratch[ptrKey]);
            }
            scratch[ptrKey] = nextPtr;
            scratch[capKey] = next;
        };

        scratch.ensureForDecode = (encodedBytes, precisionBytes) => {
            if (scratch.disposed) {
                throw new Error('Workspace has been disposed');
            }
            ensureCapacity('encoded', encodedBytes, 'decode encoded scratch');
            ensureCapacity('precisions', precisionBytes, 'decode precisions scratch');
        };

        scratch.dispose = () => {
            if (scratch.disposed) {
                return;
            }
            scratch.disposed = true;
            if (scratch.encodedPtr !== 0) {
                module._free(scratch.encodedPtr);
                scratch.encodedPtr = 0;
            }
            if (scratch.precisionsPtr !== 0) {
                module._free(scratch.precisionsPtr);
                scratch.precisionsPtr = 0;
            }
            module._free(scratch.outDataPtrPtr);
            module._free(scratch.outSizePtr);
            module._free(scratch.errPtr);
        };

        return scratch;
    }

    function encodeF64WithScratch(values, dimensions, precisions, scratch) {
        const v = normalizeValues(values);
        const p = normalizePrecisions(precisions);

        scratch.ensureForEncode(v.byteLength, p.byteLength);

        module.HEAPF64.set(v, scratch.valuesPtr / 8);
        module.HEAP32.set(p, scratch.precisionsPtr / 4);
        module.HEAPU8.fill(0, scratch.errPtr, scratch.errPtr + DEFAULT_ERROR_CAPACITY);

        const status = encodeNative(
            scratch.valuesPtr,
            v.length,
            dimensions,
            scratch.precisionsPtr,
            p.length,
            scratch.outDataPtrPtr,
            scratch.outSizePtr,
            scratch.errPtr,
            DEFAULT_ERROR_CAPACITY
        );

        const outDataPtr = readPtr(module, scratch.outDataPtrPtr);
        const outSize = readSize(module, scratch.outSizePtr);

        if (status !== STATUS.OK) {
            const msg = readCString(module, scratch.errPtr) || statusMessage(status);
            throw new Error(msg);
        }

        const copy = module.HEAPU8.slice(outDataPtr, outDataPtr + outSize);
        freeU8(outDataPtr, outSize);
        return copy;
    }

    function decodeF64WithScratch(encoded, dimensions, precisions, scratch) {
        const e = normalizeEncoded(encoded);
        const p = normalizePrecisions(precisions);

        scratch.ensureForDecode(e.byteLength, p.byteLength);

        module.HEAPU8.set(e, scratch.encodedPtr);
        module.HEAP32.set(p, scratch.precisionsPtr / 4);
        module.HEAPU8.fill(0, scratch.errPtr, scratch.errPtr + DEFAULT_ERROR_CAPACITY);

        const status = decodeNative(
            scratch.encodedPtr,
            e.byteLength,
            dimensions,
            scratch.precisionsPtr,
            p.length,
            scratch.outDataPtrPtr,
            scratch.outSizePtr,
            scratch.errPtr,
            DEFAULT_ERROR_CAPACITY
        );

        const outDataPtr = readPtr(module, scratch.outDataPtrPtr);
        const outSize = readSize(module, scratch.outSizePtr);

        if (status !== STATUS.OK) {
            const msg = readCString(module, scratch.errPtr) || statusMessage(status);
            throw new Error(msg);
        }

        const view = module.HEAPF64.subarray(outDataPtr / 8, (outDataPtr / 8) + outSize);
        const copy = Float64Array.from(view);
        freeF64(outDataPtr, outSize);
        return copy;
    }

    function coreVersion() {
        const ptr = coreVersionNative();
        return readCString(module, ptr);
    }

    assertCoreCompatibility(coreVersion());

    function decodeHeaderInternal(encoded) {
        const e = normalizeEncoded(encoded);
        const encodedPtr = module._malloc(e.byteLength);
        const versionPtr = module._malloc(4);
        const precisionPtr = module._malloc(4);
        const dimensionsPtr = module._malloc(4);
        const geometryTypePtr = module._malloc(4);
        const errPtr = module._malloc(DEFAULT_ERROR_CAPACITY);

        try {
            module.HEAPU8.set(e, encodedPtr);
            module.HEAPU8.fill(0, errPtr, errPtr + DEFAULT_ERROR_CAPACITY);

            const status = decodeHeaderNative(
                encodedPtr,
                e.byteLength,
                versionPtr,
                precisionPtr,
                dimensionsPtr,
                geometryTypePtr,
                errPtr,
                DEFAULT_ERROR_CAPACITY
            );

            if (status !== STATUS.OK) {
                const msg = readCString(module, errPtr) || statusMessage(status);
                throw new Error(msg);
            }

            return [
                module.HEAP32[versionPtr >>> 2],
                module.HEAP32[precisionPtr >>> 2],
                module.HEAP32[dimensionsPtr >>> 2],
                module.HEAP32[geometryTypePtr >>> 2]
            ];
        } finally {
            module._free(encodedPtr);
            module._free(versionPtr);
            module._free(precisionPtr);
            module._free(dimensionsPtr);
            module._free(geometryTypePtr);
            module._free(errPtr);
        }
    }

    function decodeGeometryBody(body, geometryType, dimensions, precision, decodeSegmentRows) {

        if (geometryType === EncodedGeometryType.POINT) {
            const rows = decodeSegmentRows(body, 'point');
            if (rows.length !== 1) {
                throw new TypeError('Expected one point for POINT geometry');
            }
            return { type: 'Point', coordinates: rows[0] };
        }

        if (geometryType === EncodedGeometryType.LINESTRING) {
            const rows = decodeSegmentRows(body, 'linestring');
            if (rows.length < 2) {
                throw new TypeError('Expected at least two points for LINESTRING geometry');
            }
            return { type: 'LineString', coordinates: rows };
        }

        if (geometryType === EncodedGeometryType.POLYGON) {
            const rings = splitBody(body, ',', 'polygon rings').map((segment) => decodeSegmentRows(segment, 'polygon ring'));
            return { type: 'Polygon', coordinates: rings };
        }

        if (geometryType === EncodedGeometryType.MULTIPOINT) {
            const points = splitBody(body, ';', 'multipoint parts').map((segment) => {
                const rows = decodeSegmentRows(segment, 'multipoint point');
                if (rows.length !== 1) {
                    throw new TypeError('Expected one coordinate per MULTIPOINT part');
                }
                return rows[0];
            });
            return { type: 'MultiPoint', coordinates: points };
        }

        if (geometryType === EncodedGeometryType.MULTILINESTRING) {
            const lines = splitBody(body, ';', 'multilinestring parts').map((segment) => decodeSegmentRows(segment, 'multilinestring part'));
            return { type: 'MultiLineString', coordinates: lines };
        }

        if (geometryType === EncodedGeometryType.MULTIPOLYGON) {
            const polygons = splitBody(body, ';', 'multipolygon parts').map((polygonBody) =>
                splitBody(polygonBody, ',', 'multipolygon rings').map((ringBody) => decodeSegmentRows(ringBody, 'multipolygon ring'))
            );
            return { type: 'MultiPolygon', coordinates: polygons };
        }

        throw new TypeError(`Unsupported geometry type in header: ${geometryType}`);
    }

    function encodeGeometryBody(geometry, dimensions, precision, encodeRows) {
        if (!geometry || typeof geometry !== 'object' || typeof geometry.type !== 'string') {
            throw new TypeError('geometry must be an object with GeoJSON-like type and coordinates');
        }

        if (geometry.type === 'Point') {
            return [EncodedGeometryType.POINT, encodeRows([geometry.coordinates], 'point coordinate')];
        }

        if (geometry.type === 'LineString') {
            return [EncodedGeometryType.LINESTRING, encodeRows(geometry.coordinates, 'linestring coordinates')];
        }

        if (geometry.type === 'Polygon') {
            if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
                throw new TypeError('polygon coordinates must contain at least one ring');
            }
            const body = geometry.coordinates.map((ring) => encodeRows(ring, 'polygon ring')).join(',');
            return [EncodedGeometryType.POLYGON, body];
        }

        if (geometry.type === 'MultiPoint') {
            if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
                throw new TypeError('multipoint coordinates must contain at least one point');
            }
            const body = geometry.coordinates.map((point) => encodeRows([point], 'multipoint point')).join(';');
            return [EncodedGeometryType.MULTIPOINT, body];
        }

        if (geometry.type === 'MultiLineString') {
            if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
                throw new TypeError('multilinestring coordinates must contain at least one line');
            }
            const body = geometry.coordinates.map((line) => encodeRows(line, 'multilinestring part')).join(';');
            return [EncodedGeometryType.MULTILINESTRING, body];
        }

        if (geometry.type === 'MultiPolygon') {
            if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
                throw new TypeError('multipolygon coordinates must contain at least one polygon');
            }
            const body = geometry.coordinates
                .map((polygon) => {
                    if (!Array.isArray(polygon) || polygon.length === 0) {
                        throw new TypeError('each multipolygon part must contain at least one ring');
                    }
                    return polygon.map((ring) => encodeRows(ring, 'multipolygon ring')).join(',');
                })
                .join(';');
            return [EncodedGeometryType.MULTIPOLYGON, body];
        }

        throw new TypeError(`Unsupported geometry type: ${geometry.type}`);
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

    class Workspace {
        constructor(initialCapacity = 4096) {
            if (!Number.isInteger(initialCapacity) || initialCapacity <= 0) {
                throw new TypeError('initialCapacity must be a positive integer');
            }
            this.initialCapacity = initialCapacity;
            this._encodeScratch = createEncodeScratch(initialCapacity);
            this._decodeScratch = createDecodeScratch(initialCapacity);
            this._valueScratch = {
                values: new Float64Array(Math.max(1, Math.ceil(initialCapacity / Float64Array.BYTES_PER_ELEMENT)))
            };
        }

        encodeF64(values, dimensions, precisions) {
            return encodeF64WithScratch(values, dimensions, precisions, this._encodeScratch);
        }

        decodeF64(encoded, dimensions, precisions) {
            return decodeF64WithScratch(encoded, dimensions, precisions, this._decodeScratch);
        }

        dispose() {
            this._encodeScratch.dispose();
            this._decodeScratch.dispose();
        }
    }

    let defaultWorkspace = null;

    function resolveWorkspace(workspace) {
        if (workspace == null) {
            if (defaultWorkspace == null) {
                defaultWorkspace = new Workspace();
            }
            return defaultWorkspace;
        }
        if (!(workspace instanceof Workspace)) {
            throw new TypeError('workspace must be a Workspace instance');
        }
        return workspace;
    }

    function decodeHeader(encoded) {
        return decodeHeaderInternal(encoded);
    }

    function decode(encoded, workspace = undefined) {
        const ws = resolveWorkspace(workspace);
        const ascii = toAscii(encoded);
        const [version, precision, dimensions, geometryType] = decodeHeader(ascii);
        const geometry = decodeGeometryBody(
            ascii.slice(4),
            geometryType,
            dimensions,
            precision,
            (segment) => rowsFromFlatValues(ws.decodeF64(segment, dimensions, [precision]), dimensions)
        );
        return { version, precision, dimensions, geometry };
    }

    function encodePoint(geometry, precision, workspace = undefined) {
        if (!geometry || geometry.type !== 'Point') {
            throw new TypeError('geometry must be a Point geometry');
        }
        const ws = resolveWorkspace(workspace);
        const dimensions = geometryDimensions(geometry);
        const [geometryType, body] = encodeGeometryBody(
            geometry,
            dimensions,
            precision,
            (rows, label) => TEXT_DECODER.decode(ws.encodeF64(flattenCoordRowsInto(rows, dimensions, label, ws._valueScratch), dimensions, [precision]))
        );
        return `${encodeHeaderField(1, 'version')}${encodeHeaderField(precision, 'precision')}${encodeHeaderField(dimensions, 'dimensions')}${encodeHeaderField(geometryType, 'geometry type')}${body}`;
    }

    function encodeLineString(geometry, precision, workspace = undefined) {
        if (!geometry || geometry.type !== 'LineString') {
            throw new TypeError('geometry must be a LineString geometry');
        }
        const ws = resolveWorkspace(workspace);
        const dimensions = geometryDimensions(geometry);
        const [geometryType, body] = encodeGeometryBody(
            geometry,
            dimensions,
            precision,
            (rows, label) => TEXT_DECODER.decode(ws.encodeF64(flattenCoordRowsInto(rows, dimensions, label, ws._valueScratch), dimensions, [precision]))
        );
        return `${encodeHeaderField(1, 'version')}${encodeHeaderField(precision, 'precision')}${encodeHeaderField(dimensions, 'dimensions')}${encodeHeaderField(geometryType, 'geometry type')}${body}`;
    }

    function encodePolygon(geometry, precision, workspace = undefined) {
        if (!geometry || geometry.type !== 'Polygon') {
            throw new TypeError('geometry must be a Polygon geometry');
        }
        const ws = resolveWorkspace(workspace);
        const dimensions = geometryDimensions(geometry);
        const [geometryType, body] = encodeGeometryBody(
            geometry,
            dimensions,
            precision,
            (rows, label) => TEXT_DECODER.decode(ws.encodeF64(flattenCoordRowsInto(rows, dimensions, label, ws._valueScratch), dimensions, [precision]))
        );
        return `${encodeHeaderField(1, 'version')}${encodeHeaderField(precision, 'precision')}${encodeHeaderField(dimensions, 'dimensions')}${encodeHeaderField(geometryType, 'geometry type')}${body}`;
    }

    function encodeMultiPoint(geometry, precision, workspace = undefined) {
        if (!geometry || geometry.type !== 'MultiPoint') {
            throw new TypeError('geometry must be a MultiPoint geometry');
        }
        const ws = resolveWorkspace(workspace);
        const dimensions = geometryDimensions(geometry);
        const [geometryType, body] = encodeGeometryBody(
            geometry,
            dimensions,
            precision,
            (rows, label) => TEXT_DECODER.decode(ws.encodeF64(flattenCoordRowsInto(rows, dimensions, label, ws._valueScratch), dimensions, [precision]))
        );
        return `${encodeHeaderField(1, 'version')}${encodeHeaderField(precision, 'precision')}${encodeHeaderField(dimensions, 'dimensions')}${encodeHeaderField(geometryType, 'geometry type')}${body}`;
    }

    function encodeMultiLineString(geometry, precision, workspace = undefined) {
        if (!geometry || geometry.type !== 'MultiLineString') {
            throw new TypeError('geometry must be a MultiLineString geometry');
        }
        const ws = resolveWorkspace(workspace);
        const dimensions = geometryDimensions(geometry);
        const [geometryType, body] = encodeGeometryBody(
            geometry,
            dimensions,
            precision,
            (rows, label) => TEXT_DECODER.decode(ws.encodeF64(flattenCoordRowsInto(rows, dimensions, label, ws._valueScratch), dimensions, [precision]))
        );
        return `${encodeHeaderField(1, 'version')}${encodeHeaderField(precision, 'precision')}${encodeHeaderField(dimensions, 'dimensions')}${encodeHeaderField(geometryType, 'geometry type')}${body}`;
    }

    function encodeMultiPolygon(geometry, precision, workspace = undefined) {
        if (!geometry || geometry.type !== 'MultiPolygon') {
            throw new TypeError('geometry must be a MultiPolygon geometry');
        }
        const ws = resolveWorkspace(workspace);
        const dimensions = geometryDimensions(geometry);
        const [geometryType, body] = encodeGeometryBody(
            geometry,
            dimensions,
            precision,
            (rows, label) => TEXT_DECODER.decode(ws.encodeF64(flattenCoordRowsInto(rows, dimensions, label, ws._valueScratch), dimensions, [precision]))
        );
        return `${encodeHeaderField(1, 'version')}${encodeHeaderField(precision, 'precision')}${encodeHeaderField(dimensions, 'dimensions')}${encodeHeaderField(geometryType, 'geometry type')}${body}`;
    }

    const encodeF64 = (values, dimensions, precisions, workspace = undefined) => resolveWorkspace(workspace).encodeF64(values, dimensions, precisions);
    const decodeF64 = (encoded, dimensions, precisions, workspace = undefined) => resolveWorkspace(workspace).decodeF64(encoded, dimensions, precisions);

    return {
        bindingVersion: BINDING_VERSION,
        coreCompatibility: CORE_COMPATIBILITY,
        Workspace,
        decodeHeader,
        decode,
        encodePoint,
        encodeLineString,
        encodePolygon,
        encodeMultiPoint,
        encodeMultiLineString,
        encodeMultiPolygon,
        encodeF64,
        decodeF64,
        coreVersion,
        EncodedGeometryType
    };
}
