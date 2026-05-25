import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {defineConfig} from 'vitest/config';

const featureDir = path.dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  test: {
    environment: 'node',
    include: ['react/__tests__/**/*.test.ts'],
  },
  resolve: {
    alias: [
      {
        find: /^\.\.\/maho_space_create\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_space_create.mojom-webui.js'),
      },
    ],
  },
});
