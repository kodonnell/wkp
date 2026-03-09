function splitTopLevel(input) {
    const out = [];
    let depth = 0;
    let start = 0;
    for (let i = 0; i < input.length; i += 1) {
        const ch = input[i];
        if (ch === '(') {
            depth += 1;
        } else if (ch === ')') {
            depth -= 1;
        } else if (ch === ',' && depth === 0) {
            out.push(input.slice(start, i).trim());
            start = i + 1;
        }
    }
    out.push(input.slice(start).trim());
    return out.filter((x) => x.length > 0);
}

function parsePosition(raw) {
    const parts = raw.trim().split(/\s+/).map(Number);
    if (parts.length < 2 || parts.some((n) => !Number.isFinite(n))) {
        throw new TypeError(`Invalid coordinate: ${raw}`);
    }
    return parts;
}

function parsePositionList(raw) {
    return splitTopLevel(raw).map(parsePosition);
}

function stripOuterParens(raw) {
    const s = raw.trim();
    if (!s.startsWith('(') || !s.endsWith(')')) {
        throw new TypeError('WKT has invalid parenthesis structure');
    }
    return s.slice(1, -1).trim();
}

export function parseWkt(input) {
    if (typeof input !== 'string' || input.trim().length === 0) {
        throw new TypeError('WKT must be a non-empty string');
    }

    const text = input.trim();
    const idx = text.indexOf('(');
    if (idx <= 0) {
        throw new TypeError('WKT is missing geometry body');
    }

    const type = text.slice(0, idx).trim().toUpperCase();
    const body = stripOuterParens(text.slice(idx));

    if (type === 'POINT') {
        return { type: 'Point', coordinates: parsePosition(body) };
    }
    if (type === 'LINESTRING') {
        return { type: 'LineString', coordinates: parsePositionList(body) };
    }
    if (type === 'POLYGON') {
        return {
            type: 'Polygon',
            coordinates: splitTopLevel(body).map((ring) => parsePositionList(stripOuterParens(ring)))
        };
    }
    if (type === 'MULTIPOINT') {
        const parts = splitTopLevel(body);
        const coordinates = parts.map((part) => {
            const trimmed = part.trim();
            const pointText = trimmed.startsWith('(') ? stripOuterParens(trimmed) : trimmed;
            return parsePosition(pointText);
        });
        return { type: 'MultiPoint', coordinates };
    }
    if (type === 'MULTILINESTRING') {
        return {
            type: 'MultiLineString',
            coordinates: splitTopLevel(body).map((line) => parsePositionList(stripOuterParens(line)))
        };
    }
    if (type === 'MULTIPOLYGON') {
        return {
            type: 'MultiPolygon',
            coordinates: splitTopLevel(body).map((polygon) =>
                splitTopLevel(stripOuterParens(polygon)).map((ring) => parsePositionList(stripOuterParens(ring)))
            )
        };
    }

    throw new TypeError(`Unsupported WKT geometry type: ${type}`);
}

function formatNumber(value, fractionDigits = 12) {
    const safeDigits = Math.max(0, Math.min(100, Number.parseInt(String(fractionDigits), 10) || 0));
    return Number(value).toFixed(safeDigits);
}

function positionToText(position, fractionDigits = 12) {
    return position.map((v) => formatNumber(v, fractionDigits)).join(' ');
}

export function geometryToWkt(geometry, fractionDigits = 12) {
    if (!geometry || typeof geometry !== 'object' || typeof geometry.type !== 'string') {
        throw new TypeError('Invalid geometry object');
    }

    if (geometry.type === 'Point') {
        return `POINT (${positionToText(geometry.coordinates, fractionDigits)})`;
    }
    if (geometry.type === 'LineString') {
        return `LINESTRING (${geometry.coordinates.map((p) => positionToText(p, fractionDigits)).join(', ')})`;
    }
    if (geometry.type === 'Polygon') {
        const rings = geometry.coordinates
            .map((ring) => `(${ring.map((p) => positionToText(p, fractionDigits)).join(', ')})`)
            .join(', ');
        return `POLYGON (${rings})`;
    }
    if (geometry.type === 'MultiPoint') {
        const points = geometry.coordinates
            .map((pt) => `(${positionToText(pt, fractionDigits)})`)
            .join(', ');
        return `MULTIPOINT (${points})`;
    }
    if (geometry.type === 'MultiLineString') {
        const lines = geometry.coordinates
            .map((line) => `(${line.map((p) => positionToText(p, fractionDigits)).join(', ')})`)
            .join(', ');
        return `MULTILINESTRING (${lines})`;
    }
    if (geometry.type === 'MultiPolygon') {
        const polygons = geometry.coordinates
            .map((poly) => `(${poly.map((ring) => `(${ring.map((p) => positionToText(p, fractionDigits)).join(', ')})`).join(', ')})`)
            .join(', ');
        return `MULTIPOLYGON (${polygons})`;
    }

    throw new TypeError(`Unsupported geometry type: ${geometry.type}`);
}
