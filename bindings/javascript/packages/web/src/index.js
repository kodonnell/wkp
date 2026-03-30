import initWkpCore from '../dist/wkp_core.js';
import { loadVersionMetadata } from './version.js';

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
    return TEXT_DECODER.decode(new Uint8Array(bytes));
}

function readU32(module, ptr) {
    return module.HEAPU32[ptr >>> 2] >>> 0;
}

function readSize(module, ptrToSize) {
    return readU32(module, ptrToSize);
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
    const module = await initWkpCore(options);

    const resolveStatus = (symbol, fallback) => {
        const getter = module.cwrap(symbol, 'number', []);
        if (typeof getter === 'function') {
            return getter();
        }
        return fallback;
    };

    const STATUS = Object.freeze({
        OK: resolveStatus('wkp_wasm_status_ok', 0),
        INVALID_ARGUMENT: resolveStatus('wkp_wasm_status_invalid_argument', 1),
        MALFORMED_INPUT: resolveStatus('wkp_wasm_status_malformed_input', 2),
        ALLOCATION_FAILED: resolveStatus('wkp_wasm_status_allocation_failed', 3),
        BUFFER_TOO_SMALL: resolveStatus('wkp_wasm_status_buffer_too_small', 4),
        LIMIT_EXCEEDED: resolveStatus('wkp_wasm_status_limit_exceeded', 5),
        INTERNAL_ERROR: resolveStatus('wkp_wasm_status_internal_error', 255)
    });

    const checkedMalloc = (size, label) => {
        const ptr = module._malloc(size);
        if (ptr === 0) {
            throw new Error(`Allocation failed for ${label}`);
        }
        return ptr;
    };

    const withTempAllocation = (byteLength, label, fn) => {
        const ptr = checkedMalloc(Math.max(1, byteLength), label);
        try {
            return fn(ptr);
        } finally {
            module._free(ptr);
        }
    };

    const workspaceCreateNative = module.cwrap(
        'wkp_wasm_workspace_create',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const workspaceDestroyNative = module.cwrap('wkp_wasm_workspace_destroy', null, ['number']);

    const workspaceEncodeF64Native = module.cwrap(
        'wkp_wasm_workspace_encode_f64',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const workspaceDecodeF64Native = module.cwrap(
        'wkp_wasm_workspace_decode_f64',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const workspaceEncodeGeometryFrameNative = module.cwrap(
        'wkp_wasm_workspace_encode_geometry_frame_f64',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const workspaceDecodeGeometryFrameNative = module.cwrap(
        'wkp_wasm_workspace_decode_geometry_frame_f64',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const decodeHeaderNative = module.cwrap(
        'wkp_wasm_decode_geometry_header',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const coreVersionNative = module.cwrap('wkp_wasm_core_version', 'number', []);
    const basicSelfTestNative = module.cwrap('wkp_wasm_basic_self_test', 'number', ['number']);

    const EncodedGeometryType = Object.freeze({
        POINT: module.cwrap('wkp_wasm_geometry_point', 'number', [])(),
        LINESTRING: module.cwrap('wkp_wasm_geometry_linestring', 'number', [])(),
        POLYGON: module.cwrap('wkp_wasm_geometry_polygon', 'number', [])(),
        MULTIPOINT: module.cwrap('wkp_wasm_geometry_multipoint', 'number', [])(),
        MULTILINESTRING: module.cwrap('wkp_wasm_geometry_multilinestring', 'number', [])(),
        MULTIPOLYGON: module.cwrap('wkp_wasm_geometry_multipolygon', 'number', [])()
    });

    class Context {
        constructor(initialCapacity = 4096) {
            if (!Number.isInteger(initialCapacity) || initialCapacity <= 0) {
                throw new TypeError('initialCapacity must be a positive integer');
            }

            this.initialCapacity = initialCapacity;
            this._disposed = false;
            this._workspacePtr = 0;

            this._workspacePtrPtr = checkedMalloc(4, 'workspace pointer');
            this._outDataPtrPtr = checkedMalloc(4, 'output data pointer');
            this._outSizePtr = checkedMalloc(4, 'output size pointer');
            this._versionPtr = checkedMalloc(4, 'decode version');
            this._precisionPtr = checkedMalloc(4, 'decode precision');
            this._dimensionsPtr = checkedMalloc(4, 'decode dimensions');
            this._geometryTypePtr = checkedMalloc(4, 'decode geometry type');
            this._coordsPtrPtr = checkedMalloc(4, 'decode coords pointer');
            this._coordCountPtr = checkedMalloc(4, 'decode coord count');
            this._segmentCountsPtrPtr = checkedMalloc(4, 'decode segment counts pointer');
            this._segmentCountPtr = checkedMalloc(4, 'decode segment count');
            this._groupCountsPtrPtr = checkedMalloc(4, 'decode group counts pointer');
            this._groupCountPtr = checkedMalloc(4, 'decode group count');
            this._errPtr = checkedMalloc(DEFAULT_ERROR_CAPACITY, 'workspace error message');

            module.HEAPU8.fill(0, this._errPtr, this._errPtr + DEFAULT_ERROR_CAPACITY);
            const status = workspaceCreateNative(
                initialCapacity,
                256,
                -1,
                -1,
                this._workspacePtrPtr,
                this._errPtr,
                DEFAULT_ERROR_CAPACITY
            );
            if (status !== STATUS.OK) {
                const msg = readCString(module, this._errPtr) || statusMessage(status, STATUS);
                this._freeResources();
                throw new Error(msg);
            }

            this._workspacePtr = readU32(module, this._workspacePtrPtr);
            if (!this._workspacePtr) {
                this._freeResources();
                throw new Error('Failed to create native workspace');
            }
        }

        _checkNotDisposed() {
            if (this._disposed || !this._workspacePtr) {
                throw new Error('Context has been disposed');
            }
        }

        _clearErrorBuffer() {
            module.HEAPU8.fill(0, this._errPtr, this._errPtr + DEFAULT_ERROR_CAPACITY);
        }

        _statusError(status) {
            return readCString(module, this._errPtr) || statusMessage(status, STATUS);
        }

        encodeF64(values, dimensions, precisions) {
            this._checkNotDisposed();
            const v = normalizeValues(values);
            const p = normalizePrecisions(precisions);

            return withTempAllocation(v.byteLength, 'encode values', (valuesPtr) =>
                withTempAllocation(p.byteLength, 'encode precisions', (precisionsPtr) => {
                    module.HEAPF64.set(v, valuesPtr >>> 3);
                    module.HEAP32.set(p, precisionsPtr >>> 2);
                    this._clearErrorBuffer();

                    const status = workspaceEncodeF64Native(
                        this._workspacePtr,
                        valuesPtr,
                        v.length,
                        dimensions,
                        precisionsPtr,
                        p.length,
                        this._outDataPtrPtr,
                        this._outSizePtr,
                        this._errPtr,
                        DEFAULT_ERROR_CAPACITY
                    );

                    if (status !== STATUS.OK) {
                        throw new Error(this._statusError(status));
                    }

                    const outDataPtr = readU32(module, this._outDataPtrPtr);
                    const outSize = readSize(module, this._outSizePtr);
                    return module.HEAPU8.slice(outDataPtr, outDataPtr + outSize);
                })
            );
        }

        decodeF64(encoded, dimensions, precisions) {
            this._checkNotDisposed();
            const e = normalizeEncoded(encoded);
            const p = normalizePrecisions(precisions);

            return withTempAllocation(e.byteLength, 'decode encoded', (encodedPtr) =>
                withTempAllocation(p.byteLength, 'decode precisions', (precisionsPtr) => {
                    module.HEAPU8.set(e, encodedPtr);
                    module.HEAP32.set(p, precisionsPtr >>> 2);
                    this._clearErrorBuffer();

                    const status = workspaceDecodeF64Native(
                        this._workspacePtr,
                        encodedPtr,
                        e.byteLength,
                        dimensions,
                        precisionsPtr,
                        p.length,
                        this._outDataPtrPtr,
                        this._outSizePtr,
                        this._errPtr,
                        DEFAULT_ERROR_CAPACITY
                    );

                    if (status !== STATUS.OK) {
                        throw new Error(this._statusError(status));
                    }

                    const outDataPtr = readU32(module, this._outDataPtrPtr);
                    const outSize = readSize(module, this._outSizePtr);
                    const view = module.HEAPF64.subarray(outDataPtr >>> 3, (outDataPtr >>> 3) + outSize);
                    return Float64Array.from(view);
                })
            );
        }

        encodeGeometry(geometry, precision) {
            this._checkNotDisposed();
            const dimensions = geometryDimensions(geometry);
            const frame = buildGeometryFrame(geometry, dimensions, EncodedGeometryType);

            return withTempAllocation(frame.coords.byteLength, 'geometry coords', (coordsPtr) =>
                withTempAllocation(frame.groupSegmentCounts.byteLength, 'geometry group counts', (groupCountsPtr) =>
                    withTempAllocation(frame.segmentPointCounts.byteLength, 'geometry segment counts', (segmentCountsPtr) => {
                        module.HEAPF64.set(frame.coords, coordsPtr >>> 3);
                        module.HEAPU32.set(frame.groupSegmentCounts, groupCountsPtr >>> 2);
                        module.HEAPU32.set(frame.segmentPointCounts, segmentCountsPtr >>> 2);
                        this._clearErrorBuffer();

                        const status = workspaceEncodeGeometryFrameNative(
                            this._workspacePtr,
                            frame.geometryType,
                            coordsPtr,
                            frame.coords.length,
                            dimensions,
                            precision,
                            groupCountsPtr,
                            frame.groupSegmentCounts.length,
                            segmentCountsPtr,
                            frame.segmentPointCounts.length,
                            this._outDataPtrPtr,
                            this._outSizePtr,
                            this._errPtr,
                            DEFAULT_ERROR_CAPACITY
                        );

                        if (status !== STATUS.OK) {
                            throw new Error(this._statusError(status));
                        }

                        const outDataPtr = readU32(module, this._outDataPtrPtr);
                        const outSize = readSize(module, this._outSizePtr);
                        const encodedBytes = module.HEAPU8.slice(outDataPtr, outDataPtr + outSize);
                        return TEXT_DECODER.decode(encodedBytes);
                    })
                )
            );
        }

        decodeGeometry(encoded) {
            this._checkNotDisposed();
            const e = normalizeEncoded(encoded);

            return withTempAllocation(e.byteLength, 'decode geometry encoded', (encodedPtr) => {
                module.HEAPU8.set(e, encodedPtr);
                this._clearErrorBuffer();

                const status = workspaceDecodeGeometryFrameNative(
                    this._workspacePtr,
                    encodedPtr,
                    e.byteLength,
                    this._versionPtr,
                    this._precisionPtr,
                    this._dimensionsPtr,
                    this._geometryTypePtr,
                    this._coordsPtrPtr,
                    this._coordCountPtr,
                    this._segmentCountsPtrPtr,
                    this._segmentCountPtr,
                    this._groupCountsPtrPtr,
                    this._groupCountPtr,
                    this._errPtr,
                    DEFAULT_ERROR_CAPACITY
                );

                if (status !== STATUS.OK) {
                    throw new Error(this._statusError(status));
                }

                const version = module.HEAP32[this._versionPtr >>> 2];
                const precision = module.HEAP32[this._precisionPtr >>> 2];
                const dimensions = module.HEAP32[this._dimensionsPtr >>> 2];
                const geometryType = module.HEAP32[this._geometryTypePtr >>> 2];

                const coordsPtr = readU32(module, this._coordsPtrPtr);
                const coordCount = readSize(module, this._coordCountPtr);
                const segmentCountsPtr = readU32(module, this._segmentCountsPtrPtr);
                const segmentCount = readSize(module, this._segmentCountPtr);
                const groupCountsPtr = readU32(module, this._groupCountsPtrPtr);
                const groupCount = readSize(module, this._groupCountPtr);

                const coords = Float64Array.from(module.HEAPF64.subarray(coordsPtr >>> 3, (coordsPtr >>> 3) + coordCount));
                const segmentPointCounts = Uint32Array.from(module.HEAPU32.subarray(segmentCountsPtr >>> 2, (segmentCountsPtr >>> 2) + segmentCount));
                const groupSegmentCounts = Uint32Array.from(module.HEAPU32.subarray(groupCountsPtr >>> 2, (groupCountsPtr >>> 2) + groupCount));

                return {
                    version,
                    precision,
                    dimensions,
                    geometry: frameToGeometry(
                        geometryType,
                        dimensions,
                        coords,
                        segmentPointCounts,
                        groupSegmentCounts,
                        EncodedGeometryType
                    )
                };
            });
        }

        _freeResources() {
            if (this._workspacePtr) {
                workspaceDestroyNative(this._workspacePtr);
                this._workspacePtr = 0;
            }

            const ptrs = [
                '_workspacePtrPtr',
                '_outDataPtrPtr',
                '_outSizePtr',
                '_versionPtr',
                '_precisionPtr',
                '_dimensionsPtr',
                '_geometryTypePtr',
                '_coordsPtrPtr',
                '_coordCountPtr',
                '_segmentCountsPtrPtr',
                '_segmentCountPtr',
                '_groupCountsPtrPtr',
                '_groupCountPtr',
                '_errPtr'
            ];

            for (const name of ptrs) {
                if (this[name]) {
                    module._free(this[name]);
                    this[name] = 0;
                }
            }
        }

        dispose() {
            if (this._disposed) {
                return;
            }
            this._disposed = true;
            this._freeResources();
        }
    }

    function coreVersion() {
        const ptr = coreVersionNative();
        return readCString(module, ptr);
    }

    function runSelfTest() {
        return withTempAllocation(4, 'basic self test failed check', (failedCheckPtr) => {
            module.HEAP32[failedCheckPtr >>> 2] = 0;
            const status = basicSelfTestNative(failedCheckPtr);
            if (status !== STATUS.OK) {
                const failedCheck = module.HEAP32[failedCheckPtr >>> 2];
                throw new Error(`WKP core self-test failed (check ${failedCheck})`);
            }
            return true;
        });
    }

    const runtimeCoreVersion = coreVersion();
    const coreCompatibility = versionMetadata.coreCompatibility ?? formatMajorMinorCompatibility(runtimeCoreVersion);

    assertCoreCompatibility(runtimeCoreVersion, versionMetadata.bindingVersion, coreCompatibility);
    runSelfTest();

    function decodeHeader(encoded) {
        const e = normalizeEncoded(encoded);

        return withTempAllocation(e.byteLength, 'decode header encoded', (encodedPtr) =>
            withTempAllocation(4, 'decode header version', (versionPtr) =>
                withTempAllocation(4, 'decode header precision', (precisionPtr) =>
                    withTempAllocation(4, 'decode header dimensions', (dimensionsPtr) =>
                        withTempAllocation(4, 'decode header geometry type', (geometryTypePtr) =>
                            withTempAllocation(DEFAULT_ERROR_CAPACITY, 'decode header error', (errPtr) => {
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
                                    const msg = readCString(module, errPtr) || statusMessage(status, STATUS);
                                    throw new Error(msg);
                                }

                                return [
                                    module.HEAP32[versionPtr >>> 2],
                                    module.HEAP32[precisionPtr >>> 2],
                                    module.HEAP32[dimensionsPtr >>> 2],
                                    module.HEAP32[geometryTypePtr >>> 2]
                                ];
                            })
                        )
                    )
                )
            )
        );
    }

    function asContext(ctx) {
        if (!(ctx instanceof Context)) {
            throw new TypeError('ctx must be a Context instance');
        }
        return ctx;
    }

    function decode(ctx, encoded) {
        return asContext(ctx).decodeGeometry(encoded);
    }

    function encode(ctx, geometry, precision) {
        return asContext(ctx).encodeGeometry(geometry, precision);
    }

    function encodeFloats(ctx, floats, precisions) {
        const precisionList = normalizePrecisionList(precisions);
        const dimensions = precisionList.length;
        const values = flattenFloatRows(floats, dimensions);
        return asContext(ctx).encodeF64(values, dimensions, precisionList);
    }

    function decodeFloats(ctx, encoded, precisions) {
        const precisionList = normalizePrecisionList(precisions);
        const dimensions = precisionList.length;
        const decoded = asContext(ctx).decodeF64(encoded, dimensions, precisionList);
        const rows = [];
        for (let i = 0; i < decoded.length; i += dimensions) {
            rows.push(Array.from(decoded.slice(i, i + dimensions)));
        }
        return rows;
    }

    return {
        bindingVersion: versionMetadata.bindingVersion,
        coreCompatibility,
        Context,
        decodeHeader,
        decode,
        encode,
        encodeFloats,
        decodeFloats,
        runSelfTest,
        coreVersion,
        EncodedGeometryType
    };
}
