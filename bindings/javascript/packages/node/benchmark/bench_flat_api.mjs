#!/usr/bin/env node
/**
 * Micro-benchmark for the flat-array API paths (Node).
 *
 * Measures:
 *   encode / decode (GeoJSON round-trip)
 *   decodeFrame     (Float64Array, no GeoJSON)
 *   encodeFloats / decodeFloats   (array-of-arrays)
 *   decodeFloatsArray             (Float64Array, if available)
 *
 * Run before and after the flat-API changes to confirm no regression.
 *
 * Usage:
 *   node scripts/bench_flat_api.mjs [--points=N] [--iterations=N]
 */

import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);

const {
    Context, encode, decode, decodeFrame,
    encodeFloats, decodeFloats,
} = require('..');

// decodeFloatsArray may not exist yet
const { decodeFloatsArray } = require('..');

// --- CLI args ---
function intArg(flag, def) {
    const a = process.argv.find(v => v.startsWith(`${flag}=`));
    return a ? parseInt(a.slice(flag.length + 1), 10) : def;
}

const POINTS = intArg('--points', 10_000);
const ITERS = intArg('--iterations', 500);
const WARMUP = intArg('--warmup', 20);

// --- Timing helpers ---
function meanMs(fn, warmup = WARMUP, n = ITERS) {
    for (let i = 0; i < warmup; i++) fn();
    const t0 = process.hrtime.bigint();
    for (let i = 0; i < n; i++) fn();
    return Number(process.hrtime.bigint() - t0) / n / 1_000_000;
}

function row(label, ms, note = '') {
    const suffix = note ? `  (${note})` : '';
    console.log(`  ${label.padEnd(36)} ${ms.toFixed(3)} ms${suffix}`);
}

function header(title) {
    console.log(`\n${title}`);
    console.log('-'.repeat(title.length));
}

// --- Build test data ---
const coords2d = Array.from({ length: POINTS }, (_, i) => [i * 0.001, Math.sin(i * 0.001) * 90]);
const geometry = { type: 'LineString', coordinates: coords2d };

const ctx = new Context();
const encGeom = encode(geometry, 5, ctx);
const encFloats = encodeFloats(coords2d, [5, 5], ctx);

const { version: pkgVersion } = require('../package.json');
console.log(`@wkpjs/node ${pkgVersion}  |  points=${POINTS}  iterations=${ITERS}  warmup=${WARMUP}`);
console.log(`  encoded geometry size: ${encGeom.length.toLocaleString()} bytes`);
console.log(`  encoded floats size:   ${encFloats.length.toLocaleString()} bytes`);

// --- Geometry paths ---
header('Geometry encode / decode');
row('encode (->string)', meanMs(() => encode(geometry, 5, ctx)));
row('decode (->GeoJSON)', meanMs(() => decode(encGeom, ctx)));
row('decodeFrame (->Float64Array)', meanMs(() => decodeFrame(encGeom, ctx)));

// --- Float paths ---
header('Float encode / decode');
row('encodeFloats (array-of-arrays input)', meanMs(() => encodeFloats(coords2d, [5, 5], ctx)));
row('decodeFloats (->array-of-arrays)', meanMs(() => decodeFloats(encFloats, [5, 5], ctx)));

if (typeof decodeFloatsArray === 'function') {
    row('decodeFloatsArray (->Float64Array)', meanMs(() => decodeFloatsArray(encFloats, [5, 5], ctx)));
} else {
    row('decodeFloatsArray (->Float64Array)', 0, 'NOT YET IMPLEMENTED');
}

// --- Overhead: flat Float64Array → array-of-arrays ---
const rawFlat = typeof decodeFloatsArray === 'function'
    ? decodeFloatsArray(encFloats, [5, 5], ctx)
    : (() => { const r = decodeFloats(encFloats, [5, 5], ctx); return new Float64Array(r.flat()); })();

header('Overhead: flat Float64Array -> array-of-arrays');
const dims = 2;
row('slice loop (flat -> rows)', meanMs(() => {
    const rows = [];
    for (let i = 0; i < rawFlat.length; i += dims) {
        rows.push(Array.from(rawFlat.slice(i, i + dims)));
    }
    return rows;
}), `length=${rawFlat.length}`);
