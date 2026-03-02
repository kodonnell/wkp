import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const jsRoot = resolve(__dirname, '..');

const require = createRequire(import.meta.url);

function parseSemverMajorMinor(version) {
    const corePart = String(version).split('-', 1)[0];
    const parts = corePart.split('.');
    if (parts.length < 2) {
        throw new Error(`Invalid semantic version: ${version}`);
    }
    const major = Number.parseInt(parts[0], 10);
    const minor = Number.parseInt(parts[1], 10);
    if (!Number.isInteger(major) || !Number.isInteger(minor)) {
        throw new Error(`Invalid semantic version: ${version}`);
    }
    return [major, minor];
}

function assertCompatible(runtimeCoreVersion, compatibility, label) {
    const [runtimeMajor, runtimeMinor] = parseSemverMajorMinor(runtimeCoreVersion);
    const match = /^([0-9]+)\.([0-9]+)\.x$/.exec(compatibility);
    if (!match) {
        throw new Error(`Invalid compatibility range '${compatibility}' from ${label}`);
    }
    const requiredMajor = Number.parseInt(match[1], 10);
    const requiredMinor = Number.parseInt(match[2], 10);

    if (runtimeMajor !== requiredMajor || runtimeMinor !== requiredMinor) {
        throw new Error(
            `${label} incompatible runtime core version: got ${runtimeCoreVersion}, expected ${compatibility}`
        );
    }
}

const nodeBindings = require(resolve(jsRoot, 'packages', 'node'));
const nodeVersion = nodeBindings.coreVersion();
assertCompatible(nodeVersion, nodeBindings.coreCompatibility, '@wkpjs/node');

const webEntry = resolve(jsRoot, 'packages', 'web', 'src', 'index.js');
const { createWkp } = await import(pathToFileURL(webEntry).href);
const webBindings = await createWkp();
const webVersion = webBindings.coreVersion();
assertCompatible(webVersion, webBindings.coreCompatibility, '@wkpjs/web');

console.log('Runtime compatibility checks passed for @wkpjs/node and @wkpjs/web');
