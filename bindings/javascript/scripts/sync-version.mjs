import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const jsRoot = resolve(__dirname, '..');
const repoRoot = resolve(jsRoot, '..', '..');
const headerPath = resolve(repoRoot, 'core', 'include', 'wkp', '_version.h');

const packageFiles = [
    resolve(jsRoot, 'package.json'),
    resolve(jsRoot, 'packages', 'node', 'package.json'),
    resolve(jsRoot, 'packages', 'web', 'package.json')
];

const lockFile = resolve(jsRoot, 'package-lock.json');

function readCoreVersion() {
    const header = readFileSync(headerPath, 'utf8');
    const match = header.match(/#define\s+WKP_CORE_VERSION\s+"([^"]+)"/);
    if (!match) {
        throw new Error(`Could not parse WKP_CORE_VERSION from ${headerPath}`);
    }
    return match[1];
}

function updateJsonVersion(path, version, write) {
    const text = readFileSync(path, 'utf8');
    const json = JSON.parse(text);
    const current = json.version;
    const changed = current !== version;

    if (changed && write) {
        json.version = version;
        writeFileSync(path, `${JSON.stringify(json, null, 2)}\n`, 'utf8');
    }

    return { path, current, changed };
}

function updateLockVersion(path, version, write) {
    const text = readFileSync(path, 'utf8');
    const lock = JSON.parse(text);

    const fields = [
        ['version'],
        ['packages', '', 'version'],
        ['packages', 'packages/node', 'version'],
        ['packages', 'packages/web', 'version']
    ];

    let changed = false;
    for (const f of fields) {
        let obj = lock;
        for (let i = 0; i < f.length - 1; i += 1) {
            obj = obj?.[f[i]];
        }
        const key = f[f.length - 1];
        if (obj && obj[key] !== version) {
            obj[key] = version;
            changed = true;
        }
    }

    if (changed && write) {
        writeFileSync(path, `${JSON.stringify(lock, null, 2)}\n`, 'utf8');
    }

    return { path, changed };
}

const write = process.argv.includes('--write');
const check = process.argv.includes('--check');

if ((write && check) || (!write && !check)) {
    console.error('Usage: node scripts/sync-version.mjs --write | --check');
    process.exit(2);
}

const version = readCoreVersion();

const pkgResults = packageFiles.map((p) => updateJsonVersion(p, version, write));
const lockResult = updateLockVersion(lockFile, version, write);

const drift = pkgResults.filter((r) => r.changed || r.current !== version);
const anyChanges = pkgResults.some((r) => r.changed) || lockResult.changed;

if (write) {
    if (anyChanges) {
        console.log(`Synchronized JavaScript package versions to ${version}`);
    } else {
        console.log(`JavaScript package versions already synchronized at ${version}`);
    }
    process.exit(0);
}

if (drift.length || lockResult.changed) {
    console.error(`Version drift detected. Expected ${version} from core/include/wkp/_version.h`);
    for (const r of drift) {
        console.error(`- ${r.path}: ${r.current} -> ${version}`);
    }
    if (lockResult.changed) {
        console.error(`- ${lockResult.path}: lockfile version entries differ`);
    }
    process.exit(1);
}

console.log(`JavaScript package versions match core version ${version}`);
