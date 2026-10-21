// Builds the Rust Node-API addon (native/) and copies it to where index.js looks for it.
//
// Usage: node scripts/build-native.mjs [--target <rust-triple>] [--prebuild] [--zig] [--debug]
//   --target    cross-compile for a Rust target triple (e.g. aarch64-apple-darwin)
//   --prebuild  write to prebuilds/<platform>-<arch>/wkp.node (as published) instead of
//               build/Release/wkp.node; platform/arch come from --target when given
//   --zig       link with `cargo zigbuild`: against glibc 2.17 for Linux gnu targets (so the
//               addon loads on older distros), and against musl for Linux musl targets (Alpine);
//               needs `pip install ziglang cargo-zigbuild`
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const pkgRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const repoRoot = resolve(pkgRoot, '../../../..');

const args = process.argv.slice(2);
const option = (name) => {
    const i = args.indexOf(name);
    return i >= 0 ? args[i + 1] : undefined;
};
const target = option('--target');
const prebuild = args.includes('--prebuild');
const profile = args.includes('--debug') ? 'debug' : 'release';
const zig = args.includes('--zig');

// Rust target triple -> Node's process.platform / process.arch naming.
function nodeTarget(triple) {
    if (!triple) {
        return { platform: process.platform, arch: process.arch };
    }
    const arch = triple.startsWith('x86_64') ? 'x64' : triple.startsWith('aarch64') ? 'arm64' : triple.split('-')[0];
    const platform = triple.includes('windows') ? 'win32' : triple.includes('darwin') ? 'darwin' : 'linux';
    return { platform, arch, musl: triple.includes('musl') };
}

const cargoArgs = [zig ? 'zigbuild' : 'build', '-p', 'wkp-node', ...(profile === 'release' ? ['--release'] : [])];
if (target) {
    // zigbuild takes the minimum glibc as a suffix; artifacts still land under the plain triple.
    cargoArgs.push('--target', zig && target.endsWith('-linux-gnu') ? `${target}.2.17` : target);
}
// Rust's musl targets link the C runtime statically by default, which rules out a shared library
// such as a .node addon; link it dynamically, as Node itself is on Alpine.
const env = { ...process.env };
if (target?.includes('-musl')) {
    env.RUSTFLAGS = [env.RUSTFLAGS, '-C target-feature=-crt-static'].filter(Boolean).join(' ');
}
const result = spawnSync('cargo', cargoArgs, { cwd: repoRoot, stdio: 'inherit', shell: process.platform === 'win32', env });
if (result.status !== 0) {
    process.exit(result.status ?? 1);
}

const { platform, arch, musl } = nodeTarget(target);
const libName = platform === 'win32' ? 'wkp_node.dll' : platform === 'darwin' ? 'libwkp_node.dylib' : 'libwkp_node.so';
const targetDir = process.env.CARGO_TARGET_DIR ? resolve(process.env.CARGO_TARGET_DIR) : resolve(repoRoot, 'target');
const built = resolve(targetDir, ...(target ? [target] : []), profile, libName);
const outDir = prebuild
    ? resolve(pkgRoot, 'prebuilds', `${platform}-${arch}${musl ? '-musl' : ''}`)
    : resolve(pkgRoot, 'build', 'Release');
mkdirSync(outDir, { recursive: true });
copyFileSync(built, resolve(outDir, 'wkp.node'));
console.log(`Built ${resolve(outDir, 'wkp.node')}`);
