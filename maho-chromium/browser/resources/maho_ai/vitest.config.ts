import {defineConfig} from 'vitest/config';

const featureUrl = new URL('./', import.meta.url);
const sharedNodeModulesUrl = new URL('../../../../website/node_modules/', featureUrl);

function featurePath(relativePath: string): string {
  return new URL(relativePath, featureUrl).pathname;
}

function sharedNodeModulePath(relativePath: string): string {
  return new URL(relativePath, sharedNodeModulesUrl).pathname;
}

export default defineConfig({
  test: {
    environment: 'jsdom',
    include: ['react/__tests__/**/*.test.ts', 'react/__tests__/**/*.test.tsx'],
  },
  resolve: {
    alias: [
      {
        find: /^chrome:\/\/resources\/js\/parse_html_subset\.js$/,
        replacement: featurePath('react/__tests__/parse_html_subset_mock.ts'),
      },
      {
        find: /^\.\/maho_ai\.mojom-webui\.js$/,
        replacement: featurePath('standalone/maho_ai.mojom-webui.js'),
      },
      {
        find: /^\.\.\/maho_ai\.mojom-webui\.js$/,
        replacement: featurePath('standalone/maho_ai.mojom-webui.js'),
      },
      {
        find: /^\.\.\/\.\.\/maho_ai\.mojom-webui\.js$/,
        replacement: featurePath('standalone/maho_ai.mojom-webui.js'),
      },
      {
        find: /^\.\.\/\.\.\/\.\.\/maho_ai\.mojom-webui\.js$/,
        replacement: featurePath('standalone/maho_ai.mojom-webui.js'),
      },
      {
        find: /^react$/,
        replacement: sharedNodeModulePath('react'),
      },
      {
        find: /^react\/jsx-runtime$/,
        replacement: sharedNodeModulePath('react/jsx-runtime.js'),
      },
      {
        find: /^react\/jsx-dev-runtime$/,
        replacement: sharedNodeModulePath('react/jsx-dev-runtime.js'),
      },
      {
        find: /^react-dom\/client$/,
        replacement: sharedNodeModulePath('react-dom/client.js'),
      },
      {
        find: /^tailwind-merge$/,
        replacement: sharedNodeModulePath('tailwind-merge'),
      },
      {
        find: /^clsx$/,
        replacement: sharedNodeModulePath('clsx'),
      },
      {
        find: /^@lib\/utils$/,
        replacement: featurePath('../maho_common/react/lib/utils.ts'),
      },
      {
        find: /^@ui\/(.+)$/,
        replacement: featurePath('../maho_common/react/ui/$1.tsx'),
      },
      {
        find: /^@icons\/lucide$/,
        replacement: featurePath('../maho_common/react/icons/lucide.ts'),
      },
      {
        find: /^@theme\/(.+)$/,
        replacement: featurePath('../maho_common/react/theme/$1'),
      },
      {
        find: /^lucide-react$/,
        replacement: sharedNodeModulePath('lucide-react/dist/esm/lucide-react.js'),
      },
      {
        find: /^class-variance-authority$/,
        replacement: sharedNodeModulePath('class-variance-authority'),
      },
      {
        find: /^marked$/,
        replacement: sharedNodeModulePath('marked/lib/marked.esm.js'),
      },
      {
        find: /^sonner$/,
        replacement: sharedNodeModulePath('sonner/dist/index.mjs'),
      },
      {
        find: /^@radix-ui\/react-slot$/,
        replacement: sharedNodeModulePath('@radix-ui/react-slot'),
      },
      {
        find: /^@radix-ui\/react-popover$/,
        replacement: sharedNodeModulePath('@radix-ui/react-popover'),
      },
      {
        find: /^@radix-ui\/react-dialog$/,
        replacement: sharedNodeModulePath('@radix-ui/react-dialog'),
      },
      {
        find: /^@radix-ui\/react-tabs$/,
        replacement: sharedNodeModulePath('@radix-ui/react-tabs'),
      },
      {
        find: /^@radix-ui\/react-switch$/,
        replacement: sharedNodeModulePath('@radix-ui/react-switch'),
      },
      {
        find: /^@radix-ui\/react-checkbox$/,
        replacement: sharedNodeModulePath('@radix-ui/react-checkbox'),
      },
      {
        find: /^@radix-ui\/react-label$/,
        replacement: sharedNodeModulePath('@radix-ui/react-label'),
      },
      {
        find: /^@radix-ui\/react-select$/,
        replacement: sharedNodeModulePath('@radix-ui/react-select'),
      },
      {
        find: /^@radix-ui\/react-slider$/,
        replacement: sharedNodeModulePath('@radix-ui/react-slider'),
      },
      {
        find: /^@radix-ui\/react-toggle-group$/,
        replacement: sharedNodeModulePath('@radix-ui/react-toggle-group'),
      },
      {
        find: /^@radix-ui\/react-toggle$/,
        replacement: sharedNodeModulePath('@radix-ui/react-toggle'),
      },
      {
        find: /^@radix-ui\/react-tooltip$/,
        replacement: sharedNodeModulePath('@radix-ui/react-tooltip'),
      },
      {
        find: /^@radix-ui\/react-dropdown-menu$/,
        replacement: sharedNodeModulePath('@radix-ui/react-dropdown-menu'),
      },
      {
        find: /^@radix-ui\/react-context-menu$/,
        replacement: sharedNodeModulePath('@radix-ui/react-context-menu'),
      },
    ],
  },
});
