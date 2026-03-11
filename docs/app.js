import { parseWkt, geometryToWkt } from './wkt.js';

const $ = (id) => document.getElementById(id);
const te = new TextEncoder();

const ui = {
    loadFailureBanner: $('loadFailureBanner'),
    exampleGeometry: $('exampleGeometry'),
    generateExampleBtn: $('generateExampleBtn'),
    settingsReminder: $('settingsReminder'),
    settingsRows: Array.from(document.querySelectorAll('#exampleSettings [data-show-for]')),
    settingDimensions: $('settingDimensions'),
    settingDp: $('settingDp'),
    settingSort: $('settingSort'),
    settingPointCount: $('settingPointCount'),
    settingRingCount: $('settingRingCount'),
    settingPointsPerRing: $('settingPointsPerRing'),
    settingLineCount: $('settingLineCount'),
    settingPointsPerLine: $('settingPointsPerLine'),
    settingPolygonCount: $('settingPolygonCount'),
    settingRingsPerPolygon: $('settingRingsPerPolygon'),
    settingMpPointsPerRing: $('settingMpPointsPerRing'),

    wktInput: $('wktInput'),
    precisionInput: $('precisionInput'),
    encodeBtn: $('encodeBtn'),
    encodedOutputText: $('encodedOutputText'),
    copyEncodedBtn: $('copyEncodedBtn'),
    encodeMetrics: $('encodeMetrics'),
    encodeStatus: $('encodeStatus'),
    encodeProgressWrap: $('encodeProgressWrap'),
    encodeProgressFill: $('encodeProgressFill'),
    encodeProgressText: $('encodeProgressText'),

    encodedInput: $('encodedInput'),
    useEncodedBtn: $('useEncodedBtn'),
    decodeBtn: $('decodeBtn'),
    wktOutputText: $('wktOutputText'),
    copyWktBtn: $('copyWktBtn'),
    decodeMetrics: $('decodeMetrics'),
    decodeStatus: $('decodeStatus'),
    decodeProgressWrap: $('decodeProgressWrap'),
    decodeProgressFill: $('decodeProgressFill'),
    decodeProgressText: $('decodeProgressText'),

    decodeHeaderMetrics: $('decodeHeaderMetrics'),
    webBindingVersion: $('webBindingVersion')
};

const WEB_MODULE_CANDIDATES = [
    '../bindings/javascript/packages/web/src/index.js',
    './web/src/index.js'
];

const WEB_PACKAGE_CANDIDATES = [
    '../bindings/javascript/packages/web/package.json',
    './web/package.json'
];

let wkp = null;
let ctx = null;
let worker = null;
let appReady = false;
let settingsDirty = false;

const baseMetrics = {
    encode: [],
    decode: []
};

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

function setControlsEnabled(enabled) {
    const controls = document.querySelectorAll('button, input, select, textarea');
    for (const control of controls) {
        control.disabled = !enabled;
    }
    const cards = document.querySelectorAll('.card');
    for (const card of cards) {
        card.classList.toggle('degraded', !enabled);
    }
}

function enterDegradedMode(message) {
    appReady = false;
    setControlsEnabled(false);
    if (ui.loadFailureBanner) {
        ui.loadFailureBanner.textContent = message;
        ui.loadFailureBanner.classList.remove('hidden');
    }
}

function clearDegradedMode() {
    if (ui.loadFailureBanner) {
        ui.loadFailureBanner.textContent = '';
        ui.loadFailureBanner.classList.add('hidden');
    }
    setControlsEnabled(true);
}

function clampInt(value, min, max, fallback) {
    const n = Number.parseInt(String(value), 10);
    if (!Number.isInteger(n)) {
        return fallback;
    }
    return Math.min(max, Math.max(min, n));
}

function randomInRange(min, max) {
    return min + (Math.random() * (max - min));
}

function roundDp(value, dp) {
    const f = 10 ** dp;
    return Math.round(value * f) / f;
}

function randomPosition(dimensions, dp, baseX = null, baseY = null) {
    const x = baseX == null ? randomInRange(-180, 180) : baseX;
    const y = baseY == null ? randomInRange(-85, 85) : baseY;
    const out = [roundDp(x, dp), roundDp(y, dp)];
    for (let i = 2; i < dimensions; i += 1) {
        out.push(roundDp(randomInRange(-1000, 1000), dp));
    }
    return out;
}

