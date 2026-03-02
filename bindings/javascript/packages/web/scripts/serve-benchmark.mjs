import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const packageRoot = resolve(__filename, '..', '..');

function parsePort(defaultPort) {
    const arg = process.argv.find((v) => v.startsWith('--port='));
    if (!arg) {
        return defaultPort;
    }
    const port = Number.parseInt(arg.slice('--port='.length), 10);
    if (!Number.isFinite(port) || port <= 0 || port > 65535) {
        throw new Error(`Invalid --port value: ${arg}`);
    }
    return port;
}

const MIME = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.wasm': 'application/wasm',
    '.css': 'text/css; charset=utf-8'
};

const port = parsePort(8080);

function safePathFromUrl(urlPathname) {
    const normalized = normalize(decodeURIComponent(urlPathname)).replace(/^([/\\])+/, '');
    const resolved = resolve(packageRoot, normalized);
    if (!resolved.startsWith(packageRoot)) {
        return null;
    }
    return resolved;
}

const server = createServer(async (req, res) => {
    try {
        const reqUrl = new URL(req.url ?? '/', `http://${req.headers.host ?? 'localhost'}`);
        let pathname = reqUrl.pathname;

        if (pathname === '/') {
            pathname = '/benchmark/index.html';
        }

        const filePath = safePathFromUrl(pathname);
        if (!filePath) {
            res.statusCode = 403;
            res.end('Forbidden');
            return;
        }

        const data = await readFile(filePath);
        const mimeType = MIME[extname(filePath)] ?? 'application/octet-stream';
        res.statusCode = 200;
        res.setHeader('Content-Type', mimeType);
        res.end(data);
    } catch {
        res.statusCode = 404;
        res.end('Not found');
    }
});

server.listen(port, () => {
    console.log(`WKP benchmark server running at http://localhost:${port}/benchmark/index.html`);
});
