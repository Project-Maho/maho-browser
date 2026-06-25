import {defineConfig} from '@playwright/test';
import * as path from 'node:path';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));

// Visual regression captures must run against our actual Maho build, since the
// welcome flow lives at `chrome://maho-welcome/` (a Maho-only WebUI host).
const MAHO_BIN = path.resolve(
  here,
  '../../../../chromium/src/out/Default/Maho.app/Contents/MacOS/Maho',
);

export default defineConfig({
  testDir: './__tests__',
  fullyParallel: false,
  workers: 1,
  use: {
    baseURL: 'chrome://maho-welcome/',
    launchOptions: {
      executablePath: MAHO_BIN,
      args: ['--no-first-run', '--no-default-browser-check'],
    },
  },
  projects: [
    {name: 'maho-welcome', use: {channel: 'chromium'}},
  ],
});