function randomWalkLine(pointCount, dimensions, dp, startX = null, startY = null) {
    const line = [];
    let x = startX == null ? randomInRange(-170, 170) : startX;
    let y = startY == null ? randomInRange(-80, 80) : startY;
    for (let i = 0; i < pointCount; i += 1) {
        if (i > 0) {
            x += randomInRange(-0.4, 0.4);
            y += randomInRange(-0.4, 0.4);
        }
        line.push(randomPosition(dimensions, dp, x, y));
    }
    return line;
}

function randomRing(pointCount, dimensions, dp, cx = null, cy = null, radius = null) {
    const centerX = cx == null ? randomInRange(-150, 150) : cx;
    const centerY = cy == null ? randomInRange(-70, 70) : cy;
    const r = radius == null ? randomInRange(0.2, 3.5) : radius;
    const ring = [];
    const safeCount = Math.max(4, pointCount);
    for (let i = 0; i < safeCount - 1; i += 1) {
        const t = (i / (safeCount - 1)) * Math.PI * 2;
        const wobble = 1 + (Math.sin(i * 1.7) * 0.08);
        const x = centerX + (Math.cos(t) * r * wobble);
        const y = centerY + (Math.sin(t) * r * wobble);
        ring.push(randomPosition(dimensions, dp, x, y));
    }
    ring.push([...ring[0]]);
    return ring;
}

function generateGeometry(type, settings) {
    const {
        dimensions,
        dp,
        pointCount,
        ringCount,
        pointsPerRing,
        lineCount,
        pointsPerLine,
        polygonCount,
        ringsPerPolygon,
        mpPointsPerRing
    } = settings;

    if (type === 'Point') {
        return { type: 'Point', coordinates: randomPosition(dimensions, dp) };
    }
    if (type === 'LineString') {
        return { type: 'LineString', coordinates: randomWalkLine(pointCount, dimensions, dp) };
    }
    if (type === 'Polygon') {
        const coordinates = [];
        for (let i = 0; i < ringCount; i += 1) {
            coordinates.push(randomRing(pointsPerRing, dimensions, dp));
        }
        return { type: 'Polygon', coordinates };
    }
    if (type === 'MultiPoint') {
        const coordinates = new Array(pointCount);
        for (let i = 0; i < pointCount; i += 1) {
            coordinates[i] = randomPosition(dimensions, dp);
        }
        return { type: 'MultiPoint', coordinates };
    }
    if (type === 'MultiLineString') {
        const coordinates = new Array(lineCount);
        for (let i = 0; i < lineCount; i += 1) {
            coordinates[i] = randomWalkLine(pointsPerLine, dimensions, dp);
        }
        return { type: 'MultiLineString', coordinates };
    }
    if (type === 'MultiPolygon') {
        const coordinates = new Array(polygonCount);
        for (let i = 0; i < polygonCount; i += 1) {
            const poly = new Array(ringsPerPolygon);
            for (let r = 0; r < ringsPerPolygon; r += 1) {
                poly[r] = randomRing(mpPointsPerRing, dimensions, dp);
            }
            coordinates[i] = poly;
        }
        return { type: 'MultiPolygon', coordinates };
    }
    throw new TypeError(`Unsupported example geometry type: ${type}`);
}

function sortGeometryCoordinates(geometry) {
    const cmp = (a, b) => {
        const len = Math.max(a.length, b.length);
        for (let i = 0; i < len; i += 1) {
            const av = a[i] ?? 0;
            const bv = b[i] ?? 0;
            if (av !== bv) {
                return av - bv;
            }
        }
        return 0;
    };

    const sortRows = (rows) => rows.slice().sort(cmp);

    if (geometry.type === 'Point') {
        return geometry;
    }
    if (geometry.type === 'LineString' || geometry.type === 'MultiPoint') {
        return { ...geometry, coordinates: sortRows(geometry.coordinates) };
    }
    if (geometry.type === 'Polygon' || geometry.type === 'MultiLineString') {
        return {
            ...geometry,
            coordinates: geometry.coordinates.map((part) => sortRows(part))
        };
    }
    if (geometry.type === 'MultiPolygon') {
        return {
            ...geometry,
            coordinates: geometry.coordinates.map((poly) => poly.map((ring) => sortRows(ring)))
        };
    }
    return geometry;
}

function updateSettingsVisibility() {
    const selected = ui.exampleGeometry.value;
    for (const row of ui.settingsRows) {
        const showFor = String(row.dataset.showFor || '')
            .split(',')
            .map((x) => x.trim())
            .filter((x) => x.length > 0);
        row.classList.toggle('hidden', !showFor.includes(selected));
    }
}

