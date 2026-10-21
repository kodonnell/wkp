// Builds the Rust WebAssembly module (native/) into dist/wkp_core.wasm.
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const pkgRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repoRoot = resolve(pkgRoot, '../../../..');
const target = 'wasm32-unknown-unknown';
const profile = 'release-wasm';

// In CI, build exactly what Cargo.lock pins (and fail if it is out of date).
const locked = process.env.CI ? ['--locked'] : [];
const result = spawnSync('cargo', ['build', '-p', 'wkp-wasm', '--target', target, '--profile', profile, ...locked], {
    cwd: repoRoot,
    stdio: 'inherit',
    shell: process.platform === 'win32'
});
if (result.error) {
    console.error('Failed to invoke cargo. Install Rust (https://rustup.rs) and run `rustup target add wasm32-unknown-unknown`.');
    console.error(result.error.message);
    process.exit(1);
}
if (result.status !== 0) {
    process.exit(result.status ?? 1);
}

const targetDir = process.env.CARGO_TARGET_DIR ? resolve(process.env.CARGO_TARGET_DIR) : resolve(repoRoot, 'target');
const outDir = resolve(pkgRoot, 'dist');
mkdirSync(outDir, { recursive: true });
// Emscripten JS glue from builds before 0.6.0; the Rust module needs none.
rmSync(resolve(outDir, 'wkp_core.js'), { force: true });
const out = resolve(outDir, 'wkp_core.wasm');
copyFileSync(resolve(targetDir, target, profile, 'wkp_core.wasm'), out);
console.log('Built:', out);
