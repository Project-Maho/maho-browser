import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {defineConfig} from 'vitest/config';

const featureDir = path.dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  test: {
    environment: 'jsdom',
    include: ['react/__tests__/**/*.test.ts'],
  },
  resolve: {
    alias: [
      {
        find: /^chrome:\/\/resources\/js\/parse_html_subset\.js$/,
        replacement: path.join(featureDir, 'react/__tests__/parse_html_subset_mock.ts'),
      },
      {
        find: /^\.\/maho_ai\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_ai.mojom-webui.js'),
      },
      {
        find: /^\.\.\/\.\.\/maho_ai\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_ai.mojom-webui.js'),
      },
    ],
  },
});