function currentExampleSettings() {
    return {
        dimensions: clampInt(ui.settingDimensions.value, 2, 6, 2),
        dp: clampInt(ui.settingDp.value, 0, 50, 5),
        sort: ui.settingSort.checked,
        pointCount: clampInt(ui.settingPointCount.value, 2, 20000, 100),
        ringCount: clampInt(ui.settingRingCount.value, 1, 8, 1),
        pointsPerRing: clampInt(ui.settingPointsPerRing.value, 4, 5000, 60),
        lineCount: clampInt(ui.settingLineCount.value, 1, 200, 3),
        pointsPerLine: clampInt(ui.settingPointsPerLine.value, 2, 5000, 50),
        polygonCount: clampInt(ui.settingPolygonCount.value, 1, 100, 2),
        ringsPerPolygon: clampInt(ui.settingRingsPerPolygon.value, 1, 8, 1),
        mpPointsPerRing: clampInt(ui.settingMpPointsPerRing.value, 4, 5000, 40)
    };
}

function generateExampleWkt() {
    const settings = currentExampleSettings();
    let geometry = generateGeometry(ui.exampleGeometry.value, settings);
    if (settings.sort) {
        geometry = sortGeometryCoordinates(geometry);
    }
    ui.wktInput.value = geometryToWkt(geometry, settings.dp);
    updateActionButtons();
}

function setSettingsDirty(value) {
    settingsDirty = Boolean(value);
    if (ui.settingsReminder) {
        ui.settingsReminder.classList.toggle('is-visible', settingsDirty);
    }
    if (ui.generateExampleBtn) {
        ui.generateExampleBtn.classList.toggle('button-attention', settingsDirty);
    }
}

function fmt(ms) {
    return `${ms.toFixed(3)} ms`;
}

function renderMetrics(container, rows) {
    container.innerHTML = rows
        .map((row) => `<div class="metric"><div class="k">${row.k}</div><div class="v">${row.v}</div></div>`)
        .join('');
}

function renderInfoInline(container, rows) {
    container.innerHTML = rows
        .map((row) => `<div class="info-chip"><span class="k">${row.k}</span><span class="v">${row.v}</span></div>`)
        .join('');
}

function setError(el, text) {
    el.className = 'status warn';
    el.textContent = text;
}

function clearError(el) {
    el.className = 'status';
    el.textContent = '';
}

function setWebBindingVersion(text) {
    if (ui.webBindingVersion) {
        ui.webBindingVersion.textContent = text;
    }
}

async function loadWebBindingVersion() {
    for (const candidate of WEB_PACKAGE_CANDIDATES) {
        try {
            const packageUrl = new URL(candidate, import.meta.url);
            const response = await fetch(packageUrl, { cache: 'no-store' });
            if (!response.ok) {
                continue;
            }
            const pkg = await response.json();
            if (typeof pkg.version === 'string' && pkg.version.trim().length > 0) {
                setWebBindingVersion(pkg.version.trim());
                return;
            }
        } catch {
            // Try next candidate.
        }
    }
    setWebBindingVersion('unknown');
}

function geometryTypeName(typeId) {
    const mapping = {
        [wkp.EncodedGeometryType.POINT]: 'POINT',
        [wkp.EncodedGeometryType.LINESTRING]: 'LINESTRING',
        [wkp.EncodedGeometryType.POLYGON]: 'POLYGON',
        [wkp.EncodedGeometryType.MULTIPOINT]: 'MULTIPOINT',
        [wkp.EncodedGeometryType.MULTILINESTRING]: 'MULTILINESTRING',
        [wkp.EncodedGeometryType.MULTIPOLYGON]: 'MULTIPOLYGON'
    };
    return mapping[typeId] || `UNKNOWN (${typeId})`;
}

function encodeGeometry(geometry, precision) {
    return wkp.encode(ctx, geometry, precision);
}

function updateHeader(encoded) {
    const [version, precision, dimensions, geometryType] = wkp.decodeHeader(encoded);
    renderInfoInline(ui.decodeHeaderMetrics, [
        { k: 'Version', v: String(version) },
        { k: 'Precision', v: String(precision) },
        { k: 'Dims', v: String(dimensions) },
        { k: 'Type', v: geometryTypeName(geometryType) }
    ]);
}

function updateActionButtons() {
    const wktHasInput = ui.wktInput.value.trim().length > 0;
    const encodedHasInput = ui.encodedInput.value.trim().length > 0;
    ui.encodeBtn.disabled = !appReady || !wktHasInput;
    ui.decodeBtn.disabled = !appReady || !encodedHasInput;
}

