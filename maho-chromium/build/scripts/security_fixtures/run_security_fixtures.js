#!/usr/bin/env node
'use strict';

// CLI to start the two-origin security fixtures for manual/browser use.
//
//   node run_security_fixtures.js            # start, print manifest, stay alive
//   node run_security_fixtures.js --once     # start, print manifest, exit 0
//   node run_security_fixtures.js --ca-out <path>  # also write the CA PEM
//
// Prints a single JSON line with origins, ports, PID, and CA fingerprint so
// other processes can consume it deterministically (no sleeps). SIGINT/SIGTERM
// close both servers and remove the temp cert dir.

const fs = require('node:fs');
const { startSecurityFixtures } = require('./fixture_server');

function parseArgs(argv) {
  const args = { once: false, caOut: null };
  for (let i = 2; i < argv.length; i += 1) {
    if (argv[i] === '--once') args.once = true;
    else if (argv[i] === '--ca-out') args.caOut = argv[i + 1];
  }
  return args;
}

async function main() {
  const args = parseArgs(process.argv);
  const fx = await startSecurityFixtures();

  if (args.caOut) fs.writeFileSync(args.caOut, fx.ca);

  process.stdout.write(
    JSON.stringify({
      ready: true,
      pid: process.pid,
      origins: { a: fx.a.origin, b: fx.b.origin },
      caFingerprint: fx.caFingerprint,
      manifest: fx.manifest,
    }) + '\n',
  );

  if (args.once) {
    await fx.close();
    return;
  }

  const shutdown = async () => {
    await fx.close();
    process.exit(0);
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);
}

main().catch((err) => {
  process.stderr.write(`security fixtures failed to start: ${err.message}\n`);
  process.exit(1);
});
