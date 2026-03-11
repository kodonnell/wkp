const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { Context, encodeFloats, decodeFloats, encode, decode } = require('..');

const CASES_ROOT = path.resolve(__dirname, '..', '..', '..', '..', '..', 'data', 'integration_tests');

function readCases(dir) {
    const files = fs.readdirSync(dir)
        .filter((name) => name.endsWith('.txt'))
        .sort();
    const out = [];
    for (const file of files) {
        const full = path.join(dir, file);
        const lines = fs.readFileSync(full, 'utf8').split(/\r?\n/);
        for (let idx = 0; idx < lines.length; idx += 1) {
            const line = lines[idx].trim();
            if (!line || line.startsWith('#')) {
                continue;
            }
            out.push({ file, lineNo: idx + 1, line });
        }
    }
    return out;
}

function parseTabLine(line, expectedParts) {
    const parts = line.split('\t').map((p) => p.trim());
    assert.equal(parts.length, expectedParts, `expected ${expectedParts} tab-separated fields in: ${line}`);
    return parts;
}

function assertNestedClose(actual, expected, epsilon = 1e-9) {
    if (Array.isArray(expected)) {
        assert.ok(Array.isArray(actual));
        assert.equal(actual.length, expected.length);
        for (let i = 0; i < expected.length; i += 1) {
            assertNestedClose(actual[i], expected[i], epsilon);
        }
        return;
    }
    assert.ok(Math.abs(actual - expected) <= epsilon, `expected ${actual} to be within ${epsilon} of ${expected}`);
}

function parseCoordPair(text) {
    const values = text.trim().split(/\s+/).map(Number);
    assert.equal(values.length, 2, `invalid coordinate pair: ${text}`);
    return values;
}

function parseLineStringCoords(text) {
    return text.split(',').map((piece) => parseCoordPair(piece));
}

function parseWktToGeoJson(wkt) {
    const trimmed = wkt.trim();

    if (trimmed.toUpperCase().startsWith('POINT')) {
        const match = /^POINT\s*\(([^()]+)\)$/i.exec(trimmed);
        assert.ok(match, `unsupported POINT WKT: ${wkt}`);
        return { type: 'Point', coordinates: parseCoordPair(match[1]) };
    }

    if (trimmed.toUpperCase().startsWith('LINESTRING')) {
        const match = /^LINESTRING\s*\(([^()]+)\)$/i.exec(trimmed);
        assert.ok(match, `unsupported LINESTRING WKT: ${wkt}`);
        return { type: 'LineString', coordinates: parseLineStringCoords(match[1]) };
    }

    if (trimmed.toUpperCase().startsWith('MULTILINESTRING')) {
        const match = /^MULTILINESTRING\s*\((.*)\)$/i.exec(trimmed);
        assert.ok(match, `unsupported MULTILINESTRING WKT: ${wkt}`);
        const body = match[1];
        const lines = [];
        const lineRegex = /\(([^()]+)\)/g;
        for (const lineMatch of body.matchAll(lineRegex)) {
            lines.push(parseLineStringCoords(lineMatch[1]));
        }
        assert.ok(lines.length > 0, `invalid MULTILINESTRING WKT: ${wkt}`);
        return { type: 'MultiLineString', coordinates: lines };
    }

    assert.fail(`unsupported WKT type for integration tests: ${wkt}`);
}

test('integration floats encode fixtures (node)', () => {
    const ctx = new Context();
    const cases = readCases(path.join(CASES_ROOT, 'floats', 'encode'));

    for (const item of cases) {
        const [precisionText, rowsText, expected] = parseTabLine(item.line, 3);
        if (expected === 'TODO') {
            continue;
        }

        const precisions = JSON.parse(precisionText);
        const rows = JSON.parse(rowsText);
        const actual = encodeFloats(ctx, rows, precisions).toString('ascii');

        assert.equal(actual, expected, `${item.file}:${item.lineNo}`);
    }
});

test('integration floats decode fixtures (node)', () => {
    const ctx = new Context();
    const cases = readCases(path.join(CASES_ROOT, 'floats', 'decode'));

    for (const item of cases) {
        const [precisionText, encoded, expectedText] = parseTabLine(item.line, 3);
        if (encoded === 'TODO' || expectedText === 'TODO') {
            continue;
        }

        const precisions = JSON.parse(precisionText);
        const expected = JSON.parse(expectedText);
        const actual = decodeFloats(ctx, encoded, precisions);

        assertNestedClose(actual, expected, 1e-12);
    }
});

test('integration geometry encode fixtures (node)', () => {
    const ctx = new Context();
    const cases = readCases(path.join(CASES_ROOT, 'geometry', 'encode'));

    for (const item of cases) {
        const [precisionText, wkt, expected] = parseTabLine(item.line, 3);
        if (expected === 'TODO') {
            continue;
        }

        const precision = Number.parseInt(precisionText, 10);
        const geojson = parseWktToGeoJson(wkt);
        const actual = encode(ctx, geojson, precision).toString('ascii');

        assert.equal(actual, expected, `${item.file}:${item.lineNo}`);
    }
});

test('integration geometry decode fixtures (node)', () => {
    const ctx = new Context();
    const cases = readCases(path.join(CASES_ROOT, 'geometry', 'decode'));

    for (const item of cases) {
        const [encoded, wkt] = parseTabLine(item.line, 2);
        if (encoded === 'TODO' || wkt === 'TODO') {
            continue;
        }

        const expected = parseWktToGeoJson(wkt);
        const decoded = decode(ctx, encoded);

        assert.equal(decoded.version, 1, `${item.file}:${item.lineNo}`);
        assertNestedClose(decoded.geometry.coordinates, expected.coordinates, 1e-9);
        assert.equal(decoded.geometry.type, expected.type);
    }
});