function progressElements(kind) {
    if (kind === 'encode') {
        return {
            wrap: ui.encodeProgressWrap,
            fill: ui.encodeProgressFill,
            text: ui.encodeProgressText,
            metrics: ui.encodeMetrics
        };
    }
    return {
        wrap: ui.decodeProgressWrap,
        fill: ui.decodeProgressFill,
        text: ui.decodeProgressText,
        metrics: ui.decodeMetrics
    };
}

function initProgress(kind) {
    const t = progressElements(kind);
    t.wrap.classList.add('is-hidden');
    t.fill.style.width = '0%';
    t.text.textContent = '';
}

function startProgress(kind) {
    const t = progressElements(kind);
    t.wrap.classList.remove('is-hidden');
    t.fill.style.width = '0%';
    t.text.textContent = 'Calculating performance metrics...';
}

function updateProgress(kind, progress) {
    const t = progressElements(kind);
    const pct = Math.max(0, Math.min(100, Math.round((Number(progress || 0)) * 100)));
    t.fill.style.width = `${pct}%`;
}

function renderCombinedMetrics(kind, timing = null) {
    const t = progressElements(kind);
    const rows = [...baseMetrics[kind]];
    if (timing) {
        rows.push({ k: 'Iterations', v: String(timing.iterations) });
        rows.push({ k: 'Elapsed', v: fmt(timing.totalMs) });
        rows.push({ k: 'Mean', v: fmt(timing.meanMs) });
        rows.push({ k: 'Stddev', v: fmt(timing.stddevMs) });
    }
    renderMetrics(t.metrics, rows);
}

function postWorker(msg) {
    if (!worker) {
        worker = new Worker(new URL('./metrics-worker.js', import.meta.url), { type: 'module' });
        worker.onerror = (event) => {
            const detail = event?.message || 'Failed to load metrics worker';
            setError(ui.encodeStatus, detail);
            setError(ui.decodeStatus, detail);
        };
        worker.onmessageerror = () => {
            setError(ui.encodeStatus, 'Metrics worker message could not be parsed.');
            setError(ui.decodeStatus, 'Metrics worker message could not be parsed.');
        };
        worker.onmessage = (event) => {
            const data = event.data;
            if (!data) {
                return;
            }

            if (data.phase === 'error') {
                if (data.kind === 'encode') {
                    setError(ui.encodeStatus, data.message);
                } else if (data.kind === 'decode') {
                    setError(ui.decodeStatus, data.message);
                }
                return;
            }

            if (data.phase === 'first' || data.phase === 'progress') {
                updateProgress(data.kind, data.progress);
                renderCombinedMetrics(data.kind, {
                    iterations: data.iterations,
                    totalMs: data.totalMs,
                    meanMs: data.meanMs,
                    stddevMs: data.stddevMs
                });
                return;
            }

            if (data.phase === 'done') {
                const s = data.stats;
                updateProgress(data.kind, 1);
                progressElements(data.kind).text.textContent = '';
                renderCombinedMetrics(data.kind, {
                    iterations: s.iterations,
                    totalMs: s.totalMs,
                    meanMs: s.meanMs,
                    stddevMs: s.stddevMs
                });
            }
        };
    }

    startProgress(msg.type);
    worker.postMessage(msg);
}

ui.exampleGeometry.addEventListener('change', () => {
    updateSettingsVisibility();
    setSettingsDirty(true);
});

[
    ui.settingDimensions,
    ui.settingDp,
    ui.settingSort,
    ui.settingPointCount,
    ui.settingRingCount,
    ui.settingPointsPerRing,
    ui.settingLineCount,
    ui.settingPointsPerLine,
    ui.settingPolygonCount,
    ui.settingRingsPerPolygon,
    ui.settingMpPointsPerRing
].forEach((input) => {
    if (!input) {
        return;
    }
    input.addEventListener('input', () => setSettingsDirty(true));
    input.addEventListener('change', () => setSettingsDirty(true));
});

ui.wktInput.addEventListener('input', updateActionButtons);
ui.encodedInput.addEventListener('input', updateActionButtons);

ui.generateExampleBtn.addEventListener('click', () => {
    try {
        clearError(ui.encodeStatus);
        generateExampleWkt();
        setSettingsDirty(false);
    } catch (error) {
        setError(ui.encodeStatus, error?.message || String(error));
    }
});

