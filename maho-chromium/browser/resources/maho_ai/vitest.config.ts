import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {defineConfig} from 'vitest/config';

const featureDir = path.dirname(fileURLToPath(import.meta.url));
const sharedNodeModulesDir = path.resolve(featureDir, '../../../../website/node_modules');

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
        find: /^\.\.\/maho_ai\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_ai.mojom-webui.js'),
      },
      {
        find: /^\.\.\/\.\.\/maho_ai\.mojom-webui\.js$/,
        replacement: path.join(featureDir, 'standalone/maho_ai.mojom-webui.js'),
      },
      {
        find: /^react$/,
        replacement: path.join(sharedNodeModulesDir, 'react'),
      },
      {
        find: /^react\/jsx-runtime$/,
        replacement: path.join(sharedNodeModulesDir, 'react/jsx-runtime.js'),
      },
      {
        find: /^react\/jsx-dev-runtime$/,
        replacement: path.join(sharedNodeModulesDir, 'react/jsx-dev-runtime.js'),
      },
      {
        find: /^react-dom\/client$/,
        replacement: path.join(sharedNodeModulesDir, 'react-dom/client.js'),
      },
      {
        find: /^clsx$/,
        replacement: path.join(sharedNodeModulesDir, 'clsx'),
      },
      {
        find: /^class-variance-authority$/,
        replacement: path.join(sharedNodeModulesDir, 'class-variance-authority'),
      },
      {
        find: /^@radix-ui\/react-slot$/,
        replacement: path.join(sharedNodeModulesDir, '@radix-ui/react-slot'),
      },
      {
        find: /^@radix-ui\/react-dropdown-menu$/,
        replacement: path.join(sharedNodeModulesDir, '@radix-ui/react-dropdown-menu'),
      },
    ],
  },
});
