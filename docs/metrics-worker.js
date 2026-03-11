import { parseWkt } from './wkt.js';

let wkpPromise = null;
let ctx = null;
let resolvedWebVersion = null;
const WEB_LOCAL_MODULE_CANDIDATE = '../bindings/javascript/packages/web/src/index.js';
const WEB_PAGES_UNVERSIONED_MODULE_CANDIDATE = './web/src/index.js';
const WEB_PACKAGE_CANDIDATES = [
    '../bindings/javascript/packages/web/package.json',
    './web/package.json'
];
const MAX_ITERATIONS = 1000000;
const MAX_DURATION_MS = 1000;
const PROGRESS_INTERVAL_MS = 100;

async function importFirstAvailable(specifiers) {
    let lastError = null;
    for (const specifier of specifiers) {
        try {
            const url = new URL(specifier, import.meta.url);
            const probe = await fetch(url, { method: 'HEAD', cache: 'no-store' });
            if (!probe.ok) {
                continue;
            }
            return await import(specifier);
        } catch (error) {
            lastError = error;
        }
    }
    throw lastError || new Error('Unable to load WKP web module');
}

async function resolveWebBindingVersion() {
    if (resolvedWebVersion) {
        return resolvedWebVersion;
    }
    for (const candidate of WEB_PACKAGE_CANDIDATES) {
        try {
            const packageUrl = new URL(candidate, import.meta.url);
            const response = await fetch(packageUrl, { cache: 'no-store' });
            if (!response.ok) {
                continue;
            }
            const pkg = await response.json();
            if (typeof pkg.version === 'string' && pkg.version.trim().length > 0) {
                resolvedWebVersion = pkg.version.trim();
                return resolvedWebVersion;
            }
        } catch {
            // Try next candidate.
        }
    }
    return null;
}

async function resolveWebModuleCandidates() {
    const candidates = [WEB_LOCAL_MODULE_CANDIDATE];
    const version = await resolveWebBindingVersion();
    if (version) {
        candidates.push(`./web/${version}/src/index.js`);
    }
    candidates.push(WEB_PAGES_UNVERSIONED_MODULE_CANDIDATE);
    return candidates;
}

function ensureWkp() {
    if (!wkpPromise) {
        wkpPromise = resolveWebModuleCandidates().then((candidates) => importFirstAvailable(candidates)).then((mod) => {
            if (!mod || typeof mod.createWkp !== 'function') {
                throw new Error('WKP web module did not export createWkp()');
            }
            return mod.createWkp();
        });
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
        if (!ctx) {
            ctx = new wkp.Context();
        }

        if (msg.type === 'encode') {
            const geometry = parseWkt(msg.wkt);
            const precision = msg.precision;

            const stats = runTimedLoop(
                () => {
                    wkp.encode(ctx, geometry, precision);
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
                wkp.decode(ctx, encoded);
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
