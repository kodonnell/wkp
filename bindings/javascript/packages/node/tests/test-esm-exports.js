const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

const cjs = require('../index.js');

test('every export is available as an ESM named import', async () => {
    const esm = await import(pathToFileURL(path.join(__dirname, '..', 'index.js')).href);
    for (const name of Object.keys(cjs)) {
        assert.ok(name in esm, `import { ${name} } from '@wkpjs/node' is missing`);
        assert.equal(esm[name], cjs[name], name);
    }
});
