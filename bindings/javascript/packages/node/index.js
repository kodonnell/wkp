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
    const compatibilityMatch = /^([0-9]+)\.([0-9]+)\.x$/.exec(CORE_COMPATIBILITY);
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

const EncodedGeometryType = Object.freeze({ ...core.EncodedGeometryType });

const decoder = new TextDecoder();

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

function toAscii(input) {
    if (typeof input === 'string') {
        return input;
    }
    return decoder.decode(normalizeEncodedBytes(input));
}

function decodeHeaderInternal(encodedValue) {
    const header = core.decodeGeometryHeader(encodedValue);
    return [header[0], header[1], header[2], header[3]];
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

function decodeBody(body, geometryType, dimensions, precision) {
    const decodeSegmentRows = (segment) => decodePolylineSegment(segment, dimensions, precision);

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

function encodeGeometry(geometry, precision, dimensions, workspace) {
    const ws = workspace;
    if (!geometry || typeof geometry !== 'object' || typeof geometry.type !== 'string') {
        throw new TypeError('geometry must be an object with GeoJSON-like type and coordinates');
    }

    if (geometry.type === 'Point') {
        const values = flattenCoordRowsInto([geometry.coordinates], dimensions, 'point coordinate', ws._valueScratch);
        return decoder.decode(core.encodePointF64(values, dimensions, precision));
    }

    if (geometry.type === 'LineString') {
        const values = flattenCoordRowsInto(geometry.coordinates, dimensions, 'linestring coordinates', ws._valueScratch);
        return decoder.decode(core.encodeLineStringF64(values, dimensions, precision));
    }

    if (geometry.type === 'Polygon') {
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError('polygon coordinates must contain at least one ring');
        }
        const ringPointCounts = [];
        const flat = [];
        for (const ring of geometry.coordinates) {
            const ringValues = flattenCoordRows(ring, dimensions, 'polygon ring');
            ringPointCounts.push(ring.length);
            for (const v of ringValues) {
                flat.push(v);
            }
        }
        return decoder.decode(core.encodePolygonF64(Float64Array.from(flat), dimensions, precision, ringPointCounts));
    }

    if (geometry.type === 'MultiPoint') {
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError('multipoint coordinates must contain at least one point');
        }
        const values = flattenCoordRowsInto(geometry.coordinates, dimensions, 'multipoint points', ws._valueScratch);
        return decoder.decode(core.encodeMultiPointF64(values, dimensions, precision, geometry.coordinates.length));
    }

    if (geometry.type === 'MultiLineString') {
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError('multilinestring coordinates must contain at least one line');
        }
        const linePointCounts = [];
        const flat = [];
        for (const line of geometry.coordinates) {
            const lineValues = flattenCoordRows(line, dimensions, 'multilinestring part');
            linePointCounts.push(line.length);
            for (const v of lineValues) {
                flat.push(v);
            }
        }
        return decoder.decode(core.encodeMultiLineStringF64(Float64Array.from(flat), dimensions, precision, linePointCounts));
    }

    if (geometry.type === 'MultiPolygon') {
        if (!Array.isArray(geometry.coordinates) || geometry.coordinates.length === 0) {
            throw new TypeError('multipolygon coordinates must contain at least one polygon');
        }

        const polygonRingCounts = [];
        const ringPointCounts = [];
        const flat = [];

        for (const polygon of geometry.coordinates) {
            if (!Array.isArray(polygon) || polygon.length === 0) {
                throw new TypeError('each multipolygon part must contain at least one ring');
            }
            polygonRingCounts.push(polygon.length);
            for (const ring of polygon) {
                const ringValues = flattenCoordRows(ring, dimensions, 'multipolygon ring');
                ringPointCounts.push(ring.length);
                for (const v of ringValues) {
                    flat.push(v);
                }
            }
        }

        return decoder.decode(core.encodeMultiPolygonF64(
            Float64Array.from(flat),
            dimensions,
            precision,
            polygonRingCounts,
            ringPointCounts
        ));
    }

    throw new TypeError(`Unsupported geometry type: ${geometry.type}`);
}

function decodeGeometry(encodedValue, workspace) {
    void (workspace);
    return core.decodeGeometryFrame(encodedValue);
}

class Workspace {
    constructor(initialCapacity = 4096) {
        if (!Number.isInteger(initialCapacity) || initialCapacity <= 0) {
            throw new TypeError('initialCapacity must be a positive integer');
        }
        this.initialCapacity = initialCapacity;
        this._valueScratch = {
            values: new Float64Array(Math.max(1, Math.ceil(initialCapacity / Float64Array.BYTES_PER_ELEMENT)))
        };
    }

    encodeF64(values, dimensions, precisions) {
        return core.encodeF64(values, dimensions, precisions);
    }

    decodeF64(encoded, dimensions, precisions) {
        return core.decodeF64(encoded, dimensions, precisions);
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

function decodeHeader(encodedValue) {
    return decodeHeaderInternal(encodedValue);
}

function decode(encodedValue, workspace = undefined) {
    return decodeGeometry(encodedValue, resolveWorkspace(workspace));
}

function encodePoint(geometry, precision, workspace = undefined) {
    if (!geometry || geometry.type !== 'Point') {
        throw new TypeError('geometry must be a Point geometry');
    }
    const dims = geometryDimensions(geometry);
    return encodeGeometry(geometry, precision, dims, resolveWorkspace(workspace));
}

function encodeLineString(geometry, precision, workspace = undefined) {
    if (!geometry || geometry.type !== 'LineString') {
        throw new TypeError('geometry must be a LineString geometry');
    }
    const dims = geometryDimensions(geometry);
    return encodeGeometry(geometry, precision, dims, resolveWorkspace(workspace));
}

function encodePolygon(geometry, precision, workspace = undefined) {
    if (!geometry || geometry.type !== 'Polygon') {
        throw new TypeError('geometry must be a Polygon geometry');
    }
    const dims = geometryDimensions(geometry);
    return encodeGeometry(geometry, precision, dims, resolveWorkspace(workspace));
}

function encodeMultiPoint(geometry, precision, workspace = undefined) {
    if (!geometry || geometry.type !== 'MultiPoint') {
        throw new TypeError('geometry must be a MultiPoint geometry');
    }
    const dims = geometryDimensions(geometry);
    return encodeGeometry(geometry, precision, dims, resolveWorkspace(workspace));
}

function encodeMultiLineString(geometry, precision, workspace = undefined) {
    if (!geometry || geometry.type !== 'MultiLineString') {
        throw new TypeError('geometry must be a MultiLineString geometry');
    }
    const dims = geometryDimensions(geometry);
    return encodeGeometry(geometry, precision, dims, resolveWorkspace(workspace));
}

function encodeMultiPolygon(geometry, precision, workspace = undefined) {
    if (!geometry || geometry.type !== 'MultiPolygon') {
        throw new TypeError('geometry must be a MultiPolygon geometry');
    }
    const dims = geometryDimensions(geometry);
    return encodeGeometry(geometry, precision, dims, resolveWorkspace(workspace));
}

const encodeF64 = (values, dimensions, precisions, workspace = undefined) => resolveWorkspace(workspace).encodeF64(values, dimensions, precisions);
const decodeF64 = (encoded, dimensions, precisions, workspace = undefined) => resolveWorkspace(workspace).decodeF64(encoded, dimensions, precisions);

module.exports = {
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
    coreVersion: core.coreVersion,
    EncodedGeometryType
};
