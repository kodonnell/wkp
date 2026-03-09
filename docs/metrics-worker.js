import { createWkp } from './web/src/index.js';
import { parseWkt, geometryToWkt } from './wkt.js';

let wkpPromise = null;
let workspace = null;
const MAX_ITERATIONS = 1000000;
const MAX_DURATION_MS = 1000;
const PROGRESS_INTERVAL_MS = 100;

function ensureWkp() {
    if (!wkpPromise) {
        wkpPromise = createWkp();
    }
    return wkpPromise;
}

function mean(values) {
    return values.reduce((a, b) => a + b, 0) / values.length;
}

function stddev(values) {
    const m = mean(values);
    const variance = values.reduce((acc, v) => acc + ((v - m) * (v - m)), 0) / values.length;
    return Math.sqrt(variance);
}

function runTimedLoop(task, onFirst, onProgress) {
    const times = [];
    const started = self.performance.now();
    let iterations = 0;
    let lastProgressEmit = started;

    const currentStats = () => {
        const totalMs = self.performance.now() - started;
        const meanMs = times.length > 0 ? mean(times) : 0;
        const stddevMs = times.length > 1 ? stddev(times) : 0;
        return {
            iterations,
            totalMs,
            meanMs,
            stddevMs,
            progress: Math.min(1, totalMs / MAX_DURATION_MS)
        };
    };

    while (iterations < MAX_ITERATIONS) {
        const t0 = self.performance.now();
        task();
        const elapsed = self.performance.now() - t0;
        times.push(elapsed);
        iterations += 1;

        if (iterations === 1) {
            onFirst(elapsed, currentStats());
        }

        const now = self.performance.now();
        const elapsedMilliseconds = now - started;
        if (now - lastProgressEmit >= PROGRESS_INTERVAL_MS) {
            lastProgressEmit = now;
            onProgress(currentStats());
        }

        if (elapsedMilliseconds >= MAX_DURATION_MS) {
            break;
        }
    }

    const totalMs = self.performance.now() - started;
    return {
        iterations,
        totalMs,
        meanMs: mean(times),
        stddevMs: stddev(times)
    };
}

self.onmessage = async (event) => {
    const msg = event.data;
    if (!msg || (msg.type !== 'encode' && msg.type !== 'decode')) {
        return;
    }

    try {
        const wkp = await ensureWkp();
        if (!workspace) {
            workspace = new wkp.Workspace();
        }

        if (msg.type === 'encode') {
            const geometry = parseWkt(msg.wkt);
            const precision = msg.precision;

            const stats = runTimedLoop(
                () => {
                    const fn = {
                        Point: wkp.encodePoint,
                        LineString: wkp.encodeLineString,
                        Polygon: wkp.encodePolygon,
                        MultiPoint: wkp.encodeMultiPoint,
                        MultiLineString: wkp.encodeMultiLineString,
                        MultiPolygon: wkp.encodeMultiPolygon
                    }[geometry.type];
                    fn(geometry, precision, workspace);
                },
                (firstMs, progressStats) => {
                    self.postMessage({ phase: 'first', kind: 'encode', firstMs, ...progressStats });
                },
                (progressStats) => {
                    self.postMessage({ phase: 'progress', kind: 'encode', ...progressStats });
                }
            );

            self.postMessage({ phase: 'done', kind: 'encode', stats });
            return;
        }

        const encoded = msg.encoded;
        const stats = runTimedLoop(
            () => {
                const decoded = wkp.decode(encoded, workspace);
                geometryToWkt(decoded.geometry);
            },
            (firstMs, progressStats) => {
                self.postMessage({ phase: 'first', kind: 'decode', firstMs, ...progressStats });
            },
            (progressStats) => {
                self.postMessage({ phase: 'progress', kind: 'decode', ...progressStats });
            }
        );

        self.postMessage({ phase: 'done', kind: 'decode', stats });
    } catch (error) {
        self.postMessage({
            phase: 'error',
            kind: msg.type,
            message: error?.stack || error?.message || String(error)
        });
    }
};
