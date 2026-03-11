import { spawnSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const pkgRoot = resolve(__dirname, '..');
const outDir = resolve(pkgRoot, 'dist');
const shimCpp = resolve(pkgRoot, 'src', 'wasm_shim.cpp');
const coreCpp = resolve(pkgRoot, '../../../../core/src/core.c');
const coreInclude = resolve(pkgRoot, '../../../../core/include');
const outJs = resolve(outDir, 'wkp_core.js');
const emcc = process.env.EMCC || 'emcc';

mkdirSync(outDir, { recursive: true });

const args = [
    '-O3',
    '-std=c++17',
    '-I', coreInclude,
    shimCpp,
    '-x', 'c++',
    coreCpp,
    '-s', 'MODULARIZE=1',
    '-s', 'EXPORT_ES6=1',
    '-s', 'ENVIRONMENT=web,worker,node',
    '-s', 'ALLOW_MEMORY_GROWTH=1',
    '-s', 'FILESYSTEM=0',
    '-s', 'EXPORTED_RUNTIME_METHODS=["cwrap"]',
    '-s', 'EXPORTED_FUNCTIONS=["_malloc","_free","_wkp_wasm_workspace_create","_wkp_wasm_workspace_destroy","_wkp_wasm_workspace_encode_f64","_wkp_wasm_workspace_decode_f64","_wkp_wasm_workspace_encode_geometry_frame_f64","_wkp_wasm_workspace_decode_geometry_frame_f64","_wkp_wasm_decode_geometry_header","_wkp_wasm_core_version","_wkp_wasm_basic_self_test","_wkp_wasm_geometry_point","_wkp_wasm_geometry_linestring","_wkp_wasm_geometry_polygon","_wkp_wasm_geometry_multipoint","_wkp_wasm_geometry_multilinestring","_wkp_wasm_geometry_multipolygon","_wkp_wasm_status_ok","_wkp_wasm_status_invalid_argument","_wkp_wasm_status_malformed_input","_wkp_wasm_status_allocation_failed","_wkp_wasm_status_buffer_too_small","_wkp_wasm_status_limit_exceeded","_wkp_wasm_status_internal_error"]',
    '-o', outJs
];

const result = spawnSync(emcc, args, {
    cwd: pkgRoot,
    stdio: 'inherit',
    env: {
        ...process.env,
        EM_NODE_JS: process.env.EM_NODE_JS || process.execPath
    },
    shell: process.platform === 'win32'
});

if (result.error) {
    console.error('Failed to invoke emcc. Ensure Emscripten is installed and activated.');
    console.error(result.error.message);
    process.exit(1);
}

if (result.status !== 0) {
    process.exit(result.status ?? 1);
}

console.log('Built:', outJs);
