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
        segmentPointCounts,
        groupSegmentCounts
    };
}

function decodeHeader(encodedValue) {
    const encoded = normalizeEncodedBytes(encodedValue);
    const header = core.decodeGeometryHeader(encoded);
    return [header[0], header[1], header[2], header[3]];
}

function decode(ctx, encodedValue) {
    asContext(ctx);
    return core.decodeGeometryFrame(normalizeEncodedBytes(encodedValue));
}

function encode(ctx, geometry, precision) {
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

function encodeFloats(ctx, floats, precisions) {
    asContext(ctx);
    const precisionList = normalizePrecisions(precisions);
    const dimensions = precisionList.length;
    const flatValues = normalizeFloatRows(floats, dimensions);
    return core.encodeF64(flatValues, dimensions, precisionList);
}

function decodeFloats(ctx, encodedValue, precisions) {
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
    decodeHeader,
    decode,
    encode,
    encodeFloats,
    decodeFloats,
    runSelfTest: core.runSelfTest,
    coreVersion: core.coreVersion,
    EncodedGeometryType
};
