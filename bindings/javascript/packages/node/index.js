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

function pad2(value, name) {
    if (!Number.isInteger(value) || value < 0 || value > 99) {
        throw new TypeError(`${name} must be an integer in [0, 99]`);
    }
    return String(value).padStart(2, '0');
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

function encodeBody(geometry, dimensions, precision, encodeRows) {
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

class GeometryEncoder {
    constructor(precision, dimensions, initialCapacity = 4096) {
        if (!Number.isInteger(dimensions) || dimensions <= 0 || dimensions > 16) {
            throw new TypeError('dimensions must be between 1 and 16');
        }
        if (!Number.isInteger(precision) || precision < 0 || precision > 99) {
            throw new TypeError('precision must be an integer in [0, 99]');
        }
        if (!Number.isInteger(initialCapacity) || initialCapacity <= 0) {
            throw new TypeError('initialCapacity must be a positive integer');
        }

        this.precision = precision;
        this.dimensions = dimensions;
        this.initialCapacity = initialCapacity;
        this._precisions = [this.precision];
        this._valueScratch = {
            values: new Float64Array(Math.max(1, Math.ceil(this.initialCapacity / Float64Array.BYTES_PER_ELEMENT)))
        };
    }

    _encodeRows(rows, label) {
        const values = flattenCoordRowsInto(rows, this.dimensions, label, this._valueScratch);
        return decoder.decode(core.encodeF64(values, this.dimensions, this._precisions));
    }

    encodeBytes(geometry) {
        const [geometryType, body] = encodeBody(
            geometry,
            this.dimensions,
            this.precision,
            (rows, label) => this._encodeRows(rows, label)
        );
        const header = `${pad2(1, 'version')}${pad2(this.precision, 'precision')}${pad2(this.dimensions, 'dimensions')}${pad2(geometryType, 'geometry type')}`;
        return Buffer.from(header + body, 'ascii');
    }

    encode(geometry) {
        return this.encodeBytes(geometry).toString('ascii');
    }

    encodeStr(geometry) {
        return this.encode(geometry);
    }

    decodeBytes(encodedValue) {
        const ascii = toAscii(encodedValue);
        const [version, precision, dimensions, geometryType] = GeometryEncoder.decodeHeader(ascii);
        if (precision !== this.precision || dimensions !== this.dimensions) {
            throw new TypeError(
                `Encoded geometry has precision=${precision}, dimensions=${dimensions}, but this encoder is precision=${this.precision}, dimensions=${this.dimensions}`
            );
        }
        return {
            version,
            precision,
            dimensions,
            geometry: decodeBody(ascii.slice(8), geometryType, dimensions, precision)
        };
    }

    decodeStr(encoded) {
        return this.decodeBytes(encoded);
    }

    static decodeHeader(encodedValue) {
        return decodeHeaderInternal(encodedValue);
    }

    static decode(encodedValue) {
        const [version, precision, dimensions] = GeometryEncoder.decodeHeader(encodedValue);
        const decoderInstance = new GeometryEncoder(precision, dimensions);
        const decoded = decoderInstance.decodeBytes(encodedValue);
        if (decoded.version !== version) {
            throw new TypeError('Unexpected decoded version mismatch');
        }
        return decoded;
    }
}

function decodeGeometryHeader(encodedValue) {
    return GeometryEncoder.decodeHeader(encodedValue);
}

module.exports = {
    bindingVersion: BINDING_VERSION,
    coreCompatibility: CORE_COMPATIBILITY,
    encodeF64: core.encodeF64,
    decodeF64: core.decodeF64,
    coreVersion: core.coreVersion,
    EncodedGeometryType,
    GeometryEncoder,
    decodeGeometryHeader
};
