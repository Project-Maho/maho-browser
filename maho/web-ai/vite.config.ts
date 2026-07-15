import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';

export default defineConfig({
  plugins: [
    preact(),
    {
      name: 'maho-classic-file-protocol',
      transformIndexHtml(html) {
        return html.replace(/type="module"/g, 'defer').replace(/\s+crossorigin/g, '');
      },
    },
  ],
  // Relative asset base + classic (non-module) IIFE output so the bundle loads under
  // file:// (WKWebView / Android WebView). ES module scripts and crossorigin links are
  // blocked by CORS from the null file:// origin, which leaves the app blank.
  base: './',
  build: {
    outDir: 'dist',
    rollupOptions: {
      input: 'index.html',
      output: {
        format: 'iife',
        inlineDynamicImports: true,
        entryFileNames: 'ai-bundle.js',
        assetFileNames: (assetInfo) => {
          if (assetInfo.names?.some((n) => n.endsWith('.css'))) {
            return 'ai-bundle.css';
          }
          return '[name]-[hash][extname]';
        },
      },
    },
  },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./tests/setup.ts'],
  },
});