async function init() {
    updateSettingsVisibility();
    await loadWebBindingVersion();

    if (window.location.protocol === 'file:') {
        enterDegradedMode('Oops: this demo must be served over HTTP(S), not file://. Use scripts/run_docs_demo.py from the repo root.');
        setError(ui.encodeStatus, 'Run this page from HTTP(S), not file://');
        setError(ui.decodeStatus, 'Run this page from HTTP(S), not file://');
        return;
    }

    try {
        const webModule = await importFirstAvailable(WEB_MODULE_CANDIDATES);
        if (!webModule || typeof webModule.createWkp !== 'function') {
            throw new Error('WKP web module loaded but did not export createWkp()');
        }
        wkp = await webModule.createWkp();
        ctx = new wkp.Context();
    } catch (error) {
        const detail = error?.message || String(error);
        enterDegradedMode(
            `Oops: WKP/WASM failed to load (${detail}). Run scripts/run_docs_demo.py from the repo root, then open /docs/.`
        );
        setError(ui.encodeStatus, detail);
        setError(ui.decodeStatus, detail);
        return;
    }

    appReady = true;
    clearDegradedMode();

    ui.wktInput.value = '';
    ui.encodedInput.value = '';
    ui.encodedOutputText.textContent = '';
    ui.wktOutputText.textContent = '';

    clearError(ui.encodeStatus);
    clearError(ui.decodeStatus);

    baseMetrics.encode = [];
    baseMetrics.decode = [];
    renderCombinedMetrics('encode');
    renderCombinedMetrics('decode');
    renderInfoInline(ui.decodeHeaderMetrics, []);

    updateActionButtons();
    setSettingsDirty(false);

    initProgress('encode');
    initProgress('decode');
}

ui.encodeBtn.addEventListener('click', () => {
    try {
        clearError(ui.encodeStatus);
        const precision = Number.parseInt(ui.precisionInput.value, 10);
        if (!Number.isInteger(precision) || precision < 0 || precision > 15) {
            throw new TypeError('Precision must be an integer in [0, 15]');
        }

        const geometry = parseWkt(ui.wktInput.value);
        const encoded = encodeGeometry(geometry, precision);
        ui.encodedOutputText.textContent = encoded;

        const wktBytes = te.encode(ui.wktInput.value).length;
        const wkpBytes = te.encode(encoded).length;
        const ratio = wkpBytes > 0 ? (wktBytes / wkpBytes) : 0;

        baseMetrics.encode = [
            { k: 'WKT bytes', v: String(wktBytes) },
            { k: 'WKP bytes', v: String(wkpBytes) },
            { k: 'Compression (higher is better)', v: `${ratio.toFixed(2)}x` }
        ];

        renderCombinedMetrics('encode');
        postWorker({ type: 'encode', wkt: ui.wktInput.value, precision });
    } catch (error) {
        setError(ui.encodeStatus, error?.message || String(error));
    }
});

ui.useEncodedBtn.addEventListener('click', () => {
    const text = ui.encodedOutputText.textContent || '';
    if (!text) {
        setError(ui.decodeStatus, 'No encoded output is available yet.');
        return;
    }
    clearError(ui.decodeStatus);
    ui.encodedInput.value = text;
    updateActionButtons();
});

ui.decodeBtn.addEventListener('click', () => {
    try {
        clearError(ui.decodeStatus);
        const encoded = ui.encodedInput.value.trim();
        if (!encoded) {
            throw new TypeError('Encoded WKP is required');
        }

        const decoded = wkp.decode(ctx, encoded);
        const wkt = geometryToWkt(decoded.geometry);
        ui.wktOutputText.textContent = wkt;

        const wktBytes = te.encode(wkt).length;
        const wkpBytes = te.encode(encoded).length;
        const ratio = wkpBytes > 0 ? (wktBytes / wkpBytes) : 0;

        baseMetrics.decode = [
            { k: 'WKT bytes', v: String(wktBytes) },
            { k: 'WKP bytes', v: String(wkpBytes) },
            { k: 'Compression (higher is better)', v: `${ratio.toFixed(2)}x` }
        ];

        renderCombinedMetrics('decode');
        updateHeader(encoded);
        postWorker({ type: 'decode', encoded });
    } catch (error) {
        setError(ui.decodeStatus, error?.message || String(error));
    }
});

ui.copyEncodedBtn.addEventListener('click', async () => {
    const text = ui.encodedOutputText.textContent || '';
    if (!text) {
        return;
    }
    await navigator.clipboard.writeText(text);
});

ui.copyWktBtn.addEventListener('click', async () => {
    const text = ui.wktOutputText.textContent || '';
    if (!text) {
        return;
    }
    await navigator.clipboard.writeText(text);
});

init().catch((error) => {
    enterDegradedMode('Oops: WKP demo failed to initialize. Check console for details.');
    setError(ui.encodeStatus, error?.message || String(error));
    setError(ui.decodeStatus, error?.message || String(error));
});
