'use strict';

const {spawnSync} = require('node:child_process');
const path = require('node:path');

const testFile = path.join(__dirname, 'desktop-mail-integration.test.js');
const result = spawnSync(process.execPath, ['--test', '--test-concurrency=1', testFile], {
    cwd: __dirname,
    encoding: 'utf8',
    stdio: 'pipe',
});
process.stdout.write(result.stdout);
process.stderr.write(result.stderr);
process.exit(result.status ?? 1);
