import { createWkp } from '../src/index.js';

function parseIntArg(flag, defaultValue) {
    const arg = process.argv.find((v) => v.startsWith(`${flag}=`));
    if (!arg) {
        return defaultValue;
    }
    const value = Number.parseInt(arg.slice(flag.length + 1), 10);
    if (!Number.isFinite(value) || value <= 0) {
        throw new Error(`Invalid ${flag} value: ${arg}`);
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

function measureMs(fn) {
    const start = process.hrtime.bigint();
    const result = fn();
    const end = process.hrtime.bigint();
    const elapsedMs = Number(end - start) / 1_000_000;
    return { elapsedMs, result };
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

const points = parseIntArg('--points', 10_000);
const precision = parseIntArg('--precision', 5);
const iterations = parseIntArg('--iterations', 200);

const geometry = makeLineString(points);

const wkp = await createWkp();
const ctx = new wkp.Context();

const warmEncoded = wkp.encode(ctx, geometry, precision);
wkp.decode(ctx, warmEncoded);

const encodeTimes = [];
const decodeTimes = [];
let encodedBytes = warmEncoded.length;

for (let i = 0; i < iterations; i += 1) {
    const encodeRun = measureMs(() => wkp.encode(ctx, geometry, precision));
    encodedBytes = encodeRun.result.length;
    encodeTimes.push(encodeRun.elapsedMs);

    const decodeRun = measureMs(() => wkp.decode(ctx, encodeRun.result));
    decodeTimes.push(decodeRun.elapsedMs);
}

console.log('WKP Web(WASM in Node) benchmark');
console.log(`points=${points} dimensions=2 precision=${precision} iterations=${iterations}`);
console.log(`encodedBytes=${encodedBytes}`);
console.log(`encode mean=${format(mean(encodeTimes))}ms p95=${format(p95(encodeTimes))}ms`);
console.log(`decode mean=${format(mean(decodeTimes))}ms p95=${format(p95(decodeTimes))}ms`);
