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
        find: /^\.\.\/maho_welcome\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_welcome.mojom-webui.js'),
      },
      {
        find: /^\.\.\/\.\.\/maho_welcome\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_welcome.mojom-webui.js'),
      },
      {
        find: /^\/\/resources\/maho_common\/react\/store_utils\.js$/,
        replacement: path.join(featureDir, '../maho_common/react/store_utils.ts'),
      },
    ],
  },
});
