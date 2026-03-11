import { createWkp } from '../src/index.js';

function parseIntParam(name, defaultValue) {
    const params = new URLSearchParams(window.location.search);
    const raw = params.get(name);
    if (!raw) {
        return defaultValue;
    }
    const value = Number.parseInt(raw, 10);
    if (!Number.isFinite(value) || value <= 0) {
        throw new Error(`Invalid query param ${name}=${raw}`);
    }
    return value;
}

function makeLineString(pointCount) {
    const coordinates = new Array(pointCount);
    for (let i = 0; i < pointCount; i += 1) {
        coordinates[i] = [
            i * 0.00123,
            Math.sin(i * 0.0007) * 90.0
        ];
    }
    return {
        type: 'LineString',
        coordinates
    };
}

function mean(values) {
    return values.reduce((a, b) => a + b, 0) / values.length;
}

function p95(values) {
    const sorted = [...values].sort((a, b) => a - b);
    const index = Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95));
    return sorted[index];
}

function format(num) {
    return num.toFixed(3);
}

function runTimed(fn) {
    const start = performance.now();
    const result = fn();
    const end = performance.now();
    return { elapsedMs: end - start, result };
}

function log(line) {
    const pre = document.getElementById('log');
    pre.textContent += `${line}\n`;
    console.log(line);
}

async function run() {
    const points = parseIntParam('points', 10_000);
    const precision = parseIntParam('precision', 5);
    const iterations = parseIntParam('iterations', 200);

    const geometry = makeLineString(points);

    log('Loading WASM module...');
    const wkp = await createWkp();
    const ctx = new wkp.Context();
    log('WASM module loaded.');

    const warmEncoded = wkp.encode(ctx, geometry, precision);
    wkp.decode(ctx, warmEncoded);

    const encodeTimes = [];
    const decodeTimes = [];
    let encodedBytes = warmEncoded.length;

    for (let i = 0; i < iterations; i += 1) {
        const encodeRun = runTimed(() => wkp.encode(ctx, geometry, precision));
        encodedBytes = encodeRun.result.length;
        encodeTimes.push(encodeRun.elapsedMs);

        const decodeRun = runTimed(() => wkp.decode(ctx, encodeRun.result));
        decodeTimes.push(decodeRun.elapsedMs);

        if ((i + 1) % Math.max(1, Math.floor(iterations / 10)) === 0) {
            log(`progress ${i + 1}/${iterations}`);
            await new Promise((r) => setTimeout(r, 0));
        }
    }

    log('');
    log('WKP Web benchmark');
    log(`points=${points} dimensions=2 precision=${precision} iterations=${iterations}`);
    log(`encodedBytes=${encodedBytes}`);
    log(`encode mean=${format(mean(encodeTimes))}ms p95=${format(p95(encodeTimes))}ms`);
    log(`decode mean=${format(mean(decodeTimes))}ms p95=${format(p95(decodeTimes))}ms`);
}

run().catch((error) => {
    log(`ERROR: ${error?.stack || error?.message || String(error)}`);
});
